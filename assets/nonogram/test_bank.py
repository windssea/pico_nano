# SPDX-License-Identifier: Apache-2.0
"""独立核验数织题包CRC、边界、字库覆盖与纯行推理唯一解。 / Verify Nonogram bank CRC, bounds, font glyph coverage and pure line-propagation uniqueness."""
import json
import pathlib
import struct
import zlib
from generate import clues_of, line_solve

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent

allowed = set(json.loads((ROOT / "LICENSES/reading-font-manifest.json").read_text(encoding="utf-8"))["unicode_requested"])
meta = json.loads((HERE / "puzzles.json").read_text(encoding="utf-8"))
puzzles = meta["puzzles"]
raw = (HERE / "puzzles.pnng").read_bytes()

assert raw[:4] == b"PNNG"
assert struct.unpack_from("<HHII", raw, 4) == (1, 64, 90, zlib.crc32(raw[32:]) & 0xFFFFFFFF)
assert len(raw) == 5792 and not any(raw[16:32]) and len(puzzles) == 90

seen = set()
for i, p in enumerate(puzzles):
    rec = raw[32 + i * 64:32 + (i + 1) * 64]
    w, h, tier, filled, pid, passes, reserved = struct.unpack_from("<BBBBHBB", rec, 0)
    assert pid == i and tier == i // 30 and reserved == 0
    assert (w, h) in ((5, 5), (8, 8)) if tier == 0 else ((w, h) == (10, 10) if tier == 1 else (w, h) == (15, 15))
    title_raw = rec[8:32]
    zero = title_raw.index(0)
    assert zero > 0 and not any(title_raw[zero:])
    title = title_raw[:zero].decode("utf-8")
    assert title == p["title"] and all(ord(ch) in allowed for ch in title)
    bits = rec[32:64]
    for bit_idx in range(w * h, 256):
        assert not (bits[bit_idx // 8] & (1 << (bit_idx % 8)))
    grid = [[1 if (bits[(r * w + c) // 8] & (1 << ((r * w + c) % 8))) else 0 for c in range(w)] for r in range(h)]
    assert sum(sum(row) for row in grid) == filled == p["filled"]
    r_cl, c_cl = clues_of(grid, w, h)
    max_runs = max(len(cl) for cl in r_cl + c_cl)
    assert max_runs <= (6 if w == 15 else 5)
    sig = (tuple(r_cl), tuple(c_cl))
    assert sig not in seen
    seen.add(sig)
    solved, actual_passes, out = line_solve(w, h, r_cl, c_cl)
    assert solved and out == grid and actual_passes == passes
    if tier == 1:
        assert passes >= 4
    elif tier == 2:
        assert passes >= 6

print("90 Nonogram puzzles: packed CRC, glyph repertoire, bounds and pure line-propagation uniqueness passed")
