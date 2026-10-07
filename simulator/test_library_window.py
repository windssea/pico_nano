"""SDL书架选择/阅读/返回事件验证。 / SDL shelf selection, reading and return event verification."""
import os
import pathlib
import re
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    books = root / "books"
    books.mkdir()
    for i in range(8):
        (books / f"book{i}.txt").write_text("Read Pico keeps your place. Read a book every day.\n" * 200, encoding="utf8")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_LIBRARY_SCRIPT="enter,next,back,library-next,library-previous,enter,back,library-next,quit")
    result = subprocess.run([sys.argv[1], "--library", str(books), "--state-dir", str(root / "state")], capture_output=True, text=True, env=env, timeout=30)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert "library_open name=book0.txt" in result.stdout and "library_return" in result.stdout and "library_page first=book6.txt" in result.stdout
    assert "used=0 live=0" in result.stdout
    positions = [int(x) for x in re.findall(r"library_position offset=(\d+)", result.stdout)]
    assert len(positions) == 2 and positions[0] == 0 and positions[1] > 0, result.stdout
    assert result.stdout.count("library_page first=book6.txt") == 2, result.stdout
    env["PN_SIM_LIBRARY_SCRIPT"] = "enter,back,quit"
    resumed = subprocess.run([sys.argv[1], "--library", str(books), "--state-dir", str(root / "state")], capture_output=True, text=True, env=env, timeout=30)
    assert resumed.returncode == 0 and f"library_position offset={positions[1]}" in resumed.stdout and "used=0 live=0" in resumed.stdout, (resumed.stdout, resumed.stderr)
print("library window: actual catalog selection, reader turn, saved return and next page passed")
