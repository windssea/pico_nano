// 网页安装器写入计划测试；用 node --test 运行。/ Web-installer plan tests; run with node --test.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import vm from "node:vm";
import { crc32, extractReleasePackage, giteeDownloadInfo, parseZip } from "../../installer/package.js";
import { disableNativeWatchdogs, resetAfterFlash } from "../../installer/reset.js";
import { isLittleFs, layoutOf, preservedLayoutOf, otaNeedsReset, md5, parseTable, planInstall, TABLE_OFFSET, TABLE_SIZE } from "../../installer/plan.js";

const root = fileURLToPath(new URL("../../", import.meta.url));
const release = join(root, "releases", readdirSync(join(root, "releases")).filter((n) => /^pico_nano-\d+\.\d+\.\d+-esp32s3$/.test(n)).sort().at(-1));
const read = (name) => new Uint8Array(readFileSync(join(release, name)));
const files = {
  bootloader: read("bootloader.bin"), partitionTable: read("partition-table.bin"), otaData: read("ota_data_initial.bin"),
  app: read("pico_nano.bin"), factoryData: read("first-install/factory-data.bin"), factoryWallpaper: read("first-install/factory-wallpaper.bin"),
};
const blankHead = new Uint8Array(0x100000).fill(0xff);
const fsHead = files.factoryData.slice(0, 0x2000);

/// 构造二进制分区表。/ Build a binary partition table.
function table(rows) {
  const out = new Uint8Array(0xc00).fill(0xff);
  const view = new DataView(out.buffer);
  rows.forEach(([label, type, subtype, offset, size], i) => {
    const at = i * 32;
    view.setUint16(at, 0x50aa, true); out[at + 2] = type; out[at + 3] = subtype;
    view.setUint32(at + 4, offset, true); view.setUint32(at + 8, size, true);
    out.fill(0, at + 12, at + 28);
    for (let c = 0; c < label.length; c++) out[at + 12 + c] = label.charCodeAt(c);
    view.setUint32(at + 28, 0, true);
  });
  return out;
}
const names = (plan) => plan.steps.map((s) => `${s.kind}@0x${s.address.toString(16)}`);

test("native USB reset clears download latch and uses esptool S3 watchdog sequence", async () => {
  const writes = [], after = [], waits = [];
  const loader = { chip: { CHIP_NAME: "ESP32-S3" }, async writeReg(...args) { writes.push(args); }, async after(mode) { after.push(mode); } };
  await resetAfterFlash(loader, { usbVendorId: 0x303a, usbProductId: 0x1001 }, async (ms) => waits.push(ms));
  assert.deepEqual(writes, [[0x6000812c, 0, 1], [0x600080b0, 0x50d83aa1], [0x6000809c, 2000], [0x60008098, 0xd0000102], [0x600080b0, 0]]);
  assert.deepEqual(after, []);
  assert.deepEqual(waits, [500]);
  writes.length = 0;
  await resetAfterFlash(loader, { usbVendorId: 0x10c4, usbProductId: 0xea60 });
  assert.deepEqual(writes, [[0x6000812c, 0, 1]]);
  assert.deepEqual(after, ["hard_reset"]);
});

test("reset transport failure is tolerated only after watchdog trigger; wrong target never writes", async () => {
  const native = { usbVendorId: 0x303a, usbProductId: 0x1001 };
  const loader = { chip: { CHIP_NAME: "ESP32-S3" }, async writeReg(address) { if (address === 0x60008098) throw new Error("USB disconnected"); } };
  assert.equal((await resetAfterFlash(loader, native, async () => {})).responseLost, true);
  loader.writeReg = async () => { throw new Error("initial write failed"); };
  await assert.rejects(resetAfterFlash(loader, native, async () => {}), /initial write failed/);
  loader.chip.CHIP_NAME = "ESP32";
  await assert.rejects(resetAfterFlash(loader, native), /不是 ESP32-S3/);
});

test("page distinguishes post-verify reset failure from write failure", async () => {
  const elements = new Map();
  const element = () => ({ checked: false, dataset: {}, style: {}, append() {}, replaceChildren() {}, addEventListener() {} });
  const document = { createElement: element, getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); } };
  let resetCalls = 0;
  const loader = { ui8ToBstr() { return "image"; }, async writeFlash() {} };
  const context = vm.createContext({ document, navigator: {}, console: { ...console, error() {} }, loader, files,
    resetAfterFlash: async () => { resetCalls++; throw new Error("reset unavailable"); }, md5 });
  const source = readFileSync(join(root, "installer/app.js"), "utf8").replace(/^import .*;\r?$/gm, "").replace("init().then(syncButtons);", "");
  vm.runInContext(source, context);
  const setup = "state.release={mode:'app-only'};document.getElementById('confirm-model').checked=true;state.loader=loader;state.device={portInfo:{}};state.plan={steps:[{name:'app',address:0x20000,data:files.app}]};";
  await vm.runInContext(setup + "install();", context);
  assert.equal(resetCalls, 1);
  assert.match(document.getElementById("status").textContent, /固件写入校验成功，自动复位未完成/);
  assert.equal(vm.runInContext("state.plan", context), null);
  loader.writeFlash = async () => { throw new Error("write unavailable"); };
  await vm.runInContext(setup + "install();", context);
  assert.equal(resetCalls, 1);
  assert.match(document.getElementById("status").textContent, /安装中断：write unavailable/);
});

test("shipped preserving upgrade writes only app and blocks other layouts or destructive options", () => {
  const folder = join(root, "releases/pico_nano-0.0.57-c696890-upgrade");
  const preserving = { app: new Uint8Array(readFileSync(join(folder, "pico_nano.bin"))), partitionTable: new Uint8Array(readFileSync(join(folder, "expected-partition-table.bin"))) };
  const input = { files: preserving, deviceTable: preserving.partitionTable, mode: "app-only", otaReadback: new Uint8Array(0x2000).fill(0xff) };
  const plan = planInstall(input);
  assert.equal(plan.mode, "app-only");
  assert.deepEqual(names(plan), ["write@0x20000"]);
  assert.deepEqual(plan.unrecognizedStorage, []);
  for (const deviceTable of [files.partitionTable, preserving.partitionTable.slice(0, 32), new Uint8Array(0xc00)])
    assert.throws(() => planInstall({ ...input, deviceTable }), /不匹配/);
  assert.throws(() => planInstall({ ...input, clearNvs: true }), /不能清除/);
  assert.throws(() => planInstall({ ...input, initializeStorage: true }), /不能清除/);
  assert.throws(() => planInstall({ ...input, files: { ...preserving, app: new Uint8Array(0x600001) } }), /无效/);
});

