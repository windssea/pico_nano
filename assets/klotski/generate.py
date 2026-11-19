"""生成并离线精确BFS证明60关三国华容道题库（PNKL v1）。
Generate and offline exact-BFS verify 60 classic Three Kingdoms Klotski puzzles (PNKL v1)."""
import collections
import json
import pathlib
import random
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parent
COLS, ROWS = 4, 5
SHAPE_1X1, SHAPE_1X2, SHAPE_2X1, SHAPE_2X2 = 0, 1, 2, 3
DIMS = {SHAPE_1X1: (1, 1), SHAPE_1X2: (1, 2), SHAPE_2X1: (2, 1), SHAPE_2X2: (2, 2)}
DIRS = ((0, -1), (0, 1), (-1, 0), (1, 0))  # 0=U, 1=D, 2=L, 3=R


def parse_layout(rows_str):
    """从5x4字符图解析棋块：(shape, pos)列表，0号固定为2x2曹操。/ Parse 5x4 ASCII layout into (shape, pos) list with Cao Cao at index 0."""
    grid = [line.strip() for line in rows_str.strip().splitlines()]
    assert len(grid) == 5 and all(len(r) == 4 for r in grid), f"Bad layout:\n{rows_str}"
    seen = set()
    pieces = []
    for r in range(5):
        for c in range(4):
            ch = grid[r][c]
            if ch == "." or ch in seen:
                continue
            seen.add(ch)
            coords = [(rr, cc) for rr in range(5) for cc in range(4) if grid[rr][cc] == ch]
            rs = [p[0] for p in coords]
            cs = [p[1] for p in coords]
            w = max(cs) - min(cs) + 1
            h = max(rs) - min(rs) + 1
            assert len(coords) == w * h, f"Non-rectangular piece {ch}"
            shape = {(1, 1): SHAPE_1X1, (1, 2): SHAPE_1X2, (2, 1): SHAPE_2X1, (2, 2): SHAPE_2X2}[(w, h)]
            pieces.append((ch, shape, min(rs) * 4 + min(cs)))
    caocao = [p for p in pieces if p[1] == SHAPE_2X2]
    assert len(caocao) == 1, "Must have exactly one 2x2 Cao Cao"
    ordered = [caocao[0]] + [p for p in pieces if p[1] != SHAPE_2X2]
    # 按形制与位置确定性排序（曹操固定0号，其余按形状2x1/1x2/1x1再按位置）/ Deterministic order after Cao Cao
    rest = sorted(ordered[1:], key=lambda x: (-x[1], x[2]))
    result = [(ordered[0][1], ordered[0][2])] + [(p[1], p[2]) for p in rest]
    assert len(result) <= 12
    # 验证空位恰好2格 / Verify exactly 2 empty cells
    occ = board_from_pieces(result)
    assert occ.count(-1) == 2, f"Expected 2 empty cells, got {occ.count(-1)}"
    return result


def board_from_pieces(pieces):
    occ = [-1] * 20
    for idx, (shape, pos) in enumerate(pieces):
        w, h = DIMS[shape]
        r0, c0 = divmod(pos, 4)
        for dr in range(h):
            for dc in range(w):
                cell = (r0 + dr) * 4 + (c0 + dc)
                assert occ[cell] == -1, "Overlapping pieces"
                occ[cell] = idx
    return occ


def mirror_pieces(pieces):
    res = []
    for shape, pos in pieces:
        w, _ = DIMS[shape]
        r, c = divmod(pos, 4)
        res.append((shape, r * 4 + (4 - w - c)))
    return res


def canonical_key(shapes, positions):
    """按块形制归一化的状态键，用于高效精确BFS去重。/ Shape-canonical state key for fast exact BFS."""
    # 每格放块形制+1（空为0），20格编码为整数 / Cell stores shape+1 (0=empty)
    cells = [0] * 20
    for shape, pos in zip(shapes, positions):
        w, h = DIMS[shape]
        r0, c0 = divmod(pos, 4)
        val = shape + 1
        for dr in range(h):
            for dc in range(w):
                cells[(r0 + dr) * 4 + (c0 + dc)] = val
    return bytes(cells)


