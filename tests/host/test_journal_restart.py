"""独立进程重新打开实际文件，验证截断恢复。 / Reopen real files in separate processes to verify truncation recovery."""
import pathlib
import subprocess
import sys
import tempfile
import zlib
binary = sys.argv[1]
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    def run(*args):
        return subprocess.run([binary, *args], check=True, capture_output=True, text=True).stdout.strip()
    run("save", directory, "123")
    run("save", directory, "456")
    assert run("load", directory) == "456"
    new = (root / "b").read_bytes()
    old = (root / "a").read_bytes()
    assert new[:4] == b"PNJR" and int.from_bytes(new[4:6], "little") == 1
    assert int.from_bytes(new[6:8], "little") == 64 and int.from_bytes(new[8:16], "little") == 2
    assert int.from_bytes(new[-4:], "little") == zlib.crc32(new[:-4])
    for length in range(len(new)):
        (root / "b").write_bytes(new[:length])
        assert run("load", directory) == "123", length
        assert (root / "a").read_bytes() == old
    (root / "b").write_bytes(new)
    assert run("load", directory) == "456"
print("journal: separate-process recovery from every real-file truncation boundary")
