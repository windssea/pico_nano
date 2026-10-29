# SPDX-License-Identifier: Apache-2.0
"""扫雷真实事件、三档存档和空闲不刷屏。 / Real Minesweeper events, three-tier saves and idle without repaint."""
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
        for file in state.glob("minesweeper.[ab]"):
            data = file.read_bytes()
            assert data[:4] == b"PNJR" and len(data) == 180
            assert zlib.crc32(data[:-4]) == int.from_bytes(data[-4:], "little")
            records.append((int.from_bytes(data[8:16], "little"), data[16:-4]))
        return max(records)[1]

    def run(script):
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_CLOCK="12:00", PN_SIM_LIBRARY_SCRIPT=script)
        result = subprocess.run([sys.argv[1], "--library", str(books), "--state-dir", str(state), "--budget", "2097152"], env=env, capture_output=True, text=True, timeout=90)
        assert result.returncode == 0 and "used=0 live=0" in result.stdout, (result.stdout, result.stderr)
        assert "minesweeper screen=" in result.stdout, result.stdout
        assert not re.search(r"minesweeper screen=\d+ status=[1-9]", result.stdout), result.stdout
        return result.stdout

    enter = "library,tap:460:190,tap:342:888,wait:300,"
    run(enter + "window-close")
    assert not list(state.glob("minesweeper.[ab]"))
    for tier, width, height in ((0, 9, 9), (1, 12, 16), (2, 14, 18)):
        choose = "key:r,tap:340:744," if tier else ""
        run(enter + choose + f"key:{tier + 1},tap:340:980,window-close")
        saved = snapshot()
        assert saved[:4] == b"PNMV" and saved[8] == tier and saved[9] == 0
        cell = min(64, 588 // width, 756 // height) // 2 * 2
        x = (684 - cell * width) // 2 + cell // 2
        y = 244 + (756 - cell * height) // 2 + cell // 2
        # 首次翻开与跨进程恢复均使用真实点击。 / First reveal and cross-process resume use real clicks.
        run(enter + f"tap:{x}:{y},window-close")
        revealed = snapshot()
        assert revealed[9] == 1 and revealed[26:28] == b"\0\0" and any(revealed[64:96])
        out = run(enter + "wait:2500,window-close")
        assert out.count("minesweeper screen=1 status=0") == 1, out
        # 当前页签无绘制；异档取消或确认后退出均保留已保存旧局。 / The current tab does not paint; cancelling another tier or leaving its selection preserves the saved game.
        out = run(enter + f"tap:{134 + tier * 208}:150,window-close")
        assert out.count("minesweeper screen=1 status=0") == 1, out
        wanted = (tier + 1) % 3
        for action in ("tap:340:850", "tap:340:744"):
            run(enter + f"tap:{134 + wanted * 208}:150," + action + ",window-close")
            current = snapshot()
            assert current[8] == tier and current[12:16] == revealed[12:16] and current[26:128] == revealed[26:128]
        run(enter + "key:r,tap:340:850,key:f,window-close")
        assert snapshot()[10] == 1
        run(enter + "key:f,lock,wait:500,enter,wait:300,window-close")
        assert snapshot()[10] == 0
        run(enter + "key:r,tap:340:850,enter,window-close")
        assert snapshot()[8] == tier
    assert not list(state.glob("sudoku.*")) and not list(state.glob("sokoban.*"))
print("minesweeper SDL: three tiers, first reveal, saves, mode, confirmation, lock, 2MiB, idle and clean exits passed")
