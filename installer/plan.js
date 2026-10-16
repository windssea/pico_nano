/// 网页安装器的纯逻辑：分区表解析、写入计划与 MD5；不碰串口，主机可测。
/// Pure web-installer logic: partition parsing, write planning and MD5; no serial access, testable on the host.

/// 分区表固定位置与长度。/ Fixed partition-table location and length.
export const TABLE_OFFSET = 0x8000;
export const TABLE_SIZE = 0xc00;
/// 擦除与 NVS 页的扇区大小。/ Sector size for erases and NVS pages.
export const SECTOR = 0x1000;

const TYPE_APP = 0x00;
const TYPE_DATA = 0x01;
const SUBTYPE_OTA = 0x00;
const SUBTYPE_NVS = 0x02;

/// 解析 ESP-IDF 二进制分区表；遇到 MD5 行或空白结束，非法时返回 null。
/// Parse an ESP-IDF binary partition table; stops at the MD5 row or blank space, returns null when invalid.
export function parseTable(bytes) {
  if (!bytes || bytes.length < 32) return null;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const entries = [];
  for (let at = 0; at + 32 <= bytes.length; at += 32) {
    const magic = view.getUint16(at, true);
    if (magic === 0xebeb || magic === 0xffff) break;
    if (magic !== 0x50aa) return null;
    let label = "";
    for (let i = 0; i < 16 && bytes[at + 12 + i]; i++) label += String.fromCharCode(bytes[at + 12 + i]);
    entries.push({
      type: bytes[at + 2], subtype: bytes[at + 3],
      offset: view.getUint32(at + 4, true), size: view.getUint32(at + 8, true), label,
    });
  }
  return entries.length ? entries : null;
}

/// 判断区域开头是否为 LittleFS 超级块（块 0 或块 1 的第 8 字节起为 "littlefs"）。
/// Whether a region starts with a LittleFS superblock ("littlefs" at byte 8 of block 0 or 1).
export function isLittleFs(head) {
  if (!(head instanceof Uint8Array)) return false;
  const tag = [0x6c, 0x69, 0x74, 0x74, 0x6c, 0x65, 0x66, 0x73];
  for (const block of [0, SECTOR]) {
    if (head.length < block + 16) continue;
    if (tag.every((value, i) => head[block + 8 + i] === value)) return true;
  }
  return false;
}

function find(entries, predicate, what) {
  const entry = entries.find(predicate);
  if (!entry) throw new Error(`本固件分区表缺少 ${what} / firmware table lacks ${what}`);
  return entry;
}

/// 从本固件分区表取出安装用到的分区。/ Pick the partitions the installer needs from the firmware table.
export function layoutOf(tableBytes) {
  const entries = parseTable(tableBytes);
  if (!entries) throw new Error("本固件分区表无效 / firmware partition table is invalid");
  const ordered = [...entries].sort((a, b) => a.offset - b.offset);
  if (ordered.some((e, i) => !e.size || e.offset < 0x9000 || e.offset % SECTOR || e.size % SECTOR ||
      e.offset + e.size > 0x1000000 || (i && ordered[i - 1].offset + ordered[i - 1].size > e.offset)))
    throw new Error("本固件分区范围无效 / invalid firmware partition bounds");
  return {
    nvs: find(entries, (e) => e.type === TYPE_DATA && e.subtype === SUBTYPE_NVS, "nvs"),
    otadata: find(entries, (e) => e.type === TYPE_DATA && e.subtype === SUBTYPE_OTA, "otadata"),
    app: find(entries, (e) => e.type === TYPE_APP && e.label === "ota_0", "ota_0"),
    data: find(entries, (e) => e.label === "data", "data"),
    wallpaper: find(entries, (e) => e.label === "wallpaper", "wallpaper"),
  };
}

