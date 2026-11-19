# SPDX-License-Identifier: Apache-2.0
"""围棋死活题真实窗口事件、提示落子过关、败局悔棋、A/B存档与空闲零重绘。 / Real Tsumego window events, hint-to-solve, undo from fail, A/B save and idle without repaint."""
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    books, state = root / "books", root / "state"
    books.mkdir()
    state.mkdir()

    def snapshot():
        records = []
        for file in state.glob("tsumego.[ab]"):
            data = file.read_bytes()
            assert data[:4] == b"PNBL"
            assert (zlib.crc32(data[:-4]) & 0xFFFFFFFF) == int.from_bytes(data[-4:], "little")
            records.append((int.from_bytes(data[12:20], "little"), data[20:-4]))
        assert records
        return max(records)[1]

    def run(script):
        assert len(script) <= 500, len(script)
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_CLOCK="12:00", PN_SIM_LIBRARY_SCRIPT=script)
        result = subprocess.run(
            [sys.argv[1], "--library", str(books), "--state-dir", str(state), "--budget", "2097152"],
            env=env, capture_output=True, text=True, timeout=90,
        )
        assert result.returncode == 0 and "used=0 live=0" in result.stdout, (result.stdout, result.stderr)
        assert "tsumego screen=" in result.stdout, result.stdout
        assert not re.search(r"tsumego screen=\d+ status=[1-9]", result.stdout), result.stdout
        return result.stdout

    enter = "library,tap:460:190,tap:128:888,wait:200,"
    run(enter + "window-close")
    assert not list(state.glob("tsumego.[ab]"))

    # 开启入门第1题（直三点眼），故意点错(8,8)触发白反击失败，再悔棋，再锁屏恢复 / Start #0, wrong move -> fail -> undo -> lock
    run(enter + "tap:340:880,tap:614:772,window-close")
    saved = snapshot()
    assert saved[:4] == b"PTG1" and saved[6] == 1 and saved[8] == 1 and saved[9] == 2  # PN_TG_FAILED

    out = run(enter + "wait:2200,window-close")
    assert out.count("tsumego screen=1 status=0") == 1, out

    # 悔棋后点(0,4)直三中点一击通关（第1题直三点眼：col=4 -> x=70+4*68=342, row=0 -> y=228）
    run(enter + "key:z,tap:342:228,wait:200,back,window-close")
    solved = snapshot()
    assert solved[9] == 1 and (solved[24] & 1) == 1  # PN_TG_SOLVED and completed bit 0

    assert not list(state.glob("sudoku.*")) and not list(state.glob("klotski.*"))
print("tsumego SDL: refute, undo, tap-to-solve, A/B save, 2MiB budget, idle and clean exits passed")