test("0.6.8 full-installed devices can preserve either exact known layout when upgrading to 0.6.9", () => {
  const old = join(root, "releases/pico_nano-0.6.8-c4a44e5-full");
  const next = join(root, "releases/pico_nano-0.6.9-e446d03-upgrade");
  const preserving = {
    app: new Uint8Array(readFileSync(join(next, "pico_nano.bin"))),
    partitionTable: new Uint8Array(readFileSync(join(next, "expected-partition-table.bin"))),
    currentPartitionTable: new Uint8Array(readFileSync(join(root, "releases/pico_nano-0.6.9-e446d03-full/partition-table.bin"))),
  };
  const installed = new Uint8Array(readFileSync(join(old, "partition-table.bin")));
  assert.deepEqual(installed, preserving.currentPartitionTable);
  assert.notDeepEqual(installed, preserving.partitionTable);
  assert.throws(() => planInstall({ files: { ...preserving, currentPartitionTable: undefined }, deviceTable: installed, mode: "app-only" }), /不匹配/);
  for (const deviceTable of [installed, preserving.partitionTable]) {
    const input = { files: preserving, deviceTable, mode: "app-only", otaReadback: new Uint8Array(0x2000).fill(0xff) };
    const plan = planInstall(input);
    assert.deepEqual(names(plan), ["write@0x20000"]);
    assert.equal(plan.steps[0].data, preserving.app);
    assert.throws(() => planInstall({ ...input, clearNvs: true }), /不能清除/);
    assert.throws(() => planInstall({ ...input, initializeStorage: true }), /不能清除/);
  }
  for (const at of [0, 8, 12, 31, 95, 511, 3071]) {
    const changed = installed.slice(); changed[at] ^= 1;
    assert.throws(() => planInstall({ files: preserving, deviceTable: changed, mode: "app-only" }), /不匹配/);
  }
  assert.throws(() => planInstall({ files: preserving, deviceTable: installed.slice(0, -1), mode: "app-only" }), /不匹配/);
});

test("page downloads and authenticates the additional preserving table before building any plan", async () => {
  const elements = new Map();
  const element = () => ({ checked: false, dataset: {}, style: {}, append() {}, replaceChildren() {}, addEventListener() {} });
  const document = { createElement: element, getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); } };
  const source = readFileSync(join(root, "installer/app.js"), "utf8").replace(/^import .*;\r?$/gm, "").replace("init().then(syncButtons);", "");
  const downloads = [], data = { "legacy.bin": files.partitionTable, "current.bin": files.partitionTable, "app.bin": files.app };
  const release = { mode: "app-only", path: "firmware/known/", files: Object.fromEntries([
    ["partitionTable", "legacy.bin"], ["currentPartitionTable", "current.bin"], ["app", "app.bin"],
  ].map(([key, name]) => [key, { name, size: data[name].length, sha256: createHash("sha256").update(data[name]).digest("hex") }])) };
  let corrupt = false;
  const context = vm.createContext({ document, navigator: {}, console, URL, Uint8Array, layoutOf, release,
    location: { href: "https://example.test/" }, crypto: { subtle: { async digest(algorithm, bytes) { return createHash("sha256").update(bytes).digest(); } } },
    async fetch(url) { const name = url.pathname.split("/").at(-1); downloads.push(name); const bytes = data[name].slice(); if (corrupt && name === "current.bin") bytes[0] ^= 1; return { ok: true, async arrayBuffer() { return bytes.buffer; } }; } });
  vm.runInContext(source, context);
  const loaded = await vm.runInContext("loadFiles(release)", context);
  assert.deepEqual(downloads, ["legacy.bin", "current.bin", "app.bin"]);
  assert.deepEqual(loaded.currentPartitionTable, files.partitionTable);
  corrupt = true; downloads.length = 0;
  await assert.rejects(vm.runInContext("loadFiles(release)", context), /current.bin 校验失败/);
  assert.deepEqual(downloads, ["legacy.bin", "current.bin"]);
});

test("md5 matches node crypto", () => {
  for (const size of [0, 1, 55, 56, 63, 64, 65, 1000, 4096]) {
    const data = new Uint8Array(size).map((_, i) => (i * 31 + 7) & 0xff);
    assert.equal(md5(data), createHash("md5").update(data).digest("hex"), `size ${size}`);
  }
  assert.equal(md5(files.bootloader), createHash("md5").update(files.bootloader).digest("hex"));
});

test("parses the shipped partition table", () => {
  const rows = parseTable(files.partitionTable);
  assert.deepEqual(rows.map((r) => [r.label, r.offset]), [
    ["nvs", 0x9000], ["otadata", 0xf000], ["phy_init", 0x11000], ["ota_0", 0x20000],
    ["ota_1", 0x620000], ["data", 0xc20000], ["assets", 0xd20000], ["wallpaper", 0xf00000]]);
  assert.equal(parseTable(new Uint8Array(0xc00).fill(0xff)), null);
  assert.equal(parseTable(new Uint8Array(0xc00)), null);
});

test("detects LittleFS heads", () => {
  assert.ok(isLittleFs(fsHead));
  assert.ok(isLittleFs(files.factoryWallpaper.slice(0, 0x2000)));
  assert.ok(!isLittleFs(blankHead));
});

test("upgrade keeps NVS, data and the table", () => {
  const plan = planInstall({ files, deviceTable: files.partitionTable, dataHead: fsHead, wallpaperHead: fsHead });
  assert.equal(plan.mode, "upgrade");
  assert.deepEqual(names(plan), ["write@0x20000", "write@0xf000", "write@0x0"]);
  assert.ok(plan.steps.at(-1).optional, "bootloader is written only when it differs");
  assert.deepEqual(plan.keptNvs, [0x9000, 0xa000, 0xb000, 0xc000, 0xd000, 0xe000]);
});

test("upgrade on a blank data partition writes the first-install images", () => {
  const plan = planInstall({ files, deviceTable: files.partitionTable, dataHead: blankHead, wallpaperHead: blankHead });
  assert.deepEqual(names(plan), ["write@0x20000", "write@0xf000", "write@0xc20000", "write@0xf00000", "write@0x0"]);
});

