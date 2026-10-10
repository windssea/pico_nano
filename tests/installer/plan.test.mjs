// 网页安装器写入计划测试；用 node --test 运行。/ Web-installer plan tests; run with node --test.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { isLittleFs, md5, parseTable, planInstall, TABLE_OFFSET } from "../../installer/plan.js";

const root = fileURLToPath(new URL("../../", import.meta.url));
const release = join(root, "releases", readdirSync(join(root, "releases")).filter((n) => n.startsWith("pico_nano-")).sort().at(-1));
const read = (name) => new Uint8Array(readFileSync(join(release, name)));
const files = {
  bootloader: read("bootloader.bin"), partitionTable: read("partition-table.bin"), otaData: read("ota_data_initial.bin"),
  app: read("pico_nano.bin"), factoryData: read("first-install/factory-data.bin"), factoryWallpaper: read("first-install/factory-wallpaper.bin"),
};
const blankHead = new Uint8Array(0x2000).fill(0xff);
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
