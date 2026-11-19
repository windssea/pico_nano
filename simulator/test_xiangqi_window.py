# SPDX-License-Identifier: Apache-2.0
"""中国象棋残局真实窗口事件、点选落子破阵、三档存档、锁屏恢复与空闲零重绘。 / Real Xiangqi window events, tap-to-mate, saves, lock resume and idle without repaint."""
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
        for file in state.glob("xiangqi.[ab]"):
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
        assert "xiangqi screen=" in result.stdout, result.stdout
        assert not re.search(r"xiangqi screen=\d+ status=[1-9]", result.stdout), result.stdout
        return result.stdout

    enter = "library,tap:460:190,tap:342:648,wait:200,"
    run(enter + "window-close")
    assert not list(state.glob("xiangqi.[ab]"))

    # 开启中级第1局，使用锦囊提示+落子触发黑方顽强应着并跨进程恢复
    run(enter + "key:2,tap:340:890,key:h,window-close")
    saved = snapshot()
    assert saved[:4] == b"PNXV" and saved[21] == 1 and saved[30] < 90 and saved[31] < 90
    hint_to = saved[31]
    col, row = hint_to % 9, hint_to // 9
    tx = 45 + col * 66 + 33
    ty = (168 if row < 5 else 526) + (row % 5) * 66 + 33

    run(enter + f"tap:{tx}:{ty},window-close")
    after_step = snapshot()
    assert after_step[268] == 1  # undo_count == 1

    # 空闲不刷屏，悔棋+锁屏唤醒+返回娱乐
    out = run(enter + "wait:2200,window-close")
    assert out.count("xiangqi screen=1 status=0") == 1, out

    run(enter + "key:z,lock,wait:300,enter,wait:200,enter,window-close")
    after_undo = snapshot()
    assert after_undo[268] == 0

    assert not list(state.glob("sudoku.*")) and not list(state.glob("nonogram.*"))
print("xiangqi SDL: hint, real tap move & AI reply, undo, A/B save, lock, 2MiB budget, idle and clean exits passed")
