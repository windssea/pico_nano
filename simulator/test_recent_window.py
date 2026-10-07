"""最近入口续读与跨进程恢复。 / Recent entry resumption and cross-process recovery."""
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
    (books / "book.txt").write_text("Read Pico resumes the right book.\n" * 300, encoding="utf8")
    args = [sys.argv[1], "--library", str(books), "--state-dir", str(root / "state")]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_LIBRARY_SCRIPT="enter,next,back,recent,continue,back,quit")
    result = subprocess.run(args, capture_output=True, text=True, env=env, timeout=30)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert "recent_page start=0 count=1" in result.stdout, result.stdout
    positions = [int(x) for x in re.findall(r"library_position offset=(\d+)", result.stdout)]
    assert len(positions) == 2 and positions[0] == 0 and positions[1] > 0, result.stdout
    env["PN_SIM_LIBRARY_SCRIPT"] = "continue,back,quit"
    reopened = subprocess.run(args, capture_output=True, text=True, env=env, timeout=30)
    assert reopened.returncode == 0 and f"library_position offset={positions[1]}" in reopened.stdout and "used=0 live=0" in reopened.stdout, (reopened.stdout, reopened.stderr)
    (books / "book.txt").write_text("A different book at the same path.\n" * 300, encoding="utf8")
    env["PN_SIM_LIBRARY_SCRIPT"] = "recent,continue,quit"
    stale = subprocess.run(args, capture_output=True, text=True, env=env, timeout=30)
    assert stale.returncode == 0 and "recent_identity status=7" in stale.stdout and "library_open" not in stale.stdout, (stale.stdout, stale.stderr)
print("recent window: recorded display, durable continue and changed-content refusal passed")