def solve_bfs(pieces, max_states=150000):
    """精确BFS求单块直线滑动（1步或2步）的最短解。/ Exact BFS for shortest straight-slide solution (1 or 2 cells per move)."""
    shapes = tuple(s for s, _ in pieces)
    start_pos = tuple(p for _, p in pieces)
    if start_pos[0] == 3 * 4 + 1:
        return []
    n = len(pieces)
    dims = [DIMS[s] for s in shapes]
    start_key = canonical_key(shapes, start_pos)
    visited = {start_key}
    queue = collections.deque([start_pos])
    parent = {start_pos: None}
    move_taken = {}

    while queue:
        pos = queue.popleft()
        if len(visited) > max_states:
            return None
        # 构建占用表 / Build occupancy grid
        occ = [-1] * 20
        for i in range(n):
            w, h = dims[i]
            r0, c0 = divmod(pos[i], 4)
            occ[pos[i]] = i
            if w == 2:
                occ[pos[i] + 1] = i
            if h == 2:
                occ[pos[i] + 4] = i
                if w == 2:
                    occ[pos[i] + 5] = i

        for i in range(n):
            w, h = dims[i]
            r0, c0 = divmod(pos[i], 4)
            for d, (dc, dr) in enumerate(DIRS):
                for steps in (1, 2):
                    nr, nc = r0 + dr * steps, c0 + dc * steps
                    if nr < 0 or nc < 0 or nr + h > 5 or nc + w > 4:
                        break
                    # 检查新位置是否全为空或自身 / Check destination cells empty or self
                    ok = True
                    for rr in range(nr, nr + h):
                        for cc in range(nc, nc + w):
                            who = occ[rr * 4 + cc]
                            if who != -1 and who != i:
                                ok = False
                                break
                        if not ok:
                            break
                    if not ok:
                        break
                    npos = list(pos)
                    npos[i] = nr * 4 + nc
                    npos_t = tuple(npos)
                    code = (i << 4) | (d << 2) | (steps - 1)
                    if i == 0 and npos_t[0] == 13:
                        # 回溯完整着法 / Reconstruct solution path
                        path = [code]
                        cur = pos
                        while parent[cur] is not None:
                            path.append(move_taken[cur])
                            cur = parent[cur]
                        path.reverse()
                        return path
                    key = canonical_key(shapes, npos_t)
                    if key not in visited:
                        visited.add(key)
                        parent[npos_t] = pos
                        move_taken[npos_t] = code
                        queue.append(npos_t)
    return None


def verify_solution(pieces, sol):
    """按C引擎完全一致的规则逐步验证解序列。/ Step-by-step verify solution under exact C engine rules."""
    shapes = [s for s, _ in pieces]
    pos = [p for _, p in pieces]
    for code in sol:
        idx = (code >> 4) & 0x0F
        d = (code >> 2) & 0x03
        steps = (code & 0x03) + 1
        assert 0 <= idx < len(pieces) and steps in (1, 2)
        dc, dr = DIMS[shapes[idx]]
        w, h = dc, dr
        mx, my = DIRS[d]
        occ = board_from_pieces(list(zip(shapes, pos)))
        r0, c0 = divmod(pos[idx], 4)
        for s in range(1, steps + 1):
            nr, nc = r0 + my * s, c0 + mx * s
            assert 0 <= nr and nr + h <= 5 and 0 <= nc and nc + w <= 4
            for rr in range(nr, nr + h):
                for cc in range(nc, nc + w):
                    who = occ[rr * 4 + cc]
                    assert who == -1 or who == idx
        pos[idx] = (r0 + my * steps) * 4 + (c0 + mx * steps)
    assert pos[0] == 13, f"Cao Cao ended at {pos[0]}, expected 13"


