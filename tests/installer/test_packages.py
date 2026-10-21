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
from package_upgrade import full_payload, validate_build_version, validate_build_config


class FullPackages(unittest.TestCase):
    def test_product_config_rejects_stale_policy_and_diagnostics(self):
        config = "CONFIG_SPIRAM_SPEED_120M=y\nCONFIG_ESP_PHY_ENABLE_USB=y\nCONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y\n"
        validate_build_config(config)
        for key in ("CONFIG_SPIRAM_SPEED_120M", "CONFIG_ESP_PHY_ENABLE_USB", "CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC"):
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_build_config(config.replace(key + "=y", "# " + key + " is not set"))
        for key in ("CONFIG_PN_PROFILE", "CONFIG_PN_TEST_CONSOLE"):
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_build_config(config + key + "=y\n")

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

    def upgrade(self, app=None):
        """独立升级包只固定旧表，额外表须站点从同源完整包核验。 / An independent upgrade pins the old table; the site must verify any extra table against the matching full package."""
        folder = self.root / "releases/pico_nano-0.6.7-abcdef0-upgrade"
        folder.mkdir(parents=True)
        payload = {"pico_nano.bin": self.app if app is None else app,
                   "expected-partition-table.bin": (ROOT / "releases/pico_nano-0.0.57-6840523-upgrade/expected-partition-table.bin").read_bytes()}
        for name, data in payload.items():
            (folder / name).write_bytes(data)
        metadata = {"version": "0.6.7", "source_commit": "abcdef0" + "0" * 33,
                    "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name, data in payload.items()}}
        (folder / "manifest.json").write_text(json.dumps(metadata), encoding="utf-8")
        return folder

    def test_preserving_site_adds_only_verified_same_source_full_table(self):
        self.package()
        self.upgrade()
        (self.root / "installer").mkdir()
        with patch.object(installer_site, "ROOT", self.root):
            installer_site.build(self.root / "site")
        release = json.loads((self.root / "site/firmware/manifest.json").read_text())["releases"][0]
        self.assertEqual(release["mode"], "app-only")
        table = release["files"]["currentPartitionTable"]
        copied = (self.root / "site" / release["path"] / table["name"]).read_bytes()
        self.assertEqual(copied, self.table)
        self.assertEqual(hashlib.sha256(copied).hexdigest(), table["sha256"])
        self.assertNotEqual(table["sha256"], release["files"]["partitionTable"]["sha256"])

    def test_hardware_confirmation_matches_both_modes_by_exact_image_identity(self):
        self.package()
        self.upgrade()
        (self.root / "installer").mkdir()
        registry = {"schema": 1, "releases": [{"version": "0.6.7", "source_commit": "abcdef0" + "0" * 33,
                    "application_sha256": hashlib.sha256(self.app).hexdigest(), "verified_on_hardware": True}]}
        path = self.root / "releases/hardware-verification.json"
        for field, wrong in ((None, None), ("version", "0.6.8"), ("source_commit", "abcdef0" + "1" * 33),
                             ("application_sha256", "0" * 64)):
            with self.subTest(field=field):
                record = dict(registry["releases"][0])
                if field:
                    record[field] = wrong
                path.write_text(json.dumps({"schema": 1, "releases": [record]}), encoding="utf-8")
                with patch.object(installer_site, "ROOT", self.root):
                    installer_site.build(self.root / "site")
                releases = json.loads((self.root / "site/firmware/manifest.json").read_text())["releases"]
                self.assertEqual(len(releases), 2)
                self.assertTrue(all(r["verifiedOnHardware"] == (field is None) for r in releases))
                self.assertTrue(all(r["files"]["app"]["sha256"] == hashlib.sha256(self.app).hexdigest() for r in releases))

    def test_invalid_hardware_confirmation_does_not_publish_a_site(self):
        self.upgrade()
        (self.root / "installer").mkdir()
        good = {"version": "0.6.7", "source_commit": "abcdef0" + "0" * 33,
                "application_sha256": hashlib.sha256(self.app).hexdigest(), "verified_on_hardware": True}
        for records in ([{**good, "verified_on_hardware": "true"}], [{**good, "source_commit": "abcdef0"}], [good, good]):
            with self.subTest(records=records):
                (self.root / "releases/hardware-verification.json").write_text(
                    json.dumps({"schema": 1, "releases": records}), encoding="utf-8")
                with patch.object(installer_site, "ROOT", self.root), self.assertRaisesRegex(SystemExit, "hardware verification"):
                    installer_site.build(self.root / "site")
                self.assertFalse((self.root / "site").exists())

    def test_preserving_site_without_matching_full_has_no_extra_allowlist(self):
        self.upgrade()
        (self.root / "installer").mkdir()
        with patch.object(installer_site, "ROOT", self.root):
            installer_site.build(self.root / "site")
        release = json.loads((self.root / "site/firmware/manifest.json").read_text())["releases"][0]
        self.assertNotIn("currentPartitionTable", release["files"])

    def test_different_full_application_blocks_preserving_allowlist(self):
        self.package()
        self.upgrade(app=b"different app")
        (self.root / "installer").mkdir()
        with patch.object(installer_site, "ROOT", self.root), self.assertRaisesRegex(SystemExit, "application mismatch"):
            installer_site.build(self.root / "site")

    def test_other_source_full_package_does_not_extend_allowlist(self):
        full = self.package()
        metadata = json.loads((full / "manifest.json").read_text())
        metadata["source_commit"] = "1234567" + "0" * 33
        (full / "manifest.json").write_text(json.dumps(metadata), encoding="utf-8")
        full.rename(full.with_name("pico_nano-0.6.7-1234567-full"))
        self.upgrade()
        (self.root / "installer").mkdir()
        with patch.object(installer_site, "ROOT", self.root):
            installer_site.build(self.root / "site")
        release = next(r for r in json.loads((self.root / "site/firmware/manifest.json").read_text())["releases"] if r["mode"] == "app-only")
        self.assertNotIn("currentPartitionTable", release["files"])

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