test("foreign layout never silently clears NVS; explicit choice enables migration", () => {
  const factory = table([["nvs", 1, 2, 0x9000, 0x5000], ["phy_init", 1, 1, 0xe000, 0x1000], ["factory", 0, 0, 0x10000, 0x200000], ["spiffs", 1, 0x82, 0x210000, 0x500000]]);
  const plan = planInstall({ files, deviceTable: factory, dataHead: blankHead, wallpaperHead: blankHead });
  assert.equal(plan.mode, "install");
  assert.ok(plan.knownTable);
  assert.deepEqual(plan.keptNvs, [0x9000, 0xa000, 0xb000, 0xc000, 0xd000, 0xe000]);
  assert.ok(plan.needsNvsClear);
  assert.equal(plan.steps.filter((s) => s.kind === "erase").length, 0);
  assert.deepEqual(names(plan), ["write@0x20000", "write@0xf000", "write@0xc20000", "write@0xf00000", "write@0x0", `write@0x${TABLE_OFFSET.toString(16)}`]);
  const cleared = planInstall({ files, deviceTable: factory, dataHead: blankHead, wallpaperHead: blankHead, clearNvs: true });
  assert.equal(cleared.needsNvsClear, false);
  assert.equal(cleared.steps.filter((s) => s.kind === "erase").length, 6);
  assert.ok(!plan.steps.at(-2).optional, "bootloader is always written when switching layout");
});

test("unknown layout requires explicit NVS clearing; no automatic erases", () => {
  const plan = planInstall({ files, deviceTable: new Uint8Array(0xc00).fill(0xff), dataHead: blankHead, wallpaperHead: blankHead });
  assert.ok(!plan.knownTable);
  assert.equal(plan.steps.filter((s) => s.kind === "erase").length, 0);
  assert.ok(plan.needsNvsClear);
  const cleared = planInstall({ files, deviceTable: files.partitionTable, dataHead: fsHead, wallpaperHead: fsHead, clearNvs: true });
  assert.equal(cleared.steps.filter((s) => s.kind === "erase").length, 6);
  assert.deepEqual(cleared.keptNvs, []);
});

test("full migration initializes incompatible nonblank storage only by explicit choice", () => {
  const foreign = table([["nvs", 1, 2, 0x9000, 0x6000], ["factory", 0, 0, 0x10000, 0x600000]]);
  const input = { files, deviceTable: foreign, dataHead: fsHead, wallpaperHead: fsHead };
  const plan = planInstall(input);
  assert.ok(plan.needsStorageInitialization);
  assert.equal(plan.needsNvsClear, false);
  assert.ok(!plan.steps.some((s) => s.address === 0xc20000 || s.address === 0xf00000));
  const initialized = planInstall({ ...input, initializeStorage: true });
  assert.equal(initialized.needsStorageInitialization, false);
  assert.ok(initialized.steps.some((s) => s.address === 0xc20000));
  assert.ok(initialized.steps.some((s) => s.address === 0xf00000));
  assert.equal(initialized.steps.at(-1).address, TABLE_OFFSET);
  const recognized = planInstall({ ...input, deviceTable: files.partitionTable, initializeStorage: true });
  assert.ok(recognized.steps.some((s) => s.address === 0xc20000), "explicit initialization also clears recognized data");
});

test("full package rejects missing images, wrong sizes, short table reads and overlapping layout", () => {
  const input = { files, deviceTable: files.partitionTable, dataHead: fsHead, wallpaperHead: fsHead };
  assert.throws(() => planInstall({ ...input, deviceTable: files.partitionTable.slice(0, 32) }), /读取不完整/);
  for (const key of ["app", "bootloader", "otaData", "factoryData", "factoryWallpaper"])
    assert.throws(() => planInstall({ ...input, files: { ...files, [key]: undefined } }), /缺少/);
  assert.throws(() => planInstall({ ...input, files: { ...files, factoryData: fsHead } }), /大小无效/);
  const overlap = table(parseTable(files.partitionTable).map((e) => [e.label, e.type, e.subtype, e.label === "data" ? 0x20000 : e.offset, e.size]));
  assert.throws(() => planInstall({ ...input, files: { ...files, partitionTable: overlap } }), /范围无效/);
});

test("rejects an app larger than ota_0", () => {
  const big = { ...files, app: new Uint8Array(0x600001) };
  assert.throws(() => planInstall({ files: big, deviceTable: files.partitionTable, dataHead: fsHead, wallpaperHead: fsHead }));
});

test("unrecognized nonblank storage is preserved until explicit initialization", () => {
  const damaged = new Uint8Array(blankHead); damaged[123] = 0x42;
  const plan = planInstall({ files, deviceTable: files.partitionTable, dataHead: damaged, wallpaperHead: damaged });
  assert.deepEqual(names(plan), ["write@0x20000", "write@0xf000", "write@0x0"]);
  assert.deepEqual(plan.unrecognizedStorage, ["data", "wallpaper"]);
  const reset = planInstall({ files, deviceTable: files.partitionTable, dataHead: damaged, wallpaperHead: damaged, initializeStorage: true });
  assert.ok(reset.steps.some((s) => s.address === 0xc20000));
  assert.ok(reset.steps.some((s) => s.address === 0xf00000));
});

test("short or missing storage reads never authorize automatic initialization", () => {
  for (const head of [undefined, new Uint8Array(0), blankHead.slice(0, 4096), blankHead.slice(0, 8192)]) {
    const plan = planInstall({ files, deviceTable: files.partitionTable, dataHead: head, wallpaperHead: fsHead });
    assert.ok(!plan.steps.some((s) => s.address === 0xc20000));
    assert.deepEqual(plan.unrecognizedStorage, ["data"]);
  }
});

test("blank headers with data later in the partition are preserved", () => {
  const bytes = new Uint8Array(blankHead); bytes[0x9000] = 1;
  const plan = planInstall({ files, deviceTable: files.partitionTable, dataHead: bytes, wallpaperHead: fsHead });
  assert.ok(!plan.steps.some((s) => s.address === 0xc20000));
  assert.deepEqual(plan.unrecognizedStorage, ["data"]);
});

