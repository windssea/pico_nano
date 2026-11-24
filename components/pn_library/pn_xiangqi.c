/* SPDX-License-Identifier: Apache-2.0
 * 中国象棋残局纯规则引擎、杀着证明/顽强应着搜索与A/B事务存档编解码。/ Pure Xiangqi endgame engine, mate/defense search and A/B save codec. */
#include "pn_xiangqi_internal.h"
#include "pn_crc32.h"
#include <stdio.h>
#include <string.h>

extern const uint8_t pn_xiangqi_bank_bytes[];
extern const size_t pn_xiangqi_bank_size;

enum {
    XQ_HEADER_BYTES = 32,
    XQ_RECORD_BYTES = 96
};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void wr64(uint8_t *p, uint64_t v) { wr32(p, (uint32_t)v); wr32(p + 4, (uint32_t)(v >> 32)); }

static int side_of(uint8_t p) { return p == 0 ? -1 : ((p & PN_XQ_BLACK) ? 1 : 0); }
static unsigned kind_of(uint8_t p) { return (unsigned)(p & 7u); }
static uint8_t make_piece(unsigned side, unsigned kind) { return (uint8_t)((side ? PN_XQ_BLACK : 0u) | kind); }

static bool in_palace(unsigned side, int x, int y) {
    if (x < 3 || x > 5) return false;
    return side ? (y >= 0 && y <= 2) : (y >= 7 && y <= 9);
}

static bool valid_advisor_sq(unsigned side, int x, int y) {
    if (!in_palace(side, x, y)) return false;
    int cy = side ? 1 : 8;
    return (x == 4 && y == cy) || ((x == 3 || x == 5) && (y == cy - 1 || y == cy + 1));
}

static bool valid_elephant_sq(unsigned side, int x, int y) {
    if (side) {
        return (y == 0 && (x == 2 || x == 6)) || (y == 2 && (x == 0 || x == 4 || x == 8)) ||
               (y == 4 && (x == 2 || x == 6));
    }
    return (y == 9 && (x == 2 || x == 6)) || (y == 7 && (x == 0 || x == 4 || x == 8)) ||
           (y == 5 && (x == 2 || x == 6));
}

static bool kings_facing(const uint8_t board[PN_XQ_CELLS]) {
    int rk = -1, bk = -1;
    for (int i = 0; i < PN_XQ_CELLS; i++) {
        if (board[i] == PN_XQ_KING) rk = i;
        else if (board[i] == (PN_XQ_BLACK | PN_XQ_KING)) bk = i;
    }
    if (rk < 0 || bk < 0 || (rk % 9) != (bk % 9)) return false;
    int col = rk % 9;
    for (int y = bk / 9 + 1; y < rk / 9; y++) {
        if (board[y * 9 + col] != 0) return false;
    }
    return true;
}

