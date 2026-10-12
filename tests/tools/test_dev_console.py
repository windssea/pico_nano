"""默认GBK控制台不会把成功子进程误报为失败。 / A default GBK console cannot turn a successful subprocess into a failed command."""
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


class DevConsole(unittest.TestCase):
    def test_utf8_subprocess_output_survives_gbk_parent(self):
        tools = pathlib.Path(__file__).resolve().parents[2] / "tools"
        with tempfile.TemporaryDirectory() as folder:
            script = "import sys;sys.path.insert(0,sys.argv[1]);import dev;dev.prepare_console();from pathlib import Path;raise SystemExit(dev.run([sys.executable,'-c',\"import sys;sys.stdout.reconfigure(encoding='utf-8');print('\\u2714 \\u4e2d\\u6587')\"],False,Path(sys.argv[2])))"
            result = subprocess.run([sys.executable, "-c", script, str(tools), str(pathlib.Path(folder) / "unicode.log")], env={**os.environ, "PYTHONIOENCODING": "gbk"}, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("✔ 中文", result.stdout.decode("utf-8"))


if __name__ == "__main__":
    unittest.main()
