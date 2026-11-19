"""独立离线校验 puzzles.pntg 全部60道死活题盘面、CRC与完整变化树合法性。
Independent offline verification of puzzles.pntg boards, CRC, and complete Go-rules variation trees."""
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parent
SIZE, CELLS = 9, 81
EMPTY, BLACK, WHITE = 0, 1, 2


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


def group_libs(board, start):
    color = board[start]
    stack, stones, libs = [start], {start}, set()
    while stack:
        cur = stack.pop()
        for nb in neighbors(cur):
            if board[nb] == EMPTY:
                libs.add(nb)
            elif board[nb] == color and nb not in stones:
                stones.add(nb)
                stack.append(nb)
    return stones, libs


def play(board, pos, color, ko_prev=None):
    if not (0 <= pos < CELLS) or board[pos] != EMPTY:
        return None
    nxt = list(board)
    nxt[pos] = color
    opp = WHITE if color == BLACK else BLACK
    cap = 0
    seen = set()
    for nb in neighbors(pos):
        if nxt[nb] == opp and nb not in seen:
            g, l = group_libs(nxt, nb)
            seen |= g
            if not l:
                for p in g:
                    nxt[p] = EMPTY
                cap += len(g)
    if not group_libs(nxt, pos)[1]:
        return None
    if ko_prev is not None and cap == 1 and nxt == ko_prev:
        return None
    return nxt


def verify_bank(path=ROOT / "puzzles.pntg"):
    raw = path.read_bytes()
    assert len(raw) >= 16
    magic, version, count, crc, reserved = struct.unpack_from("<4sHHII", raw, 0)
    assert magic == b"PNTG" and version == 1 and count == 60 and reserved == 0
    body = raw[16:]
    assert (zlib.crc32(body) & 0xFFFFFFFF) == crc
    dir_size = count * 56
    node_bytes = body[dir_size:]
    assert len(node_bytes) % 4 == 0
    total_nodes = len(node_bytes) // 4
    tier_counts = [0, 0, 0]
    seen_titles = set()

    for i in range(count):
        rec = body[i * 56:(i + 1) * 56]
        pid, tier, goal, edge_mask, noff, ncnt, main_moves = struct.unpack_from("<BBBBHBB", rec, 0)
        assert pid == i and tier in (0, 1, 2) and goal <= 4 and edge_mask != 0
        assert 2 <= ncnt <= 48 and noff + ncnt <= total_nodes and 1 <= main_moves <= 6
        title = rec[8:32].split(b"\x00", 1)[0].decode("utf-8")
        assert title and title not in seen_titles
        seen_titles.add(title)
        tier_counts[tier] += 1

        packed = rec[32:53]
        board = [(packed[k >> 2] >> ((k & 3) * 2)) & 3 for k in range(CELLS)]
        assert all(v <= 2 for v in board)
        # 初始无无气死子 / No zero-liberty initial stones
        checked = set()
        for k, v in enumerate(board):
            if v and k not in checked:
                g, l = group_libs(board, k)
                checked |= g
                assert l, f"Puzzle #{i} dead initial stone"

        nodes = [struct.unpack_from("<BBBB", node_bytes, (noff + k) * 4) for k in range(ncnt)]
        assert nodes[0][0] == 0xFF and nodes[0][1] == 0 and nodes[0][3] == 0xFF

        def dfs(idx, cur_board, prev_board):
            has_win = False
            for k in range(1, ncnt):
                bpos, flags, wpos, parent = nodes[k]
                if parent != idx:
                    continue
                after_b = play(cur_board, bpos, BLACK, prev_board)
                assert after_b is not None, f"Puzzle #{i} illegal black move {bpos}"
                if flags == 1:
                    assert wpos == 0xFF
                    has_win = True
                elif flags == 3:
                    if wpos != 0xFF:
                        assert play(after_b, wpos, WHITE, cur_board) is not None
                elif flags == 2:
                    after_w = play(after_b, wpos, WHITE, cur_board)
                    assert after_w is not None, f"Puzzle #{i} illegal white reply {wpos}"
                    assert dfs(k, after_w, after_b)
                    has_win = True
                else:
                    raise AssertionError(f"Bad node flag {flags}")
            return has_win

        assert dfs(0, board, None), f"Puzzle #{i} has no winning line"
    assert tier_counts == [20, 20, 20]
    print(f"Verified 60 Go Tsumego puzzles ({len(raw)} bytes, CRC=0x{crc:08x}, {total_nodes} nodes valid)")


if __name__ == "__main__":
    verify_bank()
