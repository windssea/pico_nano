# SPDX-License-Identifier: Apache-2.0
"""原创数织题库生成器与纯行推理唯一解证明器。 / Original Nonogram puzzle bank generator and pure line-propagation uniqueness prover."""
import argparse
import json
import pathlib
import random
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent


def runs_of(bits, length):
    """计算单行或单列连续填涂段。 / Compute contiguous filled run lengths of a line."""
    runs = []
    cur = 0
    for i in range(length):
        if (bits >> i) & 1:
            cur += 1
        elif cur:
            runs.append(cur)
            cur = 0
    if cur:
        runs.append(cur)
    return tuple(runs) if runs else (0,)


def clues_of(grid, w, h):
    """提取全部行与列的提示数字。 / Extract row and column clue tuples from a bitmap."""
    rows = []
    for r in range(h):
        mask = 0
        for c in range(w):
            if grid[r][c]:
                mask |= 1 << c
        rows.append(runs_of(mask, w))
    cols = []
    for c in range(w):
        mask = 0
        for r in range(h):
            if grid[r][c]:
                mask |= 1 << r
        cols.append(runs_of(mask, h))
    return rows, cols


def _solve_single_line(length, runs, known_one, known_zero):
    """单行动态规划求所有合法排布的交集（必涂与必空）。 / Dynamic programming over one line to find forced fills and forced empties."""
    if runs == (0,):
        if known_one:
            return None
        return 0, (1 << length) - 1

    k = len(runs)
    # dp_fwd[idx][pos]: can place runs[:idx] within prefix [0..pos)
    # Faster exact placement DP: f[i][p] = possible to finish runs[i:] starting from >= p
    suffix_possible = [[False] * (length + 2) for _ in range(k + 1)]
    for p in range(length + 1):
        if (known_one >> p) == 0:
            suffix_possible[k][p] = True
    suffix_possible[k][length + 1] = True

    for i in range(k - 1, -1, -1):
        run = runs[i]
        last_run = (i == k - 1)
        for p in range(length - run, -1, -1):
            # Option 1: cell p is empty (0), skip to p+1
            ok = False
            if not ((known_one >> p) & 1) and suffix_possible[i][p + 1]:
                ok = True
            # Option 2: place run at [p .. p+run-1]
            block_mask = ((1 << run) - 1) << p
            if not (known_zero & block_mask):
                if last_run:
                    if (known_one >> (p + run)) == 0:
                        ok = True
                else:
                    gap = p + run
                    if gap < length and not ((known_one >> gap) & 1) and suffix_possible[i + 1][gap + 1]:
                        ok = True
            suffix_possible[i][p] = ok

    if not suffix_possible[0][0]:
        return None

    # Forward reachability + accumulate possible 1s and 0s
    can_one = 0
    can_zero = 0
    # Reachable states (i, p): ready to process from position p with runs[i:] remaining
    reachable = [[False] * (length + 2) for _ in range(k + 1)]
    reachable[0][0] = True
    for i in range(k):
        run = runs[i]
        last_run = (i == k - 1)
        for p in range(length - run + 1):
            if not reachable[i][p]:
                continue
            # Transition 1: p is empty
            if p < length and not ((known_one >> p) & 1) and suffix_possible[i][p + 1]:
                can_zero |= (1 << p)
                reachable[i][p + 1] = True
            # Transition 2: place run at [p .. p+run-1]
            block_mask = ((1 << run) - 1) << p
            if not (known_zero & block_mask):
                if last_run:
                    if (known_one >> (p + run)) == 0:
                        can_one |= block_mask
                        rem_mask = ((1 << (length - (p + run))) - 1) << (p + run)
                        can_zero |= rem_mask
                else:
                    gap = p + run
                    if gap < length and not ((known_one >> gap) & 1) and suffix_possible[i + 1][gap + 1]:
                        can_one |= block_mask
                        can_zero |= (1 << gap)
                        reachable[i + 1][gap + 1] = True

    all_bits = (1 << length) - 1
    forced_one = can_one & ~can_zero
    forced_zero = can_zero & ~can_one & all_bits
    return forced_one, forced_zero


