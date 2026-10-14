"""完整刷入包与站点摘要回归，不连接设备。 / Full-install package/site checksum regressions without device access."""
import hashlib
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import installer_site
from package_upgrade import full_payload, validate_build_version


class FullPackages(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.board = self.root / "board"
        self.factory = self.root / "factory"
        self.board.mkdir()
        self.factory.mkdir()
        shipped = ROOT / "releases/pico_nano-0.0.57-esp32s3"
        for source, target in (("bootloader.bin", "bootloader/bootloader.bin"),
                               ("ota_data_initial.bin", "ota_data_initial.bin")):
            path = self.board / target
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((shipped / source).read_bytes())
        self.app = (shipped / "pico_nano.bin").read_bytes()
        self.table = (shipped / "partition-table.bin").read_bytes()
        for name in ("factory-data.bin", "factory-wallpaper.bin"):
            (self.factory / name).write_bytes((shipped / "first-install" / name).read_bytes())
        (self.board / "flash_args").write_text(
            "--flash-mode dio --flash-freq 80m --flash-size 16MB\n"
            "0x8000 partition_table/partition-table.bin\n0x0 bootloader/bootloader.bin\n"
            "0xf000 ota_data_initial.bin\n0x20000 pico_nano.bin\n", encoding="utf-8")

    def package(self):
        folder = self.root / "releases/pico_nano-0.6.7-abcdef0-full"
        payload = full_payload(self.board, self.factory, self.app, self.table)
        for name, data in payload.items():
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        metadata = {"mode": "full", "target": "esp32s3", "board": "RDP-G01-W", "version": "0.6.7",
                    "source_commit": "abcdef0" + "0" * 33,
                    "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                              for name, data in payload.items()}}
        (folder / "manifest.json").write_text(json.dumps(metadata), encoding="utf-8")
        return folder

    def test_complete_package_and_same_version_modes_have_distinct_paths(self):
        folder = self.package()
        full = installer_site.read_full(folder)
        self.assertEqual(full["mode"], "full")
        self.assertEqual(len(full["files"]), 6)
        self.assertEqual(full["path"], "firmware/0.6.7+abcdef0-full/")
        upgrade_folder = self.root / "releases/pico_nano-0.6.7-abcdef0-upgrade"
        upgrade_folder.mkdir()
        upgrade_data = {"pico_nano.bin": self.app, "expected-partition-table.bin": self.table}
        for name, data in upgrade_data.items():
            (upgrade_folder / name).write_bytes(data)
        metadata = {"version": "0.6.7", "source_commit": "abcdef0" + "0" * 33,
                    "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                              for name, data in upgrade_data.items()}}
        (upgrade_folder / "manifest.json").write_text(json.dumps(metadata), encoding="utf-8")
        preserving = installer_site.read_upgrade(upgrade_folder)
        self.assertNotEqual(full["path"], preserving["path"])
        (self.root / "installer").mkdir()
        with patch.object(installer_site, "ROOT", self.root):
            installer_site.build(self.root / "site")
        manifest = json.loads((self.root / "site/firmware/manifest.json").read_text(encoding="utf-8"))
        self.assertEqual([r["mode"] for r in manifest["releases"]], ["app-only", "full"])
        self.assertEqual(manifest["releases"][0]["version"], manifest["releases"][1]["version"])
        for entry in full["files"].values():
            copied = (self.root / "site" / full["path"] / entry["name"]).read_bytes()
            self.assertEqual(hashlib.sha256(copied).hexdigest(), entry["sha256"])

    def test_tampered_or_missing_bootloader_blocks_site(self):
        folder = self.package()
        path = folder / "bootloader.bin"
        path.write_bytes(b"tampered")
        with self.assertRaisesRegex(SystemExit, "checksum mismatch"):
            installer_site.read_full(folder)
        path.unlink()
        with self.assertRaises(FileNotFoundError):
            installer_site.read_full(folder)

    def test_wrong_identity_blocks_site(self):
        folder = self.package()
        path = folder / "manifest.json"
        metadata = json.loads(path.read_text(encoding="utf-8"))
        metadata["source_commit"] = "1" * 40
        path.write_text(json.dumps(metadata), encoding="utf-8")
        with self.assertRaisesRegex(SystemExit, "identity"):
            installer_site.read_full(folder)

    def test_wrong_offsets_or_short_factory_image_blocks_packaging(self):
        path = self.board / "flash_args"
        original = path.read_text(encoding="utf-8")
        path.write_text(original.replace("0x20000", "0x10000"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "addresses"):
            full_payload(self.board, self.factory, self.app, self.table)
        path.write_text(original, encoding="utf-8")
        (self.factory / "factory-data.bin").write_bytes(b"short")
        with self.assertRaisesRegex(ValueError, "sizes"):
            full_payload(self.board, self.factory, self.app, self.table)

    def test_stale_board_build_cannot_be_labeled_as_current_full_version(self):
        description = {"target": "esp32s3", "project_version": "0.0.57"}
        validate_build_version(self.app, "0.0.57", description)
        with self.assertRaisesRegex(ValueError, "version/target mismatch"):
            validate_build_version(self.app, "0.6.7", description)
        with self.assertRaisesRegex(ValueError, "version/target mismatch"):
            validate_build_version(self.app, "0.6.7", {**description, "project_version": "0.6.7"})


if __name__ == "__main__":
    unittest.main()
