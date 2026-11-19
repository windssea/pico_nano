# SPDX-License-Identifier: Apache-2.0
"""中国象棋残局题库生成（杀型逆推+弃子构造）与精确强制绝杀证明器。
Chinese Chess (Xiangqi) endgame bank generator (retrograde mating-net construction) and exact forced-mate prover."""
import argparse
import json
import pathlib
import random
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent

K_KING = 1      # 帅 / 将
K_ADVISOR = 2   # 仕 / 士
K_ELEPHANT = 3  # 相 / 象
K_HORSE = 4     # 马
K_ROOK = 5      # 车
K_CANNON = 6    # 炮
K_PAWN = 7      # 兵 / 卒
BLACK_BIT = 8

RED_PALACE = {(3, 7), (4, 7), (5, 7), (3, 8), (4, 8), (5, 8), (3, 9), (4, 9), (5, 9)}
BLK_PALACE = {(3, 0), (4, 0), (5, 0), (3, 1), (4, 1), (5, 1), (3, 2), (4, 2), (5, 2)}
RED_ADV_PTS = {(3, 7), (5, 7), (4, 8), (3, 9), (5, 9)}
BLK_ADV_PTS = {(3, 0), (5, 0), (4, 1), (3, 2), (5, 2)}
RED_ELE_PTS = {(2, 5), (6, 5), (0, 7), (4, 7), (8, 7), (2, 9), (6, 9)}
BLK_ELE_PTS = {(2, 0), (6, 0), (0, 2), (4, 2), (8, 2), (2, 4), (6, 4)}


def side_of(p):
    if p == 0:
        return -1
    return 1 if (p & BLACK_BIT) else 0


def kind_of(p):
    return p & 7


def make_piece(side, kind):
    return (BLACK_BIT | kind) if side else kind


