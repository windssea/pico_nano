"""真实SDL事件队列和关闭恢复测试，可用dummy驱动。 / Actual SDL event-queue and close/reopen tests with dummy driver."""
import os
import pathlib
import re
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    book = root / "book.txt"
    book.write_text("Read Pico keeps your place. Read a book every day.\n" * 200, encoding="utf8")
    def run(script):
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_INPUT_SCRIPT=script)
        result = subprocess.run([sys.argv[1], "--book", str(book), "--state-dir", str(root / "state")], env=env, capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert "used=0 live=0" in result.stdout and "reader_close status=0" in result.stdout
        return [(action, int(status), int(offset)) for action, status, offset in re.findall(r"reader_event action=(\w+) status=(\d+) offset=(\d+)", result.stdout)]
    events = run("next,next,previous,larger,smaller,quit")
    assert len(events) == 6 and all(status == 0 for _, status, _ in events), events
    assert events[0][2] == 0 and events[1][2] > 0 and events[2][2] > events[1][2]
    assert events[3][2] == events[1][2] == events[4][2] == events[5][2], events
    reopened = run("quit")
    assert reopened == [("open", 0, events[-1][2])], reopened
print("SDL reader: event-driven turns, reflow anchor, save on close and process reopen passed")
