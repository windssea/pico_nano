// 网页安装器写入计划测试；用 node --test 运行。/ Web-installer plan tests; run with node --test.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import vm from "node:vm";
import { resetAfterFlash } from "../../installer/reset.js";
import { isLittleFs, layoutOf, md5, parseTable, planInstall, TABLE_OFFSET, TABLE_SIZE } from "../../installer/plan.js";

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
  const setup = "state.loader=loader;state.device={portInfo:{}};state.plan={steps:[{name:'app',address:0x20000,data:files.app}]};";
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
  const input = { files: preserving, deviceTable: preserving.partitionTable, mode: "app-only" };
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

test("factory-style layout keeps old NVS sectors and clears the rest", () => {
  const factory = table([["nvs", 1, 2, 0x9000, 0x5000], ["phy_init", 1, 1, 0xe000, 0x1000], ["factory", 0, 0, 0x10000, 0x200000], ["spiffs", 1, 0x82, 0x210000, 0x500000]]);
  const plan = planInstall({ files, deviceTable: factory, dataHead: blankHead, wallpaperHead: blankHead });
  assert.equal(plan.mode, "install");
  assert.ok(plan.knownTable);
  assert.deepEqual(plan.keptNvs, [0x9000, 0xa000, 0xb000, 0xc000, 0xd000]);
  assert.deepEqual(names(plan), ["write@0x20000", "write@0xf000", "erase@0xe000", "write@0xc20000", "write@0xf00000", "write@0x0", `write@0x${TABLE_OFFSET.toString(16)}`]);
  assert.ok(plan.steps.find((s) => s.address === 0xe000).data.every((b) => b === 0xff));
  assert.ok(!plan.steps.at(-2).optional, "bootloader is always written when switching layout");
});

test("unknown layout clears the whole NVS range; clearNvs does the same on upgrade", () => {
  const plan = planInstall({ files, deviceTable: new Uint8Array(0xc00).fill(0xff), dataHead: blankHead, wallpaperHead: blankHead });
  assert.ok(!plan.knownTable);
  assert.equal(plan.steps.filter((s) => s.kind === "erase").length, 6);
  const cleared = planInstall({ files, deviceTable: files.partitionTable, dataHead: fsHead, wallpaperHead: fsHead, clearNvs: true });
  assert.equal(cleared.steps.filter((s) => s.kind === "erase").length, 6);
  assert.deepEqual(cleared.keptNvs, []);
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
  const context = vm.createContext({ document, navigator: {}, console, files, loader, isLittleFs, layoutOf, md5, planInstall, TABLE_OFFSET, TABLE_SIZE });
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
  const loader = { async readFlash(address, size) { reads.push([address, size]); return files.partitionTable; } };
  const context = vm.createContext({ document, navigator: {}, console, files, loader, isLittleFs, layoutOf, md5, planInstall, TABLE_OFFSET, TABLE_SIZE });
  const source = readFileSync(join(root, "installer/app.js"), "utf8").replace(/^import .*;\r?$/gm, "").replace("init().then(syncButtons);", "");
  vm.runInContext(source, context);
  document.getElementById("clear-nvs").checked = true;
  await vm.runInContext("state.files=files; state.release={mode:'app-only'}; state.loader=loader; state.device={}; prepare();", context);
  assert.deepEqual(reads, [[0x8000, 0xc00]]);
  assert.equal(document.getElementById("clear-nvs").checked, false);
  assert.equal(document.getElementById("clear-nvs").disabled, true);
  assert.equal(document.getElementById("initialize-storage").disabled, true);
  assert.equal(document.getElementById("backup").checked, false);
  assert.equal(vm.runInContext("state.plan.steps.length", context), 1);
  await assert.rejects(vm.runInContext("state.loader.readFlash=async()=>new Uint8Array(0); prepare();", context), /不匹配/);
  assert.equal(vm.runInContext("state.plan", context), null);
});
