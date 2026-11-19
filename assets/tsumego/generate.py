"""生成并离线用完整围棋规则（气/提子/禁自杀/劫）验证60道经典围棋死活题（PNTG v1）。
Generate and verify 60 classic Go Tsumego puzzles (PNTG v1) under full Go rules (liberties, capture, suicide, Ko)."""
import json
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parent
SIZE = 9
CELLS = 81
EMPTY, BLACK, WHITE = 0, 1, 2
EDGE_TOP, EDGE_RIGHT, EDGE_BOTTOM, EDGE_LEFT = 1, 2, 4, 8

GOAL_KILL = 0      # 黑先杀白
GOAL_LIVE = 1      # 黑先做活
GOAL_CAPTURE = 2   # 黑先吃子
GOAL_SEKI = 3      # 黑先双活
GOAL_KO = 4        # 黑先打劫

NODE_ROOT = 0
NODE_CORRECT_LEAF = 1
NODE_CORRECT_CONT = 2
NODE_WRONG_REFUTE = 3


def neighbors(pos):
    r, c = divmod(pos, SIZE)
    if r > 0:
        yield (r - 1) * SIZE + c
    if r + 1 < SIZE:
        yield (r + 1) * SIZE + c
    if c > 0:
        yield r * SIZE + (c - 1)
    if c + 1 < SIZE:
        yield r * SIZE + (c + 1)


def group_and_liberties(board, start):
    color = board[start]
    assert color in (BLACK, WHITE)
    stack = [start]
    stones = {start}
    libs = set()
    while stack:
        cur = stack.pop()
        for nb in neighbors(cur):
            v = board[nb]
            if v == EMPTY:
                libs.add(nb)
            elif v == color and nb not in stones:
                stones.add(nb)
                stack.append(nb)
    return stones, libs


def play_move(board, pos, color, ko_prev=None):
    """完整围棋落子规则：判空、提无气敌子、禁自杀、判单劫。/ Full Go move: empty check, capture, no-suicide, simple Ko."""
    if not (0 <= pos < CELLS) or board[pos] != EMPTY:
        return None, 0
    nxt = list(board)
    nxt[pos] = color
    opp = WHITE if color == BLACK else BLACK
    captured = 0
    checked = set()
    for nb in neighbors(pos):
        if nxt[nb] == opp and nb not in checked:
            grp, libs = group_and_liberties(nxt, nb)
            checked |= grp
            if not libs:
                for p in grp:
                    nxt[p] = EMPTY
                captured += len(grp)
    _, self_libs = group_and_liberties(nxt, pos)
    if not self_libs:
        return None, 0
    if ko_prev is not None and captured == 1 and nxt == ko_prev:
        return None, 0
    return nxt, captured


def validate_initial_board(board):
    seen = set()
    for i, v in enumerate(board):
        if v in (BLACK, WHITE) and i not in seen:
            grp, libs = group_and_liberties(board, i)
            seen |= grp
            if not libs:
                return False, f"Dead initial group at {divmod(i, 9)}"
    return True, ""


def parse_rows(rows):
    """从不超过9行的9列字符串解析9x9棋盘与标记点。/ Parse up to 9 rows of 9 columns into 9x9 board and label map."""
    padded = list(rows) + ["........."] * (SIZE - len(rows))
    assert len(padded) == SIZE and all(len(r) == SIZE for r in padded), f"Bad rows: {padded}"
    board = [EMPTY] * CELLS
    marks = {}
    for r, row in enumerate(padded):
        for c, ch in enumerate(row):
            pos = r * SIZE + c
            if ch == "X":
                board[pos] = BLACK
            elif ch == "O":
                board[pos] = WHITE
            elif ch != ".":
                assert ch not in marks, f"Duplicate mark {ch}"
                marks[ch] = pos
    ok, msg = validate_initial_board(board)
    assert ok, f"Invalid initial board: {msg}\n" + "\n".join(padded)
    return board, marks


def pack_board(board):
    out = bytearray(21)
    for i, v in enumerate(board):
        out[i >> 2] |= (v & 3) << ((i & 3) * 2)
    return bytes(out)