def line_solve(w, h, row_clues, col_clues):
    """纯行列传播求解，返回(是否完全唯一解出, 求解轮数, 解矩阵)。 / Pure row/col line propagation returning (solved, passes, grid)."""
    row_one = [0] * h
    row_zero = [0] * h
    col_one = [0] * w
    col_zero = [0] * w
    passes = 0
    while True:
        changed = False
        passes += 1
        for r in range(h):
            if (row_one[r] | row_zero[r]) == (1 << w) - 1:
                continue
            res = _solve_single_line(w, row_clues[r], row_one[r], row_zero[r])
            if res is None:
                return False, passes, None
            f1, f0 = res
            new1 = f1 & ~row_one[r]
            new0 = f0 & ~row_zero[r]
            if new1 or new0:
                changed = True
                row_one[r] |= new1
                row_zero[r] |= new0
                for c in range(w):
                    if (new1 >> c) & 1:
                        col_one[c] |= (1 << r)
                    if (new0 >> c) & 1:
                        col_zero[c] |= (1 << r)
        for c in range(w):
            if (col_one[c] | col_zero[c]) == (1 << h) - 1:
                continue
            res = _solve_single_line(h, col_clues[c], col_one[c], col_zero[c])
            if res is None:
                return False, passes, None
            f1, f0 = res
            new1 = f1 & ~col_one[c]
            new0 = f0 & ~col_zero[c]
            if new1 or new0:
                changed = True
                col_one[c] |= new1
                col_zero[c] |= new0
                for r in range(h):
                    if (new1 >> r) & 1:
                        row_one[r] |= (1 << c)
                    if (new0 >> r) & 1:
                        row_zero[r] |= (1 << c)
        if all((row_one[r] | row_zero[r]) == (1 << w) - 1 for r in range(h)):
            grid = [[(row_one[r] >> c) & 1 for c in range(w)] for r in range(h)]
            return True, passes, grid
        if not changed:
            return False, passes, None


# Hand-crafted original pixel-art seeds + structured motif synthesizer
MOTIFS_5 = [
    ("爱心", ["01010", "11111", "11111", "01110", "00100"]),
    ("小船", ["00100", "01100", "00100", "11111", "01110"]),
    ("雨伞", ["01110", "11111", "00100", "00101", "00110"]),
    ("茶杯", ["10100", "00000", "11110", "11111", "01110"]),
    ("松树", ["00100", "01110", "00100", "01110", "11111"]),
    ("房屋", ["00100", "01110", "11111", "10101", "11011"]),
    ("星星", ["00100", "11111", "01110", "01010", "10001"]),
    ("月亮", ["01110", "11000", "11000", "11000", "01110"]),
    ("音符", ["00111", "00101", "00101", "11100", "11100"]),
    ("沙漏", ["11111", "01110", "00100", "01010", "11111"]),
    ("钥匙", ["01110", "01010", "01110", "00100", "00110"]),
    ("铃铛", ["00100", "01110", "01110", "11111", "00100"]),
    ("小鱼", ["00110", "10111", "11101", "10111", "00110"]),
    ("王冠", ["10101", "11111", "10101", "11111", "01110"]),
    ("风筝", ["01100", "11110", "01100", "00010", "00001"]),
]

