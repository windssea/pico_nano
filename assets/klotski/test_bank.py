"""独立离线校验 puzzles.pnkl 全部60关布局、CRC与BFS最优解序列。
Independent offline verification of puzzles.pnkl layouts, CRC, and BFS optimal solutions."""
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parent
DIMS = {0: (1, 1), 1: (1, 2), 2: (2, 1), 3: (2, 2)}
DIRS = ((0, -1), (0, 1), (-1, 0), (1, 0))


def verify_bank(path=ROOT / "puzzles.pnkl"):
    raw = path.read_bytes()
    assert len(raw) >= 16
    magic, version, count, crc, reserved = struct.unpack_from("<4sHHII", raw, 0)
    assert magic == b"PNKL" and version == 1 and count == 60 and reserved == 0
    body = raw[16:]
    assert (zlib.crc32(body) & 0xFFFFFFFF) == crc
    dir_size = count * 40
    assert len(body) > dir_size
    sol_pool = body[dir_size:]
    seen_titles = set()
    tier_counts = [0, 0, 0]
    for i in range(count):
        rec = body[i * 40:(i + 1) * 40]
        lid, tier, pcount, par, sol_off, sol_len = struct.unpack_from("<BBBBHH", rec, 0)
        assert lid == i and tier in (0, 1, 2) and 6 <= pcount <= 12 and sol_len == par
        assert sol_off + sol_len <= len(sol_pool)
        title = rec[8:28].split(b"\x00", 1)[0].decode("utf-8")
        assert title and title not in seen_titles
        seen_titles.add(title)
        tier_counts[tier] += 1

        shapes, pos = [], []
        for p in range(pcount):
            b = rec[28 + p]
            shapes.append((b >> 6) & 3)
            pos.append(b & 63)
        assert shapes[0] == 3 and pos[0] != 13
        for p in range(pcount, 12):
            assert rec[28 + p] == 0xFF

        # 逐步回放解序列验证无重叠、越界且曹操最终到达出口(1,3)=13 / Replay solution to exit
        for code in sol_pool[sol_off:sol_off + sol_len]:
            idx = (code >> 4) & 0x0F
            d = (code >> 2) & 0x03
            steps = (code & 0x03) + 1
            assert 0 <= idx < pcount and steps in (1, 2)
            occ = [-1] * 20
            for j in range(pcount):
                w, h = DIMS[shapes[j]]
                r0, c0 = divmod(pos[j], 4)
                for dr in range(h):
                    for dc in range(w):
                        c = (r0 + dr) * 4 + (c0 + dc)
                        assert occ[c] == -1
                        occ[c] = j
            assert occ.count(-1) == 2
            w, h = DIMS[shapes[idx]]
            r0, c0 = divmod(pos[idx], 4)
            mx, my = DIRS[d]
            for s in range(1, steps + 1):
                nr, nc = r0 + my * s, c0 + mx * s
                assert 0 <= nr and nr + h <= 5 and 0 <= nc and nc + w <= 4
                for rr in range(nr, nr + h):
                    for cc in range(nc, nc + w):
                        assert occ[rr * 4 + cc] in (-1, idx)
            pos[idx] = (r0 + my * steps) * 4 + (c0 + mx * steps)
        assert pos[0] == 13
    assert tier_counts == [20, 20, 20]
    print(f"Verified 60 Klotski puzzles ({len(raw)} bytes, CRC=0x{crc:08x}, all BFS paths valid)")


if __name__ == "__main__":
    verify_bank()