def build_puzzle(title, tier, goal, edge_mask, rows, tree_spec, default_refute=None):
    board, marks = parse_rows(rows)

    def resolve(ref):
        if ref is None:
            return 0xFF
        if isinstance(ref, str):
            assert ref in marks, f"Unknown mark {ref} in {title}"
            return marks[ref]
        r, c = ref
        return r * SIZE + c

    nodes = []
    root_refute = resolve(default_refute)
    nodes.append([0xFF, NODE_ROOT, root_refute, 0xFF])

    def verify_and_add(parent_idx, cur_board, prev_board, branches, def_ref):
        if def_ref != 0xFF:
            rb, _ = play_move(cur_board, def_ref, WHITE, None)
            assert rb is not None, f"[{title}] illegal default white refute at {divmod(def_ref, 9)}"
        has_win = False
        seen_moves = set()
        for br in branches:
            bpos = resolve(br[0])
            assert bpos not in seen_moves, f"[{title}] duplicate black move {divmod(bpos, 9)}"
            seen_moves.add(bpos)
            kind = br[1]
            after_b, _ = play_move(cur_board, bpos, BLACK, prev_board)
            assert after_b is not None, f"[{title}] illegal Black move {br[0]} ({divmod(bpos, 9)})"
            if kind == "WIN":
                nodes.append([bpos, NODE_CORRECT_LEAF, 0xFF, parent_idx])
                has_win = True
            elif kind == "WRONG":
                wpos = resolve(br[2])
                if wpos != 0xFF:
                    after_w, _ = play_move(after_b, wpos, WHITE, cur_board)
                    assert after_w is not None, f"[{title}] illegal White refute {br[2]} after Black {br[0]}"
                nodes.append([bpos, NODE_WRONG_REFUTE, wpos, parent_idx])
            elif kind == "CONT":
                wpos = resolve(br[2])
                sub_branches = br[3]
                after_w, _ = play_move(after_b, wpos, WHITE, cur_board)
                assert after_w is not None, f"[{title}] illegal White reply {br[2]} after Black {br[0]}"
                idx = len(nodes)
                nodes.append([bpos, NODE_CORRECT_CONT, wpos, parent_idx])
                sub_win = verify_and_add(idx, after_w, after_b, sub_branches, 0xFF)
                assert sub_win, f"[{title}] CONT branch has no winning leaf"
                has_win = True
            else:
                raise ValueError(f"Unknown branch kind {kind}")
        return has_win

    assert verify_and_add(0, board, None, tree_spec, root_refute), f"[{title}] no winning path"
    assert len(nodes) <= 48, f"[{title}] too many nodes: {len(nodes)}"

    def main_depth(idx):
        for i in range(1, len(nodes)):
            if nodes[i][3] == idx:
                if nodes[i][1] == NODE_CORRECT_LEAF:
                    return 1
                if nodes[i][1] == NODE_CORRECT_CONT:
                    return 1 + main_depth(i)
        return 0

    depth = main_depth(0)
    assert 1 <= depth <= 6
    return {
        "title": title,
        "tier": tier,
        "goal": goal,
        "edge_mask": edge_mask,
        "board": board,
        "nodes": nodes,
        "main_moves": depth,
    }