MOTIFS_8 = [
    ("水墨猫脸", [
        "10000001",
        "11000011",
        "11111111",
        "10111101",
        "11111111",
        "01100110",
        "00111100",
        "00011000",
    ]),
    ("江上帆影", [
        "00010000",
        "00110000",
        "01110100",
        "11110110",
        "00010000",
        "11111111",
        "01111110",
        "00111100",
    ]),
    ("红泥茶壶", [
        "00011000",
        "00111100",
        "10111110",
        "11111101",
        "01111101",
        "01111110",
        "00111100",
        "00100100",
    ]),
    ("蝴蝶展翅", [
        "11000011",
        "11100111",
        "11111111",
        "01111110",
        "00111100",
        "01100110",
        "11000011",
        "10000001",
    ]),
    ("竹林灯笼", [
        "00011000",
        "01111110",
        "11011011",
        "11011011",
        "11111111",
        "01111110",
        "00011000",
        "00100100",
    ]),
    ("远航铁锚", [
        "00011000",
        "00100100",
        "00011000",
        "01111110",
        "00011000",
        "10011001",
        "11011011",
        "01111110",
    ]),
    ("林间蘑菇", [
        "00111100",
        "01101110",
        "11011101",
        "11111111",
        "00111100",
        "00111100",
        "00111100",
        "01111110",
    ]),
    ("古堡塔楼", [
        "10100101",
        "11111111",
        "01111110",
        "01100110",
        "01100110",
        "01111110",
        "11100111",
        "11100111",
    ]),
    ("清脆铜铃", [
        "00011000",
        "00111100",
        "01111110",
        "01100110",
        "01111110",
        "11111111",
        "11111111",
        "00011000",
    ]),
    ("白鹅戏水", [
        "00001110",
        "00001111",
        "00001100",
        "01111100",
        "11111100",
        "11111110",
        "01111100",
        "10101010",
    ]),
    ("山间木屋", [
        "00011000",
        "00111100",
        "01111110",
        "11111111",
        "01001110",
        "01001010",
        "01111010",
        "01111110",
    ]),
    ("飞天火箭", [
        "00011000",
        "00111100",
        "00100100",
        "00111100",
        "01111110",
        "11111111",
        "10100101",
        "00011000",
    ]),
    ("小狗坐姿", [
        "11000000",
        "11110000",
        "01111000",
        "01100001",
        "01111111",
        "01111110",
        "01010110",
        "01010101",
    ]),
    ("冬日雪人", [
        "00111100",
        "01111110",
        "00101100",
        "00111100",
        "01111110",
        "11101111",
        "11111111",
        "01111110",
    ]),
    ("折纸飞机", [
        "00000011",
        "00001111",
        "00111110",
        "11110110",
        "01111100",
        "00101100",
        "00100100",
        "00100000",
    ]),
]

TITLES_10 = [
    "水墨熊猫", "荷塘青蛙", "孤岛灯塔", "竹叶清风", "古琴雅韵",
    "折扇轻摇", "松间仙鹤", "云端飞雁", "林间猫头鹰", "书卷留香",
    "碧波鲸跃", "乡间风车", "青铜宝剑", "老式相机", "木吉他声",
    "玉兔望月", "庭院盆栽", "锦鲤戏莲", "纸鸢高飞", "沙洲水鸟",
    "远山日出", "秋日银杏", "石桥流水", "窗前风铃", "海上归帆",
    "寒梅傲雪", "松鼠抱果", "弈棋执子", "怀表滴答", "古镇牌坊",
]

TITLES_15 = [
    "幽篁独坐", "孤帆远影", "飞檐古塔", "苍松迎客", "骏马奔腾",
    "仙鹤亮翅", "秋山晚亭", "明月松间", "江南乌篷", "飞龙在天",
    "烟雨石桥", "听琴品茗", "雪夜归人", "荷风送香", "兰亭鹅影",
    "危楼摘星", "渔舟唱晚", "古寺钟声", "竹影摇窗", "云海奇峰",
    "白鹭洲头", "琵琶半遮", "踏雪寻梅", "山寺桃花", "长河落日",
    "海屋添筹", "牧童短笛", "平沙落雁", "寒江独钓", "万象更新",
]


def _check_glyphs(titles):
    manifest_path = ROOT / "LICENSES/reading-font-manifest.json"
    allowed = set(json.loads(manifest_path.read_text(encoding="utf-8"))["unicode_requested"])
    for t in titles:
        for ch in t:
            if ord(ch) not in allowed:
                raise ValueError(f"Character {ch!r} in title {t!r} missing from reading font")