def kings_facing(board):
    rk = bk = -1
    for i, p in enumerate(board):
        if p == K_KING:
            rk = i
        elif p == (BLACK_BIT | K_KING):
            bk = i
    if rk < 0 or bk < 0 or (rk % 9) != (bk % 9):
        return False
    col = rk % 9
    for y in range(bk // 9 + 1, rk // 9):
        if board[y * 9 + col] != 0:
            return False
    return True


def attacks_square(board, attacker_side, tx, ty):
    target_idx = ty * 9 + tx
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        x, y = tx + dx, ty + dy
        screens = 0
        while 0 <= x < 9 and 0 <= y < 10:
            p = board[y * 9 + x]
            if p != 0:
                if side_of(p) == attacker_side:
                    k = kind_of(p)
                    if screens == 0 and k == K_ROOK:
                        return True
                    if screens == 0 and k == K_KING and board[target_idx] == make_piece(1 - attacker_side, K_KING):
                        return True
                    if screens == 1 and k == K_CANNON:
                        return True
                screens += 1
                if screens >= 2:
                    break
            x += dx
            y += dy

    palace = BLK_PALACE if attacker_side else RED_PALACE
    if (tx, ty) in palace:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = tx + dx, ty + dy
            if (nx, ny) in palace and board[ny * 9 + nx] == make_piece(attacker_side, K_KING):
                return True

    for hx_off, hy_off in ((-2, -1), (-2, 1), (2, -1), (2, 1), (-1, -2), (1, -2), (-1, 2), (1, 2)):
        hx, hy = tx + hx_off, ty + hy_off
        if 0 <= hx < 9 and 0 <= hy < 10 and board[hy * 9 + hx] == make_piece(attacker_side, K_HORSE):
            leg_x = hx + (-1 if hx_off > 0 else 1) if abs(hx_off) == 2 else hx
            leg_y = hy + (-1 if hy_off > 0 else 1) if abs(hy_off) == 2 else hy
            if board[leg_y * 9 + leg_x] == 0:
                return True

    if attacker_side == 0:
        if ty + 1 <= 6 and board[(ty + 1) * 9 + tx] == make_piece(0, K_PAWN):
            return True
        if ty <= 4:
            for dx in (-1, 1):
                if 0 <= tx + dx < 9 and board[ty * 9 + tx + dx] == make_piece(0, K_PAWN):
                    return True
    else:
        if ty - 1 >= 3 and board[(ty - 1) * 9 + tx] == make_piece(1, K_PAWN):
            return True
        if ty >= 5:
            for dx in (-1, 1):
                if 0 <= tx + dx < 9 and board[ty * 9 + tx + dx] == make_piece(1, K_PAWN):
                    return True

    if (tx, ty) in palace:
        for dx, dy in ((-1, -1), (-1, 1), (1, -1), (1, 1)):
            nx, ny = tx + dx, ty + dy
            if (nx, ny) in palace and board[ny * 9 + nx] == make_piece(attacker_side, K_ADVISOR):
                return True
    ele_pts = BLK_ELE_PTS if attacker_side else RED_ELE_PTS
    if (tx, ty) in ele_pts:
        for dx, dy in ((-2, -2), (-2, 2), (2, -2), (2, 2)):
            nx, ny = tx + dx, ty + dy
            if (nx, ny) in ele_pts and board[ny * 9 + nx] == make_piece(attacker_side, K_ELEPHANT):
                if board[(ty + dy // 2) * 9 + (tx + dx // 2)] == 0:
                    return True
    return False


def in_check(board, side):
    king_code = make_piece(side, K_KING)
    for idx, p in enumerate(board):
        if p == king_code:
            return attacks_square(board, 1 - side, idx % 9, idx // 9)
    return True


def pseudo_moves(board, side):
    moves = []
    palace = BLK_PALACE if side else RED_PALACE
    for idx, p in enumerate(board):
        if side_of(p) != side:
            continue
        x, y = idx % 9, idx // 9
        k = kind_of(p)
        if k == K_KING:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if (nx, ny) in palace and side_of(board[ny * 9 + nx]) != side:
                    moves.append((idx, ny * 9 + nx))
        elif k == K_ADVISOR:
            for dx, dy in ((-1, -1), (-1, 1), (1, -1), (1, 1)):
                nx, ny = x + dx, y + dy
                if (nx, ny) in palace and side_of(board[ny * 9 + nx]) != side:
                    moves.append((idx, ny * 9 + nx))
        elif k == K_ELEPHANT:
            for dx, dy in ((-2, -2), (-2, 2), (2, -2), (2, 2)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < 9 and ((0 <= ny <= 4) if side else (5 <= ny <= 9)):
                    if board[(y + dy // 2) * 9 + (x + dx // 2)] == 0 and side_of(board[ny * 9 + nx]) != side:
                        moves.append((idx, ny * 9 + nx))
        elif k == K_HORSE:
            for lx, ly, dx, dy in (
                (1, 0, 2, 1), (1, 0, 2, -1), (-1, 0, -2, 1), (-1, 0, -2, -1),
                (0, 1, 1, 2), (0, 1, -1, 2), (0, -1, 1, -2), (0, -1, -1, -2),
            ):
                if 0 <= x + lx < 9 and 0 <= y + ly < 10 and board[(y + ly) * 9 + (x + lx)] == 0:
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < 9 and 0 <= ny < 10 and side_of(board[ny * 9 + nx]) != side:
                        moves.append((idx, ny * 9 + nx))
        elif k == K_ROOK:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                while 0 <= nx < 9 and 0 <= ny < 10:
                    q = board[ny * 9 + nx]
                    if q == 0:
                        moves.append((idx, ny * 9 + nx))
                    else:
                        if side_of(q) != side:
                            moves.append((idx, ny * 9 + nx))
                        break
                    nx += dx
                    ny += dy
        elif k == K_CANNON:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                screen = False
                while 0 <= nx < 9 and 0 <= ny < 10:
                    q = board[ny * 9 + nx]
                    if not screen:
                        if q == 0:
                            moves.append((idx, ny * 9 + nx))
                        else:
                            screen = True
                    else:
                        if q != 0:
                            if side_of(q) != side:
                                moves.append((idx, ny * 9 + nx))
                            break
                    nx += dx
                    ny += dy
        elif k == K_PAWN:
            fwd = 1 if side else -1
            ny = y + fwd
            if 0 <= ny < 10 and side_of(board[ny * 9 + x]) != side:
                moves.append((idx, ny * 9 + x))
            crossed = (y >= 5) if side else (y <= 4)
            if crossed:
                for dx in (-1, 1):
                    nx = x + dx
                    if 0 <= nx < 9 and side_of(board[y * 9 + nx]) != side:
                        moves.append((idx, y * 9 + nx))
    return moves


def legal_moves(board, side):
    out = []
    for frm, to in pseudo_moves(board, side):
        captured = board[to]
        board[to] = board[frm]
        board[frm] = 0
        if not kings_facing(board) and not in_check(board, side):
            out.append((frm, to))
        board[frm] = board[to]
        board[to] = captured
    return out


def has_legal_move(board, side):
    for frm, to in pseudo_moves(board, side):
        captured = board[to]
        board[to] = board[frm]
        board[frm] = 0
        ok = not kings_facing(board) and not in_check(board, side)
        board[frm] = board[to]
        board[to] = captured
        if ok:
            return True
    return False


def position_valid(board):
    counts = [[0] * 8, [0] * 8]
    total = 0
    for idx, p in enumerate(board):
        if p == 0:
            continue
        s = side_of(p)
        k = kind_of(p)
        if k < 1 or k > 7 or (p & ~0xF):
            return False
        counts[s][k] += 1
        total += 1
        x, y = idx % 9, idx // 9
        if k == K_KING and (x, y) not in (BLK_PALACE if s else RED_PALACE):
            return False
        if k == K_ADVISOR and (x, y) not in (BLK_ADV_PTS if s else RED_ADV_PTS):
            return False
        if k == K_ELEPHANT and (x, y) not in (BLK_ELE_PTS if s else RED_ELE_PTS):
            return False
        if k == K_PAWN:
            if s == 0:
                if y > 6 or (y in (5, 6) and x % 2 != 0):
                    return False
            else:
                if y < 3 or (y in (3, 4) and x % 2 != 0):
                    return False
    if total < 3 or total > 16:
        return False
    for s in (0, 1):
        if counts[s][K_KING] != 1:
            return False
        if any(counts[s][k] > 2 for k in (K_ADVISOR, K_ELEPHANT, K_HORSE, K_ROOK, K_CANNON)):
            return False
        if counts[s][K_PAWN] > 5:
            return False
    if kings_facing(board) or in_check(board, 1) or not has_legal_move(board, 0):
        return False
    return True


def prove_forced_mate(board, max_red_moves, node_budget=100000):
    """带杀手表启发式的精确极大极小绝杀证明器（对一切合法应着100%精确，无漏剪）。
    Exact minimax forced-mate prover with killer-move ordering (100% exact over all legal moves)."""
    memo = {}
    killer = {}
    nodes = [0]

    def can_force(side, plies_left):
        nodes[0] += 1
        if nodes[0] > node_budget:
            raise TimeoutError("Node budget exceeded")
        b_bytes = bytes(board)
        key = (b_bytes, side, plies_left)
        if key in memo:
            return memo[key]
        if plies_left == 0:
            won = not has_legal_move(board, side)
            res = (won, ())
            memo[key] = res
            return res
        moves = legal_moves(board, side)
        if not moves:
            res = (side == 1, ())
            memo[key] = res
            return res

        if side == 0:
            scored = []
            km = killer.get((0, plies_left))
            for frm, to in moves:
                cap = board[to]
                board[to] = board[frm]
                board[frm] = 0
                chk = in_check(board, 1)
                # Exact safe optimization at final Red ply: if Black is NOT in check and has any free piece move,
                # has_legal_move will quickly return True, so put non-checks last!
                board[frm] = board[to]
                board[to] = cap
                scored.append((1 if (frm, to) == km else 0, 1 if chk else 0, 1 if cap else 0, frm, to))
            scored.sort(reverse=True)
            best_win = None
            for _, _, _, frm, to in scored:
                cap = board[to]
                board[to] = board[frm]
                board[frm] = 0
                ok, line = can_force(1, plies_left - 1)
                board[frm] = board[to]
                board[to] = cap
                if ok:
                    killer[(0, plies_left)] = (frm, to)
                    cand = ((frm, to),) + line
                    if best_win is None or len(cand) < len(best_win):
                        best_win = cand
                        if len(best_win) <= 1:
                            break
            res = (True, best_win) if best_win is not None else (False, ())
            memo[key] = res
            return res
        else:
            km = killer.get((1, plies_left))
            if km and km in moves:
                frm, to = km
                cap = board[to]
                board[to] = board[frm]
                board[frm] = 0
                ok, _ = can_force(0, plies_left - 1)
                board[frm] = board[to]
                board[to] = cap
                if not ok:
                    memo[key] = (False, ())
                    return (False, ())
            scored_b = []
            for frm, to in moves:
                if (frm, to) == km:
                    continue
                cap = board[to]
                board[to] = board[frm]
                board[frm] = 0
                chk = in_check(board, 0)
                board[frm] = board[to]
                board[to] = cap
                scored_b.append((1 if chk else 0, 1 if cap else 0, frm, to))
            scored_b.sort(reverse=True)
            longest_line = ()
            # If km was already verified ok above, include its line too!
            if km and km in moves:
                frm, to = km
                cap = board[to]
                board[to] = board[frm]
                board[frm] = 0
                _, line = can_force(0, plies_left - 1)
                board[frm] = board[to]
                board[to] = cap
                longest_line = ((frm, to),) + line
            for _, _, frm, to in scored_b:
                cap = board[to]
                board[to] = board[frm]
                board[frm] = 0
                ok, line = can_force(0, plies_left - 1)
                board[frm] = board[to]
                board[to] = cap
                if not ok:
                    killer[(1, plies_left)] = (frm, to)
                    memo[key] = (False, ())
                    return (False, ())
                cand = ((frm, to),) + line
                if len(cand) > len(longest_line):
                    longest_line = cand
            res = (True, longest_line)
            memo[key] = res
            return res

    for m in range(1, max_red_moves + 1):
        memo.clear()
        killer.clear()
        nodes[0] = 0
        ok, line = can_force(0, 2 * m - 1)
        if ok:
            return m, line
    return None, None


def board_from_pieces(pieces):
    cmap = {
        'K': make_piece(0, K_KING), 'A': make_piece(0, K_ADVISOR), 'E': make_piece(0, K_ELEPHANT),
        'H': make_piece(0, K_HORSE), 'R': make_piece(0, K_ROOK), 'C': make_piece(0, K_CANNON), 'P': make_piece(0, K_PAWN),
        'k': make_piece(1, K_KING), 'a': make_piece(1, K_ADVISOR), 'e': make_piece(1, K_ELEPHANT),
        'h': make_piece(1, K_HORSE), 'r': make_piece(1, K_ROOK), 'c': make_piece(1, K_CANNON), 'p': make_piece(1, K_PAWN),
    }
    b = [0] * 90
    for code, x, y in pieces:
        idx = y * 9 + x
        assert b[idx] == 0, f"Overlap at ({x},{y})"
        b[idx] = cmap[code]
    return b


# Hand-crafted & verified tactical foundations across 1, 2, 3, and 4 move combinations!
CLASSIC_TIER0 = [
    ("对面笑", [('K', 4, 9), ('R', 2, 4), ('P', 3, 2), ('k', 3, 0), ('a', 4, 1), ('r', 8, 8)]),
    ("铁门栓", [('K', 5, 9), ('C', 4, 9), ('A', 4, 8), ('R', 1, 1), ('k', 4, 0), ('a', 3, 0), ('a', 4, 1)]),
    ("重炮杀", [('K', 4, 9), ('C', 0, 0), ('C', 1, 3), ('P', 2, 1), ('k', 3, 0), ('a', 4, 1)]),
    ("闷宫杀", [('K', 3, 9), ('C', 1, 3), ('H', 6, 2), ('k', 4, 0), ('a', 3, 0), ('a', 4, 1), ('r', 8, 8)]),
    ("卧槽马", [('K', 3, 9), ('H', 3, 3), ('R', 1, 2), ('k', 4, 0), ('a', 5, 0), ('a', 4, 1), ('r', 8, 8)]),
    ("挂角马", [('K', 4, 9), ('H', 4, 4), ('R', 1, 4), ('P', 2, 1), ('k', 4, 0), ('a', 5, 0), ('a', 4, 1)]),
    ("钓鱼马", [('K', 3, 9), ('H', 7, 5), ('R', 0, 2), ('k', 5, 1), ('a', 5, 0), ('a', 4, 1), ('r', 8, 8)]),
    ("八角马", [('K', 5, 9), ('H', 6, 3), ('R', 0, 4), ('k', 3, 0), ('a', 4, 1), ('r', 4, 0)]),
    ("白脸将", [('K', 4, 9), ('R', 1, 2), ('C', 0, 0), ('P', 1, 0), ('k', 3, 1), ('a', 4, 1), ('a', 3, 2)]),
    ("双车错", [('K', 3, 9), ('R', 1, 3), ('R', 2, 4), ('k', 4, 0), ('a', 5, 0), ('a', 5, 2), ('r', 8, 8)]),
    ("海底捞月", [('K', 3, 9), ('R', 5, 0), ('C', 1, 4), ('k', 3, 1), ('a', 4, 1), ('a', 3, 2)]),
    ("马后炮", [('K', 4, 9), ('C', 0, 0), ('H', 1, 2), ('P', 2, 1), ('k', 3, 0), ('a', 4, 1)]),
    ("天地炮", [('K', 3, 9), ('C', 4, 7), ('C', 1, 0), ('P', 5, 1), ('k', 4, 0), ('a', 5, 0), ('a', 4, 1), ('e', 4, 2)]),
    ("二鬼拍门", [('K', 4, 9), ('P', 3, 1), ('P', 5, 1), ('R', 0, 3), ('k', 4, 0), ('a', 4, 1), ('r', 8, 8)]),
    ("大胆穿心", [('K', 5, 9), ('C', 4, 7), ('R', 4, 4), ('R', 1, 2), ('k', 4, 0), ('a', 3, 0), ('a', 4, 1)]),
    ("夹车炮", [('K', 4, 9), ('C', 1, 2), ('R', 2, 3), ('C', 0, 0), ('k', 4, 0), ('a', 4, 1), ('a', 5, 0)]),
    ("拔簧马", [('K', 3, 9), ('R', 4, 3), ('H', 4, 2), ('k', 4, 0), ('a', 3, 0), ('a', 5, 0)]),
    ("侧面虎", [('K', 4, 9), ('R', 1, 3), ('H', 4, 2), ('k', 3, 1), ('a', 4, 1), ('a', 3, 2)]),
    ("双马饮泉", [('K', 4, 9), ('H', 2, 2), ('H', 5, 3), ('k', 4, 0), ('a', 5, 0), ('a', 4, 1), ('r', 0, 1)]),
    ("老卒搜山", [('K', 5, 9), ('P', 3, 1), ('R', 1, 4), ('k', 4, 0), ('a', 5, 0), ('a', 4, 1)]),
]

TITLES_TIER1 = [
    "调虎离山", "顺手牵羊", "声东击西", "引蛇出洞", "隔岸观火",
    "金钩挂玉", "单骑救主", "三子归边", "臣压君位", "双献美酒",
    "云遮月影", "长驱直入", "出奇制胜", "借力打力", "釜底抽薪",
    "投石问路", "步步为营", "李代桃僵", "围魏救赵", "一锤定音",
]

TITLES_TIER2 = [
    "七星残影", "千里独行", "大征东局", "小征东局", "火烧连营",
    "暗渡陈仓", "十面埋伏", "霸王卸甲", "单刀赴会", "四面楚歌",
    "猿猴摘果", "青龙出水", "困兽犹斗", "背水一战", "奇门遁甲",
    "巧女穿针", "推窗望月", "古谱遗珍", "满盘生辉", "破釜沉舟",
]


def _retrograde_compose(rng, mated_boards, target_moves, seen):
    """从真实绝杀终盘反向退步（含还原弃子与黑卒一步绝杀反威胁），秒级构造精确N步连杀残局。
    Unmake exact plies from real checkmate positions with sacrifice uncaptures and Black pawn 1-move mate threats."""
    while True:
        b = list(rng.choice(mated_boards))
        # Clear lower half (y=6..9) and install an authentic Black Pawn 1-move checkmate threat
        # that can NEVER retreat to y=0..4, forcing Red to win via continuous forcing checks/sacrifices!
        for i in range(54, 90):
            # Keep Red Cannons on file 3/4/5 if they are part of the mating net!
            if b[i] != make_piece(0, K_CANNON):
                b[i] = 0

        # Choose a Red King column that doesn't accidentally create an unwanted open-file check
        threat_style = rng.randint(0, 2)
        if threat_style == 0 and b[9 * 9 + 3] == 0 and b[9 * 9 + 4] == 0 and b[8 * 9 + 2] == 0 and b[7 * 9 + 3] == 0:
            b[9 * 9 + 3] = make_piece(0, K_KING)
            b[9 * 9 + 4] = make_piece(0, K_ADVISOR)
            b[8 * 9 + 2] = make_piece(1, K_PAWN)
            b[7 * 9 + 3] = make_piece(1, K_PAWN)
        elif threat_style == 1 and b[9 * 9 + 5] == 0 and b[9 * 9 + 4] == 0 and b[8 * 9 + 6] == 0 and b[7 * 9 + 5] == 0:
            b[9 * 9 + 5] = make_piece(0, K_KING)
            b[9 * 9 + 4] = make_piece(0, K_ADVISOR)
            b[8 * 9 + 6] = make_piece(1, K_PAWN)
            b[7 * 9 + 5] = make_piece(1, K_PAWN)
        elif b[9 * 9 + 4] == 0 and b[9 * 9 + 3] == 0 and b[9 * 9 + 5] == 0 and b[8 * 9 + 3] == 0 and b[7 * 9 + 4] == 0:
            b[9 * 9 + 4] = make_piece(0, K_KING)
            b[9 * 9 + 3] = make_piece(0, K_ADVISOR)
            b[9 * 9 + 5] = make_piece(0, K_ADVISOR)
            b[8 * 9 + 3] = make_piece(1, K_PAWN)
            b[7 * 9 + 4] = make_piece(1, K_PAWN)
        else:
            continue

        if kings_facing(b) or in_check(b, 0) or has_legal_move(b, 1):
            continue

        def unmake_red_move(bd):
            cands = []
            for idx, p in enumerate(bd):
                if side_of(p) == 0 and kind_of(p) in (K_ROOK, K_HORSE, K_CANNON, K_PAWN):
                    x, y = idx % 9, idx // 9
                    k = kind_of(p)
                    if k in (K_ROOK, K_CANNON):
                        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                            nx, ny = x + dx, y + dy
                            screens = 0
                            while 0 <= nx < 9 and 0 <= ny < 8:
                                if bd[ny * 9 + nx] == 0:
                                    if screens == 0:
                                        cands.append((idx, ny * 9 + nx, False))
                                    elif k == K_CANNON and screens == 1:
                                        cands.append((idx, ny * 9 + nx, True))
                                else:
                                    screens += 1
                                    if screens > 1 or k == K_ROOK:
                                        break
                                nx += dx
                                ny += dy
                    elif k == K_HORSE:
                        for lx, ly, dx, dy in (
                            (1, 0, 2, 1), (1, 0, 2, -1), (-1, 0, -2, 1), (-1, 0, -2, -1),
                            (0, 1, 1, 2), (0, 1, -1, 2), (0, -1, 1, -2), (0, -1, -1, -2),
                        ):
                            px, py = x - dx, y - dy
                            if 0 <= px < 9 and 0 <= py < 8 and bd[py * 9 + px] == 0:
                                if 0 <= px + lx < 9 and 0 <= py + ly < 10 and bd[(py + ly) * 9 + (px + lx)] == 0:
                                    cands.append((idx, py * 9 + px, False))
                    elif k == K_PAWN:
                        if y + 1 <= 5 and bd[(y + 1) * 9 + x] == 0 and (y + 1 <= 4 or x % 2 == 0):
                            cands.append((idx, (y + 1) * 9 + x, False))
                        if y <= 4:
                            for dx in (-1, 1):
                                if 0 <= x + dx < 9 and bd[y * 9 + x + dx] == 0:
                                    cands.append((idx, y * 9 + x + dx, False))
            if not cands:
                return False
            rng.shuffle(cands)
            for cur_sq, prev_sq, need_cap in cands:
                saved_cur = bd[cur_sq]
                bd[prev_sq] = saved_cur
                bd[cur_sq] = 0
                cx, cy = cur_sq % 9, cur_sq // 9
                placed_cap = False
                if need_cap or rng.random() < 0.3:
                    if (cx, cy) in BLK_ADV_PTS and sum(1 for v in bd if v == make_piece(1, K_ADVISOR)) < 2:
                        bd[cur_sq] = make_piece(1, K_ADVISOR)
                        placed_cap = True
                    elif (cx, cy) in BLK_ELE_PTS and sum(1 for v in bd if v == make_piece(1, K_ELEPHANT)) < 2:
                        bd[cur_sq] = make_piece(1, K_ELEPHANT)
                        placed_cap = True
                if need_cap and not placed_cap:
                    bd[cur_sq] = saved_cur
                    bd[prev_sq] = 0
                    continue
                if not kings_facing(bd) and not in_check(bd, 1) and not in_check(bd, 0):
                    return True
                bd[cur_sq] = saved_cur
                bd[prev_sq] = 0
            return False

        def unmake_black_move(bd):
            cands = []
            for idx, p in enumerate(bd):
                if side_of(p) == 1 and idx < 45:
                    x, y = idx % 9, idx // 9
                    k = kind_of(p)
                    if k == K_KING:
                        for nx, ny in BLK_PALACE:
                            if abs(nx - x) + abs(ny - y) == 1 and bd[ny * 9 + nx] == 0:
                                cands.append((idx, ny * 9 + nx))
                    elif k == K_ADVISOR:
                        for nx, ny in BLK_ADV_PTS:
                            if abs(nx - x) == 1 and abs(ny - y) == 1 and bd[ny * 9 + nx] == 0:
                                cands.append((idx, ny * 9 + nx))
                    elif k == K_ELEPHANT:
                        for nx, ny in BLK_ELE_PTS:
                            if abs(nx - x) == 2 and abs(ny - y) == 2 and bd[(y + ny) // 2 * 9 + (x + nx) // 2] == 0 and bd[ny * 9 + nx] == 0:
                                cands.append((idx, ny * 9 + nx))
            if not cands:
                return False
            rng.shuffle(cands)
            for cur_sq, prev_sq in cands:
                moved = bd[cur_sq]
                bd[prev_sq] = moved
                bd[cur_sq] = 0
                # Optional Red sacrifice on cur_sq (Black's forward move captured Red's checking sacrifice!)
                if rng.random() < 0.5:
                    for sac in rng.sample([K_ROOK, K_CANNON, K_HORSE, K_PAWN], 4):
                        if sac == K_PAWN and cur_sq // 9 > 4:
                            continue
                        if sum(1 for v in bd if v == make_piece(0, sac)) >= (5 if sac == K_PAWN else 2):
                            continue
                        bd[cur_sq] = make_piece(0, sac)
                        if not kings_facing(bd) and in_check(bd, 1) and not in_check(bd, 0):
                            return True
                        bd[cur_sq] = 0
                if not kings_facing(bd) and in_check(bd, 1) and not in_check(bd, 0):
                    return True
                bd[cur_sq] = moved
                bd[prev_sq] = 0
            return False

        want_max = max(target_moves)
        if not unmake_red_move(b):
            continue
        ok_chain = True
        for _ in range(want_max - 1):
            if not unmake_black_move(b) or not unmake_red_move(b):
                ok_chain = False
                break
        if not ok_chain or not position_valid(b) or in_check(b, 0):
            continue
        sig = bytes(b)
        if sig in seen:
            continue
        try:
            m, line = prove_forced_mate(b, want_max, node_budget=30000)
        except TimeoutError:
            continue
        if m not in target_moves or not line:
            continue
        try:
            m2, line2 = prove_forced_mate(b, m, node_budget=150000)
        except TimeoutError:
            continue
        if m2 != m or not line2:
            continue
        seen.add(sig)
        return b, m2, line2


def _check_glyphs(titles):
    allowed = set(json.loads((ROOT / "LICENSES/reading-font-manifest.json").read_text(encoding="utf-8"))["unicode_requested"])
    for t in titles:
        for ch in t:
            if ord(ch) not in allowed:
                raise ValueError(f"Character {ch!r} in title {t!r} missing from reading font")


def build_all(seed=20261118):
    _check_glyphs([t for t, _ in CLASSIC_TIER0] + TITLES_TIER1 + TITLES_TIER2)
    rng = random.Random(seed)
    seen = set()
    endgames = []
    mated_boards = []

    # Tier 0: 20 classic 1-2 move patterns
    tier0_items = []
    for title, plist in CLASSIC_TIER0:
        b = board_from_pieces(plist)
        if position_valid(b) and not in_check(b, 0):
            m, line = prove_forced_mate(b, 2, node_budget=100000)
        else:
            m, line = None, None
        assert m is not None and 1 <= m <= 2, f"Classic pattern {title} failed 1-2 move mate check (got {m})"
        sig = bytes(b)
        assert sig not in seen
        seen.add(sig)
        mb = list(b)
        for frm, to in line:
            mb[to] = mb[frm]
            mb[frm] = 0
        mated_boards.append(mb)
        tier0_items.append((m, len(line), title, b, line))

    tier0_items.sort(key=lambda x: (x[0], x[1]))
    for m, _, title, b, line in tier0_items:
        endgames.append({
            "id": len(endgames),
            "tier": 0,
            "title": title,
            "mate_moves": m,
            "max_moves": 16,
            "pieces": [[pos, p] for pos, p in enumerate(b) if p != 0],
            "proof": [[frm, to] for frm, to in line],
        })

    # Tier 1: 20 intermediate 2-3 move tactical sacrifices & combinations
    tier1_items = []
    for idx, title in enumerate(TITLES_TIER1):
        want = {2} if idx < 8 else {3}
        b, m, line = _retrograde_compose(rng, mated_boards, want, seen)
        tier1_items.append((m, len(line), title, b, line))
    tier1_items.sort(key=lambda x: (x[0], x[1]))
    for m, _, title, b, line in tier1_items:
        endgames.append({
            "id": len(endgames),
            "tier": 1,
            "title": title,
            "mate_moves": m,
            "max_moves": 20,
            "pieces": [[pos, p] for pos, p in enumerate(b) if p != 0],
            "proof": [[frm, to] for frm, to in line],
        })

    # Tier 2: 20 advanced 3-4 move deep combinations
    tier2_items = []
    for idx, title in enumerate(TITLES_TIER2):
        want = {3} if idx < 8 else {4}
        b, m, line = _retrograde_compose(rng, mated_boards, want, seen)
        tier2_items.append((m, len(line), title, b, line))
    tier2_items.sort(key=lambda x: (x[0], x[1]))
    for m, _, title, b, line in tier2_items:
        endgames.append({
            "id": len(endgames),
            "tier": 2,
            "title": title,
            "mate_moves": m,
            "max_moves": 24,
            "pieces": [[pos, p] for pos, p in enumerate(b) if p != 0],
            "proof": [[frm, to] for frm, to in line],
        })

    return endgames


def pack_bank(endgames):
    assert len(endgames) == 60
    records = bytearray()
    for i, e in enumerate(endgames):
        assert e["id"] == i and e["tier"] == i // 20
        pieces = e["pieces"]
        proof = e["proof"]
        assert 3 <= len(pieces) <= 16
        assert 1 <= len(proof) <= 15 and len(proof) % 2 == 1
        title_bytes = e["title"].encode("utf-8")
        assert 0 < len(title_bytes) < 24
        title_buf = title_bytes + b"\x00" * (24 - len(title_bytes))

        piece_buf = bytearray(32)
        for slot in range(16):
            if slot < len(pieces):
                piece_buf[slot * 2] = pieces[slot][0]
                piece_buf[slot * 2 + 1] = pieces[slot][1]
            else:
                piece_buf[slot * 2] = 0xFF
                piece_buf[slot * 2 + 1] = 0

        proof_buf = bytearray(32)
        for slot in range(16):
            if slot < len(proof):
                proof_buf[slot * 2] = proof[slot][0]
                proof_buf[slot * 2 + 1] = proof[slot][1]
            else:
                proof_buf[slot * 2] = 0
                proof_buf[slot * 2 + 1] = 0

        rec = struct.pack("<BBBBBBBB", i, e["tier"], len(pieces), len(proof), e["max_moves"], e["mate_moves"], 0, 0)
        rec += title_buf + bytes(piece_buf) + bytes(proof_buf)
        assert len(rec) == 96
        records.extend(rec)

    crc = zlib.crc32(records) & 0xFFFFFFFF
    header = b"PNXQ" + struct.pack("<HHII", 1, 96, 60, crc) + b"\x00" * 16
    assert len(header) == 32
    return header + bytes(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=20261118)
    parser.add_argument("--out", type=pathlib.Path, default=HERE)
    args = parser.parse_args()
    endgames = build_all(args.seed)
    blob = pack_bank(endgames)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "endgames.pnxq").write_bytes(blob)
    meta = {
        "version": 1,
        "seed": args.seed,
        "count": len(endgames),
        "bytes": len(blob),
        "endgames": endgames,
    }
    (args.out / "endgames.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(endgames)} Xiangqi endgames ({len(blob)} bytes) into {args.out}")


if __name__ == "__main__":
    main()
