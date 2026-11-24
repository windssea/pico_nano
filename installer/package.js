/// 本地 ZIP 固件包解析、校验与 Gitee 下载链接生成；纯逻辑，主机可测。
/// Local ZIP firmware package parsing, verification and Gitee download link helper; pure logic, testable on the host.
import { layoutOf } from "./plan.js";

const GITEE_RELEASES = "https://gitee.com/windssea/pico_nano/releases";
const MAX_ZIP_ENTRIES = 512;
const MAX_ENTRY_BYTES = 16 * 1024 * 1024;
const EOCD_SIG = 0x06054b50;
const CD_SIG = 0x02014b50;
const LFH_SIG = 0x04034b50;

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let bit = 0; bit < 8; bit++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    table[i] = c >>> 0;
  }
  return table;
})();

/// 计算 IEEE 802.3 CRC-32（用于核对 ZIP 条目完整性）。/ Compute IEEE 802.3 CRC-32 for ZIP entry integrity.
export function crc32(bytes) {
  let crc = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) crc = CRC_TABLE[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

/// 生成当前所选版本和刷入方式对应的 Gitee 发布页与固件包直链。
/// Build the Gitee release page and direct ZIP download link for the selected release and install mode.
export function giteeDownloadInfo(release) {
  if (!release || release.legacy) {
    return { releaseUrl: GITEE_RELEASES, downloadUrl: GITEE_RELEASES, fileName: "", modeLabel: "" };
  }
  const version = String(release.version || "").split("+")[0];
  const shortCommit = String(release.commit || "").slice(0, 7).toLowerCase();
  if (!/^\d+(?:\.\d+)+$/.test(version) || !/^[0-9a-f]{7}$/.test(shortCommit)) {
    return { releaseUrl: GITEE_RELEASES, downloadUrl: GITEE_RELEASES, fileName: "", modeLabel: "" };
  }
  const tag = `v${version}`;
  const kind = release.mode === "app-only" ? "upgrade" : "full";
  const fileName = `PicoNano-${shortCommit}-${kind}.zip`;
  return {
    releaseUrl: `${GITEE_RELEASES}/tag/${tag}`,
    downloadUrl: `${GITEE_RELEASES}/download/${tag}/${fileName}`,
    fileName,
    modeLabel: release.mode === "app-only" ? "升级包" : "完整包",
  };
}

function safeEntryName(name) {
  if (!name || name.startsWith("/") || name.includes("\\") || /[\x00-\x1f]/.test(name)) {
    throw new Error(`压缩包内文件路径无效：${name} / invalid archive entry path`);
  }
  const parts = name.split("/");
  if (parts.some((part) => part === "..")) {
    throw new Error(`压缩包内存在非法相对路径：${name} / unsafe archive entry path`);
  }
  return name;
}

/// 解析标准 ZIP32 中央目录，返回按路径索引的条目表；不解压数据，避免一次性展开大文件。
/// Parse a standard ZIP32 central directory into a path-indexed map without eagerly inflating entries.
export function parseZip(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length < 22) {
    throw new Error("不是有效的 ZIP 压缩包 / not a valid ZIP archive");
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const minOffset = Math.max(0, bytes.length - 65557);
  let eocd = -1;
  for (let at = bytes.length - 22; at >= minOffset; at--) {
    if (view.getUint32(at, true) === EOCD_SIG && at + 22 + view.getUint16(at + 20, true) === bytes.length) {
      eocd = at;
      break;
    }
  }
  if (eocd < 0) throw new Error("无法识别 ZIP 目录尾，请确认下载完整 / invalid ZIP end of central directory");
  const disk = view.getUint16(eocd + 4, true);
  const cdDisk = view.getUint16(eocd + 6, true);
  const diskCount = view.getUint16(eocd + 8, true);
  const totalCount = view.getUint16(eocd + 10, true);
  const cdSize = view.getUint32(eocd + 12, true);
  const cdOffset = view.getUint32(eocd + 16, true);
  if (disk !== 0 || cdDisk !== 0 || diskCount !== totalCount) {
    throw new Error("不支持分卷 ZIP 压缩包 / multi-disk ZIP archives are unsupported");
  }
  if (totalCount === 0xffff || cdSize === 0xffffffff || cdOffset === 0xffffffff) {
    throw new Error("不支持 ZIP64 格式压缩包 / ZIP64 archives are unsupported");
  }
  if (totalCount > MAX_ZIP_ENTRIES) {
    throw new Error("压缩包内文件数量过多，请确认是官方固件包 / too many ZIP entries");
  }
  if (cdOffset + cdSize > eocd) {
    throw new Error("ZIP 中央目录范围损坏 / corrupt ZIP central directory bounds");
  }
  const decoder = new TextDecoder("utf-8", { fatal: true });
  const entries = new Map();
  let at = cdOffset;
  for (let i = 0; i < totalCount; i++) {
    if (at + 46 > cdOffset + cdSize || view.getUint32(at, true) !== CD_SIG) {
      throw new Error("ZIP 中央目录记录损坏 / corrupt ZIP central directory entry");
    }
    const flags = view.getUint16(at + 8, true);
    const method = view.getUint16(at + 10, true);
    const expectedCrc = view.getUint32(at + 16, true);
    const compressedSize = view.getUint32(at + 20, true);
    const uncompressedSize = view.getUint32(at + 24, true);
    const nameLen = view.getUint16(at + 28, true);
    const extraLen = view.getUint16(at + 30, true);
    const commentLen = view.getUint16(at + 32, true);
    const localOffset = view.getUint32(at + 42, true);
    const end = at + 46 + nameLen + extraLen + commentLen;
    if (end > cdOffset + cdSize) throw new Error("ZIP 条目越界 / ZIP entry exceeds central directory");
    let rawName;
    try {
      rawName = decoder.decode(bytes.subarray(at + 46, at + 46 + nameLen));
    } catch {
      throw new Error("ZIP 文件名编码无效 / invalid UTF-8 ZIP entry name");
    }
    const name = safeEntryName(rawName);
    if (!name.endsWith("/")) {
      if (entries.has(name)) throw new Error(`ZIP 存在重复文件：${name} / duplicate ZIP entry`);
      entries.set(name, { name, flags, method, crc32: expectedCrc, compressedSize, uncompressedSize, localOffset });
    }
    at = end;
  }
  if (at !== cdOffset + cdSize) throw new Error("ZIP 中央目录长度不匹配 / ZIP central directory size mismatch");
  return entries;
}

async function inflateRawBounded(compressed, expectedSize) {
  if (typeof DecompressionStream !== "function") {
    throw new Error("当前浏览器不支持本地解压，请升级至新版 Chrome 或 Edge / browser lacks DecompressionStream");
  }
  const stream = new Blob([compressed]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
  const reader = stream.getReader();
  const out = new Uint8Array(expectedSize);
  let offset = 0;
  try {
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      if (offset + value.length > expectedSize) {
        await reader.cancel();
        throw new Error("压缩包条目解压后超出声明长度 / inflated entry exceeds declared size");
      }
      out.set(value, offset);
      offset += value.length;
    }
  } finally {
    reader.releaseLock();
  }
  if (offset !== expectedSize) {
    throw new Error("压缩包条目解压不完整 / truncated deflate stream");
  }
  return out;
}

/// 按需读取单个 ZIP 条目并核对长度与 CRC-32。/ Lazily read a single ZIP entry and verify length and CRC-32.
export async function readZipEntry(bytes, entry) {
  if (entry.flags & 1) throw new Error(`不支持加密 ZIP 条目：${entry.name} / encrypted ZIP entry unsupported`);
  if (entry.method !== 0 && entry.method !== 8) {
    throw new Error(`不支持的压缩方式（${entry.method}）：${entry.name} / unsupported ZIP compression method`);
  }
  if (entry.uncompressedSize > MAX_ENTRY_BYTES) {
    throw new Error(`压缩包条目过大：${entry.name} / ZIP entry exceeds size limit`);
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (entry.localOffset + 30 > bytes.length || view.getUint32(entry.localOffset, true) !== LFH_SIG) {
    throw new Error(`ZIP 本地文件头损坏：${entry.name} / corrupt ZIP local header`);
  }
  const lfhNameLen = view.getUint16(entry.localOffset + 26, true);
  const lfhExtraLen = view.getUint16(entry.localOffset + 28, true);
  const dataStart = entry.localOffset + 30 + lfhNameLen + lfhExtraLen;
  const dataEnd = dataStart + entry.compressedSize;
  if (dataEnd > bytes.length) throw new Error(`ZIP 数据区越界：${entry.name} / ZIP entry data out of bounds`);
  const payload = bytes.subarray(dataStart, dataEnd);
  let out;
  if (entry.method === 0) {
    if (payload.length !== entry.uncompressedSize) throw new Error(`ZIP 未压缩条目长度不匹配：${entry.name} / stored size mismatch`);
    out = bytes.slice(dataStart, dataEnd);
  } else {
    out = await inflateRawBounded(payload, entry.uncompressedSize);
  }
  if (crc32(out) !== entry.crc32) throw new Error(`ZIP 文件校验（CRC-32）失败：${entry.name} / ZIP CRC-32 mismatch`);
  return out;
}

function detectRootPrefix(entries) {
  if (entries.has("manifest.json") || entries.has("firmware/manifest.json")) return "";
  const roots = new Set();
  for (const name of entries.keys()) {
    const slash = name.indexOf("/");
    if (slash <= 0) return "";
    roots.add(name.slice(0, slash + 1));
    if (roots.size > 1) return "";
  }
  const [prefix] = roots;
  if (prefix && (entries.has(`${prefix}manifest.json`) || entries.has(`${prefix}firmware/manifest.json`))) return prefix;
  return "";
}

function parseJsonEntry(bytes, label) {
  try {
    return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
  } catch {
    throw new Error(`${label} JSON 格式无效 / invalid JSON in ${label}`);
  }
}

/// 从本地官方 ZIP（升级包、完整包或离线网页安装包）提取并严格按当前发布清单的 SHA-256 校验固件。
/// Extract firmware files from a local official ZIP (upgrade, full, or offline web-installer) and verify every file by exact size and SHA-256 against the selected release.
export async function extractReleasePackage(zipBytes, release, sha256Fn, options = {}) {
  if (!release || !release.files) throw new Error("请先等待页面加载固件版本清单 / release manifest is not ready");
  const preserving = release.mode === "app-only";
  const keys = preserving
    ? ["partitionTable", ...(release.files.currentPartitionTable ? ["currentPartitionTable"] : []), "app"]
    : ["bootloader", "partitionTable", "otaData", "app", "factoryData", "factoryWallpaper"];
  const entries = parseZip(zipBytes);
  const rootPrefix = detectRootPrefix(entries);
  const webManifestEntry = entries.get(`${rootPrefix}firmware/manifest.json`);
  const pkgManifestEntry = entries.get(`${rootPrefix}manifest.json`);

  let filePrefix = rootPrefix;
  let packageLabel = "";
  let pkgManifest = null;

  if (webManifestEntry) {
    // 离线网页安装包同时包含保留布局升级和完整刷入两套目录，按当前所选模式定位子目录。
    // Offline web-installer ZIP contains both modes; select the subdirectory matching the active release.
    const siteManifest = parseJsonEntry(await readZipEntry(zipBytes, webManifestEntry), "firmware/manifest.json");
    const releases = Array.isArray(siteManifest?.releases) ? siteManifest.releases : [];
    const matched = releases.find((item) => (item.mode || "full") === (release.mode || "full") && item.version === release.version)
      || releases.find((item) => (item.mode || "full") === (release.mode || "full"));
    if (!matched) {
      throw new Error("离线网页包中不含当前所选刷入方式的固件 / offline web package lacks the selected mode");
    }
    if (matched.version !== release.version || (release.commit && matched.commit && matched.commit !== release.commit)) {
      throw new Error(`导入的离线网页包版本为 ${matched.version}，与当前页面所选 ${release.version} 不一致 / offline web package version mismatch`);
    }
    filePrefix = `${rootPrefix}${matched.path}`;
    packageLabel = `离线网页包 ${matched.version}`;
  } else if (pkgManifestEntry) {
    pkgManifest = parseJsonEntry(await readZipEntry(zipBytes, pkgManifestEntry), "manifest.json");
    if (pkgManifest?.brand !== "PicoNano" || pkgManifest?.target !== "esp32s3" || pkgManifest?.board !== "RDP-G01-W") {
      throw new Error("压缩包不是 PicoNano（ESP32-S3 / RDP-G01-W）官方固件包 / package target mismatch");
    }
    const pkgMode = pkgManifest.mode === "full" ? "full" : "app-only";
    const pkgCommit = String(pkgManifest.source_commit || "");
    const pkgVersion = pkgCommit ? `${pkgManifest.version}+${pkgCommit.slice(0, 7)}` : String(pkgManifest.version || "");
    if (pkgMode !== (release.mode || "full")) {
      throw new Error(pkgMode === "full"
        ? `检测到导入的是完整刷入包（${pkgVersion}），请先将上方“刷入方式”改为“完整刷入”再导入（不会自动切换刷入方式） / full package imported while in app-only mode`
        : `检测到导入的是保留布局升级包（${pkgVersion}），无法用于“完整刷入”。请下载完整包（*-full.zip）或离线网页包，或将刷入方式切回“保留布局升级” / upgrade package cannot be used for full mode`);
    }
    if ((release.commit && pkgCommit && pkgCommit !== release.commit) ||
        (pkgManifest.version && release.version && pkgManifest.version !== String(release.version).split("+")[0])) {
      throw new Error(`导入的固件包版本为 ${pkgVersion}，与当前页面所选 ${release.version} 不一致；请从下方 Gitee 链接下载对应版本 / package version mismatch`);
    }
    packageLabel = `${pkgMode === "full" ? "完整包" : "升级包"} ${pkgVersion}`;
  } else {
    throw new Error("压缩包内未找到 PicoNano 固件清单（manifest.json），请选择从 Gitee 或 GitHub 下载的官方固件 ZIP / missing PicoNano manifest.json");
  }

  const files = {};
  for (const key of keys) {
    const expected = release.files[key];
    const relPath = `${filePrefix}${expected.name}`;
    let entry = entries.get(relPath);
    let bytes = null;
    if (entry) {
      bytes = await readZipEntry(zipBytes, entry);
      if (pkgManifest?.files?.[expected.name]) {
        const inner = pkgManifest.files[expected.name];
        const innerHash = await sha256Fn(bytes);
        if (bytes.length !== inner.bytes || innerHash !== inner.sha256) {
          throw new Error(`压缩包内 ${expected.name} 与包内清单不符，文件可能已损坏 / package manifest digest mismatch`);
        }
      }
    } else if (key === "currentPartitionTable" && preserving && files.partitionTable) {
      // 若同版本两个分区表摘要相同则直接复用；不同时（历史版本独立升级包）可选回退拉取 3 KiB 校验表。
      // Reuse identical partition table directly; for historical standalone upgrades with a distinct table, allow an optional 3 KiB fallback loader.
      if (files.partitionTable.length === expected.size && (await sha256Fn(files.partitionTable)) === expected.sha256) {
        bytes = files.partitionTable.slice();
      } else if (typeof options.loadMissingFile === "function") {
        bytes = await options.loadMissingFile(key, expected);
      } else {
        throw new Error(`压缩包缺少 ${expected.name}，请改用离线网页包（PicoNano-web-installer-*.zip）或检查网络连接 / missing ${expected.name}`);
      }
    } else {
      throw new Error(`压缩包缺少必需文件 ${expected.name} / missing required file ${expected.name}`);
    }
    const digest = await sha256Fn(bytes);
    if (bytes.length !== expected.size || digest !== expected.sha256) {
      throw new Error(`${expected.name} SHA-256 校验失败，请重新从 Gitee 下载官方固件包 / ${expected.name} checksum mismatch`);
    }
    files[key] = bytes;
  }

  layoutOf(files.partitionTable);
  if (files.currentPartitionTable) layoutOf(files.currentPartitionTable);
  return { files, label: packageLabel };
}