def _synthesize_art(rng, side, min_passes, max_runs_limit, seen):
    """生成具有连贯几何块结构、美观且纯行推理唯一可解的像素画。 / Synthesize coherent geometric/symmetric pixel art proven uniquely line-solvable."""
    while True:
        grid = [[0] * side for _ in range(side)]
        symmetry = rng.choice(["mirror_x", "scene", "diagonal", "framed"])
        # Paint 4..9 coherent strokes/ellipses/rectangles so it looks like structured pixel art, not scatter noise
        blobs = rng.randint(5, 9) if side == 10 else rng.randint(8, 14)
        for _ in range(blobs):
            kind = rng.choice(["disc", "box", "diamond", "line"])
            cx = rng.randint(1, side - 2)
            cy = rng.randint(1, side - 2)
            val = 1 if rng.random() < 0.78 else 0
            if kind == "disc":
                rx = rng.randint(1, max(2, side // 4))
                ry = rng.randint(1, max(2, side // 4))
                for r in range(side):
                    for c in range(side):
                        if ((c - cx) / rx) ** 2 + ((r - cy) / ry) ** 2 <= 1.05:
                            grid[r][c] = val
            elif kind == "box":
                rw = rng.randint(2, max(3, side // 3))
                rh = rng.randint(2, max(3, side // 3))
                for r in range(max(0, cy - rh // 2), min(side, cy + (rh + 1) // 2)):
                    for c in range(max(0, cx - rw // 2), min(side, cx + (rw + 1) // 2)):
                        grid[r][c] = val
            elif kind == "diamond":
                rad = rng.randint(1, max(2, side // 4))
                for r in range(side):
                    for c in range(side):
                        if abs(c - cx) + abs(r - cy) <= rad:
                            grid[r][c] = val
            else:
                length = rng.randint(3, side - 2)
                horiz = rng.random() < 0.5
                for step in range(length):
                    rr = cy if horiz else min(side - 1, cy + step - length // 2)
                    cc = min(side - 1, cx + step - length // 2) if horiz else cx
                    if 0 <= rr < side and 0 <= cc < side:
                        grid[rr][cc] = val

        if symmetry == "mirror_x":
            for r in range(side):
                for c in range(side // 2):
                    grid[r][side - 1 - c] = grid[r][c]
            # Break 1..3 asymmetric details so solving requires cross-axis deduction!
            for _ in range(rng.randint(2, 4)):
                rr = rng.randint(0, side - 1)
                cc = rng.randint(0, side - 1)
                grid[rr][cc] ^= 1
        elif symmetry == "diagonal":
            for r in range(side):
                for c in range(r + 1, side):
                    if rng.random() < 0.7:
                        grid[r][c] = grid[c][r]

        total_ones = sum(sum(row) for row in grid)
        area = side * side
        if not (int(area * 0.36) <= total_ones <= int(area * 0.68)):
            continue
        # Avoid completely empty rows/cols so every line gives engaging clues
        if any(sum(row) == 0 or sum(row) == side for row in grid):
            continue
        if any(sum(grid[r][c] for r in range(side)) in (0, side) for c in range(side)):
            continue

        row_clues, col_clues = clues_of(grid, side, side)
        if max(len(cl) for cl in row_clues + col_clues) > max_runs_limit:
            continue
        sig = (tuple(row_clues), tuple(col_clues))
        if sig in seen:
            continue
        solved, passes, out = line_solve(side, side, row_clues, col_clues)
        if not solved or out != grid or passes < min_passes:
            continue
        seen.add(sig)
        return grid, passes


def build_all(seed=20261118):
    _check_glyphs([t for t, _ in MOTIFS_5] + [t for t, _ in MOTIFS_8] + TITLES_10 + TITLES_15)
    rng = random.Random(seed)
    seen = set()
    puzzles = []

    # Tier 0: 15 (5x5) + 15 (8x8)
    for title, rows_str in MOTIFS_5 + MOTIFS_8:
        h = len(rows_str)
        w = len(rows_str[0])
        grid = [[1 if ch == '1' else 0 for ch in row] for row in rows_str]
        r_cl, c_cl = clues_of(grid, w, h)
        solved, passes, out = line_solve(w, h, r_cl, c_cl)
        if not solved or out != grid:
            # Tweak minimal cells if any hand-crafted motif had an ambiguous 2x2 switch
            fixed = False
            for r in range(h):
                for c in range(w):
                    grid[r][c] ^= 1
                    r2, c2 = clues_of(grid, w, h)
                    s2, p2, o2 = line_solve(w, h, r2, c2)
                    if s2 and o2 == grid and max(len(x) for x in r2 + c2) <= 5:
                        r_cl, c_cl, passes, fixed = r2, c2, p2, True
                        break
                    grid[r][c] ^= 1
                if fixed:
                    break
            assert fixed, f"Motif {title} not uniquely line-solvable"
        sig = (tuple(r_cl), tuple(c_cl))
        assert sig not in seen, f"Duplicate motif {title}"
        seen.add(sig)
        puzzles.append({
            "id": len(puzzles),
            "tier": 0,
            "w": w,
            "h": h,
            "title": title,
            "passes": passes,
            "filled": sum(sum(row) for row in grid),
            "rows": ["".join(str(v) for v in row) for row in grid],
        })

    # Tier 1: 30 (10x10), pure line-solvable with passes >= 4, max_runs <= 5
    candidates_10 = []
    for title in TITLES_10:
        grid, passes = _synthesize_art(rng, 10, min_passes=4, max_runs_limit=5, seen=seen)
        candidates_10.append((passes, title, grid))
    candidates_10.sort(key=lambda item: item[0])
    for passes, title, grid in candidates_10:
        puzzles.append({
            "id": len(puzzles),
            "tier": 1,
            "w": 10,
            "h": 10,
            "title": title,
            "passes": passes,
            "filled": sum(sum(row) for row in grid),
            "rows": ["".join(str(v) for v in row) for row in grid],
        })

    # Tier 2: 30 (15x15), pure line-solvable with passes >= 6, max_runs <= 6
    candidates_15 = []
    for title in TITLES_15:
        grid, passes = _synthesize_art(rng, 15, min_passes=6, max_runs_limit=6, seen=seen)
        candidates_15.append((passes, title, grid))
    candidates_15.sort(key=lambda item: item[0])
    for passes, title, grid in candidates_15:
        puzzles.append({
            "id": len(puzzles),
            "tier": 2,
            "w": 15,
            "h": 15,
            "title": title,
            "passes": passes,
            "filled": sum(sum(row) for row in grid),
            "rows": ["".join(str(v) for v in row) for row in grid],
        })

    return puzzles


def pack_bank(puzzles):
    """打包为PNNG v1格式：32B文件头 + 90 * 64B记录 = 5792B。 / Pack into PNNG v1: 32B header + 90 * 64B records = 5792B."""
    assert len(puzzles) == 90
    records = bytearray()
    for i, p in enumerate(puzzles):
        assert p["id"] == i and p["tier"] == i // 30
        w, h = p["w"], p["h"]
        title_bytes = p["title"].encode("utf-8")
        assert 0 < len(title_bytes) < 24
        title_buf = title_bytes + b"\x00" * (24 - len(title_bytes))
        bits = bytearray(32)
        filled = 0
        for r in range(h):
            for c in range(w):
                if p["rows"][r][c] == "1":
                    idx = r * w + c
                    bits[idx // 8] |= 1 << (idx % 8)
                    filled += 1
        assert filled == p["filled"] and 0 < filled < w * h
        rec = struct.pack("<BBBBHBB", w, h, p["tier"], filled, i, min(255, p["passes"]), 0) + title_buf + bytes(bits)
        assert len(rec) == 64
        records.extend(rec)
    crc = zlib.crc32(records) & 0xFFFFFFFF
    header = b"PNNG" + struct.pack("<HHII", 1, 64, 90, crc) + b"\x00" * 16
    assert len(header) == 32
    return header + bytes(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=20261118)
    parser.add_argument("--out", type=pathlib.Path, default=HERE)
    args = parser.parse_args()
    puzzles = build_all(args.seed)
    blob = pack_bank(puzzles)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "puzzles.pnng").write_bytes(blob)
    meta = {
        "version": 1,
        "seed": args.seed,
        "count": len(puzzles),
        "bytes": len(blob),
        "puzzles": puzzles,
    }
    (args.out / "puzzles.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(puzzles)} Nonogram puzzles ({len(blob)} bytes) into {args.out}")


if __name__ == "__main__":
    main()
