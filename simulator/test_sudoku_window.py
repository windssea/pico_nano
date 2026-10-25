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

    def run(script, budget=6291456, frames=None):
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_CLOCK="12:00", PN_SIM_LIBRARY_SCRIPT=script)
        if frames:
            frames.mkdir()
            env['PN_SIM_FRAME_DIR'] = str(frames)
        r = subprocess.run([sys.argv[1], "--library", str(books), "--state-dir", str(state), "--budget", str(budget)], env=env, capture_output=True, text=True, timeout=90)
        assert r.returncode == 0 and "used=0 live=0" in r.stdout, (r.stdout, r.stderr)
        assert "sudoku screen=" in r.stdout, r.stdout
        return r.stdout

    enter = "library,tap:460:190,tap:100:400,wait:400,"
    out = run(enter + "enter,wait:1000,window-close")
    assert "sudoku screen=1 status=0" in out, out
    first = snapshot(state)
    cell = next(i for i, clue in enumerate(first[32:113]) if not clue)
    tap = f"tap:{70+cell%9*68}:{178+cell//9*68}"
    # 候选1和9后撤销9，退出重开保持1与笔记模式。/ Notes 1/9 then undo 9; reopen preserving note 1 and note mode.
    out = run(enter + tap + ",tap:550:802,tap:134:960,tap:550:1144,tap:134:802,back,window-close", 2097152)
    notes = snapshot(state)
    assert notes[8] == 1 and notes[7] == cell and int.from_bytes(notes[194+cell*2:196+cell*2], "little") == 1
    assert notes[16:20] == first[16:20] and notes[32:113] == first[32:113]
    out = run(enter + "lock,wait:1500,enter,wait:400,window-close")
    locked = snapshot(state)
    assert "lock_preview status=0" in out and "lock_preview wake status=0" in out, out
    assert locked[32:] == notes[32:] and locked[7:14] == notes[7:14]
    assert int.from_bytes(locked[24:32], "little") - int.from_bytes(notes[24:32], "little") < 1500
    # 取消换题与重玩确认均不修改局；拖出数字键不输入。/ Cancel both new/restart confirmations; dragging off a digit must not enter it.
    out = run(enter + "tap:550:874,tap:340:1040,tap:340:844,tap:340:1120,tap:588:72,tap:340:844,press:134:960,move:675:940,release:675:940,window-close")
    assert 'sudoku screen=2 status=0' not in out, out
    same = snapshot(state)
    assert same[16:24] == notes[16:24] and same[32:] == notes[32:]
    # 空闲跨过旧动画期限和30秒自动保存，不能产生额外帧。/ Cross former animation deadlines and the 30-second autosave without extra frames.
    frames = root / 'idle'
    out = run(enter + 'wait:32000,window-close', frames=frames)
    assert len(re.findall(r'sudoku screen=1 status=0', out)) == 1, out
    assert snapshot(state)[32:] == same[32:]
    # 来回进出观察工作集，退出应归零。/ Repeated entry/exit bounds the working set, with zero allocations after exit.
    out = run("library,tap:460:190," + "enter,wait:250,back," * 15 + "window-close")
    used = [int(n) for n in re.findall(r"sudoku screen=1 status=0 used=(\d+)", out)]
    assert len(used) == 15 and max(used[3:]) - min(used[3:]) < 4096, used
    print(f"sudoku window: reboot, notes/undo, lock timing, cancellations, drag, 2 MiB and 15 entries passed; used range={min(used)}..{max(used)}")

    # 旧schema1题面已不在新简单库：升级仍恢复原局，开始新题才清旧历史。/ A retired schema1 puzzle must survive upgrade; clear old history only on a new game.
    classic = '530070000600195000098000060800060003400803001700020006060000280000419005000080079'
    payload = bytearray(764)
    payload[:4] = b'PNSG'
    payload[4:6] = (1).to_bytes(2, 'little')
    payload[7:11] = bytes([2, 1, 0, 1])
    payload[12:14] = (2).to_bytes(2, 'little')
    payload[16:20] = (0x10203040).to_bytes(4, 'little')
    payload[20:24] = (1234).to_bytes(4, 'little')
    payload[24:32] = (1234567).to_bytes(8, 'little')
    payload[32:113] = payload[113:194] = bytes(map(int, classic))
    payload[116] = 6
    payload[198:200] = (9).to_bytes(2, 'little')
    payload[356:506] = bytes([255])*150
    payload[508] = 2

    def inject(body):
        template = max((p.read_bytes() for p in state.glob('sudoku.[ab]')),
                       key=lambda data: int.from_bytes(data[12:20], 'little'))
        header = bytearray(template[:20])
        header[12:20] = (int.from_bytes(header[12:20], 'little')+1).to_bytes(8, 'little')
        data = header+body
        (state/'sudoku.a').write_bytes(data+zlib.crc32(data).to_bytes(4, 'little'))

    inject(payload)
    run(enter+'back,window-close')
    migrated = snapshot(state)
    assert migrated[4:6] == b'\x02\x00'
    assert migrated[7:24] == payload[7:24] and migrated[32:] == payload[32:]
    assert int.from_bytes(migrated[24:32], 'little') >= 1234567
    # 验证schema2题库换版也只丢弃新局去重历史，笔记/撤销保持。/ A schema2 bank change also rebases only new-game history, preserving notes/undo.
    changed = bytearray(migrated)
    changed[14:16], changed[506:508] = b'\x12\x34', b'\x56\x78'
    inject(changed)
    run(enter+'back,window-close')
    resumed = snapshot(state)
    assert resumed[7:24] == changed[7:24] and resumed[32:] == changed[32:]
    run(enter+'tap:550:874,tap:340:1040,tap:340:748,wait:1000,window-close')
    fresh = snapshot(state)
    assert sum(byte.bit_count() for byte in fresh[356:506]) == 1, fresh[356:506]
    assert 44 <= sum(bool(v) for v in fresh[32:113]) <= 50
    assert fresh[16:20] != payload[16:20] and fresh[10] == 0 and not any(fresh[194:356])
    out = run(enter+'tap:134:874,tap:342:874,back,window-close')
    hinted = snapshot(state)
    assert int.from_bytes(hinted[12:14], 'little') == 1
    assert sum(a != b for a,b in zip(fresh[113:194], hinted[113:194])) == 1
    assert 'sudoku screen=2 status=0' not in out, out
    print('sudoku upgrade: schema1 retired puzzle and schema2 changed bank retain progress; new easy game rebases history')