# 精选经典三国华容道名局（字符相同为同一块，'.'为空位）/ Curated classic Three Kingdoms Klotski layouts
CLASSIC_CANDIDATES = [
    # ---- 名局与进阶经典 ----
    ("横刀立马", "ACC B\nACC B\nD EE F\nD GH F\nI .. J".replace(" ", "")),
    ("指挥若定", "ACC B\nACC B\nG EE H\nD IJ F\nD .. F".replace(" ", "")),
    ("将拥曹营", ".CC .\nACC B\nA EE B\nD GH F\nD IJ F".replace(" ", "")),
    ("齐头并进", "ACC B\nACC B\nG HI J\nD EE F\nD .. F".replace(" ", "")),
    ("兵分三路", "G CC H\nA CC B\nA EE B\nD IJ F\nD .. F".replace(" ", "")),
    ("雨声淅沥", "A CC G\nA CC H\nB EE D\nB F  D\nI F  J".replace(" ", "")),  # Wait 2 empties check below
    ("左右布兵", "G CC H\nI CC J\nA BB D\nA EF D\n. EF .".replace(" ", "")),
    ("桃花园中", "G CC H\nA CC B\nA DE B\nI DE J\n. FF .".replace(" ", "")),
    ("一路进军", "A CC B\nA CC B\nD EE F\nD GH F\n.. IJ".replace(" ", "")),
    ("一路顺风", "A CC G\nA CC H\nB EE D\nB IJ D\nF .. F".replace(" ", "")),
    ("围而不打", "A CC G\nA CC H\nB DD E\nB FF E\nI .. J".replace(" ", "")),
    ("捷足先登", "G CC H\nI CC J\n. EE .\nA BB D\nA FF D".replace(" ", "")),
    ("插翅难飞", "A CC G\nA CC H\nBB EE\nD I J F\nD . . F".replace(" ", "")),
    ("守口如瓶", "A CC B\nA CC B\nG . . H\nD EE F\nD I J F".replace(" ", "")),
    ("双将挡路", "A CC B\nA CC B\nDD EE\nF G H I\nF . . J".replace(" ", "")),
    ("峰回路转", "G H CC\nA B CC\nA B EE\nD I J F\nD . . F".replace(" ", "")),
    ("水泄不通", "A CC B\nA CC B\nDD EE\nFF G H\nI . . J".replace(" ", "")),
    ("四面楚歌", "A CC B\nA CC B\nG EE H\nD I J F\nD . . F".replace(" ", "")),
    ("过五关",   "A CC B\nA CC B\nDD EE\nFF GG\nH . . I".replace(" ", "")),
    ("兵临曹营", "G CC H\nI CC J\nA EE B\nA DF B\n. DF .".replace(" ", "")),
    ("近在咫尺", "A B D E\nA B D E\nF F G H\nI CC J\n. CC .".replace(" ", "")),
    ("层层设防", "A CC B\nA CC B\nG DD H\nI EE J\n. FF .".replace(" ", "")),
    ("屯兵东路", "CC A B\nCC A B\nEE G H\nD F I J\nD F . .".replace(" ", "")),
    ("巧过五关", "G H CC\nA B CC\nA B DD\nEE FF\nI . . J".replace(" ", "")),
    ("前呼后拥", "G H CC\nI J CC\nA B DD\nA B EE\n. . FF".replace(" ", "")),
    ("身陷重围", "A CC B\nA CC B\nG DD H\nE I J F\nE . . F".replace(" ", "")),
    ("诱敌深入", "G CC H\nA CC B\nA EE B\nI DD J\n. FF .".replace(" ", "")),
    ("兵临城下", "A CC B\nA CC B\nG H I J\nD EE F\nD . . F".replace(" ", "")),
    ("声东击西", "CC A G\nCC A H\nB DD E\nB I J E\n. FF .".replace(" ", "")),
    ("步步为营", "A CC G\nA CC H\nB DD E\nB I J E\n. . FF".replace(" ", "")),
    ("势如破竹", "A B CC\nA B CC\nG H EE\nD F I J\nD F . .".replace(" ", "")),
    ("破釜沉舟", "G CC A\nH CC A\nB EE D\nB I J D\n. FF .".replace(" ", "")),
    ("暗度陈仓", "CC A B\nCC A B\nG H DD\nI J EE\n. . FF".replace(" ", "")),
    ("十面埋伏", "A CC B\nA CC B\nG H DD\nE I J F\nE . . F".replace(" ", "")),
    ("兵贵神速", "G H I J\nA CC B\nA CC B\nD EE F\nD . . F".replace(" ", "")),
    ("单骑救主", "A B G H\nA B CC\nD E CC\nD E I J\n. . FF".replace(" ", "")),
    ("长驱直入", "G H CC\nI J CC\nA EE B\nA DF B\n. DF .".replace(" ", "")),
    ("铁壁合围", "A CC B\nA CC B\nDD G H\nEE I J\n. FF .".replace(" ", "")),
]

# 额外入门与进阶多样化题名池 / Curated classical titles for generated levels
EXTRA_TITLES = [
    "初出茅庐", "试探虚实", "轻车熟路", "顺水推舟", "小试牛刀", "旗开得胜",
    "直捣黄龙", "探囊取物", "迎刃而解", "势不可挡", "先发制人", "一马当先",
    "分进合击", "虚实相生", "调虎离山", "釜底抽薪", "以逸待劳", "围魏救赵",
    "借刀杀人", "趁火打劫", "瞒天过海", "反客为主", "金蝉脱壳", "关门捉贼",
    "远交近攻", "假道伐虢", "偷梁换柱", "指桑骂槐", "打草惊蛇", "抛砖引玉",
    "擒贼擒王", "混水摸鱼", "连环妙计", "奇兵突袭", "狭路相逢", "绝处逢生",
    "柳暗花明", "峰回谷转", "移形换影", "运筹帷幄", "决胜千里", "力挽狂澜",
    "百步穿杨", "游刃有余", "化险为夷", "突围而出", "虎口脱险", "卧薪尝胆",
    "背水一战", "奇正相生", "步步紧逼", "巧渡津关", "智取华容", "孤军深入",
]