test("page preparation reads unknown partitions fully and wires explicit recovery", async () => {
  // 运行真实页面控制器，仅替换DOM与串口；覆盖读取与计划的接线。/ Run the real page controller with DOM/serial substitutes to cover read and plan wiring.
  const elements = new Map();
  const element = () => ({ checked: false, dataset: {}, style: {}, events: {}, append() {}, replaceChildren() {}, addEventListener(name, fn) { this.events[name] = fn; } });
  const document = { createElement: element, getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); } };
  const reads = [];
  const unknown = new Uint8Array(blankHead); unknown[0x9000] = 1;
  const loader = { async readFlash(address, size) {
    reads.push([address, size]);
    if (address === TABLE_OFFSET) return files.partitionTable;
    if (address === 0xc20000) return unknown.slice(0, size);
    if (address === 0xf00000) return fsHead;
    throw new Error("Unexpected read");
  } };
  const context = vm.createContext({ document, navigator: {}, console, files, loader, isLittleFs, layoutOf, preservedLayoutOf, otaNeedsReset, md5, planInstall, TABLE_OFFSET, TABLE_SIZE });
  const source = readFileSync(join(root, "installer/app.js"), "utf8").replace(/^import .*;\r?$/gm, "").replace("init().then(syncButtons);", "");
  vm.runInContext(source, context);
  await vm.runInContext("state.files=files; state.loader=loader; state.device={}; prepare();", context);
  assert.deepEqual(reads, [[0x8000, 0xc00], [0xc20000, 8192], [0xc20000, 0x100000], [0xf00000, 8192]]);
  assert.match(document.getElementById("plan-note").textContent, /保留原数据/);
  assert.ok(document.getElementById("backup").checked);
  assert.ok(!vm.runInContext("state.plan.steps.some(s=>s.address===0xc20000)", context));
  const checkbox = document.getElementById("initialize-storage"); checkbox.checked = true; checkbox.events.change();
  assert.ok(vm.runInContext("state.plan.steps.some(s=>s.address===0xc20000)", context));
  assert.match(document.getElementById("plan-note").textContent, /已选择清除并初始化/);
});

test("preserving page reads only table, disables destructive options and clears stale plans on mismatch", async () => {
  const elements = new Map();
  const element = () => ({ checked: false, dataset: {}, style: {}, events: {}, append() {}, replaceChildren() {}, addEventListener(name, fn) { this.events[name] = fn; } });
  const document = { createElement: element, getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); } };
  const reads = [];
  const loader = { async readFlash(address, size) { reads.push([address, size]); return address === TABLE_OFFSET ? files.partitionTable : new Uint8Array(size).fill(0xff); } };
  const context = vm.createContext({ document, navigator: {}, console, files, loader, isLittleFs, layoutOf, preservedLayoutOf, otaNeedsReset, md5, planInstall, TABLE_OFFSET, TABLE_SIZE });
  const source = readFileSync(join(root, "installer/app.js"), "utf8").replace(/^import .*;\r?$/gm, "").replace("init().then(syncButtons);", "");
  vm.runInContext(source, context);
  document.getElementById("clear-nvs").checked = true;
  await vm.runInContext("state.files=files; state.release={mode:'app-only'}; state.loader=loader; state.device={}; prepare();", context);
  assert.deepEqual(reads, [[0x8000, 0xc00], [layoutOf(files.partitionTable).otadata.offset, 0x2000]]);
  assert.equal(document.getElementById("clear-nvs").checked, false);
  assert.equal(document.getElementById("clear-nvs").disabled, true);
  assert.equal(document.getElementById("initialize-storage").disabled, true);
  assert.equal(document.getElementById("backup").checked, false);
  assert.equal(vm.runInContext("state.plan.steps.length", context), 1);
  await assert.rejects(vm.runInContext("state.loader.readFlash=async()=>new Uint8Array(0); prepare();", context), /不匹配/);
  assert.equal(vm.runInContext("state.plan", context), null);
});

test("page keeps connection after mismatch and supports explicit same-source full migration", async () => {
  const elements = new Map();
  const element = () => ({ checked: false, dataset: {}, style: {}, events: {}, children: [],
    append(child) { this.children.push(child); }, replaceChildren() { this.children = []; }, addEventListener(name, fn) { this.events[name] = fn; } });
  const document = { createElement: element, getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); } };
  const foreign = table([["factory", 0, 0, 0x10000, 0x600000]]);
  let writes = 0;
  const loader = { async readFlash(address) { return address === TABLE_OFFSET ? foreign : fsHead; }, async writeFlash() { writes++; } };
  const context = vm.createContext({ document, navigator: { serial: {} }, console: { ...console, error() {} }, files, loader,
    giteeDownloadInfo, extractReleasePackage, disableNativeWatchdogs,
    isLittleFs, layoutOf, preservedLayoutOf, otaNeedsReset, md5, planInstall, TABLE_OFFSET, TABLE_SIZE });
  const source = readFileSync(join(root, "installer/app.js"), "utf8").replace(/^import .*;\r?$/gm, "").replace("init().then(syncButtons);", "");
  vm.runInContext(source, context);
  await vm.runInContext("state.files=files;state.release={mode:'app-only'};state.loader=loader;state.device={};prepare().catch(fail);", context);
  assert.match(document.getElementById("status").textContent, /完整刷入/);
  assert.equal(vm.runInContext("state.loader", context), loader);
  assert.equal(document.getElementById("install").disabled, true);
  document.getElementById("install-mode").value = "full";
  vm.runInContext("state.manifest={releases:[{version:'1+abc',commit:'abc',mode:'app-only'},{version:'1+abc',commit:'abc',mode:'full',files:{app:{size:100}}}]};selectMode('abc');", context);
  assert.equal(vm.runInContext("state.release.mode", context), "full");
  assert.equal(document.getElementById("confirm-full").checked, false);
  assert.equal(document.getElementById("clear-nvs").checked, false);
  await vm.runInContext("state.files=files;prepare();", context);
  document.getElementById("confirm-model").checked = true;
  document.getElementById("confirm-full").checked = true;
  vm.runInContext("syncButtons();", context);
  await vm.runInContext("install();", context);
  assert.equal(writes, 0, "confirmation cannot bypass required migration choices");
  document.getElementById("clear-nvs").checked = true;
  document.getElementById("clear-nvs").events.change();
  assert.equal(document.getElementById("install").disabled, true);
  document.getElementById("initialize-storage").checked = true;
  document.getElementById("initialize-storage").events.change();
  assert.equal(document.getElementById("install").disabled, false);
  document.getElementById("confirm-full").checked = false;
  vm.runInContext("syncButtons();", context);
  assert.equal(document.getElementById("install").disabled, true);
});

