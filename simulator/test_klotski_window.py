# SPDX-License-Identifier: Apache-2.0
"""三国华容道真实窗口事件、提示/撤销、A/B存档、锁屏恢复与空闲零重绘。 / Real Klotski window events, hint/undo, A/B save, lock resume and idle without repaint."""
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
        for file in state.glob("klotski.[ab]"):
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
        assert "klotski screen=" in result.stdout, result.stdout
        assert not re.search(r"klotski screen=\d+ status=[1-9]", result.stdout), result.stdout
        return result.stdout

    enter = "library,tap:460:190,tap:556:648,wait:200,"
    run(enter + "window-close")
    assert not list(state.glob("klotski.[ab]"))

    # 开启名局第1关，提示后检查存档 / Start tier-3 level 0, hint, verify A/B save
    run(enter + "key:3,tap:340:880,key:h,window-close")
    saved = snapshot()
    assert saved[:4] == b"KLS1" and saved[6] == 1 and saved[7] == 40
    assert int.from_bytes(saved[14:16], "little") == 1

    # 空闲不刷屏 / Idle never repaints
    out = run(enter + "wait:2200,window-close")
    assert out.count("klotski screen=1 status=0") == 1, out

    # 锁屏唤醒+返回娱乐 / Lock, wake, exit to entertainment
    run(enter + "lock,wait:300,enter,wait:200,back,window-close")
    assert not list(state.glob("sudoku.*")) and not list(state.glob("tsumego.*"))
print("klotski SDL: hint, A/B save, lock, 2MiB budget, idle and clean exits passed")