def random_valid_layout(rng, profile):
    """按指定块形制配比生成合法4x5布局。/ Generate a valid 4x5 layout matching a piece-shape profile."""
    n_2x1, n_1x2, n_1x1 = profile
    assert 4 + n_2x1 * 2 + n_1x2 * 2 + n_1x1 == 18
    for _ in range(300):
        grid = [-1] * 20
        pieces = []
        # 放曹操（不在终点13）/ Place Cao Cao (not at goal 13)
        cc_r = rng.choice([0, 0, 0, 1, 1, 2])
        cc_c = rng.choice([0, 1, 1, 2])
        if cc_r == 3 and cc_c == 1:
            continue
        for dr in range(2):
            for dc in range(2):
                grid[(cc_r + dr) * 4 + (cc_c + dc)] = 0
        pieces.append((SHAPE_2X2, cc_r * 4 + cc_c))

        # 放2x1与1x2 / Place 2x1 and 1x2 blocks
        ok = True
        to_place = [SHAPE_2X1] * n_2x1 + [SHAPE_1X2] * n_1x2
        rng.shuffle(to_place)
        for sh in to_place:
            w, h = DIMS[sh]
            opts = []
            for r in range(6 - h):
                for c in range(5 - w):
                    if all(grid[(r + dr) * 4 + (c + dc)] == -1 for dr in range(h) for dc in range(w)):
                        opts.append(r * 4 + c)
            if not opts:
                ok = False
                break
            p = rng.choice(opts)
            r0, c0 = divmod(p, 4)
            idx = len(pieces)
            for dr in range(h):
                for dc in range(w):
                    grid[(r0 + dr) * 4 + (c0 + dc)] = idx
            pieces.append((sh, p))
        if not ok:
            continue

        # 剩余空位中随机留2个为空，其余填1x1 / Leave 2 empties, fill rest with 1x1
        empties = [i for i in range(20) if grid[i] == -1]
        if len(empties) != n_1x1 + 2:
            continue
        rng.shuffle(empties)
        for p in empties[2:]:
            pieces.append((SHAPE_1X1, p))
        rest = sorted(pieces[1:], key=lambda x: (-x[0], x[1]))
        return [pieces[0]] + rest
    return None