static bool attacks_square(const uint8_t board[PN_XQ_CELLS], unsigned attacker_side, int tx, int ty) {
    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    uint8_t opp_king = make_piece(1u - attacker_side, PN_XQ_KING);
    int target_idx = ty * 9 + tx;
    for (unsigned d = 0; d < 4; d++) {
        int x = tx + dirs[d][0], y = ty + dirs[d][1], screens = 0;
        while (x >= 0 && x < 9 && y >= 0 && y < 10) {
            uint8_t p = board[y * 9 + x];
            if (p != 0) {
                if ((unsigned)side_of(p) == attacker_side) {
                    unsigned k = kind_of(p);
                    if (screens == 0 && k == PN_XQ_ROOK) return true;
                    if (screens == 0 && k == PN_XQ_KING && board[target_idx] == opp_king) return true;
                    if (screens == 1 && k == PN_XQ_CANNON) return true;
                }
                screens++;
                if (screens >= 2) break;
            }
            x += dirs[d][0];
            y += dirs[d][1];
        }
    }
    if (in_palace(attacker_side, tx, ty)) {
        uint8_t atk_king = make_piece(attacker_side, PN_XQ_KING);
        for (unsigned d = 0; d < 4; d++) {
            int nx = tx + dirs[d][0], ny = ty + dirs[d][1];
            if (in_palace(attacker_side, nx, ny) && board[ny * 9 + nx] == atk_king) return true;
        }
        uint8_t atk_adv = make_piece(attacker_side, PN_XQ_ADVISOR);
        static const int diags[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
        for (unsigned d = 0; d < 4; d++) {
            int nx = tx + diags[d][0], ny = ty + diags[d][1];
            if (in_palace(attacker_side, nx, ny) && board[ny * 9 + nx] == atk_adv) return true;
        }
    }
    static const int h_offs[8][2] = {{-2, -1}, {-2, 1}, {2, -1}, {2, 1}, {-1, -2}, {1, -2}, {-1, 2}, {1, 2}};
    uint8_t atk_horse = make_piece(attacker_side, PN_XQ_HORSE);
    for (unsigned i = 0; i < 8; i++) {
        int hx = tx + h_offs[i][0], hy = ty + h_offs[i][1];
        if (hx >= 0 && hx < 9 && hy >= 0 && hy < 10 && board[hy * 9 + hx] == atk_horse) {
            int lx = hx + ((h_offs[i][0] > 1) ? -1 : (h_offs[i][0] < -1 ? 1 : 0));
            int ly = hy + ((h_offs[i][1] > 1) ? -1 : (h_offs[i][1] < -1 ? 1 : 0));
            if (board[ly * 9 + lx] == 0) return true;
        }
    }
    uint8_t atk_pawn = make_piece(attacker_side, PN_XQ_PAWN);
    if (attacker_side == 0) {
        if (ty + 1 <= 6 && board[(ty + 1) * 9 + tx] == atk_pawn) return true;
        if (ty <= 4) {
            if (tx > 0 && board[ty * 9 + tx - 1] == atk_pawn) return true;
            if (tx < 8 && board[ty * 9 + tx + 1] == atk_pawn) return true;
        }
    } else {
        if (ty - 1 >= 3 && board[(ty - 1) * 9 + tx] == atk_pawn) return true;
        if (ty >= 5) {
            if (tx > 0 && board[ty * 9 + tx - 1] == atk_pawn) return true;
            if (tx < 8 && board[ty * 9 + tx + 1] == atk_pawn) return true;
        }
    }
    if (valid_elephant_sq(attacker_side, tx, ty)) {
        uint8_t atk_ele = make_piece(attacker_side, PN_XQ_ELEPHANT);
        static const int ediags[4][2] = {{-2, -2}, {-2, 2}, {2, -2}, {2, 2}};
        for (unsigned d = 0; d < 4; d++) {
            int nx = tx + ediags[d][0], ny = ty + ediags[d][1];
            if (valid_elephant_sq(attacker_side, nx, ny) && board[ny * 9 + nx] == atk_ele) {
                if (board[(ty + ediags[d][1] / 2) * 9 + (tx + ediags[d][0] / 2)] == 0) return true;
            }
        }
    }
    return false;
}

bool pn_xiangqi_in_check(const uint8_t board[PN_XQ_CELLS], unsigned side) {
    if (!board || side > 1u) return false;
    uint8_t king = make_piece(side, PN_XQ_KING);
    for (int i = 0; i < PN_XQ_CELLS; i++) {
        if (board[i] == king) return attacks_square(board, 1u - side, i % 9, i / 9);
    }
    return true;
}

static bool pseudo_legal(const uint8_t board[PN_XQ_CELLS], unsigned side, unsigned from, unsigned to) {
    if (from >= PN_XQ_CELLS || to >= PN_XQ_CELLS || from == to) return false;
    uint8_t p = board[from], q = board[to];
    if ((int)side != side_of(p) || (int)side == side_of(q)) return false;
    int fx = (int)(from % 9u), fy = (int)(from / 9u);
    int tx = (int)(to % 9u), ty = (int)(to / 9u);
    int dx = tx - fx, dy = ty - fy;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    switch (kind_of(p)) {
    case PN_XQ_KING:
        return in_palace(side, tx, ty) && (adx + ady == 1);
    case PN_XQ_ADVISOR:
        return in_palace(side, tx, ty) && adx == 1 && ady == 1;
    case PN_XQ_ELEPHANT:
        if (adx != 2 || ady != 2) return false;
        if (side ? (ty > 4) : (ty < 5)) return false;
        return board[(fy + dy / 2) * 9 + (fx + dx / 2)] == 0;
    case PN_XQ_HORSE:
        if (adx == 2 && ady == 1) return board[fy * 9 + fx + dx / 2] == 0;
        if (adx == 1 && ady == 2) return board[(fy + dy / 2) * 9 + fx] == 0;
        return false;
    case PN_XQ_ROOK:
    case PN_XQ_CANNON: {
        if (dx != 0 && dy != 0) return false;
        int sx = dx == 0 ? 0 : (dx > 0 ? 1 : -1);
        int sy = dy == 0 ? 0 : (dy > 0 ? 1 : -1);
        int screens = 0;
        for (int x = fx + sx, y = fy + sy; x != tx || y != ty; x += sx, y += sy) {
            if (board[y * 9 + x] != 0) screens++;
        }
        if (kind_of(p) == PN_XQ_ROOK) return screens == 0;
        return q == 0 ? (screens == 0) : (screens == 1);
    }
    case PN_XQ_PAWN: {
        int fwd = side ? 1 : -1;
        if (dy == fwd && dx == 0) return true;
        bool crossed = side ? (fy >= 5) : (fy <= 4);
        return crossed && dy == 0 && adx == 1;
    }
    default:
        return false;
    }
}

bool pn_xiangqi_legal_move(const uint8_t board[PN_XQ_CELLS], unsigned side, unsigned from, unsigned to) {
    if (!board || side > 1u || !pseudo_legal(board, side, from, to)) return false;
    uint8_t tmp[PN_XQ_CELLS];
    memcpy(tmp, board, PN_XQ_CELLS);
    tmp[to] = tmp[from];
    tmp[from] = 0;
    return !kings_facing(tmp) && !pn_xiangqi_in_check(tmp, side);
}

static unsigned enum_moves(uint8_t board[PN_XQ_CELLS], unsigned side, uint8_t out_pairs[256], bool stop_first) {
    unsigned n = 0;
    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    static const int diags[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
    static const int horse[8][4] = {
        {1, 0, 2, 1}, {1, 0, 2, -1}, {-1, 0, -2, 1}, {-1, 0, -2, -1},
        {0, 1, 1, 2}, {0, 1, -1, 2}, {0, -1, 1, -2}, {0, -1, -1, -2}
    };
    for (unsigned idx = 0; idx < PN_XQ_CELLS; idx++) {
        uint8_t p = board[idx];
        if (side_of(p) != (int)side) continue;
        int x = (int)(idx % 9u), y = (int)(idx / 9u);
        unsigned k = kind_of(p);
        uint8_t cands[18];
        unsigned cn = 0;
        if (k == PN_XQ_KING) {
            for (unsigned d = 0; d < 4; d++) {
                int nx = x + dirs[d][0], ny = y + dirs[d][1];
                if (in_palace(side, nx, ny) && side_of(board[ny * 9 + nx]) != (int)side) cands[cn++] = (uint8_t)(ny * 9 + nx);
            }
        } else if (k == PN_XQ_ADVISOR) {
            for (unsigned d = 0; d < 4; d++) {
                int nx = x + diags[d][0], ny = y + diags[d][1];
                if (in_palace(side, nx, ny) && side_of(board[ny * 9 + nx]) != (int)side) cands[cn++] = (uint8_t)(ny * 9 + nx);
            }
        } else if (k == PN_XQ_ELEPHANT) {
            for (unsigned d = 0; d < 4; d++) {
                int nx = x + diags[d][0] * 2, ny = y + diags[d][1] * 2;
                if (nx >= 0 && nx < 9 && (side ? (ny >= 0 && ny <= 4) : (ny >= 5 && ny <= 9))) {
                    if (board[(y + diags[d][1]) * 9 + (x + diags[d][0])] == 0 && side_of(board[ny * 9 + nx]) != (int)side) {
                        cands[cn++] = (uint8_t)(ny * 9 + nx);
                    }
                }
            }
        } else if (k == PN_XQ_HORSE) {
            for (unsigned d = 0; d < 8; d++) {
                int lx = x + horse[d][0], ly = y + horse[d][1];
                if (lx >= 0 && lx < 9 && ly >= 0 && ly < 10 && board[ly * 9 + lx] == 0) {
                    int nx = x + horse[d][2], ny = y + horse[d][3];
                    if (nx >= 0 && nx < 9 && ny >= 0 && ny < 10 && side_of(board[ny * 9 + nx]) != (int)side) {
                        cands[cn++] = (uint8_t)(ny * 9 + nx);
                    }
                }
            }
        } else if (k == PN_XQ_ROOK) {
            for (unsigned d = 0; d < 4; d++) {
                int nx = x + dirs[d][0], ny = y + dirs[d][1];
                while (nx >= 0 && nx < 9 && ny >= 0 && ny < 10) {
                    uint8_t q = board[ny * 9 + nx];
                    if (q == 0) cands[cn++] = (uint8_t)(ny * 9 + nx);
                    else {
                        if (side_of(q) != (int)side) cands[cn++] = (uint8_t)(ny * 9 + nx);
                        break;
                    }
                    nx += dirs[d][0];
                    ny += dirs[d][1];
                }
            }
        } else if (k == PN_XQ_CANNON) {
            for (unsigned d = 0; d < 4; d++) {
                int nx = x + dirs[d][0], ny = y + dirs[d][1];
                bool screen = false;
                while (nx >= 0 && nx < 9 && ny >= 0 && ny < 10) {
                    uint8_t q = board[ny * 9 + nx];
                    if (!screen) {
                        if (q == 0) cands[cn++] = (uint8_t)(ny * 9 + nx);
                        else screen = true;
                    } else if (q != 0) {
                        if (side_of(q) != (int)side) cands[cn++] = (uint8_t)(ny * 9 + nx);
                        break;
                    }
                    nx += dirs[d][0];
                    ny += dirs[d][1];
                }
            }
        } else if (k == PN_XQ_PAWN) {
            int ny = y + (side ? 1 : -1);
            if (ny >= 0 && ny < 10 && side_of(board[ny * 9 + x]) != (int)side) cands[cn++] = (uint8_t)(ny * 9 + x);
            if (side ? (y >= 5) : (y <= 4)) {
                if (x > 0 && side_of(board[y * 9 + x - 1]) != (int)side) cands[cn++] = (uint8_t)(y * 9 + x - 1);
                if (x < 8 && side_of(board[y * 9 + x + 1]) != (int)side) cands[cn++] = (uint8_t)(y * 9 + x + 1);
            }
        }
        for (unsigned c = 0; c < cn; c++) {
            uint8_t to = cands[c], cap = board[to];
            board[to] = p;
            board[idx] = 0;
            bool ok = !kings_facing(board) && !pn_xiangqi_in_check(board, side);
            board[idx] = p;
            board[to] = cap;
            if (ok) {
                if (out_pairs && n < 128u) {
                    out_pairs[n * 2u] = (uint8_t)idx;
                    out_pairs[n * 2u + 1u] = to;
                }
                n++;
                if (stop_first) return 1u;
            }
        }
    }
    return n < 128u ? n : 128u;
}

unsigned pn_xiangqi_legal_moves(const uint8_t board[PN_XQ_CELLS], unsigned side, uint8_t out_pairs[256]) {
    if (!board || side > 1u) return 0;
    uint8_t tmp[PN_XQ_CELLS];
    memcpy(tmp, board, PN_XQ_CELLS);
    return enum_moves(tmp, side, out_pairs, false);
}

static bool has_legal_move(uint8_t board[PN_XQ_CELLS], unsigned side) {
    return enum_moves(board, side, NULL, true) > 0;
}

bool pn_xq_position_valid(const uint8_t board[PN_XQ_CELLS]) {
    if (!board) return false;
    unsigned counts[2][8] = {{0}};
    unsigned total = 0;
    for (int i = 0; i < PN_XQ_CELLS; i++) {
        uint8_t p = board[i];
        if (p == 0) continue;
        unsigned s = (unsigned)side_of(p), k = kind_of(p);
        if (k < PN_XQ_KING || k > PN_XQ_PAWN || (p & ~0xFu) != 0) return false;
        counts[s][k]++;
        total++;
        int x = i % 9, y = i / 9;
        if (k == PN_XQ_KING && !in_palace(s, x, y)) return false;
        if (k == PN_XQ_ADVISOR && !valid_advisor_sq(s, x, y)) return false;
        if (k == PN_XQ_ELEPHANT && !valid_elephant_sq(s, x, y)) return false;
        if (k == PN_XQ_PAWN) {
            if (s == 0 && (y > 6 || ((y == 5 || y == 6) && (x % 2) != 0))) return false;
            if (s == 1 && (y < 3 || ((y == 3 || y == 4) && (x % 2) != 0))) return false;
        }
    }
    if (total < 3u || total > 16u) return false;
    for (unsigned s = 0; s < 2u; s++) {
        if (counts[s][PN_XQ_KING] != 1u) return false;
        if (counts[s][PN_XQ_ADVISOR] > 2u || counts[s][PN_XQ_ELEPHANT] > 2u ||
            counts[s][PN_XQ_HORSE] > 2u || counts[s][PN_XQ_ROOK] > 2u || counts[s][PN_XQ_CANNON] > 2u ||
            counts[s][PN_XQ_PAWN] > 5u) return false;
    }
    uint8_t tmp[PN_XQ_CELLS];
    memcpy(tmp, board, PN_XQ_CELLS);
    return !kings_facing(tmp) && !pn_xiangqi_in_check(tmp, 1) && has_legal_move(tmp, 0);
}

static pn_status_t check_bank(uint32_t *out_crc) {
    static bool bank_ok = false;
    static uint32_t cached_crc = 0;
    if (bank_ok) {
        if (out_crc) *out_crc = cached_crc;
        return PN_OK;
    }
    if (pn_xiangqi_bank_size != XQ_HEADER_BYTES + (size_t)PN_XQ_ENDGAMES * XQ_RECORD_BYTES)
        return PN_CORRUPT;
    if (memcmp(pn_xiangqi_bank_bytes, "PNXQ", 4) != 0) return PN_CORRUPT;
    if (rd16(pn_xiangqi_bank_bytes + 4) != 1u || rd16(pn_xiangqi_bank_bytes + 6) != XQ_RECORD_BYTES) return PN_CORRUPT;
    if (rd32(pn_xiangqi_bank_bytes + 8) != PN_XQ_ENDGAMES) return PN_CORRUPT;
    for (unsigned i = 16; i < XQ_HEADER_BYTES; i++) {
        if (pn_xiangqi_bank_bytes[i] != 0) return PN_CORRUPT;
    }
    uint32_t expect = rd32(pn_xiangqi_bank_bytes + 12);
    uint32_t actual = pn_crc32_update(0u, pn_xiangqi_bank_bytes + XQ_HEADER_BYTES, (size_t)PN_XQ_ENDGAMES * XQ_RECORD_BYTES);
    if (expect != actual) return PN_CORRUPT;
    bank_ok = true;
    cached_crc = actual;
    if (out_crc) *out_crc = actual;
    return PN_OK;
}

uint32_t pn_xq_bank_crc(void) {
    uint32_t crc = 0;
    return check_bank(&crc) == PN_OK ? crc : 0;
}

static bool valid_title_bytes(const uint8_t raw[24], char out[24]) {
    unsigned len = 0;
    while (len < 24u && raw[len] != 0) len++;
    if (len == 0 || len >= 24u) return false;
    for (unsigned i = len; i < 24u; i++) {
        if (raw[i] != 0) return false;
    }
    memcpy(out, raw, 24);
    return true;
}

pn_status_t pn_xiangqi_level(unsigned id, pn_xq_level_t *out) {
    if (!out || id >= PN_XQ_ENDGAMES) return PN_INVALID;
    pn_status_t st = check_bank(NULL);
    if (st != PN_OK) return st;

    const uint8_t *rec = pn_xiangqi_bank_bytes + XQ_HEADER_BYTES + id * XQ_RECORD_BYTES;
    pn_xq_level_t lv = {0};
    lv.id = rec[0];
    lv.tier = rec[1];
    lv.piece_count = rec[2];
    lv.proof_plies = rec[3];
    lv.max_moves = rec[4];
    lv.mate_moves = rec[5];
    if (lv.id != id || lv.tier != id / 20u || rec[6] != 0 || rec[7] != 0) return PN_CORRUPT;
    if (lv.piece_count < 3u || lv.piece_count > 16u) return PN_CORRUPT;
    if (lv.mate_moves < 1u || lv.mate_moves > 8u || lv.proof_plies != 2u * lv.mate_moves - 1u || lv.proof_plies > 15u)
        return PN_CORRUPT;
    if (lv.max_moves < lv.mate_moves || lv.max_moves > PN_XQ_UNDO) return PN_CORRUPT;
    if (!valid_title_bytes(rec + 8, lv.title)) return PN_CORRUPT;

    int prev_pos = -1;
    for (unsigned slot = 0; slot < 16u; slot++) {
        uint8_t pos = rec[32u + slot * 2u], code = rec[32u + slot * 2u + 1u];
        if (slot < lv.piece_count) {
            if ((int)pos <= prev_pos || pos >= PN_XQ_CELLS || code == 0) return PN_CORRUPT;
            prev_pos = (int)pos;
            lv.board[pos] = code;
        } else if (pos != PN_XQ_NONE_SQ || code != 0) {
            return PN_CORRUPT;
        }
    }
    if (!pn_xq_position_valid(lv.board) || pn_xiangqi_in_check(lv.board, 0)) return PN_CORRUPT;

    memcpy(lv.proof, rec + 64, 32);
    uint8_t sim[PN_XQ_CELLS];
    memcpy(sim, lv.board, PN_XQ_CELLS);
    for (unsigned ply = 0; ply < 16u; ply++) {
        uint8_t from = lv.proof[ply * 2u], to = lv.proof[ply * 2u + 1u];
        if (ply < lv.proof_plies) {
            if (!pn_xiangqi_legal_move(sim, ply & 1u, from, to)) return PN_CORRUPT;
            sim[to] = sim[from];
            sim[from] = 0;
        } else if (from != 0 || to != 0) {
            return PN_CORRUPT;
        }
    }
    if (has_legal_move(sim, 1)) return PN_CORRUPT;
    *out = lv;
    return PN_OK;
}

typedef struct {
    uint8_t board[PN_XQ_CELLS];
    uint8_t killer[8][2];
    uint8_t mv_buf[8][256];
    uint8_t sc_buf[8][128];
    uint32_t nodes, budget;
    bool aborted;
} xq_search_t;

static unsigned order_moves(xq_search_t *ctx, unsigned side, unsigned depth, unsigned n) {
    uint8_t *pairs = ctx->mv_buf[depth];
    uint8_t *scores = ctx->sc_buf[depth];
    if (n > 128u) n = 128u;
    uint8_t kf = ctx->killer[depth][0], kt = ctx->killer[depth][1];
    for (unsigned i = 0; i < n; i++) {
        uint8_t f = pairs[i * 2u], t = pairs[i * 2u + 1u];
        uint8_t cap = ctx->board[t], p = ctx->board[f];
        ctx->board[t] = p;
        ctx->board[f] = 0;
        bool chk = pn_xiangqi_in_check(ctx->board, 1u - side);
        ctx->board[f] = p;
        ctx->board[t] = cap;
        uint8_t sc = 0;
        if (f == kf && t == kt) sc += 64u;
        if (chk) sc += 32u;
        if (cap != 0) sc += (uint8_t)(8u + kind_of(cap));
        scores[i] = sc;
    }
    for (unsigned i = 1; i < n; i++) {
        uint8_t sf = pairs[i * 2u], st = pairs[i * 2u + 1u], ssc = scores[i];
        unsigned j = i;
        while (j > 0 && scores[j - 1u] < ssc) {
            pairs[j * 2u] = pairs[(j - 1u) * 2u];
            pairs[j * 2u + 1u] = pairs[(j - 1u) * 2u + 1u];
            scores[j] = scores[j - 1u];
            j--;
        }
        pairs[j * 2u] = sf;
        pairs[j * 2u + 1u] = st;
        scores[j] = ssc;
    }
    return n;
}

static bool can_force_mate(xq_search_t *ctx, unsigned side, unsigned depth, unsigned plies_left, uint8_t *out_f, uint8_t *out_t) {
    if (++ctx->nodes > ctx->budget) {
        ctx->aborted = true;
        return false;
    }
    if (plies_left == 0) return !has_legal_move(ctx->board, side);
    if (depth >= 8u) return false;

    unsigned n = enum_moves(ctx->board, side, ctx->mv_buf[depth], false);
    if (n == 0) return side == 1u;
    n = order_moves(ctx, side, depth, n);

    if (side == 0) {
        for (unsigned i = 0; i < n; i++) {
            uint8_t f = ctx->mv_buf[depth][i * 2u], t = ctx->mv_buf[depth][i * 2u + 1u];
            uint8_t p = ctx->board[f], cap = ctx->board[t];
            ctx->board[t] = p;
            ctx->board[f] = 0;
            bool ok = can_force_mate(ctx, 1u, depth + 1u, plies_left - 1u, NULL, NULL);
            ctx->board[f] = p;
            ctx->board[t] = cap;
            if (ctx->aborted) return false;
            if (ok) {
                ctx->killer[depth][0] = f;
                ctx->killer[depth][1] = t;
                if (out_f) *out_f = f;
                if (out_t) *out_t = t;
                return true;
            }
        }
        return false;
    }
    for (unsigned i = 0; i < n; i++) {
        uint8_t f = ctx->mv_buf[depth][i * 2u], t = ctx->mv_buf[depth][i * 2u + 1u];
        uint8_t p = ctx->board[f], cap = ctx->board[t];
        ctx->board[t] = p;
        ctx->board[f] = 0;
        bool ok = can_force_mate(ctx, 0u, depth + 1u, plies_left - 1u, NULL, NULL);
        ctx->board[f] = p;
        ctx->board[t] = cap;
        if (ctx->aborted) return false;
        if (!ok) {
            ctx->killer[depth][0] = f;
            ctx->killer[depth][1] = t;
            return false;
        }
    }
    return true;
}

static int piece_weight(uint8_t p) {
    static const int w[8] = {0, 1000, 20, 20, 45, 90, 50, 15};
    return w[kind_of(p)];
}

static bool match_proof_board(const pn_xq_level_t *lv, const uint8_t target[PN_XQ_CELLS], unsigned want_side, uint8_t *out_f, uint8_t *out_t) {
    uint8_t sim[PN_XQ_CELLS];
    memcpy(sim, lv->board, PN_XQ_CELLS);
    for (unsigned ply = 0; ply < lv->proof_plies; ply++) {
        if ((ply & 1u) == want_side && memcmp(sim, target, PN_XQ_CELLS) == 0) {
            *out_f = lv->proof[ply * 2u];
            *out_t = lv->proof[ply * 2u + 1u];
            return pn_xiangqi_legal_move(target, want_side, *out_f, *out_t);
        }
        uint8_t f = lv->proof[ply * 2u], t = lv->proof[ply * 2u + 1u];
        if (!pn_xiangqi_legal_move(sim, ply & 1u, f, t)) break;
        sim[t] = sim[f];
        sim[f] = 0;
    }
    return false;
}

bool pn_xiangqi_ai_reply(const pn_xq_state_t *s, uint8_t *out_from, uint8_t *out_to) {
    if (!s || !out_from || !out_to) return false;
    uint8_t pairs[256];
    unsigned n = pn_xiangqi_legal_moves(s->board, 1u, pairs);
    if (n == 0) return false;
    if (n == 1u) {
        *out_from = pairs[0];
        *out_to = pairs[1];
        return true;
    }

    uint8_t pf = PN_XQ_NONE_SQ, pt = PN_XQ_NONE_SQ;
    bool has_proof = match_proof_board(&s->level, s->board, 1u, &pf, &pt);

    xq_search_t ctx;
    memset(&ctx, 0, sizeof ctx);
    int best_score = -100000;
    uint8_t best_f = pairs[0], best_t = pairs[1];

    for (unsigned i = 0; i < n; i++) {
        uint8_t f = pairs[i * 2u], t = pairs[i * 2u + 1u];
        memcpy(ctx.board, s->board, PN_XQ_CELLS);
        uint8_t cap = ctx.board[t];
        ctx.board[t] = ctx.board[f];
        ctx.board[f] = 0;

        /* 若黑方能一步反杀/困毙红方，立即执行绝杀。/ Execute immediate counter-mate/stalemate if Red blundered. */
        if (!has_legal_move(ctx.board, 0u)) {
            *out_from = f;
            *out_to = t;
            return true;
        }

        int score = 0;
        ctx.nodes = 0;
        ctx.budget = 2000u;
        ctx.aborted = false;
        if (can_force_mate(&ctx, 0u, 0u, 1u, NULL, NULL)) {
            score = 100;
        } else {
            ctx.nodes = 0;
            ctx.budget = 8000u;
            ctx.aborted = false;
            if (can_force_mate(&ctx, 0u, 0u, 3u, NULL, NULL)) {
                score = 200;
            } else {
                ctx.nodes = 0;
                ctx.budget = 14000u;
                ctx.aborted = false;
                if (!ctx.aborted && can_force_mate(&ctx, 0u, 0u, 5u, NULL, NULL)) {
                    score = 300;
                } else {
                    score = 1000 + piece_weight(cap) * 4 + (pn_xiangqi_in_check(ctx.board, 0u) ? 80 : 0);
                }
            }
        }
        if (has_proof && f == pf && t == pt) score += 2;
        if (score > best_score) {
            best_score = score;
            best_f = f;
            best_t = t;
        }
    }
    *out_from = best_f;
    *out_to = best_t;
    return true;
}

bool pn_xiangqi_best_red_move(const pn_xq_state_t *s, uint8_t *out_from, uint8_t *out_to) {
    if (!s || !out_from || !out_to) return false;
    uint8_t pf = PN_XQ_NONE_SQ, pt = PN_XQ_NONE_SQ;
    if (match_proof_board(&s->level, s->board, 0u, &pf, &pt)) {
        *out_from = pf;
        *out_to = pt;
        return true;
    }

    uint8_t pairs[256];
    unsigned n = pn_xiangqi_legal_moves(s->board, 0u, pairs);
    if (n == 0) return false;

    unsigned rem = s->moves < s->level.max_moves ? (unsigned)(s->level.max_moves - s->moves) : 1u;
    if (rem > 4u) rem = 4u;
    xq_search_t ctx;
    for (unsigned m = 1u; m <= rem; m++) {
        memset(&ctx, 0, sizeof ctx);
        memcpy(ctx.board, s->board, PN_XQ_CELLS);
        ctx.budget = m <= 2u ? 12000u : 36000u;
        uint8_t bf = PN_XQ_NONE_SQ, bt = PN_XQ_NONE_SQ;
        if (can_force_mate(&ctx, 0u, 0u, 2u * m - 1u, &bf, &bt)) {
            *out_from = bf;
            *out_to = bt;
            return true;
        }
    }

    /* 偏离正着丧失强制杀时，给出当前最具威胁的将军或吃子合法着。/ Fall back to strongest checking/capturing move when no forced mate remains. */
    int best = -10000;
    uint8_t best_f = pairs[0], best_t = pairs[1];
    uint8_t tmp[PN_XQ_CELLS];
    for (unsigned i = 0; i < n; i++) {
        uint8_t f = pairs[i * 2u], t = pairs[i * 2u + 1u];
        memcpy(tmp, s->board, PN_XQ_CELLS);
        uint8_t cap = tmp[t];
        tmp[t] = tmp[f];
        tmp[f] = 0;
        int sc = piece_weight(cap) * 4 + (pn_xiangqi_in_check(tmp, 1u) ? 100 : 0);
        if (sc > best) {
            best = sc;
            best_f = f;
            best_t = t;
        }
    }
    *out_from = best_f;
    *out_to = best_t;
    return true;
}

void pn_xiangqi_move_text(const uint8_t board[PN_XQ_CELLS], unsigned from, unsigned to, char out[24]) {
    if (!out) return;
    out[0] = '\0';
    if (!board || from >= PN_XQ_CELLS || to >= PN_XQ_CELLS) return;
    uint8_t p = board[from];
    if (p == 0) return;
    static const char *const red_names[8] = {"", "帅", "仕", "相", "马", "车", "炮", "兵"};
    static const char *const blk_names[8] = {"", "将", "士", "象", "马", "车", "炮", "卒"};
    static const char *const cn_num[10] = {"", "一", "二", "三", "四", "五", "六", "七", "八", "九"};

    unsigned side = (unsigned)side_of(p), k = kind_of(p);
    int fx = (int)(from % 9u), fy = (int)(from / 9u);
    int tx = (int)(to % 9u), ty = (int)(to / 9u);
    bool diag_piece = (k == PN_XQ_ADVISOR || k == PN_XQ_ELEPHANT || k == PN_XQ_HORSE);

    const char *act = "平";
    if (ty != fy) {
        bool forward = side ? (ty > fy) : (ty < fy);
        act = forward ? "进" : "退";
    }
    if (side == 0) {
        int sfile = 9 - fx;
        int targ = (ty == fy || diag_piece) ? (9 - tx) : (ty < fy ? (fy - ty) : (ty - fy));
        if (sfile >= 1 && sfile <= 9 && targ >= 1 && targ <= 9)
            snprintf(out, 24, "%s%s%s%s", red_names[k], cn_num[sfile], act, cn_num[targ]);
    } else {
        int sfile = fx + 1;
        int targ = (ty == fy || diag_piece) ? (tx + 1) : (ty > fy ? (ty - fy) : (fy - ty));
        if (sfile >= 1 && sfile <= 9 && targ >= 1 && targ <= 9)
            snprintf(out, 24, "%s%d%s%d", blk_names[k], sfile, act, targ);
    }
}

void pn_xq_reset_board(pn_xq_state_t *s, const pn_xq_level_t *lv, uint32_t bank_crc) {
    uint8_t completed[8];
    memcpy(completed, s->completed, sizeof completed);
    memset(s, 0, sizeof *s);
    s->level = *lv;
    memcpy(s->board, lv->board, PN_XQ_CELLS);
    memcpy(s->completed, completed, sizeof completed);
    s->bank_crc = bank_crc;
    s->selected = PN_XQ_NONE_SQ;
    s->last_from = PN_XQ_NONE_SQ;
    s->last_to = PN_XQ_NONE_SQ;
    s->hint_from = PN_XQ_NONE_SQ;
    s->hint_to = PN_XQ_NONE_SQ;
    s->cursor = 49u;
    for (unsigned i = 0; i < PN_XQ_CELLS; i++) {
        if (side_of(s->board[i]) == 0 && kind_of(s->board[i]) != PN_XQ_KING) {
            s->cursor = (uint8_t)i;
            break;
        }
    }
}

pn_status_t pn_xiangqi_step(pn_xq_state_t *s, unsigned from, unsigned to) {
    if (!s || s->outcome != PN_XQ_ONGOING || s->undo_count >= PN_XQ_UNDO) return PN_INVALID;
    if (!pn_xiangqi_legal_move(s->board, 0u, from, to)) return PN_INVALID;

    pn_xq_undo_t u = {0};
    u.red_from = (uint8_t)from;
    u.red_to = (uint8_t)to;
    u.red_cap = s->board[to];
    u.blk_from = PN_XQ_NONE_SQ;
    u.blk_to = PN_XQ_NONE_SQ;

    s->board[to] = s->board[from];
    s->board[from] = 0;
    s->moves++;
    s->selected = PN_XQ_NONE_SQ;
    s->cursor = (uint8_t)to;
    s->hint_from = PN_XQ_NONE_SQ;
    s->hint_to = PN_XQ_NONE_SQ;
    s->last_from = (uint8_t)from;
    s->last_to = (uint8_t)to;

    uint8_t tmp[PN_XQ_CELLS];
    memcpy(tmp, s->board, PN_XQ_CELLS);
    if (!has_legal_move(tmp, 1u)) {
        s->outcome = PN_XQ_WON;
        if (s->level.id < PN_XQ_ENDGAMES)
            s->completed[s->level.id >> 3] |= (uint8_t)(1u << (s->level.id & 7u));
    } else if (s->moves >= s->level.max_moves) {
        s->outcome = PN_XQ_LOST;
    } else {
        uint8_t bf = PN_XQ_NONE_SQ, bt = PN_XQ_NONE_SQ;
        if (pn_xiangqi_ai_reply(s, &bf, &bt) && pn_xiangqi_legal_move(s->board, 1u, bf, bt)) {
            u.blk_from = bf;
            u.blk_to = bt;
            u.blk_cap = s->board[bt];
            s->board[bt] = s->board[bf];
            s->board[bf] = 0;
            s->last_from = bf;
            s->last_to = bt;
            memcpy(tmp, s->board, PN_XQ_CELLS);
            if (!has_legal_move(tmp, 0u)) s->outcome = PN_XQ_LOST;
        } else {
            s->outcome = PN_XQ_WON;
            if (s->level.id < PN_XQ_ENDGAMES)
                s->completed[s->level.id >> 3] |= (uint8_t)(1u << (s->level.id & 7u));
        }
    }

    s->undo[s->undo_count++] = u;
    return PN_OK;
}

void pn_xq_compute_legal_dests(const pn_xq_state_t *s, uint8_t out_dest[PN_XQ_CELLS]) {
    memset(out_dest, 0, PN_XQ_CELLS);
    if (!s || s->outcome != PN_XQ_ONGOING || s->selected >= PN_XQ_CELLS || side_of(s->board[s->selected]) != 0)
        return;
    for (unsigned to = 0; to < PN_XQ_CELLS; to++) {
        if (pn_xiangqi_legal_move(s->board, 0u, s->selected, to)) out_dest[to] = 1u;
    }
}

pn_status_t pn_xiangqi_undo(pn_xq_state_t *s) {
    if (!s) return PN_INVALID;
    if (s->outcome == PN_XQ_WON || s->undo_count == 0 || s->moves == 0) return PN_EMPTY;
    pn_xq_undo_t u = s->undo[--s->undo_count];
    memset(&s->undo[s->undo_count], 0, sizeof s->undo[0]);
    if (u.blk_from != PN_XQ_NONE_SQ) {
        if (u.blk_from >= PN_XQ_CELLS || u.blk_to >= PN_XQ_CELLS || s->board[u.blk_from] != 0) return PN_CORRUPT;
        s->board[u.blk_from] = s->board[u.blk_to];
        s->board[u.blk_to] = u.blk_cap;
    }
    if (u.red_from >= PN_XQ_CELLS || u.red_to >= PN_XQ_CELLS || s->board[u.red_from] != 0) return PN_CORRUPT;
    s->board[u.red_from] = s->board[u.red_to];
    s->board[u.red_to] = u.red_cap;
    s->moves--;
    s->outcome = PN_XQ_ONGOING;
    s->selected = u.red_from;
    s->cursor = u.red_from;
    s->hint_from = PN_XQ_NONE_SQ;
    s->hint_to = PN_XQ_NONE_SQ;
    if (s->undo_count > 0) {
        const pn_xq_undo_t *prev = &s->undo[s->undo_count - 1u];
        s->last_from = prev->blk_from != PN_XQ_NONE_SQ ? prev->blk_from : prev->red_from;
        s->last_to = prev->blk_from != PN_XQ_NONE_SQ ? prev->blk_to : prev->red_to;
    } else {
        s->last_from = PN_XQ_NONE_SQ;
        s->last_to = PN_XQ_NONE_SQ;
    }
    return PN_OK;
}

pn_status_t pn_xiangqi_hint(pn_xq_state_t *s) {
    if (!s) return PN_INVALID;
    if (s->outcome != PN_XQ_ONGOING) return PN_EMPTY;
    uint8_t bf = PN_XQ_NONE_SQ, bt = PN_XQ_NONE_SQ;
    if (!pn_xiangqi_best_red_move(s, &bf, &bt)) return PN_EMPTY;
    s->hint_from = bf;
    s->hint_to = bt;
    s->selected = bf;
    s->cursor = bf;
    if (s->hints < 65535u) s->hints++;
    return PN_OK;
}

pn_status_t pn_xq_encode_state(const pn_xq_state_t *s, bool has_saved, uint8_t out[PN_XQ_SAVE_BYTES]) {
    if (!s || !out) return PN_INVALID;
    memset(out, 0, PN_XQ_SAVE_BYTES);
    memcpy(out, "PNXV", 4);
    wr16(out + 4, 1u);
    out[6] = has_saved ? 1u : 0u;
    out[7] = s->outcome;
    memcpy(out + 8, s->completed, 8);
    wr32(out + 16, s->bank_crc);
    out[20] = s->level.id;
    out[21] = s->level.tier;
    out[22] = s->level.piece_count;
    out[23] = s->level.proof_plies;
    out[24] = s->level.max_moves;
    out[25] = s->level.mate_moves;
    out[26] = s->selected;
    out[27] = s->cursor;
    out[28] = s->last_from;
    out[29] = s->last_to;
    out[30] = s->hint_from;
    out[31] = s->hint_to;
    memcpy(out + 32, s->level.title, 24);
    memcpy(out + 56, s->level.board, PN_XQ_CELLS);
    memcpy(out + 146, s->level.proof, 32);
    memcpy(out + 178, s->board, PN_XQ_CELLS);
    out[268] = s->undo_count;
    wr16(out + 270, s->moves);
    wr16(out + 272, s->hints);
    wr64(out + 276, s->elapsed_ms);
    for (unsigned i = 0; i < s->undo_count && i < PN_XQ_UNDO; i++) {
        uint8_t *dst = out + 284u + i * 8u;
        dst[0] = s->undo[i].red_from;
        dst[1] = s->undo[i].red_to;
        dst[2] = s->undo[i].red_cap;
        dst[3] = s->undo[i].blk_from;
        dst[4] = s->undo[i].blk_to;
        dst[5] = s->undo[i].blk_cap;
    }
    return PN_OK;
}

pn_status_t pn_xq_decode_state(const uint8_t *in, size_t n, uint32_t current_crc, pn_xq_state_t *out, bool *has_saved) {
    if (!in || !out || !has_saved || n != PN_XQ_SAVE_BYTES) return PN_CORRUPT;
    if (memcmp(in, "PNXV", 4) != 0 || rd16(in + 4) != 1u) return PN_CORRUPT;
    if (in[6] > 1u || in[7] > PN_XQ_LOST || (in[15] & 0xF0u) != 0) return PN_CORRUPT;
    if (in[269] != 0 || rd16(in + 274) != 0) return PN_CORRUPT;

    pn_xq_state_t s = {0};
    bool saved = in[6] != 0;
    memcpy(s.completed, in + 8, 8);
    s.bank_crc = rd32(in + 16);
    if (s.bank_crc != current_crc) {
        memset(s.completed, 0, sizeof s.completed);
        s.bank_crc = current_crc;
    }
    if (!saved) {
        *out = s;
        *has_saved = false;
        return PN_OK;
    }

    s.outcome = in[7];
    s.level.id = in[20];
    s.level.tier = in[21];
    s.level.piece_count = in[22];
    s.level.proof_plies = in[23];
    s.level.max_moves = in[24];
    s.level.mate_moves = in[25];
    s.selected = in[26];
    s.cursor = in[27];
    s.last_from = in[28];
    s.last_to = in[29];
    s.hint_from = in[30];
    s.hint_to = in[31];
    if (s.level.id >= PN_XQ_ENDGAMES || s.level.tier > 2u || s.level.max_moves == 0 || s.level.max_moves > PN_XQ_UNDO)
        return PN_CORRUPT;
    if (s.level.mate_moves < 1u || s.level.mate_moves > 8u || s.level.proof_plies != 2u * s.level.mate_moves - 1u)
        return PN_CORRUPT;
    if (!valid_title_bytes(in + 32, s.level.title)) return PN_CORRUPT;
    memcpy(s.level.board, in + 56, PN_XQ_CELLS);
    memcpy(s.level.proof, in + 146, 32);
    if (!pn_xq_position_valid(s.level.board) || pn_xiangqi_in_check(s.level.board, 0)) return PN_CORRUPT;

    /* 校验存档内残局主变自身合法且以绝杀/困毙收尾。/ Verify the saved level's own proof line is legal and ends in mate/stalemate. */
    uint8_t sim[PN_XQ_CELLS];
    memcpy(sim, s.level.board, PN_XQ_CELLS);
    for (unsigned ply = 0; ply < 16u; ply++) {
        uint8_t f = s.level.proof[ply * 2u], t = s.level.proof[ply * 2u + 1u];
        if (ply < s.level.proof_plies) {
            if (!pn_xiangqi_legal_move(sim, ply & 1u, f, t)) return PN_CORRUPT;
            sim[t] = sim[f];
            sim[f] = 0;
        } else if (f != 0 || t != 0) {
            return PN_CORRUPT;
        }
    }
    if (has_legal_move(sim, 1u)) return PN_CORRUPT;

    memcpy(s.board, in + 178, PN_XQ_CELLS);
    s.undo_count = in[268];
    s.moves = rd16(in + 270);
    s.hints = rd16(in + 272);
    s.elapsed_ms = rd64(in + 276);
    if (s.undo_count > PN_XQ_UNDO || s.moves != s.undo_count || s.moves > s.level.max_moves || s.cursor >= PN_XQ_CELLS)
        return PN_CORRUPT;
    if (s.selected != PN_XQ_NONE_SQ && (s.selected >= PN_XQ_CELLS || side_of(s.board[s.selected]) != 0))
        return PN_CORRUPT;
    if ((s.last_from == PN_XQ_NONE_SQ) != (s.last_to == PN_XQ_NONE_SQ)) return PN_CORRUPT;
    if (s.last_from != PN_XQ_NONE_SQ && (s.last_from >= PN_XQ_CELLS || s.last_to >= PN_XQ_CELLS)) return PN_CORRUPT;
    if ((s.hint_from == PN_XQ_NONE_SQ) != (s.hint_to == PN_XQ_NONE_SQ)) return PN_CORRUPT;
    if (s.hint_from != PN_XQ_NONE_SQ && !pn_xiangqi_legal_move(s.board, 0u, s.hint_from, s.hint_to)) return PN_CORRUPT;

    /* 逐回合回放核验全部历史着法与吃子记录，确保实时盘面100%合法可达。/ Replay all undo turns from level.board to guarantee live board reachability. */
    memcpy(sim, s.level.board, PN_XQ_CELLS);
    for (unsigned i = 0; i < PN_XQ_UNDO; i++) {
        const uint8_t *src = in + 284u + i * 8u;
        if (src[6] != 0 || src[7] != 0) return PN_CORRUPT;
        if (i < s.undo_count) {
            pn_xq_undo_t u = {src[0], src[1], src[2], src[3], src[4], src[5], 0};
            if (!pn_xiangqi_legal_move(sim, 0u, u.red_from, u.red_to) || sim[u.red_to] != u.red_cap) return PN_CORRUPT;
            sim[u.red_to] = sim[u.red_from];
            sim[u.red_from] = 0;
            if (u.blk_from != PN_XQ_NONE_SQ) {
                if (i + 1u < s.undo_count || s.outcome != PN_XQ_WON) {
                    if (!pn_xiangqi_legal_move(sim, 1u, u.blk_from, u.blk_to) || sim[u.blk_to] != u.blk_cap)
                        return PN_CORRUPT;
                    sim[u.blk_to] = sim[u.blk_from];
                    sim[u.blk_from] = 0;
                } else {
                    return PN_CORRUPT;
                }
            } else {
                if (i + 1u != s.undo_count || s.outcome == PN_XQ_ONGOING) return PN_CORRUPT;
            }
            s.undo[i] = u;
        } else {
            for (unsigned b = 0; b < 6u; b++) {
                if (src[b] != 0) return PN_CORRUPT;
            }
        }
    }
    if (memcmp(sim, s.board, PN_XQ_CELLS) != 0) return PN_CORRUPT;

    bool blk_has = has_legal_move(sim, 1u);
    bool red_has = has_legal_move(sim, 0u);
    /* 进行中局面轮到红方行棋：只要求红方有合法着且未超步，不要求此时黑方也能走（残局黑将常处于受困态）。
     * Ongoing positions are Red to move: only Red needs a legal move within max_moves; Black may already be immobilized. */
    if (s.outcome == PN_XQ_WON && blk_has) return PN_CORRUPT;
    if (s.outcome == PN_XQ_ONGOING && (!red_has || s.moves >= s.level.max_moves)) return PN_CORRUPT;
    if (s.outcome == PN_XQ_LOST && red_has && s.moves < s.level.max_moves) return PN_CORRUPT;

    *out = s;
    *has_saved = true;
    return PN_OK;
}

static const char *const save_failure = "棋局保存失败，请重试";

static pn_status_t xq_storage(xq_t *g, pn_xq_state_t *s, bool *has_saved, bool write) {
    pn_media_lease_t lease = {0};
    pn_journal_files_t files;
    pn_journal_io_t io;
    size_t buf_sz = PN_XQ_SAVE_BYTES >= PN_JOURNAL_PATH_MAX * 2u ? PN_XQ_SAVE_BYTES : PN_JOURNAL_PATH_MAX * 2u;
    uint8_t *data = pn_alloc_tagged(g->pool, PN_MEM_UI, buf_sz);
    if (!data) return PN_NO_MEMORY;

    char *a = (char *)data, *b = (char *)data + PN_JOURNAL_PATH_MAX;
    int na = snprintf(a, PN_JOURNAL_PATH_MAX, "%s/xiangqi.a", g->root);
    int nb = snprintf(b, PN_JOURNAL_PATH_MAX, "%s/xiangqi.b", g->root);
    if (na < 0 || nb < 0 || na >= PN_JOURNAL_PATH_MAX || nb >= PN_JOURNAL_PATH_MAX) {
        pn_free(data);
        return PN_LIMIT;
    }

    uint32_t crc = 0;
    pn_status_t r = check_bank(&crc);
    if (r == PN_OK) r = pn_media_acquire(g->media, write ? PN_MEDIA_WRITE : PN_MEDIA_READ, &lease);
    if (r == PN_OK) r = pn_journal_files_init(&files, g->media, &lease, a, b, &io);
    if (r == PN_OK) {
        if (write) {
            size_t prev_size = 0;
            pn_xq_state_t prev;
            bool prev_saved = false;
            r = pn_blob_load_bounded(&io, g->pool, data, PN_XQ_SAVE_BYTES, &prev_size);
            if (r == PN_OK) r = pn_xq_decode_state(data, prev_size, crc, &prev, &prev_saved);
            else if (r == PN_EMPTY) r = PN_OK;
            if (r == PN_OK) {
                pn_xq_encode_state(s, true, data);
                r = pn_blob_save_bounded(&io, g->pool, data, PN_XQ_SAVE_BYTES, PN_XQ_SAVE_BYTES);
            }
        } else {
            size_t size = 0;
            r = pn_blob_load_bounded(&io, g->pool, data, PN_XQ_SAVE_BYTES, &size);
            if (r == PN_OK) r = pn_xq_decode_state(data, size, crc, s, has_saved);
        }
    }
    if (lease.ticket) (void)pn_media_release(g->media, &lease);
    pn_free(data);
    return r;
}

static void xq_time_update(xq_t *g, uint64_t now) {
    if (g->fresh_clock) {
        g->last = now;
        g->fresh_clock = false;
        return;
    }
    if (g->running && now >= g->last && g->state.outcome == PN_XQ_ONGOING) {
        uint64_t delta = now - g->last;
        g->state.elapsed_ms = delta > UINT64_MAX - 86400000u - g->state.elapsed_ms ? UINT64_MAX - 86400000u
                                                                                   : g->state.elapsed_ms + delta;
    }
    g->last = now;
}

static pn_status_t xq_save(pn_xiangqi_t *u, uint64_t now) {
    xq_t *g = u->impl;
    if (!g->has || !g->dirty) return PN_OK;
    pn_status_t r = xq_storage(g, &g->state, NULL, true);
    g->retry = r == PN_OK ? 0 : now + 3000u;
    const char *before = g->notice;
    if (r == PN_OK) {
        g->dirty = false;
        if (g->notice == save_failure) g->notice = NULL;
    } else {
        g->notice = save_failure;
    }
    if (before != g->notice) {
        g->full = true;
        u->presented = false;
    }
    return r;
}

pn_status_t pn_xiangqi_open(pn_xiangqi_t *u, pn_pool_t *pool, pn_media_t *media, const char *root, uint64_t now) {
    if (!u || u->impl || !pool || !media || !root || !*root || strlen(root) >= PN_JOURNAL_PATH_MAX - 20u)
        return PN_INVALID;
    xq_t *g = pn_alloc_tagged(pool, PN_MEM_UI, sizeof *g);
    if (!g) return PN_NO_MEMORY;
    memset(g, 0, sizeof *g);
    g->pool = pool;
    g->media = media;
    strcpy(g->root, root);
    g->last = now;
    u->impl = g;
    u->active = true;
    u->presented = false;

    bool has_saved = false;
    pn_status_t r = xq_storage(g, &g->candidate, &has_saved, false);
    if (r == PN_OK && has_saved) {
        g->state = g->candidate;
        g->has = true;
        g->tier = g->state.level.tier;
        g->selected_level = g->state.level.id % 20u;
        g->screen = g->state.outcome == PN_XQ_ONGOING ? PN_XQ_PLAY : PN_XQ_COMPLETE;
    } else if (r == PN_EMPTY || (r == PN_OK && !has_saved)) {
        g->state = g->candidate;
        g->screen = PN_XQ_SELECT;
    } else {
        g->screen = PN_XQ_ERROR;
        g->notice = "棋局读取失败，原记录保留";
    }
    g->full = true;
    return PN_OK;
}

pn_status_t pn_xiangqi_pause(pn_xiangqi_t *u, uint64_t now) {
    if (!u || !u->impl) return PN_OK;
    xq_t *g = u->impl;
    if (g->pending_start) return PN_BUSY;
    xq_time_update(g, now);
    if (g->has && g->state.outcome == PN_XQ_ONGOING && g->running) g->dirty = true;
    pn_status_t r = xq_save(u, now);
    if (r == PN_OK) {
        g->running = false;
        g->patch = false;
        g->full = true;
        u->presented = false;
    }
    return r;
}

pn_status_t pn_xiangqi_tick(pn_xiangqi_t *u, uint64_t now) {
    if (!u || !u->impl) return PN_EMPTY;
    xq_t *g = u->impl;
    xq_time_update(g, now);
    return g->dirty && u->presented && now >= g->changed && now - g->changed >= 1500u && now >= g->retry
               ? xq_save(u, now)
               : PN_EMPTY;
}

static pn_status_t start_endgame(pn_xiangqi_t *u, uint64_t now, bool restart) {
    xq_t *g = u->impl;
    pn_status_t r = xq_save(u, now);
    if (r != PN_OK) return r;
    g->restart = restart;

    pn_xq_level_t lv;
    if (restart) {
        lv = g->state.level;
    } else {
        r = pn_xiangqi_level(g->tier * 20u + g->selected_level, &lv);
        if (r != PN_OK) return r;
    }
    uint32_t crc = pn_xq_bank_crc();
    memset(&g->candidate, 0, sizeof g->candidate);
    if (g->state.bank_crc == crc) memcpy(g->candidate.completed, g->state.completed, sizeof g->candidate.completed);
    pn_xq_reset_board(&g->candidate, &lv, crc);

    r = xq_storage(g, &g->candidate, NULL, true);
    if (r != PN_OK) {
        g->pending_start = true;
        g->screen = PN_XQ_ERROR;
        g->notice = save_failure;
        g->full = true;
        u->presented = false;
        return r;
    }
    g->state = g->candidate;
    g->tier = g->state.level.tier;
    g->selected_level = g->state.level.id % 20u;
    g->has = true;
    g->pending_start = false;
    g->dirty = false;
    g->notice = NULL;
    g->screen = PN_XQ_PLAY;
    g->full = true;
    u->presented = false;
    return PN_OK;
}

static pn_status_t interact_square(pn_xq_state_t *s, unsigned sq) {
    if (sq >= PN_XQ_CELLS || s->outcome != PN_XQ_ONGOING) return PN_EMPTY;
    s->cursor = (uint8_t)sq;
    if (side_of(s->board[sq]) == 0) {
        if (s->selected == sq) {
            s->selected = PN_XQ_NONE_SQ;
        } else {
            s->selected = (uint8_t)sq;
            if (s->hint_from != sq) {
                s->hint_from = PN_XQ_NONE_SQ;
                s->hint_to = PN_XQ_NONE_SQ;
            }
        }
        return PN_OK;
    }
    if (s->selected != PN_XQ_NONE_SQ && pn_xiangqi_legal_move(s->board, 0u, s->selected, sq))
        return pn_xiangqi_step(s, s->selected, sq);
    if (s->selected != PN_XQ_NONE_SQ) {
        s->selected = PN_XQ_NONE_SQ;
        return PN_OK;
    }
    return PN_EMPTY;
}

pn_status_t pn_xiangqi_event(pn_xiangqi_t *u, int cmd, uint64_t now) {
    if (!u || !u->impl || !u->active) return PN_INVALID;
    xq_t *g = u->impl;
    xq_time_update(g, now);

    if (g->pending_start)
        return cmd == PN_XQ_RETRY ? start_endgame(u, now, g->restart) : PN_BUSY;

    if (cmd == PN_XQ_BACK || cmd == PN_XQ_EXIT) {
        pn_status_t r = pn_xiangqi_pause(u, now);
        if (r == PN_OK) u->active = false;
        return r;
    }
    if (cmd == PN_XQ_RETRY && g->screen == PN_XQ_ERROR) {
        pn_status_t r = xq_save(u, now);
        if (r == PN_OK) {
            bool has_saved = false;
            r = xq_storage(g, &g->candidate, &has_saved, false);
            if (r == PN_OK && has_saved) {
                g->state = g->candidate;
                g->has = true;
                g->tier = g->state.level.tier;
                g->selected_level = g->state.level.id % 20u;
                g->screen = g->state.outcome == PN_XQ_ONGOING ? PN_XQ_PLAY : PN_XQ_COMPLETE;
                g->notice = NULL;
                g->full = true;
                u->presented = false;
            } else if (r == PN_EMPTY || (r == PN_OK && !has_saved)) {
                g->screen = PN_XQ_SELECT;
                g->notice = NULL;
                g->full = true;
                u->presented = false;
            }
        }
        return r;
    }
    if (g->screen == PN_XQ_ERROR) return PN_EMPTY;
    if (cmd == PN_XQ_CLEAN) {
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (cmd == PN_XQ_CANCEL || cmd == PN_XQ_RESUME) {
        if (!g->has) return PN_EMPTY;
        g->tier = g->state.level.tier;
        g->selected_level = g->state.level.id % 20u;
        g->screen = g->state.outcome == PN_XQ_ONGOING ? PN_XQ_PLAY : PN_XQ_COMPLETE;
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (cmd == PN_XQ_CHOOSE || cmd == PN_XQ_RESTART) {
        g->running = false;
        if (g->has && g->state.outcome == PN_XQ_ONGOING) g->dirty = true;
        g->restart = (cmd == PN_XQ_RESTART);
        if (g->restart && g->has && g->state.outcome != PN_XQ_ONGOING)
            return start_endgame(u, now, true);
        g->screen = g->has ? PN_XQ_CONFIRM : PN_XQ_SELECT;
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (cmd == PN_XQ_ACCEPT && g->screen == PN_XQ_CONFIRM) {
        if (g->restart) return start_endgame(u, now, true);
        pn_status_t r = xq_save(u, now);
        if (r != PN_OK) return r;
        g->screen = PN_XQ_SELECT;
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (g->screen == PN_XQ_SELECT) {
        if (cmd >= PN_XQ_TIER && cmd < PN_XQ_TIER + 3) {
            g->tier = (unsigned)(cmd - PN_XQ_TIER);
            g->selected_level = 0;
        } else if (cmd == PN_XQ_PREV_LEVEL) {
            g->selected_level = (g->selected_level + 19u) % 20u;
        } else if (cmd == PN_XQ_NEXT_LEVEL) {
            g->selected_level = (g->selected_level + 1u) % 20u;
        } else if (cmd == PN_XQ_START) {
            return start_endgame(u, now, false);
        } else {
            return PN_EMPTY;
        }
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (cmd == PN_XQ_NEXT && g->screen == PN_XQ_COMPLETE && g->state.outcome == PN_XQ_WON) {
        g->tier = g->state.level.tier;
        g->selected_level = (g->state.level.id % 20u + 1u) % 20u;
        return start_endgame(u, now, false);
    }
    if (!u->presented) return PN_BUSY;

    pn_status_t r = PN_EMPTY;
    if (cmd == PN_XQ_UNDO_MOVE) {
        r = pn_xiangqi_undo(&g->state);
    } else if (g->screen == PN_XQ_PLAY) {
        if (cmd == PN_XQ_HINT) {
            r = pn_xiangqi_hint(&g->state);
        } else if (cmd >= PN_XQ_CELL && cmd < PN_XQ_CELL + PN_XQ_CELLS) {
            r = interact_square(&g->state, (unsigned)(cmd - PN_XQ_CELL));
        } else if (cmd == PN_XQ_ACTIVATE) {
            r = interact_square(&g->state, g->state.cursor);
        } else if (cmd == PN_XQ_UP && g->state.cursor >= 9u) {
            g->state.cursor -= 9u;
            r = PN_OK;
        } else if (cmd == PN_XQ_DOWN && g->state.cursor + 9u < PN_XQ_CELLS) {
            g->state.cursor += 9u;
            r = PN_OK;
        } else if (cmd == PN_XQ_LEFT && (g->state.cursor % 9u) > 0) {
            g->state.cursor -= 1u;
            r = PN_OK;
        } else if (cmd == PN_XQ_RIGHT && (g->state.cursor % 9u) < 8u) {
            g->state.cursor += 1u;
            r = PN_OK;
        }
    }
    if (r == PN_OK) {
        pn_xq_screen_t next_screen = g->state.outcome == PN_XQ_ONGOING ? PN_XQ_PLAY : PN_XQ_COMPLETE;
        if (g->state.outcome != PN_XQ_ONGOING) g->running = false;
        if (next_screen != g->screen) g->full = true;
        g->screen = next_screen;
        g->dirty = true;
        g->changed = now;
        u->presented = false;
        if (g->state.outcome != PN_XQ_ONGOING)
            (void)xq_save(u, now);
    }
    return r;
}

void pn_xiangqi_displayed(pn_xiangqi_t *u, bool ok) {
    if (!u || !u->impl) return;
    xq_t *g = u->impl;
    bool running = ok && g->screen == PN_XQ_PLAY;
    if (running && !g->running) g->fresh_clock = true;
    u->presented = ok;
    g->running = running;
    if (ok) {
        memcpy(g->shown_board, g->state.board, PN_XQ_CELLS);
        pn_xq_compute_legal_dests(&g->state, g->shown_legal);
        g->shown_selected = g->state.selected;
        g->shown_cursor = g->state.cursor;
        g->shown_last_from = g->state.last_from;
        g->shown_last_to = g->state.last_to;
        g->shown_hint_from = g->state.hint_from;
        g->shown_hint_to = g->state.hint_to;
    }
    g->patch = ok && g->screen == PN_XQ_PLAY;
    g->full = !ok;
}

void pn_xiangqi_close(pn_xiangqi_t *u) {
    if (u) {
        pn_free(u->impl);
        memset(u, 0, sizeof *u);
    }
}

const pn_xq_state_t *pn_xiangqi_state(const pn_xiangqi_t *u) {
    const xq_t *g = u ? u->impl : NULL;
    return g && g->has ? &g->state : NULL;
}

pn_xq_screen_t pn_xiangqi_screen(const pn_xiangqi_t *u) {
    return u && u->impl ? ((xq_t *)u->impl)->screen : PN_XQ_ERROR;
}
