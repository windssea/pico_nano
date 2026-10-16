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
        self.table = (pathlib.Path(__file__).resolve().parents[2] / "releases/pico_nano-0.0.57-6840523-upgrade/expected-partition-table.bin").read_bytes()
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
                pathlib.Path(command[-1]).write_bytes(self.table + b"\xff" * 0x400 if command[command.index("read-flash")+1]=="0x8000" else b"\xff"*0x2000)
        with mock.patch.object(MODULE, "run", side_effect=execute) as run:
            MODULE.flash(self.root, "TEST", 115200, "usb-reset")
            command = run.call_args.args[0]
            self.assertEqual(command[command.index("write-flash") + 1:], ["0x20000", str(self.root / "pico_nano.bin")])
            self.assertIn("watchdog-reset", command)
            self.assertEqual(run.call_count, 3)

    def test_used_otadata_is_blanked_after_application(self):
        used = b"\0" + b"\xff" * 8191
        def execute(command):
            if "read-flash" in command:
                at = command[command.index("read-flash") + 1]
                data = self.table + b"\xff" * 0x400 if at == "0x8000" else used if command[-1].endswith("readback.bin") else b"\xff" * 0x2000
                pathlib.Path(command[-1]).write_bytes(data)
        with mock.patch.object(MODULE, "run", side_effect=execute) as run:
            MODULE.flash(self.root, "TEST", 115200, "usb-reset")
            commands = [call.args[0] for call in run.call_args_list]
            verbs = [next(v for v in ("read-flash", "write-flash", "erase-region") if v in c) for c in commands]
            self.assertEqual(verbs, ["read-flash", "read-flash", "write-flash", "erase-region", "read-flash"])
            self.assertEqual(commands[3][commands[3].index("erase-region") + 1:], ["0xf000", "0x2000"])
            self.assertNotIn("watchdog-reset", commands[2])
            self.assertIn("watchdog-reset", commands[4])

    def test_otadata_not_blank_after_erase_is_reported(self):
        def execute(command):
            if "read-flash" in command:
                at = command[command.index("read-flash") + 1]
                pathlib.Path(command[-1]).write_bytes(self.table + b"\xff" * 0x400 if at == "0x8000" else b"\0" * 0x2000)
        with mock.patch.object(MODULE, "run", side_effect=execute):
            with self.assertRaisesRegex(ValueError, "blank"):
                MODULE.flash(self.root, "TEST", 115200, "usb-reset")

    def test_short_otadata_never_writes(self):
        for data in (b"\xff"*4096,):
            with tempfile.TemporaryDirectory() as tmp:
                root=pathlib.Path(tmp)
                for name in ("manifest.json","pico_nano.bin","expected-partition-table.bin"):
                    (root/name).write_bytes((self.root/name).read_bytes())
                def execute(command):
                    self.assertIn("read-flash",command)
                    pathlib.Path(command[-1]).write_bytes(self.table+b"\xff"*0x400 if command[command.index("read-flash")+1]=="0x8000" else data)
                with mock.patch.object(MODULE,"run",side_effect=execute) as run:
                    with self.assertRaisesRegex(ValueError,"incomplete"):
                        MODULE.flash(root,"TEST",115200,"usb-reset")
                    self.assertEqual(run.call_count,2)


if __name__ == "__main__":
    unittest.main()