def curated_puzzles():
    TR = EDGE_TOP | EDGE_RIGHT
    TL = EDGE_TOP | EDGE_LEFT
    TOP = EDGE_TOP
    P = []

    # ==================== 入门 20 题 (Tier 0) ====================
    # 1. 直三点眼
    P.append(build_puzzle(
        "直三点眼", 0, GOAL_KILL, TOP,
        [".XOabcOX.", ".XOOOOOX.", ".XXXXXXX."],
        [("b", "WIN"), ("a", "WRONG", "b"), ("c", "WRONG", "b")],
        default_refute="b",
    ))
    # 2. 直三做活
    P.append(build_puzzle(
        "直三做活", 0, GOAL_LIVE, TOP,
        [".OXabcXO.", ".OXXXXXO.", ".OOOOOOO."],
        [("b", "WIN"), ("a", "WRONG", "b"), ("c", "WRONG", "b")],
        default_refute="b",
    ))
    # 3. 曲三点眼
    P.append(build_puzzle(
        "曲三点眼", 0, GOAL_KILL, TR,
        [".....XOba", ".....XOOc", ".....XOOO", ".....XXXX"],
        [("a", "WIN"), ("b", "WRONG", "a"), ("c", "WRONG", "a")],
        default_refute="a",
    ))
    # 4. 曲三做活
    P.append(build_puzzle(
        "曲三做活", 0, GOAL_LIVE, TR,
        [".....OXba", ".....OXXc", ".....OXXX", ".....OOOO"],
        [("a", "WIN"), ("b", "WRONG", "a"), ("c", "WRONG", "a")],
        default_refute="a",
    ))
    # 5. 丁四点眼
    P.append(build_puzzle(
        "丁四点眼", 0, GOAL_KILL, TOP,
        [".XOabcOX.", ".XOOdOOX.", ".XOOOOOX.", ".XXXXXXX."],
        [("b", "WIN"), ("a", "WRONG", "b"), ("c", "WRONG", "b"), ("d", "WRONG", "b")],
        default_refute="b",
    ))
    # 6. 丁四做活
    P.append(build_puzzle(
        "丁四做活", 0, GOAL_LIVE, TOP,
        [".OXabcXO.", ".OXXdXXO.", ".OXXXXXO.", ".OOOOOOO."],
        [("b", "WIN"), ("a", "WRONG", "b"), ("c", "WRONG", "b"), ("d", "WRONG", "b")],
        default_refute="b",
    ))
    # 7. 刀把五杀白
    P.append(build_puzzle(
        "刀把五杀白", 0, GOAL_KILL, TR,
        ["....XOabc", "....XOdeO", "....XOOOO", "....XXXXX"],
        [("b", "WIN"), ("d", "WRONG", "b"), ("e", "WRONG", "b"), ("a", "WRONG", "b"), ("c", "WRONG", "b")],
        default_refute="b",
    ))
    # 8. 刀把五做活
    P.append(build_puzzle(
        "刀把五做活", 0, GOAL_LIVE, TR,
        ["....OXabc", "....OXdeX", "....OXXXX", "....OOOOO"],
        [("b", "WIN"), ("d", "WRONG", "b"), ("e", "WRONG", "b"), ("a", "WRONG", "b"), ("c", "WRONG", "b")],
        default_refute="b",
    ))
    # 9. 梅花五点眼
    P.append(build_puzzle(
        "梅花五点眼", 0, GOAL_KILL, TOP,
        [".XOOaOOX.", ".XObcdOX.", ".XOOeOOX.", ".XOOOOOX.", ".XXXXXXX."],
        [("c", "WIN"), ("a", "WRONG", "c"), ("b", "WRONG", "c"), ("d", "WRONG", "c"), ("e", "WRONG", "c")],
        default_refute="c",
    ))
    # 10. 梅花五做活
    P.append(build_puzzle(
        "梅花五做活", 0, GOAL_LIVE, TOP,
        [".OXXaXXO.", ".OXbcdXO.", ".OXXeXXO.", ".OXXXXXO.", ".OOOOOOO."],
        [("c", "WIN"), ("a", "WRONG", "c"), ("b", "WRONG", "c"), ("d", "WRONG", "c"), ("e", "WRONG", "c")],
        default_refute="c",
    ))
    # 11. 葡萄六点眼
    P.append(build_puzzle(
        "葡萄六点眼", 0, GOAL_KILL, TOP,
        [".XOabcOX.", ".XOdeOOX.", ".XOOfOOX.", ".XOOOOOX.", ".XXXXXXX."],
        [("b", "WIN"), ("e", "WRONG", "b"), ("d", "WRONG", "b"), ("a", "WRONG", "b"), ("c", "WRONG", "b")],
        default_refute="b",
    ))
    # 12. 葡萄六做活
    P.append(build_puzzle(
        "葡萄六做活", 0, GOAL_LIVE, TOP,
        [".OXabcXO.", ".OXdeXXO.", ".OXXfXXO.", ".OXXXXXO.", ".OOOOOOO."],
        [("b", "WIN"), ("e", "WRONG", "b"), ("d", "WRONG", "b"), ("a", "WRONG", "b"), ("c", "WRONG", "b")],
        default_refute="b",
    ))
    # 13. 经典倒扑
    P.append(build_puzzle(
        "经典倒扑", 0, GOAL_CAPTURE, TOP,
        ["..XOOa...", "..XOXbX..", "..XOOOX..", "..XXXXX.."],
        [("a", "CONT", "b", [((1, 4), "WIN")]), ("b", "WRONG", "a")],
        default_refute="a",
    ))
    # 14. 角部倒扑
    P.append(build_puzzle(
        "角部倒扑", 0, GOAL_CAPTURE, TR,
        ["...XOOa..", "...XOXbX.", "...XOOOX.", "...XXXXX."],
        [("a", "CONT", "b", [((1, 5), "WIN")]), ("b", "WRONG", "a")],
        default_refute="a",
    ))
    # 15. 一线接不归
    P.append(build_puzzle(
        "一线接不归", 0, GOAL_CAPTURE, TOP,
        ["..XOOa...", "..XXbOX..", "...XcOX..", "....XXX.."],
        [("a", "CONT", "b", [("c", "WIN")], "c"), ("b", "WRONG", "a"), ("c", "WRONG", "a")],
        default_refute="a",
    ))
    # 16. 双打吃筋
    P.append(build_puzzle(
        "双打吃筋", 0, GOAL_CAPTURE, TOP,
        [".X.bXc.X.", "..XOaOX..", "..XX.XX.."],
        [("a", "CONT", "b", [("c", "WIN")], "c"), ("b", "WRONG", "a"), ("c", "WRONG", "a")],
        default_refute="a",
    ))
    # 17. 曲四做活
    P.append(build_puzzle(
        "曲四做活", 0, GOAL_LIVE, TOP,
        [".OXabcXO.", ".OXXXdXO.", ".OXXXXXO.", ".OOOOOOO."],
        [("b", "WIN"), ("c", "WIN"), ("a", "WRONG", "c"), ("d", "WRONG", "b")],
        default_refute="b",
    ))
    # 18. 破眼妙手
    P.append(build_puzzle(
        "破眼妙手", 0, GOAL_KILL, TR,
        ["....XOaOb", "....XcOOO", "....XXXXX"],
        [("c", "WIN")],
        default_refute="c",
    ))
    # 19. 一路立杀
    P.append(build_puzzle(
        "一路立杀", 0, GOAL_KILL, TR,
        ["...XabOcO", "...XXOOOO", "...XXXXXX"],
        [("a", "WIN"), ("b", "WRONG", "a")],
        default_refute="a",
    ))
    # 20. 一路立活
    P.append(build_puzzle(
        "一路立活", 0, GOAL_LIVE, TR,
        ["..OabcXdX", "..OOXXXXX", "..OOOOOOO"],
        [("b", "WIN"), ("c", "WRONG", "b"), ("a", "WRONG", "b")],
        default_refute="b",
    ))

    # ==================== 进阶 20 题 (Tier 1) ====================
    # 21. 角部方四
    P.append(build_puzzle(
        "角部方四", 1, GOAL_KILL, TR,
        [".....XOab", ".....XOcd", ".....XOOO", ".....XXXX"],
        [("c", "CONT", "a", [("d", "WIN")], "d"), ("b", "CONT", "c", [("a", "WIN")], "a"),
         ("a", "WRONG", "c"), ("d", "WRONG", "c")],
        default_refute="c",
    ))
    # 22. 虎口做活
    P.append(build_puzzle(
        "虎口做活", 1, GOAL_LIVE, TR,
        ["...OXabOc", "...OXXXdX", "...OOOOOO"],
        [("d", "CONT", "b", [("a", "WIN")], "a"), ("b", "WRONG", "d"), ("a", "WRONG", "d"), ("c", "WRONG", "d")],
        default_refute="d",
    ))
    # 23. 角部托杀
    P.append(build_puzzle(
        "角部托杀", 1, GOAL_KILL, TR,
        ["...XaObcO", "...XXOOdO", "...XXXXXX"],
        [("c", "CONT", "b", [("d", "WIN")], "d"), ("b", "WRONG", "c"), ("d", "WRONG", "c"), ("a", "WRONG", "c")],
        default_refute="c",
    ))
    # 24. 角部急所活
    P.append(build_puzzle(
        "角部急所活", 1, GOAL_LIVE, TR,
        ["...OaXbcX", "...OOXXdX", "...OOOOOO"],
        [("c", "WIN"), ("b", "WRONG", "c"), ("d", "WRONG", "c"), ("a", "WRONG", "c")],
        default_refute="c",
    ))
    # 25. 边上胀牯牛
    P.append(build_puzzle(
        "边上胀牯牛", 1, GOAL_KILL, TOP,
        [".XOabcOX.", ".XOdXOOX.", ".XOOOOOX.", ".XXXXXXX."],
        [("b", "CONT", "a", [("d", "WIN")], "d"), ("a", "WRONG", "b"), ("c", "WRONG", "b"), ("d", "WRONG", "b")],
        default_refute="b",
    ))
    # 26. 破围做活
    P.append(build_puzzle(
        "破围做活", 1, GOAL_LIVE, TOP,
        [".OXabcXO.", ".OXdOXXO.", ".OXXXXXO.", ".OOOOOOO."],
        [("b", "CONT", "d", [("a", "WIN")], "a"), ("a", "WRONG", "b"), ("c", "WRONG", "b"), ("d", "WRONG", "b")],
        default_refute="b",
    ))
    # 27. 滚打包收
    P.append(build_puzzle(
        "滚打包收", 1, GOAL_CAPTURE, TOP,
        ["..XOOac..", "..XObOXX.", "..XXXXXX."],
        [("b", "CONT", "a", [("c", "WIN")], "c"), ("a", "WRONG", "c")],
        default_refute="a",
    ))
    # 28. 角部五目杀
    P.append(build_puzzle(
        "角部五目杀", 1, GOAL_KILL, TR,
        ["....XOabc", "....XOOef", "....XOOOO", "....XXXXX"],
        [("e", "CONT", "b", [("f", "WIN")], "f"), ("b", "WRONG", "e"),
         ("a", "WRONG", "e"), ("c", "WRONG", "e"), ("f", "WRONG", "e")],
        default_refute="e",
    ))
    # 29. 角部五目活
    P.append(build_puzzle(
        "角部五目活", 1, GOAL_LIVE, TR,
        ["....OXabc", "....OXXef", "....OXXXX", "....OOOOO"],
        [("e", "WIN"), ("b", "WRONG", "e"), ("a", "WRONG", "e"), ("c", "WRONG", "e"), ("f", "WRONG", "e")],
        default_refute="e",
    ))
    # 30. 大猪嘴破眼
    P.append(build_puzzle(
        "大猪嘴破眼", 1, GOAL_KILL, TR,
        ["...XaObcd", "...XOOOeO", "...XOOOOO", "...XXXXXX"],
        [("c", "CONT", "b", [("e", "WIN")], "e"), ("b", "WRONG", "c"), ("d", "WRONG", "c"), ("e", "WRONG", "c")],
        default_refute="c",
    ))
    # 31. 大猪嘴做活
    P.append(build_puzzle(
        "大猪嘴做活", 1, GOAL_LIVE, TR,
        ["...OaXbcd", "...OXXXeX", "...OXXXXX", "...OOOOOO"],
        [("c", "WIN"), ("b", "WRONG", "c"), ("d", "WRONG", "c"), ("e", "WRONG", "c")],
        default_refute="c",
    ))
    # 32. 一路虎活
    P.append(build_puzzle(
        "一路虎活", 1, GOAL_LIVE, TOP,
        [".OabXcO..", ".OXeXXO..", ".OOOOOO.."],
        [("a", "CONT", "b", [("e", "WIN")], "e"), ("e", "WRONG", "a"), ("b", "WRONG", "e")],
        default_refute="a",
    ))
    # 33. 破虎杀白
    P.append(build_puzzle(
        "破虎杀白", 1, GOAL_KILL, TOP,
        [".XabOcX..", ".XOeOOX..", ".XXXXXX.."],
        [("a", "CONT", "e", [("b", "WIN")], "b"), ("e", "WRONG", "a"), ("b", "WRONG", "e")],
        default_refute="a",
    ))
    # 34. 一路劫活
    P.append(build_puzzle(
        "一路劫活", 1, GOAL_KO, TR,
        ["..OOaOXbc", "..OOOXXXX", "..OOOOOOO"],
        [("a", "WIN"), ("b", "WRONG", "a"), ("c", "WRONG", "a")],
        default_refute="a",
    ))
    # 35. 二路夹杀
    P.append(build_puzzle(
        "二路夹杀", 1, GOAL_KILL, TOP,
        ["..XXaObcX", "..XXOOdOX", "..XXXXXXX"],
        [("b", "CONT", "c", [("d", "WIN")], "d"), ("a", "WRONG", "b"), ("c", "WRONG", "b"), ("d", "WRONG", "b")],
        default_refute="b",
    ))
    # 36. 二路夹活
    P.append(build_puzzle(
        "二路夹活", 1, GOAL_LIVE, TOP,
        ["..OOaXbcO", "..OOXXdXO", "..OOOOOOO"],
        [("b", "WIN"), ("a", "WRONG", "b"), ("c", "WRONG", "b"), ("d", "WRONG", "b")],
        default_refute="b",
    ))
    # 37. 左角点杀
    P.append(build_puzzle(
        "左角点杀", 1, GOAL_KILL, TL,
        ["abOcX....", "dOOOX....", "OOOOX....", "XXXXX...."],
        [("a", "CONT", "b", [("c", "WIN")], "c"), ("b", "WRONG", "a"), ("d", "WRONG", "a"), ("c", "WRONG", "a")],
        default_refute="a",
    ))
    # 38. 左角做活
    P.append(build_puzzle(
        "左角做活", 1, GOAL_LIVE, TL,
        ["abXcO....", "dXXXO....", "XXXXO....", "OOOOO...."],
        [("a", "WIN"), ("b", "WRONG", "a"), ("d", "WRONG", "a"), ("c", "WRONG", "a")],
        default_refute="a",
    ))
    # 39. 左角倒扑
    P.append(build_puzzle(
        "左角倒扑", 1, GOAL_CAPTURE, TL,
        ["..aOOX...", ".XbXOX...", ".XOOOX...", ".XXXXX..."],
        [("a", "CONT", "b", [((1, 3), "WIN")]), ("b", "WRONG", "a")],
        default_refute="a",
    ))
    # 40. 左角接不归
    P.append(build_puzzle(
        "左角接不归", 1, GOAL_CAPTURE, TL,
        ["...aOOX..", "..XObXX..", "..XOcX...", "..XXX...."],
        [("a", "CONT", "b", [("c", "WIN")], "c"), ("b", "WRONG", "a"), ("c", "WRONG", "a")],
        default_refute="a",
    ))

    # ==================== 段位 20 题 (Tier 2) ====================
    # 41. 纵列倒扑
    P.append(build_puzzle(
        "纵列倒扑", 2, GOAL_CAPTURE, TOP,
        ["..XXXXX..", "..XOOOX..", "..XOXOX..", "..XOab...", "..XXXXX.."],
        [("b", "CONT", "a", [((2, 4), "WIN")]), ("a", "WRONG", "b")],
        default_refute="b",
    ))
    # 42. 梅花做活
    P.append(build_puzzle(
        "梅花做活", 2, GOAL_LIVE, TOP,
        [".OXabcXO.", ".OXOefXO.", ".OXXXXXO.", ".OOOOOOO."],
        [("e", "CONT", "a", [("b", "WIN")], "b"), ("a", "WRONG", "e"),
         ("b", "WRONG", "e"), ("c", "WRONG", "e"), ("f", "WRONG", "e")],
        default_refute="e",
    ))
    # 43. 角部透点杀 (3手: 弃三子回点曲三)
    P.append(build_puzzle(
        "角部透点杀", 2, GOAL_KILL, TR,
        ["....XOabc", "....XOXed", "....XOOOO", "....XXXXX"],
        [("b", "CONT", "e", [("a", "CONT", "c", [("a", "WIN")]), ("c", "WRONG", "a")], "a"),
         ("a", "WRONG", "b"), ("e", "WRONG", "b"), ("c", "WRONG", "b"), ("d", "WRONG", "b")],
        default_refute="b",
    ))
    # 44. 角部透点活 (3手: 先手破侵入子做活)
    P.append(build_puzzle(
        "角部透点活", 2, GOAL_LIVE, TR,
        ["....OXabc", "....OXOed", "....OXXXX", "....OOOOO"],
        [("b", "CONT", "e", [("a", "CONT", "d", [("c", "WIN")]), ("c", "WRONG", "a")], "a"),
         ("a", "WRONG", "b"), ("e", "WRONG", "b"), ("c", "WRONG", "b"), ("d", "WRONG", "b")],
        default_refute="b",
    ))
    # 45. 三手连环吃 (3手)
    P.append(build_puzzle(
        "三手连环吃", 2, GOAL_CAPTURE, TOP,
        [".XOOaOefX", ".XObcOXXX", ".XXXOOX..", "....XXX.."],
        [("b", "CONT", "a", [("c", "CONT", "e", [("f", "WIN")], "f")], "c"),
         ("a", "WRONG", "b"), ("c", "WRONG", "a"), ("e", "WRONG", "a")],
        default_refute="a",
    ))
    # 46. 扑后连环吃 (2手: 封锁后破眼倒扑)
    P.append(build_puzzle(
        "扑后连环吃", 2, GOAL_CAPTURE, TR,
        ["....XOOa.", "....XOXbO", "....XOOOc", "....XXXXd"],
        [("c", "CONT", "b", [("a", "WIN")]),
         ("a", "WRONG", "b"), ("b", "WRONG", "c")],
        default_refute="c",
    ))
    # 47. 弃三点杀 (3手)
    P.append(build_puzzle(
        "弃三点杀", 2, GOAL_KILL, TOP,
        [".XOabcdX.", ".XOeXOOX.", ".XOOOOOX.", ".XXXXXXX."],
        [("b", "CONT", "c", [("e", "CONT", "a", [((1, 4), "WIN")], (1, 4))], "e"),
         ("a", "WRONG", "b"), ("c", "WRONG", "b"), ("d", "WRONG", "b"), ("e", "WRONG", "b")],
        default_refute="b",
    ))
    # 48. 破围净活 (2手)
    P.append(build_puzzle(
        "破围净活", 2, GOAL_LIVE, TOP,
        [".OXabcdO.", ".OXeOXXO.", ".OXXXXXO.", ".OOOOOOO."],
        [("b", "CONT", "e", [("a", "WIN")], "a"),
         ("a", "WRONG", "b"), ("c", "WRONG", "b"), ("d", "WRONG", "b"), ("e", "WRONG", "b")],
        default_refute="b",
    ))
    # 49. 封口倒扑 (2手)
    P.append(build_puzzle(
        "封口倒扑", 2, GOAL_CAPTURE, TOP,
        [".XOOaX...", ".XOXbX...", ".XOOcd...", ".XXXXX..."],
        [("c", "CONT", "b", [("a", "WIN")], "a"),
         ("a", "WRONG", "c"), ("b", "WRONG", "c"), ("d", "WRONG", "c")],
        default_refute="c",
    ))
    # 50. 三手征子 (3手)
    P.append(build_puzzle(
        "三手征子", 2, GOAL_CAPTURE, TOP,
        ["..XOOa...", "..XXbcX..", "..XXdeX..", "...XXXX.."],
        [("a", "CONT", "b", [("c", "CONT", "d", [("e", "WIN")], "e")], "c"),
         ("b", "WRONG", "a"), ("c", "WRONG", "a"), ("d", "WRONG", "a")],
        default_refute="a",
    ))
    # 51. 四手边征 (4手)
    P.append(build_puzzle(
        "四手边征", 2, GOAL_CAPTURE, TOP,
        [".XOOa....", ".XXbcX...", "..XdeX...", "..XfgX...", "...XXX..."],
        [("a", "CONT", "b", [
            ("c", "CONT", "d", [
                ("e", "CONT", "f", [("g", "WIN")], "g"),
                ("f", "WRONG", "e"),
            ], "e"),
            ("d", "WRONG", "c"),
        ], "c"), ("b", "WRONG", "a")],
        default_refute="a",
    ))
    # 52. 双活妙手
    P.append(build_puzzle(
        "双活妙手", 2, GOAL_SEKI, TOP,
        [".OXaOObXO", ".OXXcXXXO", ".OXXXXXXO", ".OOOOOOOO"],
        [("c", "WIN"), ("a", "WRONG", "c"), ("b", "WRONG", "c")],
        default_refute="c",
    ))
    # 53. 破双活杀 (2手)
    P.append(build_puzzle(
        "破双活杀", 2, GOAL_KILL, TOP,
        [".XOaXXbOX", ".XOOcOOOX", ".XOOOOOOX", ".XXXXXXXX"],
        [("c", "CONT", "a", [("b", "WIN")], "b"), ("a", "WRONG", "c"), ("b", "WRONG", "c")],
        default_refute="c",
    ))
    # 54. 左角劫活
    P.append(build_puzzle(
        "左角劫活", 2, GOAL_KO, TL,
        ["cbXOaOO..", "XXXXOOO..", "OOOOOOO.."],
        [("a", "WIN"), ("b", "WRONG", "a"), ("c", "WRONG", "a")],
        default_refute="a",
    ))
    # 55. 枷吃二子 (2手)
    P.append(build_puzzle(
        "枷吃二子", 2, GOAL_CAPTURE, TOP,
        ["..XXXa...", "..XOObX..", "..XXcdX..", "...XXXX.."],
        [("b", "CONT", "c", [("d", "WIN")], "d"),
         ("c", "WRONG", "b"), ("d", "WRONG", "b"), ("a", "WRONG", "b")],
        default_refute="b",
    ))
    # 56. 大龙紧气吃 (2手)
    P.append(build_puzzle(
        "大龙紧气吃", 2, GOAL_CAPTURE, TOP,
        [".XOOa.O..", ".XOXbXX..", ".XOXXcdX.", ".XOOOOX..", ".XXXXXX.."],
        [("a", "CONT", "c", [("d", "WIN")], "d"),
         ("c", "WRONG", "a"), ("b", "WRONG", "a"), ("d", "WRONG", "a")],
        default_refute="a",
    ))
    # 57. 左角五目杀 (2手)
    P.append(build_puzzle(
        "左角五目杀", 2, GOAL_KILL, TL,
        ["cbaOX....", "feOOX....", "OOOOX....", "XXXXX...."],
        [("e", "CONT", "b", [("f", "WIN")], "f"),
         ("b", "WRONG", "e"), ("a", "WRONG", "e"), ("c", "WRONG", "e"), ("f", "WRONG", "e")],
        default_refute="e",
    ))
    # 58. 左角丁四杀 (2手)
    P.append(build_puzzle(
        "左角丁四杀", 2, GOAL_KILL, TL,
        ["dcbOaX...", "OeOOOX...", "OOOOOX...", "XXXXXX..."],
        [("c", "CONT", "b", [("e", "WIN")], "e"),
         ("b", "WRONG", "c"), ("d", "WRONG", "c"), ("e", "WRONG", "c")],
        default_refute="c",
    ))
    # 59. 左角透点杀 (3手: 弃三子回点曲三)
    P.append(build_puzzle(
        "左角透点杀", 2, GOAL_KILL, TL,
        ["cbaOX....", "deXOX....", "OOOOX....", "XXXXX...."],
        [("b", "CONT", "e", [("a", "CONT", "c", [("a", "WIN")]), ("c", "WRONG", "a")], "a"),
         ("a", "WRONG", "b"), ("e", "WRONG", "b"), ("c", "WRONG", "b"), ("d", "WRONG", "b")],
        default_refute="b",
    ))
    # 60. 曲五点杀 (2手)
    P.append(build_puzzle(
        "曲五点杀", 2, GOAL_KILL, TOP,
        [".XOabOOX.", ".XOcdOOX.", ".XOOeOOX.", ".XOOOOOX.", ".XXXXXXX."],
        [("d", "CONT", "b", [("c", "WIN")], "c"),
         ("b", "WRONG", "d"), ("c", "WRONG", "d"), ("a", "WRONG", "d"), ("e", "WRONG", "d")],
        default_refute="d",
    ))

    return P


