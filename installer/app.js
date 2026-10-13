/// 网页安装器界面与串口流程：读取设备现状、按 plan.js 写入并逐段 MD5 校验。
/// Web-installer UI and serial flow: read the device state, write per plan.js and verify every region by MD5.
import { ESPLoader, Transport } from "./vendor/esptool-js-0.5.7/bundle.js";
import { isLittleFs, layoutOf, md5, planInstall, TABLE_OFFSET, TABLE_SIZE } from "./plan.js";
import { resetAfterFlash } from "./reset.js";

const FLASH_BYTES = 16 * 1024 * 1024;
const BAUD = 115200;
const HEAD = 0x2000;
const FILE_KEYS = ["bootloader", "partitionTable", "otaData", "app", "factoryData", "factoryWallpaper"];

const $ = (id) => document.getElementById(id);
const state = { manifest: null, release: null, files: null, loader: null, transport: null, device: null, plan: null, busy: false };

function log(text) {
  const line = document.createElement("div");
  line.textContent = text;
  $("log").append(line);
  $("log").scrollTop = $("log").scrollHeight;
}
const terminal = { clean() {}, writeLine: log, write: log };

function status(text, tone = "") {
  $("status").textContent = text;
  $("status").dataset.tone = tone;
}
function progress(label, done, total) {
  $("progress").hidden = false;
  $("progress-label").textContent = label;
  $("progress-bar").style.width = `${total ? Math.min(100, (done / total) * 100) : 0}%`;
}
const hex = (value) => `0x${value.toString(16).toUpperCase()}`;
const kib = (value) => (value >= 1048576 ? `${(value / 1048576).toFixed(2)} MiB` : `${Math.ceil(value / 1024)} KiB`);

