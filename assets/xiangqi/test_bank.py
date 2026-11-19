# SPDX-License-Identifier: Apache-2.0
"""独立核验中国象棋残局题包CRC、合法局面、字库覆盖与最短绝杀证明。 / Independently verify Xiangqi endgame bank CRC, legal positions, font coverage and shortest forced-mate proofs."""
import json
import pathlib
import struct
import zlib
from generate import legal_moves, position_valid, prove_forced_mate

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent

allowed = set(json.loads((ROOT / "LICENSES/reading-font-manifest.json").read_text(encoding="utf-8"))["unicode_requested"])
meta = json.loads((HERE / "endgames.json").read_text(encoding="utf-8"))
endgames = meta["endgames"]
raw = (HERE / "endgames.pnxq").read_bytes()

assert raw[:4] == b"PNXQ"
assert struct.unpack_from("<HHII", raw, 4) == (1, 96, 60, zlib.crc32(raw[32:]) & 0xFFFFFFFF)
assert len(raw) == 5792 and not any(raw[16:32]) and len(endgames) == 60

seen = set()
for i, e in enumerate(endgames):
    rec = raw[32 + i * 96:32 + (i + 1) * 96]
    eid, tier, pcount, plies, max_moves, mate_moves, r0, r1 = struct.unpack_from("<BBBBBBBB", rec, 0)
    assert eid == i and tier == i // 20 and r0 == 0 and r1 == 0
    assert 3 <= pcount <= 16 and plies == 2 * mate_moves - 1 and 1 <= plies <= 15
    assert max_moves >= mate_moves
    title_raw = rec[8:32]
    zero = title_raw.index(0)
    assert zero > 0 and not any(title_raw[zero:])
    title = title_raw[:zero].decode("utf-8")
    assert title == e["title"] and all(ord(ch) in allowed for ch in title)

    board = [0] * 90
    prev_pos = -1
    for slot in range(16):
        pos, code = rec[32 + slot * 2], rec[32 + slot * 2 + 1]
        if slot < pcount:
            assert prev_pos < pos < 90 and code != 0
            prev_pos = pos
            board[pos] = code
        else:
            assert pos == 0xFF and code == 0
    assert position_valid(board)
    sig = bytes(board)
    assert sig not in seen
    seen.add(sig)

    # Replay embedded proof line and confirm final checkmate/stalemate
    sim = list(board)
    for ply in range(16):
        frm, to = rec[64 + ply * 2], rec[64 + ply * 2 + 1]
        if ply < plies:
            side = ply % 2
            assert (frm, to) in legal_moves(sim, side), f"Illegal proof move at endgame {i} ply {ply}"
            sim[to] = sim[frm]
            sim[frm] = 0
        else:
            assert frm == 0 and to == 0
    assert not legal_moves(sim, 1), f"Endgame {i} proof does not end in Black checkmate/stalemate"

    # Re-prove exact forced mate depth
    actual_m, _ = prove_forced_mate(list(board), mate_moves, node_budget=250000)
    assert actual_m == mate_moves, f"Endgame {i} expected mate in {mate_moves}, got {actual_m}"
    if tier == 0:
        assert 1 <= mate_moves <= 2
    elif tier == 1:
        assert 2 <= mate_moves <= 3
    else:
        assert 3 <= mate_moves <= 4

print("60 Xiangqi endgames: packed CRC, legal positions, glyph repertoire and exact forced-mate proofs passed")