test("short backup aborts before any flash write", async () => {
  const elements = new Map();
  const element = () => ({ checked: false, dataset: {}, style: {}, append() {}, replaceChildren() {}, addEventListener() {} });
  const document = { createElement: element, getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); } };
  let writes = 0;
  const loader = { async readFlash() { return new Uint8Array(10); }, async writeFlash() { writes++; } };
  const context = vm.createContext({ document, navigator: { serial: {} }, console: { ...console, error() {} }, loader, files, md5 });
  const source = readFileSync(join(root, "installer/app.js"), "utf8").replace(/^import .*;\r?$/gm, "").replace("init().then(syncButtons);", "");
  vm.runInContext(source, context);
  await vm.runInContext("state.release={mode:'full'};state.loader=loader;state.device={};state.plan={steps:[]};document.getElementById('backup').checked=true;document.getElementById('confirm-model').checked=true;document.getElementById('confirm-full').checked=true;install();", context);
  assert.equal(writes, 0);
  assert.match(document.getElementById("status").textContent, /备份读取不完整/);
  assert.match(document.getElementById("status").textContent, /尚未写入固件/);
  assert.equal(vm.runInContext("state.plan", context), null);
});

test("release picker separates same-version modes, clears destructive choices and excludes legacy fallback", async () => {
  const elements = new Map();
  const element = () => ({ checked: false, dataset: {}, style: {}, events: {}, children: [],
    append(child) { this.children.push(child); }, replaceChildren() { this.children = []; }, addEventListener(name, fn) { this.events[name] = fn; } });
  const document = { createElement: element, getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); } };
  const releases = [
    { version: "1+abc", commit: "abc", mode: "app-only", verifiedOnHardware: true, files: { app: { size: 100 } } },
    { version: "1+abc", commit: "abc", mode: "full", files: { app: { size: 100 } } },
    { version: "0.0.57", mode: "full", legacy: true },
  ];
  const context = vm.createContext({ document, navigator: { serial: {} }, console, giteeDownloadInfo, extractReleasePackage, disableNativeWatchdogs, fetch: async () => ({ async json() { return { releases }; } }) });
  const source = readFileSync(join(root, "installer/app.js"), "utf8").replace(/^import .*;\r?$/gm, "").replace("init().then(syncButtons);", "");
  vm.runInContext(source, context);
  await vm.runInContext("init();", context);
  assert.equal(vm.runInContext("state.release.mode", context), "app-only");
  assert.equal(elements.has("hw-warning"), false);
  assert.equal(elements.has("hw-confirmed"), false);
  assert.equal(document.getElementById("release").children[0].textContent, "1+abc");
  const mode = document.getElementById("install-mode"); mode.value = "full"; mode.events.change();
  assert.equal(document.getElementById("release").children.length, 1);
  assert.equal(vm.runInContext("state.release.mode", context), "full");
  assert.equal(elements.has("hw-warning"), false);
  assert.equal(elements.has("hw-confirmed"), false);
  assert.equal(document.getElementById("release").children[0].textContent, "1+abc");
  document.getElementById("clear-nvs").checked = true;
  document.getElementById("initialize-storage").checked = true;
  document.getElementById("release").events.change();
  assert.equal(vm.runInContext("state.release.mode", context), "full");
  assert.equal(document.getElementById("clear-nvs").checked, false);
  assert.equal(document.getElementById("initialize-storage").checked, false);
  vm.runInContext("state.manifest.releases=state.manifest.releases.filter(r=>r.mode==='app-only'||r.legacy);selectMode();", context);
  assert.equal(vm.runInContext("state.release", context), undefined);
  assert.equal(elements.has("hw-warning"), false);
  assert.equal(elements.has("hw-confirmed"), false);
  assert.equal(document.getElementById("connect").disabled, true);
  assert.equal(document.getElementById("install").disabled, true);
  assert.match(document.getElementById("mode-note").textContent, /历史首装包/);
});

// OTA用过后写ota_0再清空otadata；读取不完整停止。 / After OTA use, write ota_0 then blank otadata; stop on incomplete reads.
test("preserving mode blanks used OTA selection after the app and rejects missing or short reads", () => {
  assert.equal(otaNeedsReset(new Uint8Array(8192).fill(255)), false);
  assert.equal(otaNeedsReset(new Uint8Array(8192)), true);
  for (const bytes of [undefined, new Uint8Array(4096).fill(255)]) assert.throws(() => otaNeedsReset(bytes), /OTA/);
  const folder = join(root, "releases/pico_nano-0.0.57-c696890-upgrade");
  const preserving = { app: new Uint8Array(readFileSync(join(folder, "pico_nano.bin"))), partitionTable: new Uint8Array(readFileSync(join(folder, "expected-partition-table.bin"))) };
  const used = new Uint8Array(0x2000).fill(0xff); used[0] = 1;
  const plan = planInstall({ files: preserving, deviceTable: preserving.partitionTable, mode: "app-only", otaReadback: used });
  assert.deepEqual(names(plan), ["write@0x20000", "erase@0xf000"]);
  assert.ok(plan.resetOta && plan.steps[1].data.length === 0x2000 && plan.steps[1].data.every((b) => b === 0xff));
  assert.throws(() => planInstall({ files: preserving, deviceTable: preserving.partitionTable, mode: "app-only", otaReadback: used.slice(0, 4096) }), /OTA/);
});

/// 构造未压缩 ZIP32 归档（测试损坏、篡改与路径穿越防护）。/ Build a stored ZIP32 archive for tamper/corruption/path-traversal tests.
function buildStoredZip(items) {
  const locals = [];
  const centrals = [];
  let offset = 0;
  for (const [name, data] of items) {
    const nameBytes = Buffer.from(name, "utf8");
    const payload = Buffer.from(data);
    const crc = crc32(payload);
    const local = Buffer.alloc(30 + nameBytes.length);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(0, 8);
    local.writeUInt32LE(crc, 14);
    local.writeUInt32LE(payload.length, 18);
    local.writeUInt32LE(payload.length, 22);
    local.writeUInt16LE(nameBytes.length, 26);
    nameBytes.copy(local, 30);
    locals.push(local, payload);
    const cd = Buffer.alloc(46 + nameBytes.length);
    cd.writeUInt32LE(0x02014b50, 0);
    cd.writeUInt16LE(20, 4);
    cd.writeUInt16LE(20, 6);
    cd.writeUInt16LE(0, 10);
    cd.writeUInt32LE(crc, 16);
    cd.writeUInt32LE(payload.length, 20);
    cd.writeUInt32LE(payload.length, 24);
    cd.writeUInt16LE(nameBytes.length, 28);
    cd.writeUInt32LE(offset, 42);
    nameBytes.copy(cd, 46);
    centrals.push(cd);
    offset += local.length + payload.length;
  }
  const cdBuf = Buffer.concat(centrals);
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(items.length, 8);
  eocd.writeUInt16LE(items.length, 10);
  eocd.writeUInt32LE(cdBuf.length, 12);
  eocd.writeUInt32LE(offset, 16);
  return new Uint8Array(Buffer.concat([...locals, cdBuf, eocd]));
}