async function sha256(bytes) {
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

/// 下载并以清单中的 SHA-256 校验固件文件。/ Download firmware files and check them against the manifest SHA-256.
async function loadFiles(release) {
  const files = {};
  for (const key of release.mode === "app-only" ? ["partitionTable", "app"] : FILE_KEYS) {
    const entry = release.files[key];
    const response = await fetch(new URL(entry.name, new URL(release.path, location.href)), { cache: "no-cache" });
    if (!response.ok) throw new Error(`下载 ${entry.name} 失败（${response.status}）`);
    const bytes = new Uint8Array(await response.arrayBuffer());
    if (bytes.length !== entry.size || (await sha256(bytes)) !== entry.sha256) throw new Error(`${entry.name} 校验失败，请刷新页面重试`);
    files[key] = bytes;
  }
  layoutOf(files.partitionTable);
  return files;
}

function renderRelease() {
  const release = state.release;
  $("version").textContent = release.version;
  $("release-meta").textContent = `提交 ${release.commit} · 应用 ${kib(release.files.app.size)}`;
  $("hw-warning").hidden = release.verifiedOnHardware;
}

async function init() {
  if (!("serial" in navigator)) {
    $("unsupported").hidden = false;
    $("connect").disabled = true;
  }
  try {
    const response = await fetch("firmware/manifest.json", { cache: "no-cache" });
    state.manifest = await response.json();
  } catch (error) {
    status(`读取固件清单失败：${error.message}`, "error");
    $("connect").disabled = true;
    return;
  }
  const select = $("release");
  for (const release of state.manifest.releases) {
    const option = document.createElement("option");
    option.value = release.version;
    option.textContent = `${release.version}${release.verifiedOnHardware ? "" : "（未经真机验证）"}`;
    select.append(option);
  }
  select.addEventListener("change", () => {
    state.plan = null;
    $("plan").hidden = true;
    state.release = state.manifest.releases.find((r) => r.version === select.value);
    state.files = null;
    renderRelease();
    if (state.device) prepare().catch(fail);
  });
  state.release = state.manifest.releases[0];
  renderRelease();
}

function fail(error) {
  console.error(error);
  log(`错误：${error.message || error}`);
  status(`${error.message || error}`, "error");
  state.busy = false;
  syncButtons();
}

function syncButtons() {
  const ready = state.plan && $("confirm-model").checked && !state.busy;
  $("install").disabled = !ready;
  $("connect").disabled = state.busy || !("serial" in navigator);
  $("connect").textContent = state.loader ? "重新连接" : "连接设备";
  $("release").disabled = state.busy;
}

async function disconnect() {
  if (state.transport) {
    try { await state.transport.disconnect(); } catch (error) { console.warn(error); }
  }
  state.transport = null;
  state.loader = null;
}

/// 连接、确认芯片与闪存容量，并读取当前分区表与数据分区开头。/ Connect, confirm chip and flash size, then read the table and data heads.
async function connect() {
  state.busy = true;
  state.plan = null;
  $("plan").hidden = true;
  syncButtons();
  await disconnect();
  status("请选择设备串口…");
  const port = await navigator.serial.requestPort({});
  state.transport = new Transport(port, false);
  state.loader = new ESPLoader({ transport: state.transport, baudrate: BAUD, romBaudrate: 115200, terminal });
  status("正在连接，若一直无响应请让设备进入下载模式后重试…");
  const chip = await state.loader.main();
  if (state.loader.chip.CHIP_NAME !== "ESP32-S3") throw new Error(`检测到 ${chip}，本固件只支持 ESP32-S3`);
  const flashKiB = await state.loader.getFlashSize();
  if (flashKiB * 1024 !== FLASH_BYTES) throw new Error(`闪存容量为 ${flashKiB} KiB，本固件需要 16 MiB`);
  const mac = await state.loader.chip.readMac(state.loader);
  state.device = { chip, mac, portInfo: port.getInfo() };
  $("device").textContent = `${chip} · 16 MiB · MAC ${mac}`;
  await prepare();
}

/// 读取设备现状并生成写入计划。/ Read the device state and build the write plan.
async function prepare() {
  state.busy = true;
  syncButtons();
  if (!state.files) {
    status(`正在下载并校验 ${state.release.version} 固件…`);
    state.files = await loadFiles(state.release);
  }
  const layout = layoutOf(state.files.partitionTable);
  status("正在读取设备分区表…");
  const deviceTable = await state.loader.readFlash(TABLE_OFFSET, TABLE_SIZE);
  // 未识别头部时读取完整分区，不能凭空白头部认定全部数据为空。/ Read the full partition for unrecognized headers; a blank header does not prove all data is blank.
  const readStorage = async (partition) => {
    const head = await state.loader.readFlash(partition.offset, HEAD);
    return isLittleFs(head) ? head : await state.loader.readFlash(partition.offset, partition.size);
  };
  const preserving = state.release?.mode === "app-only";
  const dataHead = preserving ? null : await readStorage(layout.data);
  const wallpaperHead = preserving ? null : await readStorage(layout.wallpaper);
  state.device.reads = { deviceTable, dataHead, wallpaperHead };
  buildPlan();
  state.busy = false;
  syncButtons();
  status("已读取设备状态，请核对下方计划。", "ok");
}

function buildPlan() {
  const { deviceTable, dataHead, wallpaperHead } = state.device.reads;
  state.plan = null;
  const preserving = state.release?.mode === "app-only";
  for (const id of ["clear-nvs", "initialize-storage"]) {
    $(id).disabled = preserving;
    if (preserving) $(id).checked = false;
  }
  state.plan = planInstall({ files: state.files, deviceTable, dataHead, wallpaperHead, clearNvs: $("clear-nvs").checked, initializeStorage: $("initialize-storage").checked, mode: preserving ? "app-only" : "full" });
  const plan = state.plan;
  const mode = preserving ? "保留布局升级：仅更新应用固件"
    : plan.mode === "upgrade" ? "升级：设备上已是本固件的分区布局"
    : plan.knownTable ? "首次安装：将把设备切换到本固件的分区布局" : "首次安装：未能识别设备上的分区表";
  $("plan-mode").textContent = mode;
  $("plan-mode").dataset.tone = preserving || plan.mode === "upgrade" ? "ok" : "warn";
  const kept = plan.keptNvs.length ? `保留设置区（NVS）扇区 ${plan.keptNvs.map(hex).join("、")}。` : "设置区（NVS）将全部清空。";
  const storage = plan.unrecognizedStorage.length ? `无法识别 ${plan.unrecognizedStorage.join("、")} 分区。${$("initialize-storage").checked ? "已选择清除并初始化，请先备份。" : "保留原数据；安装后可能无法保存，请备份后再决定恢复方式。"}` : "";
  $("plan-note").textContent = preserving ? "保留引导程序、分区表、OTA 选择、NVS、阅读记录和壁纸。仅用于当前已从 ota_0 启动的 PicoNano 阅读器。" : `${kept}${storage}不整片擦除，不触碰存储卡和屏幕电源芯片。`;
  const list = $("plan-steps");
  list.replaceChildren();
  for (const step of plan.steps) {
    const item = document.createElement("li");
    const name = document.createElement("span");
    name.textContent = step.kind === "erase" ? `清空 ${step.name}` : step.name;
    const where = document.createElement("code");
    where.textContent = `${hex(step.address)} · ${kib(step.data.length)}`;
    const why = document.createElement("small");
    why.textContent = step.optional ? `${step.reason}；与设备相同则跳过` : step.reason;
    item.append(name, where, why);
    list.append(item);
  }
  $("backup").checked = $("backup").dataset.touched ? $("backup").checked : !preserving && (plan.mode !== "upgrade" || plan.unrecognizedStorage.length > 0);
  $("plan").hidden = false;
}

function saveBlob(bytes, name) {
  const url = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.textContent = `重新下载 ${name}`;
  link.click();
  $("backup-link").replaceChildren(link);
  setTimeout(() => URL.revokeObjectURL(url), 600000);
}

/// 整片读出备份；按 1 MiB 分块以便显示进度。/ Read a full-chip backup in 1 MiB chunks for progress.
async function backup() {
  const chunk = 0x100000;
  const image = new Uint8Array(FLASH_BYTES);
  for (let at = 0; at < FLASH_BYTES; at += chunk) {
    progress(`备份整片闪存 ${kib(at)} / 16 MiB`, at, FLASH_BYTES);
    image.set(await state.loader.readFlash(at, chunk), at);
  }
  progress("备份完成", 1, 1);
  const name = `read-pico-backup-${state.device.mac.replaceAll(":", "")}-${new Date().toISOString().slice(0, 10)}.bin`;
  saveBlob(image, name);
  log(`备份已保存：${name}（SHA-256 ${await sha256(image)}）`);
}

async function install() {
  state.busy = true;
  syncButtons();
  try {
    if ($("backup").checked) {
      status("正在备份整片闪存，约需数分钟，请勿断开…");
      await backup();
    }
    const steps = [];
    for (const step of state.plan.steps) {
      if (step.optional) {
        const onDevice = await state.loader.flashMd5sum(step.address, step.data.length);
        if (onDevice === md5(step.data)) { log(`${step.name} 与设备相同，跳过`); continue; }
      }
      steps.push(step);
    }
    const total = steps.reduce((sum, step) => sum + step.data.length, 0);
    const before = steps.map((_, i) => steps.slice(0, i).reduce((sum, step) => sum + step.data.length, 0));
    status("正在写入，请勿断开设备…");
    await state.loader.writeFlash({
      fileArray: steps.map((step) => ({ data: state.loader.ui8ToBstr(step.data), address: step.address })),
      flashSize: "keep", flashMode: "keep", flashFreq: "keep", eraseAll: false, compress: true,
      reportProgress: (index, written, size) => progress(`写入 ${steps[index].name}`, before[index] + (written / size) * steps[index].data.length, total),
      calculateMD5Hash: (image) => md5(state.loader.bstrToUi8(image)),
    });
    progress("写入并校验完成", 1, 1);
    log("全部区域 MD5 校验通过，正在重启设备…");
    try {
      const reset = await resetAfterFlash(state.loader, state.device.portInfo);
      if (reset?.responseLost) log("已发送 watchdog 复位；USB响应中断，请观察设备启动。");
    } catch (error) {
      log(`固件写入与MD5校验已成功，但自动复位失败：${error.message || error}`);
      await disconnect();
      state.plan = null;
      state.busy = false;
      syncButtons();
      status("固件写入校验成功，自动复位未完成。请按板级方式复位或拔线开机，无需因复位错误重新刷写。", "warn");
      return;
    }
    await disconnect();
    state.plan = null;
    status("写入校验通过，已发送复位请求。请观察设备是否正常启动；若仍停在下载模式，请按板级方式复位或拔线开机。", "ok");
  } catch (error) {
    fail(new Error(`安装中断：${error.message || error}。设备可能无法正常启动，请手动进入下载模式后重新安装，或用外部工具写回备份。`));
    return;
  }
  state.busy = false;
  syncButtons();
}

$("connect").addEventListener("click", () => connect().catch(async (error) => { await disconnect(); fail(error); }));
$("install").addEventListener("click", () => install());
$("confirm-model").addEventListener("change", syncButtons);
$("backup").addEventListener("change", () => { $("backup").dataset.touched = "1"; });
$("clear-nvs").addEventListener("change", () => { if (state.device?.reads) buildPlan(); });
$("initialize-storage").addEventListener("change", () => { if (state.device?.reads) buildPlan(); });
init().then(syncButtons);