def main():
    puzzles = curated_puzzles()
    assert len(puzzles) == 60, f"Expected 60 puzzles, got {len(puzzles)}"
    seen = set()
    used_titles = set()
    records = bytearray()
    node_pool = bytearray()
    json_out = []

    for pid, p in enumerate(puzzles):
        key = (p["edge_mask"], bytes(p["board"]))
        assert key not in seen, f"Duplicate board at #{pid} ({p['title']})"
        seen.add(key)
        assert p["title"] not in used_titles, f"Duplicate title {p['title']}"
        used_titles.add(p["title"])

        title_b = p["title"].encode("utf-8")
        assert len(title_b) < 24, f"Title too long: {p['title']}"
        title_pad = title_b + b"\x00" * (24 - len(title_b))

        node_offset = len(node_pool) // 4
        for n in p["nodes"]:
            node_pool.extend(struct.pack("<BBBB", n[0], n[1], n[2], n[3]))

        packed = pack_board(p["board"])
        records.extend(struct.pack("<BBBBHBB", pid, p["tier"], p["goal"], p["edge_mask"],
                                   node_offset, len(p["nodes"]), p["main_moves"]))
        records.extend(title_pad)
        records.extend(packed)
        records.extend(b"\x00\x00\x00")
        assert len(records) == (pid + 1) * 56

        json_out.append({
            "id": pid,
            "tier": p["tier"],
            "goal": p["goal"],
            "edge_mask": p["edge_mask"],
            "title": p["title"],
            "main_moves": p["main_moves"],
            "node_count": len(p["nodes"]),
        })

    body = bytes(records) + bytes(node_pool)
    crc = zlib.crc32(body) & 0xFFFFFFFF
    header = struct.pack("<4sHHII", b"PNTG", 1, len(puzzles), crc, 0)
    bank = header + body

    (ROOT / "puzzles.pntg").write_bytes(bank)
    (ROOT / "puzzles.json").write_text(json.dumps(json_out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(puzzles)} Go Tsumego puzzles -> puzzles.pntg ({len(bank)} bytes, CRC=0x{crc:08x}, nodes={len(node_pool)//4})")
    for t, name in enumerate(("入门", "进阶", "段位")):
        sub = [x for x in puzzles if x["tier"] == t]
        print(f"  Tier {t} ({name}): {len(sub)} puzzles, main_moves={min(x['main_moves'] for x in sub)}..{max(x['main_moves'] for x in sub)}")


if __name__ == "__main__":
    main()