test("native USB watchdog guard matches esptool S3 register sequence and skips UART bridges", async () => {
  const ops = [];
  const loader = {
    chip: { CHIP_NAME: "ESP32-S3" },
    async readReg(addr) { ops.push(["read", addr]); return 0x00001234; },
    async writeReg(addr, value) { ops.push(["write", addr, value]); },
  };
  assert.equal(await disableNativeWatchdogs(loader, { usbVendorId: 0x303a, usbProductId: 0x1001 }), true);
  assert.deepEqual(ops, [
    ["write", 0x600080b0, 0x50d83aa1],
    ["write", 0x60008098, 0],
    ["write", 0x600080b0, 0],
    ["write", 0x600080b8, 0x8f1d312a],
    ["read", 0x600080b4],
    ["write", 0x600080b4, 0x80001234],
    ["write", 0x600080b8, 0],
  ]);
  ops.length = 0;
  assert.equal(await disableNativeWatchdogs(loader, { usbVendorId: 0x10c4, usbProductId: 0xea60 }), false);
  assert.deepEqual(ops, []);
  loader.chip.CHIP_NAME = "ESP32";
  await assert.rejects(disableNativeWatchdogs(loader, { usbVendorId: 0x303a, usbProductId: 0x1001 }), /不是 ESP32-S3/);
});

test("giteeDownloadInfo builds exact direct attachment URLs and falls back cleanly", () => {
  assert.deepEqual(giteeDownloadInfo({ version: "1.1.3+12f9c1c", commit: "12f9c1c430670398f63360f3c78f18ffea3a8cb1", mode: "app-only" }), {
    releaseUrl: "https://gitee.com/windssea/pico_nano/releases/tag/v1.1.3",
    downloadUrl: "https://gitee.com/windssea/pico_nano/releases/download/v1.1.3/PicoNano-12f9c1c-upgrade.zip",
    fileName: "PicoNano-12f9c1c-upgrade.zip",
    modeLabel: "升级包",
  });
  assert.deepEqual(giteeDownloadInfo({ version: "1.1.3+12f9c1c", commit: "12f9c1c430670398f63360f3c78f18ffea3a8cb1", mode: "full" }), {
    releaseUrl: "https://gitee.com/windssea/pico_nano/releases/tag/v1.1.3",
    downloadUrl: "https://gitee.com/windssea/pico_nano/releases/download/v1.1.3/PicoNano-12f9c1c-full.zip",
    fileName: "PicoNano-12f9c1c-full.zip",
    modeLabel: "完整包",
  });
  assert.equal(giteeDownloadInfo({ version: "0.0.57", legacy: true, mode: "full" }).fileName, "");
  assert.equal(giteeDownloadInfo(null).downloadUrl, "https://gitee.com/windssea/pico_nano/releases");
});

test("extractReleasePackage verifies shipped upgrade, full and offline web-installer ZIPs and blocks mismatches/tampering", async () => {
  const sha256Hex = async (bytes) => createHash("sha256").update(bytes).digest("hex");
  const fullDir = join(root, "releases/pico_nano-1.1.3-12f9c1c-full");
  const upgDir = join(root, "releases/pico_nano-1.1.3-12f9c1c-upgrade");
  const fullMeta = JSON.parse(readFileSync(join(fullDir, "manifest.json"), "utf8"));
  const upgMeta = JSON.parse(readFileSync(join(upgDir, "manifest.json"), "utf8"));
  const entryOf = (meta, name) => ({ name, size: meta.files[name].bytes, sha256: meta.files[name].sha256 });
  const appOnlyRelease = {
    version: "1.1.3+12f9c1c", commit: upgMeta.source_commit, mode: "app-only", path: "firmware/1.1.3+12f9c1c/",
    files: {
      partitionTable: entryOf(upgMeta, "expected-partition-table.bin"),
      currentPartitionTable: { ...entryOf(fullMeta, "partition-table.bin"), name: "current-partition-table.bin" },
      app: entryOf(upgMeta, "pico_nano.bin"),
    },
  };
  const fullRelease = {
    version: "1.1.3+12f9c1c", commit: fullMeta.source_commit, mode: "full", path: "firmware/1.1.3+12f9c1c-full/",
    files: {
      bootloader: entryOf(fullMeta, "bootloader.bin"),
      partitionTable: entryOf(fullMeta, "partition-table.bin"),
      otaData: entryOf(fullMeta, "ota_data_initial.bin"),
      app: entryOf(fullMeta, "pico_nano.bin"),
      factoryData: entryOf(fullMeta, "first-install/factory-data.bin"),
      factoryWallpaper: entryOf(fullMeta, "first-install/factory-wallpaper.bin"),
    },
  };
  const upgZip = new Uint8Array(readFileSync(join(upgDir, "PicoNano-12f9c1c-upgrade.zip")));
  const fullZip = new Uint8Array(readFileSync(join(fullDir, "PicoNano-12f9c1c-full.zip")));
  const webZip = new Uint8Array(readFileSync(join(upgDir, "PicoNano-web-installer-12f9c1c.zip")));

  const upgOut = await extractReleasePackage(upgZip, appOnlyRelease, sha256Hex);
  assert.deepEqual(upgOut.files.app, new Uint8Array(readFileSync(join(upgDir, "pico_nano.bin"))));
  assert.deepEqual(upgOut.files.currentPartitionTable, new Uint8Array(readFileSync(join(fullDir, "partition-table.bin"))));

  const fullOut = await extractReleasePackage(fullZip, fullRelease, sha256Hex);
  assert.equal(Object.keys(fullOut.files).length, 6);
  assert.deepEqual(fullOut.files.bootloader, new Uint8Array(readFileSync(join(fullDir, "bootloader.bin"))));

  const webUpg = await extractReleasePackage(webZip, appOnlyRelease, sha256Hex);
  assert.deepEqual(webUpg.files.app, upgOut.files.app);
  const webFull = await extractReleasePackage(webZip, fullRelease, sha256Hex);
  assert.deepEqual(webFull.files.factoryData, fullOut.files.factoryData);

  // 模式错配不自动切换，版本不符与篡改均拒绝。 / Never auto-switch on mode mismatch; reject wrong version and tampering.
  await assert.rejects(extractReleasePackage(fullZip, appOnlyRelease, sha256Hex), /完整刷入/);
  await assert.rejects(extractReleasePackage(upgZip, fullRelease, sha256Hex), /保留布局升级包/);
  const olderZip = new Uint8Array(readFileSync(join(root, "releases/pico_nano-1.1.2-beea4b2-full/PicoNano-beea4b2-full.zip")));
  await assert.rejects(extractReleasePackage(olderZip, fullRelease, sha256Hex), /不一致/);

  const badApp = new Uint8Array(readFileSync(join(upgDir, "pico_nano.bin")));
  badApp[100] ^= 0xff;
  const tamperedZip = buildStoredZip([
    ["manifest.json", JSON.stringify(upgMeta)],
    ["expected-partition-table.bin", readFileSync(join(upgDir, "expected-partition-table.bin"))],
    ["pico_nano.bin", badApp],
  ]);
  await assert.rejects(extractReleasePackage(tamperedZip, appOnlyRelease, sha256Hex), /清单不符|校验失败/);
  assert.throws(() => parseZip(buildStoredZip([["../evil.bin", Buffer.from("x")]])), /非法相对路径/);
  await assert.rejects(extractReleasePackage(new Uint8Array([1, 2, 3]), appOnlyRelease, sha256Hex), /ZIP/);
});

