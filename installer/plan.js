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

/// 生成写入计划。输入为本固件文件、设备上读到的分区表与 data/wallpaper 区域开头，以及用户选项。
/// 计划只写需要的区域：不整片擦除；旧分区表里本就是 NVS 的扇区保留，其余落在新 NVS 范围内的扇区清空，
/// 避免把旧系统的非 NVS 数据当成 NVS 页；勾选 clearNvs 时整段 NVS 清空。分区表最后写，降低断电后的不一致。
/// Build the write plan from the firmware files, the device's current table and data/wallpaper heads, and user options.
/// Only needed regions are written; no whole-chip erase. Sectors that were NVS in the old table are kept and the rest of the
/// new NVS range is cleared so foreign non-NVS bytes are never parsed as NVS pages; clearNvs clears the whole range.
/// The partition table is written last to narrow the inconsistent window on power loss.
export function planInstall({ files, deviceTable, dataHead, wallpaperHead, clearNvs = false }) {
  const layout = layoutOf(files.partitionTable);
  const upgrade = sameBytes(deviceTable, files.partitionTable);
  const oldEntries = upgrade ? null : parseTable(deviceTable);
  const steps = [];
  const write = (name, address, data, reason, optional = false) => steps.push({ kind: "write", name, address, data, reason, optional });
  const blank = (name, address, size, reason) => steps.push({ kind: "erase", name, address, size, data: new Uint8Array(size).fill(0xff), reason });

  if (files.app.length > layout.app.size) throw new Error("应用固件超出 ota_0 分区 / app does not fit in ota_0");
  write("应用固件 / app", layout.app.offset, files.app, "主程序 / main program");
  write("OTA 选择 / otadata", layout.otadata.offset, files.otaData, "从 ota_0 启动 / boot from ota_0");

  const keptNvs = [];
  for (let at = layout.nvs.offset; at < layout.nvs.offset + layout.nvs.size; at += SECTOR) {
    const wasNvs = upgrade || (oldEntries || []).some((e) => e.type === TYPE_DATA && e.subtype === SUBTYPE_NVS && e.offset <= at && at + SECTOR <= e.offset + e.size);
    if (clearNvs || !wasNvs) blank(`NVS 扇区 0x${at.toString(16)}`, at, SECTOR, clearNvs ? "按选择清除设置 / cleared by choice" : "原先不是 NVS / was not NVS before");
    else keptNvs.push(at);
  }
  if (!isLittleFs(dataHead)) write("内部数据 / data", layout.data.offset, files.factoryData, "尚无有效数据分区 / no valid data partition yet");
  if (!isLittleFs(wallpaperHead)) write("锁屏壁纸 / wallpaper", layout.wallpaper.offset, files.factoryWallpaper, "尚无有效壁纸分区 / no valid wallpaper partition yet");

  // 升级时引导程序相同则跳过（由调用方比对设备 MD5）。/ On upgrade an identical bootloader is skipped (caller compares device MD5).
  write("引导程序 / bootloader", 0x0, files.bootloader, "二级引导 / second-stage bootloader", upgrade);
  if (!upgrade) write("分区表 / partition table", TABLE_OFFSET, files.partitionTable, "切换到本固件布局 / switch to this layout");
  for (const step of steps) {
    if (step.address % SECTOR) throw new Error(`地址未按扇区对齐 / unaligned address 0x${step.address.toString(16)}`);
  }
  return { mode: upgrade ? "upgrade" : "install", knownTable: upgrade || !!oldEntries, keptNvs, steps };
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