def main():
    seen_keys = set()
    solved = []

    def try_add(title, pieces, is_classic=False):
        shapes = tuple(s for s, _ in pieces)
        positions = tuple(p for _, p in pieces)
        k1 = canonical_key(shapes, positions)
        m = mirror_pieces(pieces)
        k2 = canonical_key(tuple(s for s, _ in m), tuple(p for _, p in m))
        if k1 in seen_keys or k2 in seen_keys:
            return False
        sol = solve_bfs(pieces)
        if not sol or len(sol) < 6 or len(sol) > 190:
            return False
        verify_solution(pieces, sol)
        seen_keys.add(k1)
        seen_keys.add(k2)
        solved.append({
            "title": title,
            "pieces": pieces,
            "par_moves": len(sol),
            "sol": sol,
            "classic": is_classic,
        })
        return True

    # 1. 先尝试所有经典三国名局 / Try all classic named puzzles
    for title, ascii_board in CLASSIC_CANDIDATES:
        try:
            pieces = parse_layout(ascii_board)
        except AssertionError:
            continue
        try_add(title, pieces, is_classic=True)

    # 2. 用确定性随机种子补充不同难度梯度的合法残局 / Deterministically generate puzzles across tiers
    rng = random.Random(20261119)
    profiles_by_bucket = [
        # 入门易解（多1x1或较多2x1，8..26步）/ Easy tier
        (8, 26, [(2, 2, 6), (1, 3, 6), (3, 1, 6), (2, 3, 4), (3, 2, 4)], 28),
        # 进阶（27..59步）/ Medium tier
        (27, 59, [(1, 4, 4), (2, 3, 4), (3, 2, 4), (4, 1, 4), (0, 5, 4)], 28),
        # 名局（60..145步）/ Master tier
        (60, 145, [(1, 4, 4), (2, 3, 4), (0, 5, 4), (3, 2, 4)], 28),
    ]
    title_idx = 0
    for lo, hi, profiles, target in profiles_by_bucket:
        attempts = 0
        while sum(1 for x in solved if lo <= x["par_moves"] <= hi) < target and attempts < 6000:
            attempts += 1
            prof = rng.choice(profiles)
            pieces = random_valid_layout(rng, prof)
            if not pieces:
                continue
            shapes = tuple(s for s, _ in pieces)
            positions = tuple(p for _, p in pieces)
            k1 = canonical_key(shapes, positions)
            m = mirror_pieces(pieces)
            k2 = canonical_key(tuple(s for s, _ in m), tuple(p for _, p in m))
            if k1 in seen_keys or k2 in seen_keys:
                continue
            sol = solve_bfs(pieces, max_states=60000)
            if not sol or not (lo <= len(sol) <= hi):
                continue
            verify_solution(pieces, sol)
            seen_keys.add(k1)
            seen_keys.add(k2)
            name = EXTRA_TITLES[title_idx % len(EXTRA_TITLES)]
            title_idx += 1
            solved.append({
                "title": name,
                "pieces": pieces,
                "par_moves": len(sol),
                "sol": sol,
                "classic": False,
            })

    # 按经典优先选出三档各20关，每档内按最优步数递增 / Pick 20 per tier (classics prioritized), sorted by par_moves
    def pick_tier(lo, hi, count=20):
        cands = [x for x in solved if lo <= x["par_moves"] <= hi]
        classics = [x for x in cands if x["classic"]]
        others = [x for x in cands if not x["classic"]]
        others.sort(key=lambda x: x["par_moves"])
        picked = classics[:count]
        if len(picked) < count:
            need = count - len(picked)
            step = max(1, len(others) / need)
            used = set()
            for i in range(need):
                idx = min(len(others) - 1, int(i * step))
                while idx in used and idx + 1 < len(others):
                    idx += 1
                used.add(idx)
                picked.append(others[idx])
        picked.sort(key=lambda x: (x["par_moves"], x["title"]))
        assert len(picked) == count, f"Bucket [{lo},{hi}] only had {len(picked)} puzzles"
        return picked

    tier0 = pick_tier(6, 26, 20)
    tier1 = pick_tier(27, 59, 20)
    tier2 = pick_tier(60, 160, 20)
    final_levels = []
    for t, group in enumerate((tier0, tier1, tier2)):
        for item in group:
            final_levels.append((t, item))
    assert len(final_levels) == 60

    # 确保所有60关标题不重名 / Ensure unique titles across 60 puzzles
    used_titles = set()
    extra_i = 0
    for _, item in final_levels:
        if item["title"] in used_titles:
            while EXTRA_TITLES[extra_i % len(EXTRA_TITLES)] in used_titles:
                extra_i += 1
            item["title"] = EXTRA_TITLES[extra_i % len(EXTRA_TITLES)]
            extra_i += 1
        used_titles.add(item["title"])

    # 打包二进制 PNKL v1 / Pack binary PNKL v1
    records = bytearray()
    sol_pool = bytearray()
    json_out = []
    for lid, (tier, item) in enumerate(final_levels):
        sol_bytes = bytes(item["sol"])
        sol_offset = len(sol_pool)
        sol_pool.extend(sol_bytes)
        title_b = item["title"].encode("utf-8")
        assert len(title_b) < 20, f"Title too long: {item['title']}"
        title_padded = title_b + b"\x00" * (20 - len(title_b))
        piece_bytes = bytearray([0xFF] * 12)
        for i, (sh, pos) in enumerate(item["pieces"]):
            piece_bytes[i] = (sh << 6) | pos
        records.extend(struct.pack("<BBBBHH", lid, tier, len(item["pieces"]), item["par_moves"], sol_offset, len(sol_bytes)))
        records.extend(title_padded)
        records.extend(piece_bytes)
        json_out.append({
            "id": lid,
            "tier": tier,
            "title": item["title"],
            "par_moves": item["par_moves"],
            "pieces": [[sh, pos] for sh, pos in item["pieces"]],
            "sol_len": len(sol_bytes),
            "classic": item["classic"],
        })

    body = bytes(records) + bytes(sol_pool)
    crc = zlib.crc32(body) & 0xFFFFFFFF
    header = struct.pack("<4sHHII", b"PNKL", 1, len(final_levels), crc, 0)
    bank = header + body

    (ROOT / "puzzles.pnkl").write_bytes(bank)
    (ROOT / "puzzles.json").write_text(json.dumps(json_out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(final_levels)} Klotski puzzles -> puzzles.pnkl ({len(bank)} bytes, CRC=0x{crc:08x})")
    for t, name in enumerate(("入门", "进阶", "名局")):
        sub = [x for ti, x in final_levels if ti == t]
        print(f"  Tier {t} ({name}): {len(sub)} levels, par {sub[0]['par_moves']}..{sub[-1]['par_moves']} moves, classics={sum(x['classic'] for x in sub)}")


if __name__ == "__main__":
    main()