test("page local ZIP import skips network downloads, connects with watchdog guard and clears on mode switch", async () => {
  const elements = new Map();
  const element = () => ({ checked: false, hidden: false, disabled: false, value: "", dataset: {}, style: {}, events: {}, children: [],
    classList: { add() {}, remove() {} },
    append(child) { this.children.push(child); }, replaceChildren() { this.children = []; }, addEventListener(name, fn) { this.events[name] = fn; } });
  const document = { createElement: element, getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); } };
  const upgDir = join(root, "releases/pico_nano-1.1.3-12f9c1c-upgrade");
  const fullDir = join(root, "releases/pico_nano-1.1.3-12f9c1c-full");
  const upgMeta = JSON.parse(readFileSync(join(upgDir, "manifest.json"), "utf8"));
  const fullMeta = JSON.parse(readFileSync(join(fullDir, "manifest.json"), "utf8"));
  const entryOf = (meta, name) => ({ name, size: meta.files[name].bytes, sha256: meta.files[name].sha256 });
  const releases = [
    { version: "1.1.3+12f9c1c", commit: upgMeta.source_commit, mode: "app-only", path: "firmware/1.1.3+12f9c1c/",
      files: { partitionTable: entryOf(upgMeta, "expected-partition-table.bin"),
        currentPartitionTable: { ...entryOf(fullMeta, "partition-table.bin"), name: "current-partition-table.bin" },
        app: entryOf(upgMeta, "pico_nano.bin") } },
    { version: "1.1.3+12f9c1c", commit: fullMeta.source_commit, mode: "full", path: "firmware/1.1.3+12f9c1c-full/",
      files: { bootloader: entryOf(fullMeta, "bootloader.bin"), partitionTable: entryOf(fullMeta, "partition-table.bin"),
        otaData: entryOf(fullMeta, "ota_data_initial.bin"), app: entryOf(fullMeta, "pico_nano.bin"),
        factoryData: entryOf(fullMeta, "first-install/factory-data.bin"), factoryWallpaper: entryOf(fullMeta, "first-install/factory-wallpaper.bin") } },
  ];
  const fetches = [];
  const callOrder = [];
  const upgZipBuf = readFileSync(join(upgDir, "PicoNano-12f9c1c-upgrade.zip"));
  const tableBytes = new Uint8Array(readFileSync(join(upgDir, "expected-partition-table.bin")));
  const fakePort = { getInfo: () => ({ usbVendorId: 0x303a, usbProductId: 0x1001 }) };
  let lastTransport = null;
  class FakeTransport { constructor() { this.trace = () => "orig"; this.hexConvert = () => "orig"; lastTransport = this; } async disconnect() {} }
  class FakeLoader {
    constructor() { this.chip = { CHIP_NAME: "ESP32-S3", async readMac() { return "14:c1:9f:d1:85:98"; } }; }
    async main() { callOrder.push("main"); return "ESP32-S3"; }
    async getFlashSize() { callOrder.push("getFlashSize"); return 16384; }
    async readFlash(offset, size) { return offset === TABLE_OFFSET ? tableBytes : new Uint8Array(size).fill(0xff); }
  }
  const context = vm.createContext({
    document, console, URL, Uint8Array, location: { href: "https://example.test/" },
    navigator: { serial: { async requestPort() { callOrder.push("requestPort"); return fakePort; } } },
    crypto: { subtle: { async digest(_alg, bytes) { return createHash("sha256").update(bytes).digest(); } } },
    Transport: FakeTransport, ESPLoader: FakeLoader,
    giteeDownloadInfo, extractReleasePackage,
    disableNativeWatchdogs: async () => { callOrder.push("disableWatchdogs"); return true; },
    resetAfterFlash, isLittleFs, layoutOf, preservedLayoutOf, otaNeedsReset, md5, planInstall, TABLE_OFFSET, TABLE_SIZE,
    async fetch(url) {
      fetches.push(String(url));
      if (String(url) === "firmware/manifest.json") return { ok: true, async json() { return { releases }; } };
      throw new Error(`unexpected online fetch: ${url}`);
    },
  });
  const source = readFileSync(join(root, "installer/app.js"), "utf8").replace(/^import .*;\r?$/gm, "").replace("init().then(syncButtons);", "");
  vm.runInContext(source, context);
  await vm.runInContext("init();", context);
  assert.equal(document.getElementById("gitee-download").href, "https://gitee.com/windssea/pico_nano/releases/download/v1.1.3/PicoNano-12f9c1c-upgrade.zip");
  const fileObj = { name: "PicoNano-12f9c1c-upgrade.zip", size: upgZipBuf.length, async arrayBuffer() { return upgZipBuf.buffer.slice(upgZipBuf.byteOffset, upgZipBuf.byteOffset + upgZipBuf.byteLength); } };
  await document.getElementById("package-file").events.change({ target: { files: [fileObj] } });
  assert.equal(document.getElementById("clear-package").hidden, false);
  assert.match(document.getElementById("local-package-status").textContent, /PicoNano-12f9c1c-upgrade\.zip.*SHA-256 校验通过/);
  await vm.runInContext("connect();", context);
  assert.deepEqual(fetches, ["firmware/manifest.json"], "local ZIP import must skip online firmware fetches");
  assert.deepEqual(callOrder, ["requestPort", "main", "disableWatchdogs", "getFlashSize"]);
  assert.equal(lastTransport.hexConvert(new Uint8Array(32)), "", "transport trace overhead should be suppressed on instance");
  assert.equal(vm.runInContext("state.plan.steps.length", context), 1);
  // 切换刷入方式自动清空旧模式已导入包并更新 Gitee 下载链接。 / Switching mode clears the imported package and updates the Gitee link.
  document.getElementById("install-mode").value = "full";
  vm.runInContext("state.loader=null; state.device=null; selectMode();", context);
  assert.equal(vm.runInContext("state.localPackage", context), null);
  assert.equal(document.getElementById("clear-package").hidden, true);
  assert.equal(document.getElementById("gitee-download").href, "https://gitee.com/windssea/pico_nano/releases/download/v1.1.3/PicoNano-12f9c1c-full.zip");
});

