# SPDX-License-Identifier: Apache-2.0
"""数织真实窗口事件、直线拖涂、三档存档、锁屏恢复与空闲零重绘。 / Real Nonogram window events, straight-line drag strokes, saves, lock resume and idle without repaint."""
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
        for file in state.glob("nonogram.[ab]"):
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
        assert "nonogram screen=" in result.stdout, result.stdout
        assert not re.search(r"nonogram screen=\d+ status=[1-9]", result.stdout), result.stdout
        return result.stdout

    enter = "library,tap:460:190,tap:128:648,wait:200,"
    run(enter + "window-close")
    assert not list(state.glob("nonogram.[ab]"))

    # 启动第1关(5x5)：起点gx=(684-(132+380))//2+132=218, gy=166+(764-512)//2+132=424, cell=76
    run(enter + "tap:340:890,press:230:440,move:460:440,release:460:440,window-close")
    saved = snapshot()
    assert saved[:4] == b"PNNV" and saved[130] == 0

    # 跨进程续玩、切换标记×模式、锁屏唤醒与空闲零重绘
    out = run(enter + "wait:2200,window-close")
    assert out.count("nonogram screen=1 status=0") == 1, out

    run(enter + "key:x,key:h,lock,wait:300,enter,wait:200,window-close")
    saved2 = snapshot()
    assert saved2[:4] == b"PNNV" and saved2[28] == 1

    # 切选关并开中等(10x10)第1关
    run(enter + "key:p,tap:340:700,key:2,tap:340:890,window-close")
    saved_t1 = snapshot()
    assert saved_t1[130] == 1 and saved_t1[128] == 10 and saved_t1[129] == 10

    assert not list(state.glob("sudoku.*")) and not list(state.glob("xiangqi.*"))
print("nonogram SDL: straight-line stroke, cross/hint, A/B save, lock, 2MiB budget, idle and clean exits passed")
