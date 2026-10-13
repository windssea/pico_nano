"""升级包阻止错误设备/文件写入。 / Upgrade package rejects mismatched devices and files."""
import hashlib
import importlib.util
import json
import pathlib
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("flash_upgrade", pathlib.Path(__file__).resolve().parents[2] / "tools/flash_upgrade.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class UpgradeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.table = b"T" * 0xC00
        files = {"pico_nano.bin": b"firmware", "expected-partition-table.bin": self.table}
        manifest = {"source_commit": "test", "files": {}}
        for name, data in files.items():
            (self.root / name).write_bytes(data)
            manifest["files"][name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        (self.root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

    def test_tampered_package_never_contacts_device(self):
        (self.root / "pico_nano.bin").write_bytes(b"tampered")
        with mock.patch.object(MODULE, "run") as run:
            with self.assertRaises(ValueError):
                MODULE.flash(self.root, "TEST", 115200, "usb-reset")
            run.assert_not_called()

    def test_wrong_or_short_table_never_writes(self):
        for data in (b"wrong" * 820, self.table):
            with self.subTest(length=len(data)):
                readback = self.root / "device-partition-readback.bin"
                readback.unlink(missing_ok=True)
                with mock.patch.object(MODULE, "run", side_effect=lambda command: readback.write_bytes(data)) as run:
                    with self.assertRaises(ValueError):
                        MODULE.flash(self.root, "TEST", 115200, "usb-reset")
                    self.assertEqual(run.call_count, 1)
                    self.assertIn("read-flash", run.call_args.args[0])

    def test_matching_table_writes_only_application(self):
        def execute(command):
            if "read-flash" in command:
                (self.root / "device-partition-readback.bin").write_bytes(self.table + b"\xff" * 0x400)
        with mock.patch.object(MODULE, "run", side_effect=execute) as run:
            MODULE.flash(self.root, "TEST", 115200, "usb-reset")
            command = run.call_args.args[0]
            self.assertEqual(command[command.index("write-flash") + 1:], ["0x20000", str(self.root / "pico_nano.bin")])
            self.assertIn("watchdog-reset", command)
            self.assertEqual(run.call_count, 2)


if __name__ == "__main__":
    unittest.main()