test("connect downloads firmware before entering stub when online and clear-package resets to online source", async () => {
  const elements = new Map();
  const element = () => ({ checked: false, hidden: false, disabled: false, value: "", dataset: {}, style: {}, events: {}, children: [],
    classList: { add() {}, remove() {} },
    append(child) { this.children.push(child); }, replaceChildren() { this.children = []; }, addEventListener(name, fn) { this.events[name] = fn; } });
  const document = { createElement: element, getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); } };
  const upgDir = join(root, "releases/pico_nano-1.1.3-12f9c1c-upgrade");
  const upgMeta = JSON.parse(readFileSync(join(upgDir, "manifest.json"), "utf8"));
  const tableBytes = new Uint8Array(readFileSync(join(upgDir, "expected-partition-table.bin")));
  const appBytes = new Uint8Array(readFileSync(join(upgDir, "pico_nano.bin")));
  const releases = [{
    version: "1.1.3+12f9c1c", commit: upgMeta.source_commit, mode: "app-only", path: "firmware/1.1.3+12f9c1c/",
    files: {
      partitionTable: { name: "expected-partition-table.bin", size: tableBytes.length, sha256: upgMeta.files["expected-partition-table.bin"].sha256 },
      app: { name: "pico_nano.bin", size: appBytes.length, sha256: upgMeta.files["pico_nano.bin"].sha256 },
    },
  }];
  const order = [];
  const fakePort = { getInfo: () => ({ usbVendorId: 0x303a, usbProductId: 0x1001 }) };
  class FakeTransport { async disconnect() {} }
  class FakeLoader {
    constructor() { this.chip = { CHIP_NAME: "ESP32-S3", async readMac() { return "14:c1:9f:d1:85:98"; } }; }
    async main() { order.push("main"); return "ESP32-S3"; }
    async getFlashSize() { order.push("getFlashSize"); return 16384; }
    async readFlash(offset, size) { return offset === TABLE_OFFSET ? tableBytes : new Uint8Array(size).fill(0xff); }
  }
  const byName = { "expected-partition-table.bin": tableBytes, "pico_nano.bin": appBytes };
  const context = vm.createContext({
    document, console, URL, Uint8Array, location: { href: "https://example.test/" },
    navigator: { serial: { async requestPort() { order.push("requestPort"); return fakePort; } } },
    crypto: { subtle: { async digest(_alg, bytes) { return createHash("sha256").update(bytes).digest(); } } },
    Transport: FakeTransport, ESPLoader: FakeLoader,
    giteeDownloadInfo, extractReleasePackage,
    disableNativeWatchdogs: async () => { order.push("disableWatchdogs"); return true; },
    resetAfterFlash, isLittleFs, layoutOf, preservedLayoutOf, otaNeedsReset, md5, planInstall, TABLE_OFFSET, TABLE_SIZE,
    async fetch(url) {
      const s = String(url);
      if (s === "firmware/manifest.json") return { ok: true, async json() { return { releases }; } };
      const name = new URL(s).pathname.split("/").at(-1);
      order.push(`fetch:${name}`);
      const bytes = byName[name].slice();
      return { ok: true, async arrayBuffer() { return bytes.buffer; } };
    },
  });
  const source = readFileSync(join(root, "installer/app.js"), "utf8").replace(/^import .*;\r?$/gm, "").replace("init().then(syncButtons);", "");
  vm.runInContext(source, context);
  await vm.runInContext("init();", context);
  await vm.runInContext("connect();", context);
  assert.deepEqual(order, ["requestPort", "fetch:expected-partition-table.bin", "fetch:pico_nano.bin", "main", "disableWatchdogs", "getFlashSize"]);
  // 测试单层嵌套目录 ZIP 兼容与改回在线源。 / Verify single wrapper-folder ZIP and clearing back to online source.
  const wrappedZip = buildStoredZip([
    ["PicoNano-12f9c1c-upgrade/manifest.json", JSON.stringify(upgMeta)],
    ["PicoNano-12f9c1c-upgrade/expected-partition-table.bin", tableBytes],
    ["PicoNano-12f9c1c-upgrade/pico_nano.bin", appBytes],
  ]);
  const wrappedFile = { name: "wrapped.zip", size: wrappedZip.length, async arrayBuffer() { return wrappedZip.buffer.slice(wrappedZip.byteOffset, wrappedZip.byteOffset + wrappedZip.byteLength); } };
  await document.getElementById("local-package-box").events.drop({ preventDefault() {}, dataTransfer: { files: [wrappedFile] } });
  assert.equal(vm.runInContext("state.localPackage.fileName", context), "wrapped.zip");
  vm.runInContext("state.loader=null; state.device=null;", context);
  document.getElementById("clear-package").events.click();
  assert.equal(vm.runInContext("state.localPackage", context), null);
  assert.equal(vm.runInContext("state.files", context), null);
  assert.equal(document.getElementById("clear-package").hidden, true);
});
