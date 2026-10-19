"""真实SDL路由的停留、菜单/加载/锁屏排除与跨进程恢复。 / Dwell, exclusion and restart recovery through actual SDL routes."""
import hashlib
import os
import pathlib
import subprocess
import sys
import tempfile
import zlib


def snapshot(state):
    records = []
    for file in state.glob("reading.[ab]"):
        data = file.read_bytes()
        assert data[:4] == b"PNJR" and int.from_bytes(data[-4:], "little") == zlib.crc32(data[:-4])
        records.append((int.from_bytes(data[8:16], "little"), data[16:-4]))
    payload = max(records)[1]
    assert len(payload) == 252 and payload[:8] == b"PNRS\x01\0\0\0"
    value = lambda start, length=8: int.from_bytes(payload[start:start + length], "little")
    total_ms, turns = value(40), value(48)
    assert sum(value(64 + i * 16) for i in range(7)) == total_ms
    assert sum(value(60 + i * 16, 4) for i in range(7)) == turns
    assert sum(value(172 + i * 12) for i in range(7)) == total_ms
    return total_ms, turns, payload[8:40]


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    books = root / "books"
    books.mkdir()
    book = books / "book.txt"
    book.write_text("阅读统计测试。菜单和锁屏不累计停留，确认翻页才累计次数。\n" * 400, encoding="utf-8")
    digest = hashlib.sha256(book.read_bytes()).digest()

    def run(state, script):
        result = subprocess.run([sys.argv[1], "--library", str(books), "--state-dir", str(state)],
                                env=dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_PRESENT_DELAY_MS="300", PN_SIM_LIBRARY_SCRIPT=script),
                                capture_output=True, text=True, timeout=90)
        assert result.returncode == 0 and "used=0 live=0" in result.stdout, (result.stdout, result.stderr)
        return snapshot(state)

    values = []
    for name, pause in (("control", 0), ("excluded", 1800)):
        state = root / name
        script = f"library,enter,wait:700,next,previous,styles,wait:{pause},back,lock,wait:{pause},enter,wait:700,back,quit"
        ms, turns, identity = run(state, script)
        assert turns == 2 and identity == digest, (ms, turns, identity)
        assert 1000 <= ms < 7000, ms
        values.append(ms)
    # 暂停增加3.6秒，但正文计时仅允许调度抖动；不是面板/TF性能指标。/ Add 3.6 seconds paused; allow scheduling jitter only, never treating this as panel/TF performance.
    assert abs(values[1] - values[0]) < 1500, values
    ms, turns, identity = run(root / "excluded", "library,enter,wait:700,back,quit")
    assert ms >= values[1] + 500 and turns == 2 and identity == digest
    print(f"reading stats SDL: excluded menu/lock/scan delays, confirmed 2 turns and restart persistence passed; host dwell ms={values}, reopened={ms}")