function sameBytes(a, b) {
  if (!a || a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

/// 生成写入计划。输入为本固件文件、设备分区表与 data/wallpaper 头部或完整分区，以及用户选项。
/// 不整片擦除；NVS 默认保留，迁移有冲突时必须明确选择清除；分区表最后写。
/// Build the write plan from firmware files, the device table, data/wallpaper heads or full partitions, and user options.
/// No whole-chip erase; preserve NVS by default and require explicit clearing when migration conflicts exist.
/// The partition table is written last to narrow the inconsistent window on power loss.
export function preservedLayoutOf(files, deviceTable) {
  const matched = [files.partitionTable, files.currentPartitionTable].find((table) => table?.length === TABLE_SIZE && sameBytes(deviceTable, table));
  if (!matched) throw new Error("设备分区与专用升级包不匹配，停止写入。刷过其他固件请选择“完整刷入”，并核对备份与清除选项 / device layout mismatch; select full installation");
  return layoutOf(matched);
}

/// 保留布局模式不猜测运行槽：OTA选择已使用时，写完ota_0再清空otadata，使启动确定回到ota_0；读取不完整则停止。
/// Preserving mode never guesses the running slot: when OTA selection is in use, blank otadata after writing ota_0 so boot deterministically returns to ota_0; stop on incomplete reads.
export function otaNeedsReset(bytes, size = 0x2000) {
  if (!(bytes instanceof Uint8Array) || bytes.length !== size || size !== 0x2000)
    throw new Error("启动选择（otadata）读取不完整，停止写入 / incomplete OTA selection read; nothing was written");
  return bytes.some((b) => b !== 0xff);
}

export function planInstall({ files, deviceTable, dataHead, wallpaperHead, otaReadback, clearNvs = false, initializeStorage = false, mode = "full" }) {
  const layout = layoutOf(files.partitionTable);
  const upgrade = sameBytes(deviceTable, files.partitionTable);
  // 专用升级包必须匹配已核对布局，禁止迁移或初始化。 / Dedicated upgrades must match the verified layout without migration or initialization.
  if (mode === "app-only") {
    // 同源码完整包的表是另一项精确白名单，不用相似布局或版本号代替逐字节核对。
    // The same-source full package supplies another exact allowlist entry; similar layouts or version numbers never replace a byte comparison.
    const preserved = preservedLayoutOf(files, deviceTable);
    const resetOta = otaNeedsReset(otaReadback, preserved.otadata.size);
    if (clearNvs || initializeStorage) throw new Error("保留布局升级不能清除数据 / preserving upgrade cannot clear data");
    if (!files.app?.length || files.app.length > preserved.app.size || preserved.app.offset !== 0x20000)
      throw new Error("应用或升级地址无效 / invalid upgrade application or address");
    // 先写应用后清选择：中途断电仍从原有效槽启动。 / App first, selection last: a power loss in between still boots the previous valid slot.
    const steps = [{ kind: "write", name: "应用固件 / app", address: preserved.app.offset, data: files.app, reason: "保留全部布局与数据 / preserve layout and data", optional: false }];
    if (resetOta) steps.push({ kind: "erase", name: "OTA 选择 / otadata", address: preserved.otadata.offset, size: preserved.otadata.size,
      data: new Uint8Array(preserved.otadata.size).fill(0xff), reason: "设备用过 OTA，清空后从 ota_0 启动 / OTA was used; blank it to boot ota_0", optional: false });
    return { mode: "app-only", knownTable: true, keptNvs: [], unrecognizedStorage: [], resetOta, steps };
  }
  if (mode !== "full") throw new Error("未知安装模式 / unknown installation mode");
  if (!(deviceTable instanceof Uint8Array) || deviceTable.length !== TABLE_SIZE)
    throw new Error("设备分区表读取不完整，停止写入 / incomplete device partition read");
  for (const key of ["bootloader", "otaData", "app", "factoryData", "factoryWallpaper"])
    if (!(files[key] instanceof Uint8Array) || !files[key].length)
      throw new Error(`完整刷入包缺少 ${key} / incomplete full installation package`);
  if (files.bootloader.length > TABLE_OFFSET || files.otaData.length !== layout.otadata.size ||
      files.factoryData.length !== layout.data.size || files.factoryWallpaper.length !== layout.wallpaper.size)
    throw new Error("完整刷入镜像大小无效 / invalid full installation image sizes");
  const oldEntries = upgrade ? null : parseTable(deviceTable);
  const steps = [];
  const write = (name, address, data, reason, optional = false) => steps.push({ kind: "write", name, address, data, reason, optional });
  const blank = (name, address, size, reason) => steps.push({ kind: "erase", name, address, size, data: new Uint8Array(size).fill(0xff), reason });

  if (files.app.length > layout.app.size) throw new Error("应用固件超出 ota_0 分区 / app does not fit in ota_0");
  write("应用固件 / app", layout.app.offset, files.app, "主程序 / main program");
  write("OTA 选择 / otadata", layout.otadata.offset, files.otaData, "从 ota_0 启动 / boot from ota_0");

  const keptNvs = [];
  const conflictingNvs = [];
  for (let at = layout.nvs.offset; at < layout.nvs.offset + layout.nvs.size; at += SECTOR) {
    const wasNvs = upgrade || (oldEntries || []).some((e) => e.type === TYPE_DATA && e.subtype === SUBTYPE_NVS && e.offset <= at && at + SECTOR <= e.offset + e.size);
    if (!wasNvs) conflictingNvs.push(at);
    if (clearNvs) blank(`NVS 扇区 0x${at.toString(16)}`, at, SECTOR, "按选择清除设置 / cleared by choice");
    else keptNvs.push(at);
  }
  // 只把完整分区全为FF视为空白；头部损坏或短读不能授权覆盖。/ Only a complete all-FF partition is blank; damaged headers or short reads never authorize replacement.
  const unrecognizedStorage = [];
  for (const [name, partition, bytes, image] of [["data", layout.data, dataHead, files.factoryData], ["wallpaper", layout.wallpaper, wallpaperHead, files.factoryWallpaper]]) {
    // 头部相同不能证明其他布局的文件系统可迁移。/ A matching header does not establish filesystem compatibility across layouts.
    const compatible = upgrade || (oldEntries || []).some((e) => e.label === partition.label && e.type === partition.type &&
      e.subtype === partition.subtype && e.offset === partition.offset && e.size === partition.size);
    if (compatible && isLittleFs(bytes) && !initializeStorage) continue;
    const empty = bytes instanceof Uint8Array && bytes.length === partition.size && bytes.every((b) => b === 0xff);
    if (!empty) unrecognizedStorage.push(name);
    if (empty || initializeStorage) write(name === "data" ? "内部数据 / data" : "锁屏壁纸 / wallpaper", partition.offset, image,
      empty ? "完整分区为空白 / full partition is blank" : "明确选择初始化，清除原数据 / explicitly initialize and remove existing data");
  }

  // 升级时引导程序相同则跳过（由调用方比对设备 MD5）。/ On upgrade an identical bootloader is skipped (caller compares device MD5).
  write("引导程序 / bootloader", 0x0, files.bootloader, "二级引导 / second-stage bootloader", upgrade);
  if (!upgrade) write("分区表 / partition table", TABLE_OFFSET, files.partitionTable, "切换到本固件布局 / switch to this layout");
  for (const step of steps) {
    if (step.address % SECTOR) throw new Error(`地址未按扇区对齐 / unaligned address 0x${step.address.toString(16)}`);
  }
  return { mode: upgrade ? "upgrade" : "install", knownTable: upgrade || !!oldEntries, keptNvs, unrecognizedStorage,
    needsNvsClear: conflictingNvs.length > 0 && !clearNvs,
    needsStorageInitialization: !upgrade && unrecognizedStorage.length > 0 && !initializeStorage, steps };
}

/// 计算 MD5 十六进制摘要（esptool 校验写入用）。/ MD5 hex digest used to verify esptool writes.
export function md5(bytes) {
  const s = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21];
  const k = new Uint32Array(64);
  for (let i = 0; i < 64; i++) k[i] = Math.floor(Math.abs(Math.sin(i + 1)) * 2 ** 32) >>> 0;
  const length = bytes.length;
  const padded = new Uint8Array(((length + 72) >>> 6) << 6);
  padded.set(bytes);
  padded[length] = 0x80;
  const view = new DataView(padded.buffer);
  view.setUint32(padded.length - 8, (length * 8) >>> 0, true);
  view.setUint32(padded.length - 4, Math.floor(length / 0x20000000), true);
  let a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
  const m = new Uint32Array(16);
  for (let block = 0; block < padded.length; block += 64) {
    for (let i = 0; i < 16; i++) m[i] = view.getUint32(block + i * 4, true);
    let a = a0, b = b0, c = c0, d = d0;
    for (let i = 0; i < 64; i++) {
      let f, g;
      if (i < 16) { f = (b & c) | (~b & d); g = i; }
      else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
      else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
      else { f = c ^ (b | ~d); g = (7 * i) & 15; }
      const shift = s[(i >> 4) * 4 + (i & 3)];
      const sum = (a + f + k[i] + m[g]) >>> 0;
      a = d; d = c; c = b;
      b = (b + ((sum << shift) | (sum >>> (32 - shift)))) >>> 0;
    }
    a0 = (a0 + a) >>> 0; b0 = (b0 + b) >>> 0; c0 = (c0 + c) >>> 0; d0 = (d0 + d) >>> 0;
  }
  let hex = "";
  for (const word of [a0, b0, c0, d0]) for (let i = 0; i < 4; i++) hex += ((word >>> (i * 8)) & 0xff).toString(16).padStart(2, "0");
  return hex;
}
