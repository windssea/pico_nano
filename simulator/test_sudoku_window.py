"""数独真实事件、跨进程存档和锁屏恢复。 / Sudoku events, cross-process saves and lock recovery."""
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib


def snapshot(state):
    records = []
    for file in state.glob("sudoku.[ab]"):
        data = file.read_bytes()
        assert data[:4] == b"PNBL" and zlib.crc32(data[:-4]) == int.from_bytes(data[-4:], "little")
        assert len(data) == 788 and int.from_bytes(data[8:12], "little") == 764
        records.append((int.from_bytes(data[12:20], "little"), data[20:-4]))
    return max(records)[1]


with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    books, state = root / "books", root / "state"
    books.mkdir()
    state.mkdir()

    def run(script, budget=6291456):
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_CLOCK="12:00", PN_SIM_LIBRARY_SCRIPT=script)
        r = subprocess.run([sys.argv[1], "--library", str(books), "--state-dir", str(state), "--budget", str(budget)], env=env, capture_output=True, text=True, timeout=90)
        assert r.returncode == 0 and "used=0 live=0" in r.stdout, (r.stdout, r.stderr)
        assert "sudoku screen=" in r.stdout, r.stdout
        return r.stdout

    enter = "library,tap:460:190,tap:100:400,wait:400,"
    out = run(enter + "enter,wait:1000,window-close")
    assert "sudoku screen=1 status=0" in out, out
    first = snapshot(state)
    cell = next(i for i, clue in enumerate(first[32:113]) if not clue)
    tap = f"tap:{70+cell%9*68}:{250+cell//9*68}"
    # 候选1和9后撤销9，退出重开保持1与笔记模式。/ Notes 1/9 then undo 9; reopen preserving note 1 and note mode.
    out = run(enter + tap + ",tap:418:880,tap:130:980,tap:550:1130,tap:110:880,back,window-close", 2097152)
    notes = snapshot(state)
    assert notes[8] == 1 and notes[7] == cell and int.from_bytes(notes[194+cell*2:196+cell*2], "little") == 1
    assert notes[16:20] == first[16:20] and notes[32:113] == first[32:113]
    out = run(enter + "lock,wait:1500,enter,wait:400,window-close")
    locked = snapshot(state)
    assert "lock_preview status=0" in out and "lock_preview wake status=0" in out, out
    assert locked[32:] == notes[32:] and locked[7:14] == notes[7:14]
    assert int.from_bytes(locked[24:32], "little") - int.from_bytes(notes[24:32], "little") < 1500
    # 取消换题与重玩确认均不修改局；拖出数字键不输入。/ Cancel both new/restart confirmations; dragging off a digit must not enter it.
    out = run(enter + "tap:570:880,tap:340:802,tap:340:1040,tap:340:844,tap:340:1120,tap:570:880,tap:340:682,tap:340:844,back,press:130:980,move:675:940,release:675:940,window-close")
    same = snapshot(state)
    assert same[16:24] == notes[16:24] and same[32:] == notes[32:]
    # 来回进出观察工作集，退出应归零。/ Repeated entry/exit bounds the working set, with zero allocations after exit.
    out = run("library,tap:460:190," + "enter,wait:250,back," * 15 + "window-close")
    used = [int(n) for n in re.findall(r"sudoku screen=1 status=0 used=(\d+)", out)]
    assert len(used) == 15 and max(used[3:]) - min(used[3:]) < 4096, used
    print(f"sudoku window: reboot, notes/undo, lock timing, cancellations, drag, 2 MiB and 15 entries passed; used range={min(used)}..{max(used)}")
