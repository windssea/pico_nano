/* SPDX-License-Identifier: Apache-2.0
 * 数织纯规则、动态规划唯一解校验与A/B事务存档。/ Pure Nonogram rules, DP uniqueness verification and A/B transactional saves. */
#include "pn_nonogram_internal.h"
#include "pn_crc32.h"
#include <stdio.h>
#include <string.h>

static const char save_failure[] = "保存失败，请重试";
extern const uint8_t pn_nonogram_bank_bytes[];
extern const size_t pn_nonogram_bank_size;

_Static_assert(sizeof(pn_no_puzzle_t) == 64, "portable nonogram puzzle");
_Static_assert(sizeof(pn_no_undo_t) == 8, "bounded nonogram undo");

static uint64_t get_le(const uint8_t *p, unsigned n) {
    uint64_t v = 0;
    for (unsigned i = 0; i < n; i++)
        v |= (uint64_t)p[i] << (i * 8);
    return v;
}

static void put_le(uint8_t *p, uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; i++)
        p[i] = (uint8_t)(v >> (i * 8));
}

static bool dims_ok(unsigned w, unsigned h, unsigned tier) {
    if (tier == 0)
        return (w == 5 && h == 5) || (w == 8 && h == 8);
    if (tier == 1)
        return w == 10 && h == 10;
    if (tier == 2)
        return w == 15 && h == 15;
    return false;
}

static unsigned extract_runs(uint16_t mask, unsigned len, uint8_t out[PN_NO_MAX_CLUES]) {
    unsigned n = 0, run = 0;
    for (unsigned i = 0; i < len; i++) {
        if ((mask >> i) & 1u)
            run++;
        else if (run) {
            if (n < PN_NO_MAX_CLUES)
                out[n] = (uint8_t)run;
            n++;
            run = 0;
        }
    }
    if (run) {
        if (n < PN_NO_MAX_CLUES)
            out[n] = (uint8_t)run;
        n++;
    }
    if (!n) {
        out[0] = 0;
        return 1;
    }
    return n;
}

unsigned pn_nonogram_row_clues(const pn_no_puzzle_t *p, unsigned row, uint8_t out[PN_NO_MAX_CLUES]) {
    if (!p || !out || !dims_ok(p->width, p->height, p->tier) || row >= p->height)
        return 0;
    uint16_t mask = 0;
    for (unsigned c = 0; c < p->width; c++)
        if (no_sol_bit(p->solution, row * p->width + c))
            mask |= (uint16_t)(1u << c);
    return extract_runs(mask, p->width, out);
}

unsigned pn_nonogram_col_clues(const pn_no_puzzle_t *p, unsigned col, uint8_t out[PN_NO_MAX_CLUES]) {
    if (!p || !out || !dims_ok(p->width, p->height, p->tier) || col >= p->width)
        return 0;
    uint16_t mask = 0;
    for (unsigned r = 0; r < p->height; r++)
        if (no_sol_bit(p->solution, r * p->width + col))
            mask |= (uint16_t)(1u << r);
    return extract_runs(mask, p->height, out);
}

/// 单行精确动态规划：求满足提示且不违背已知位的所有排布交集。/ Exact single-line DP finding forced fills and forced blanks.
static bool solve_line(unsigned len, const uint8_t *runs, unsigned k, uint16_t known1, uint16_t known0,
                       uint16_t *forced1, uint16_t *forced0) {
    uint16_t full = (uint16_t)((1u << len) - 1u);
    if (k == 1 && runs[0] == 0) {
        if (known1)
            return false;
        *forced1 = 0;
        *forced0 = full;
        return true;
    }
    bool suffix[PN_NO_MAX_CLUES + 1][PN_NO_MAX_SIDE + 2];
    bool reach[PN_NO_MAX_CLUES + 1][PN_NO_MAX_SIDE + 2];
    memset(suffix, 0, sizeof suffix);
    memset(reach, 0, sizeof reach);

    for (unsigned p = 0; p <= len; p++)
        if ((known1 >> p) == 0)
            suffix[k][p] = true;
    suffix[k][len + 1] = true;

    for (int i = (int)k - 1; i >= 0; i--) {
        unsigned run = runs[i];
        bool last = ((unsigned)i == k - 1);
        if (!run || run > len)
            return false;
        for (int p = (int)(len - run); p >= 0; p--) {
            bool ok = !((known1 >> p) & 1u) && suffix[i][p + 1];
            uint16_t block = (uint16_t)(((1u << run) - 1u) << p);
            if (!(known0 & block)) {
                if (last) {
                    if ((known1 >> (p + (int)run)) == 0)
                        ok = true;
                } else {
                    unsigned gap = (unsigned)p + run;
                    if (gap < len && !((known1 >> gap) & 1u) && suffix[i + 1][gap + 1])
                        ok = true;
                }
            }
            suffix[i][p] = ok;
        }
    }
    if (!suffix[0][0])
        return false;

    uint16_t can1 = 0, can0 = 0;
    reach[0][0] = true;
    for (unsigned i = 0; i < k; i++) {
        unsigned run = runs[i];
        bool last = (i == k - 1);
        for (unsigned p = 0; p <= len - run; p++) {
            if (!reach[i][p])
                continue;
            if (p < len && !((known1 >> p) & 1u) && suffix[i][p + 1]) {
                can0 |= (uint16_t)(1u << p);
                reach[i][p + 1] = true;
            }
            uint16_t block = (uint16_t)(((1u << run) - 1u) << p);
            if (!(known0 & block)) {
                if (last) {
                    if ((known1 >> (p + run)) == 0) {
                        can1 |= block;
                        unsigned end = p + run;
                        if (end < len)
                            can0 |= (uint16_t)(((1u << (len - end)) - 1u) << end);
                    }
                } else {
                    unsigned gap = p + run;
                    if (gap < len && !((known1 >> gap) & 1u) && suffix[i + 1][gap + 1]) {
                        can1 |= block;
                        can0 |= (uint16_t)(1u << gap);
                        reach[i + 1][gap + 1] = true;
                    }
                }
            }
        }
    }
    *forced1 = (uint16_t)(can1 & ~can0);
    *forced0 = (uint16_t)(can0 & ~can1 & full);
    return true;
}

static bool title_valid(const char title[24]) {
    if (!title[0])
        return false;
    bool ended = false;
    for (unsigned i = 0; i < 24; i++) {
        if (!title[i])
            ended = true;
        else if (ended)
            return false;
    }
    return ended;
}

static bool puzzle_valid(const pn_no_puzzle_t *p) {
    if (!p || !dims_ok(p->width, p->height, p->tier) || p->id >= PN_NO_PUZZLES || p->id / 30 != p->tier ||
        p->reserved || !p->passes || !title_valid(p->title))
        return false;
    unsigned area = (unsigned)p->width * p->height, count = 0;
    for (unsigned i = 0; i < 256; i++) {
        bool b = no_sol_bit(p->solution, i);
        if (i >= area && b)
            return false;
        count += b;
    }
    if (!count || count >= area || count != p->target_count)
        return false;

    uint8_t rcl[PN_NO_MAX_SIDE][PN_NO_MAX_CLUES], ccl[PN_NO_MAX_SIDE][PN_NO_MAX_CLUES];
    uint8_t rlen[PN_NO_MAX_SIDE], clen[PN_NO_MAX_SIDE];
    unsigned max_runs = p->width == 15 ? 6u : 5u;
    for (unsigned r = 0; r < p->height; r++) {
        unsigned n = pn_nonogram_row_clues(p, r, rcl[r]);
        if (!n || n > max_runs)
            return false;
        rlen[r] = (uint8_t)n;
    }
    for (unsigned c = 0; c < p->width; c++) {
        unsigned n = pn_nonogram_col_clues(p, c, ccl[c]);
        if (!n || n > max_runs)
            return false;
        clen[c] = (uint8_t)n;
    }

    uint16_t r1[PN_NO_MAX_SIDE] = {0}, r0[PN_NO_MAX_SIDE] = {0};
    uint16_t c1[PN_NO_MAX_SIDE] = {0}, c0[PN_NO_MAX_SIDE] = {0};
    uint16_t wmask = (uint16_t)((1u << p->width) - 1u), hmask = (uint16_t)((1u << p->height) - 1u);

    for (unsigned pass = 0; pass < 64; pass++) {
        bool changed = false, done = true;
        for (unsigned r = 0; r < p->height; r++) {
            if ((r1[r] | r0[r]) == wmask)
                continue;
            done = false;
            uint16_t f1 = 0, f0 = 0;
            if (!solve_line(p->width, rcl[r], rlen[r], r1[r], r0[r], &f1, &f0))
                return false;
            uint16_t add1 = (uint16_t)(f1 & ~r1[r]), add0 = (uint16_t)(f0 & ~r0[r]);
            if (add1 || add0) {
                changed = true;
                r1[r] |= add1;
                r0[r] |= add0;
                for (unsigned c = 0; c < p->width; c++) {
                    if ((add1 >> c) & 1u)
                        c1[c] |= (uint16_t)(1u << r);
                    if ((add0 >> c) & 1u)
                        c0[c] |= (uint16_t)(1u << r);
                }
            }
        }
        for (unsigned c = 0; c < p->width; c++) {
            if ((c1[c] | c0[c]) == hmask)
                continue;
            done = false;
            uint16_t f1 = 0, f0 = 0;
            if (!solve_line(p->height, ccl[c], clen[c], c1[c], c0[c], &f1, &f0))
                return false;
            uint16_t add1 = (uint16_t)(f1 & ~c1[c]), add0 = (uint16_t)(f0 & ~c0[c]);
            if (add1 || add0) {
                changed = true;
                c1[c] |= add1;
                c0[c] |= add0;
                for (unsigned r = 0; r < p->height; r++) {
                    if ((add1 >> r) & 1u)
                        r1[r] |= (uint16_t)(1u << c);
                    if ((add0 >> r) & 1u)
                        r0[r] |= (uint16_t)(1u << c);
                }
            }
        }
        if (done) {
            for (unsigned r = 0; r < p->height; r++)
                for (unsigned c = 0; c < p->width; c++)
                    if ((((r1[r] >> c) & 1u) != 0) != no_sol_bit(p->solution, r * p->width + c))
                        return false;
            return true;
        }
        if (!changed)
            return false;
    }
    return false;
}

static void unpack_puzzle(pn_no_puzzle_t *p, const uint8_t *r) {
    p->width = r[0];
    p->height = r[1];
    p->tier = r[2];
    p->target_count = r[3];
    p->id = (uint16_t)get_le(r + 4, 2);
    p->passes = r[6];
    p->reserved = r[7];
    memcpy(p->title, r + 8, 24);
    memcpy(p->solution, r + 32, 32);
}

static void pack_puzzle(const pn_no_puzzle_t *p, uint8_t *r) {
    r[0] = p->width;
    r[1] = p->height;
    r[2] = p->tier;
    r[3] = p->target_count;
    put_le(r + 4, p->id, 2);
    r[6] = p->passes;
    r[7] = p->reserved;
    memcpy(r + 8, p->title, 24);
    memcpy(r + 32, p->solution, 32);
}

pn_status_t pn_nonogram_puzzle(unsigned id, pn_no_puzzle_t *out) {
    const uint8_t *b = pn_nonogram_bank_bytes;
    if (!out || id >= PN_NO_PUZZLES)
        return PN_INVALID;
    if (pn_nonogram_bank_size != 32 + PN_NO_PUZZLES * 64 || memcmp(b, "PNNG", 4) || get_le(b + 4, 2) != 1 ||
        get_le(b + 6, 2) != 64 || get_le(b + 8, 4) != PN_NO_PUZZLES ||
        pn_crc32_update(0, b + 32, PN_NO_PUZZLES * 64) != (uint32_t)get_le(b + 12, 4))
        return PN_CORRUPT;
    for (unsigned i = 16; i < 32; i++)
        if (b[i])
            return PN_CORRUPT;
    pn_no_puzzle_t p;
    unpack_puzzle(&p, b + 32 + id * 64);
    if (p.id != id || !puzzle_valid(&p))
        return PN_CORRUPT;
    *out = p;
    return PN_OK;
}

static bool state_shape(const pn_no_state_t *s) {
    return s && dims_ok(s->puzzle.width, s->puzzle.height, s->puzzle.tier);
}

uint8_t pn_nonogram_cell(const pn_no_state_t *s, unsigned cell) {
    if (!state_shape(s) || cell >= (unsigned)s->puzzle.width * s->puzzle.height)
        return PN_NO_BLANK;
    return no_cell_get(s->cells, cell);
}

unsigned pn_nonogram_filled_count(const pn_no_state_t *s) {
    if (!state_shape(s))
        return 0;
    unsigned area = (unsigned)s->puzzle.width * s->puzzle.height, n = 0;
    for (unsigned i = 0; i < area; i++)
        if (no_cell_get(s->cells, i) == PN_NO_FILL)
            n++;
    return n;
}

bool pn_nonogram_row_satisfied(const pn_no_state_t *s, unsigned row) {
    if (!state_shape(s) || row >= s->puzzle.height)
        return false;
    uint8_t target[PN_NO_MAX_CLUES], actual[PN_NO_MAX_CLUES];
    unsigned tn = pn_nonogram_row_clues(&s->puzzle, row, target);
    uint16_t mask = 0;
    for (unsigned c = 0; c < s->puzzle.width; c++)
        if (no_cell_get(s->cells, row * s->puzzle.width + c) == PN_NO_FILL)
            mask |= (uint16_t)(1u << c);
    unsigned an = extract_runs(mask, s->puzzle.width, actual);
    return tn == an && !memcmp(target, actual, tn);
}

bool pn_nonogram_col_satisfied(const pn_no_state_t *s, unsigned col) {
    if (!state_shape(s) || col >= s->puzzle.width)
        return false;
    uint8_t target[PN_NO_MAX_CLUES], actual[PN_NO_MAX_CLUES];
    unsigned tn = pn_nonogram_col_clues(&s->puzzle, col, target);
    uint16_t mask = 0;
    for (unsigned r = 0; r < s->puzzle.height; r++)
        if (no_cell_get(s->cells, r * s->puzzle.width + col) == PN_NO_FILL)
            mask |= (uint16_t)(1u << r);
    unsigned an = extract_runs(mask, s->puzzle.height, actual);
    return tn == an && !memcmp(target, actual, tn);
}

static bool solved_board(const pn_no_state_t *s) {
    unsigned area = (unsigned)s->puzzle.width * s->puzzle.height;
    for (unsigned i = 0; i < area; i++)
        if ((no_cell_get(s->cells, i) == PN_NO_FILL) != no_sol_bit(s->puzzle.solution, i))
            return false;
    return true;
}

static void push_undo(pn_no_state_t *s, pn_no_undo_t u) {
    if (s->undo_count == PN_NO_UNDO) {
        s->undo_head = (uint8_t)((s->undo_head + 1) % PN_NO_UNDO);
        s->undo_count--;
    }
    s->undo[(s->undo_head + s->undo_count) % PN_NO_UNDO] = u;
    s->undo_count++;
}

pn_status_t pn_nonogram_act(pn_no_state_t *s, unsigned cell) {
    if (!state_shape(s) || cell >= (unsigned)s->puzzle.width * s->puzzle.height)
        return PN_INVALID;
    if (s->complete)
        return PN_EMPTY;
    if (s->moves == UINT32_MAX)
        return PN_LIMIT;
    uint8_t old = no_cell_get(s->cells, cell);
    uint8_t want = s->cross_mode ? (old == PN_NO_MARK ? PN_NO_BLANK : PN_NO_MARK)
                                 : (old == PN_NO_FILL ? PN_NO_BLANK : PN_NO_FILL);
    pn_no_undo_t u = {(uint8_t)cell, 1u, 0, old};
    no_cell_set(s->cells, cell, want);
    push_undo(s, u);
    s->selected = (uint16_t)cell;
    s->moves++;
    s->complete = solved_board(s);
    return PN_OK;
}

pn_status_t pn_nonogram_stroke(pn_no_state_t *s, unsigned from, unsigned to) {
    if (!state_shape(s))
        return PN_INVALID;
    unsigned w = s->puzzle.width, area = w * s->puzzle.height;
    if (from >= area || to >= area)
        return PN_INVALID;
    if (from == to)
        return pn_nonogram_act(s, from);
    if (s->complete)
        return PN_EMPTY;
    bool same_row = (from / w) == (to / w), same_col = (from % w) == (to % w);
    if (!same_row && !same_col)
        return PN_INVALID;
    if (s->moves == UINT32_MAX)
        return PN_LIMIT;

    uint8_t origin = no_cell_get(s->cells, from);
    uint8_t match_state, next_state;
    if (!s->cross_mode) {
        if (origin == PN_NO_MARK)
            return PN_EMPTY;
        match_state = origin == PN_NO_FILL ? PN_NO_FILL : PN_NO_BLANK;
        next_state = origin == PN_NO_FILL ? PN_NO_BLANK : PN_NO_FILL;
    } else {
        if (origin == PN_NO_FILL)
            return PN_EMPTY;
        match_state = origin == PN_NO_MARK ? PN_NO_MARK : PN_NO_BLANK;
        next_state = origin == PN_NO_MARK ? PN_NO_BLANK : PN_NO_MARK;
    }

    unsigned lo = from < to ? from : to, hi = from < to ? to : from;
    unsigned step = same_row ? 1u : w;
    unsigned len = (hi - lo) / step + 1u;
    if (!len || len > PN_NO_MAX_SIDE)
        return PN_INVALID;

    uint32_t before = 0;
    bool changed = false;
    for (unsigned i = 0; i < len; i++) {
        unsigned c = lo + i * step;
        uint8_t cur = no_cell_get(s->cells, c);
        before |= (uint32_t)cur << (i * 2);
        if (cur == match_state) {
            no_cell_set(s->cells, c, next_state);
            changed = true;
        }
    }
    if (!changed)
        return PN_EMPTY;

    pn_no_undo_t u = {(uint8_t)lo, (uint8_t)((same_col ? 0x80u : 0u) | len), 0, before};
    push_undo(s, u);
    s->selected = (uint16_t)to;
    s->moves++;
    s->complete = solved_board(s);
    return PN_OK;
}

pn_status_t pn_nonogram_undo(pn_no_state_t *s) {
    if (!state_shape(s))
        return PN_INVALID;
    if (!s->undo_count)
        return PN_EMPTY;
    const pn_no_undo_t *u = &s->undo[(s->undo_head + s->undo_count - 1) % PN_NO_UNDO];
    unsigned w = s->puzzle.width, step = (u->dir_len & 0x80u) ? w : 1u, len = u->dir_len & 0x7fu;
    for (unsigned i = 0; i < len; i++)
        no_cell_set(s->cells, u->start + i * step, (uint8_t)((u->before >> (i * 2)) & 3u));
    s->selected = u->start;
    if (s->moves)
        s->moves--;
    s->undo_count--;
    s->complete = solved_board(s);
    return PN_OK;
}

pn_status_t pn_nonogram_hint(pn_no_state_t *s) {
    if (!state_shape(s))
        return PN_INVALID;
    if (s->complete)
        return PN_EMPTY;
    if (s->moves == UINT32_MAX || s->hints == UINT16_MAX)
        return PN_LIMIT;
    unsigned area = (unsigned)s->puzzle.width * s->puzzle.height;
    unsigned pick = area;
    /* 优先修正填错的格，其次揭示未填的涂黑格（从当前选中格起扫描）。/ Fix wrong fills first, then reveal an unfilled target cell starting from selected. */
    for (unsigned pass = 0; pass < 2 && pick == area; pass++) {
        for (unsigned k = 0; k < area; k++) {
            unsigned c = (s->selected + k) % area;
            uint8_t cur = no_cell_get(s->cells, c);
            bool sol = no_sol_bit(s->puzzle.solution, c);
            if (pass == 0 && ((sol && cur == PN_NO_MARK) || (!sol && cur == PN_NO_FILL))) {
                pick = c;
                break;
            }
            if (pass == 1 && sol && cur != PN_NO_FILL) {
                pick = c;
                break;
            }
        }
    }
    if (pick == area)
        return PN_EMPTY;
    uint8_t old = no_cell_get(s->cells, pick);
    uint8_t want = no_sol_bit(s->puzzle.solution, pick) ? PN_NO_FILL : PN_NO_MARK;
    pn_no_undo_t u = {(uint8_t)pick, 1u, 0, old};
    no_cell_set(s->cells, pick, want);
    push_undo(s, u);
    s->selected = (uint16_t)pick;
    s->moves++;
    s->hints++;
    s->complete = solved_board(s);
    return PN_OK;
}

static bool state_valid(const pn_no_state_t *s) {
    if (!puzzle_valid(&s->puzzle))
        return false;
    unsigned w = s->puzzle.width, h = s->puzzle.height, area = w * h;
    if (s->selected >= area || s->undo_count > PN_NO_UNDO || s->undo_head >= PN_NO_UNDO ||
        s->undo_count > s->moves || s->hints > s->moves || s->elapsed_ms > UINT64_MAX - 86400000u)
        return false;
    for (unsigned i = PN_NO_PUZZLES; i < 96; i++)
        if ((s->completed[i / 8] >> (i % 8)) & 1u)
            return false;
    for (unsigned i = 0; i < 240; i++) {
        uint8_t v = no_cell_get(s->cells, i);
        if (v > PN_NO_MARK || (i >= area && v != PN_NO_BLANK))
            return false;
    }
    if (s->complete != solved_board(s))
        return false;
    for (unsigned i = 0; i < s->undo_count; i++) {
        const pn_no_undo_t *u = &s->undo[(s->undo_head + i) % PN_NO_UNDO];
        unsigned len = u->dir_len & 0x7fu;
        bool vert = (u->dir_len & 0x80u) != 0;
        if (u->reserved || !len || len > PN_NO_MAX_SIDE || u->start >= area)
            return false;
        if (vert) {
            if (u->start / w + len > h)
                return false;
        } else {
            if (u->start % w + len > w)
                return false;
        }
        if (len < 16 && (u->before >> (len * 2)) != 0)
            return false;
        for (unsigned j = 0; j < len; j++)
            if (((u->before >> (j * 2)) & 3u) > PN_NO_MARK)
                return false;
    }
    return true;
}

static void encode(const pn_no_state_t *s, uint8_t *b) {
    memset(b, 0, NO_SAVE_BYTES);
    memcpy(b, "PNNV", 4);
    put_le(b + 4, 1, 2);
    put_le(b + 8, s->bank_crc, 4);
    put_le(b + 12, s->moves, 4);
    put_le(b + 16, s->elapsed_ms, 8);
    put_le(b + 24, s->selected, 2);
    put_le(b + 26, s->hints, 2);
    b[28] = s->cross_mode ? 1 : 0;
    b[29] = s->complete ? 1 : 0;
    b[30] = s->undo_count;
    b[31] = s->undo_head;
    memcpy(b + 32, s->completed, 12);
    memcpy(b + 44, s->cells, 60);
    pack_puzzle(&s->puzzle, b + 128);
    for (unsigned i = 0; i < PN_NO_UNDO; i++) {
        const pn_no_undo_t *u = &s->undo[i];
        uint8_t *p = b + 192 + i * 8;
        p[0] = u->start;
        p[1] = u->dir_len;
        put_le(p + 2, u->reserved, 2);
        put_le(p + 4, u->before, 4);
    }
}

static pn_status_t decode(pn_no_state_t *s, const uint8_t *b, size_t n) {
    if (n != NO_SAVE_BYTES || memcmp(b, "PNNV", 4) || get_le(b + 4, 2) != 1 || b[6] || b[7] || b[28] > 1 || b[29] > 1)
        return PN_CORRUPT;
    for (unsigned i = 104; i < 128; i++)
        if (b[i])
            return PN_CORRUPT;
    memset(s, 0, sizeof *s);
    s->bank_crc = (uint32_t)get_le(b + 8, 4);
    s->moves = (uint32_t)get_le(b + 12, 4);
    s->elapsed_ms = get_le(b + 16, 8);
    s->selected = (uint16_t)get_le(b + 24, 2);
    s->hints = (uint16_t)get_le(b + 26, 2);
    s->cross_mode = b[28] != 0;
    s->complete = b[29] != 0;
    s->undo_count = b[30];
    s->undo_head = b[31];
    memcpy(s->completed, b + 32, 12);
    memcpy(s->cells, b + 44, 60);
    unpack_puzzle(&s->puzzle, b + 128);
    for (unsigned i = 0; i < PN_NO_UNDO; i++) {
        const uint8_t *p = b + 192 + i * 8;
        s->undo[i] = (pn_no_undo_t){p[0], p[1], (uint16_t)get_le(p + 2, 2), (uint32_t)get_le(p + 4, 4)};
    }
    return state_valid(s) ? PN_OK : PN_CORRUPT;
}

static pn_status_t storage(no_t *g, pn_no_state_t *s, bool write) {
    pn_media_lease_t lease = {0};
    pn_journal_files_t files;
    pn_journal_io_t io;
    uint8_t *data = pn_alloc_tagged(g->pool, PN_MEM_UI, NO_SAVE_BYTES >= PN_JOURNAL_PATH_MAX * 2 ? NO_SAVE_BYTES : PN_JOURNAL_PATH_MAX * 2);
    if (!data)
        return PN_NO_MEMORY;
    char *a = (char *)data, *b = (char *)data + PN_JOURNAL_PATH_MAX;
    int n = snprintf(a, PN_JOURNAL_PATH_MAX, "%s/nonogram.a", g->root);
    int m = snprintf(b, PN_JOURNAL_PATH_MAX, "%s/nonogram.b", g->root);
    if (n < 0 || m < 0 || n >= PN_JOURNAL_PATH_MAX || m >= PN_JOURNAL_PATH_MAX) {
        pn_free(data);
        return PN_LIMIT;
    }
    pn_status_t r = pn_media_acquire(g->media, write ? PN_MEDIA_WRITE : PN_MEDIA_READ, &lease);
    if (r == PN_OK)
        r = pn_journal_files_init(&files, g->media, &lease, a, b, &io);
    if (r == PN_OK) {
        if (write) {
            size_t prev_size = 0;
            pn_no_state_t prev;
            r = pn_blob_load_bounded(&io, g->pool, data, NO_SAVE_BYTES, &prev_size);
            if (r == PN_OK)
                r = decode(&prev, data, prev_size);
            else if (r == PN_EMPTY)
                r = PN_OK;
            if (r == PN_OK) {
                encode(s, data);
                r = pn_blob_save_bounded(&io, g->pool, data, NO_SAVE_BYTES, NO_SAVE_BYTES);
            }
        } else {
            size_t size = 0;
            r = pn_blob_load_bounded(&io, g->pool, data, NO_SAVE_BYTES, &size);
            if (r == PN_OK)
                r = decode(s, data, size);
        }
    }
    if (lease.ticket)
        (void)pn_media_release(g->media, &lease);
    pn_free(data);
    return r;
}

static void time_update(no_t *g, uint64_t now) {
    if (g->fresh_clock) {
        g->last = now;
        g->fresh_clock = false;
        return;
    }
    if (g->running && now >= g->last && !g->state.complete) {
        uint64_t delta = now - g->last;
        g->state.elapsed_ms = delta > UINT64_MAX - 86400000u - g->state.elapsed_ms ? UINT64_MAX - 86400000u
                                                                                   : g->state.elapsed_ms + delta;
    }
    g->last = now;
}

static pn_status_t save(pn_nonogram_t *u, uint64_t now) {
    no_t *g = u->impl;
    if (!g->has || !g->dirty)
        return PN_OK;
    pn_status_t r = storage(g, &g->state, true);
    g->retry = r == PN_OK ? 0 : now + 3000;
    const char *before = g->notice;
    if (r == PN_OK) {
        g->dirty = false;
        if (g->notice == save_failure)
            g->notice = NULL;
    } else {
        g->notice = save_failure;
    }
    if (before != g->notice) {
        g->full = true;
        u->presented = false;
    }
    return r;
}

pn_status_t pn_nonogram_open(pn_nonogram_t *u, pn_pool_t *pool, pn_media_t *media, const char *root, uint64_t now) {
    if (!u || u->impl || !pool || !media || !root || !*root || strlen(root) >= PN_JOURNAL_PATH_MAX - 20)
        return PN_INVALID;
    no_t *g = pn_alloc_tagged(pool, PN_MEM_UI, sizeof *g);
    if (!g)
        return PN_NO_MEMORY;
    memset(g, 0, sizeof *g);
    g->pool = pool;
    g->media = media;
    strcpy(g->root, root);
    g->last = now;
    u->impl = g;
    u->active = true;
    u->presented = false;

    pn_status_t r = storage(g, &g->candidate, false);
    if (r == PN_OK) {
        g->state = g->candidate;
        g->has = true;
        g->tier = g->state.puzzle.tier;
        g->selected_level = g->state.puzzle.id % 30;
        g->screen = g->state.complete ? PN_NO_COMPLETE : PN_NO_PLAY;
    } else if (r == PN_EMPTY) {
        g->screen = PN_NO_SELECT;
    } else {
        g->screen = PN_NO_ERROR;
        g->notice = "棋局读取失败，原记录保留";
    }
    g->full = true;
    return PN_OK;
}

pn_status_t pn_nonogram_pause(pn_nonogram_t *u, uint64_t now) {
    if (!u || !u->impl)
        return PN_OK;
    no_t *g = u->impl;
    if (g->pending_start)
        return PN_BUSY;
    time_update(g, now);
    if (g->has && !g->state.complete && g->running)
        g->dirty = true;
    pn_status_t r = save(u, now);
    if (r == PN_OK) {
        g->running = false;
        g->down = false;
        g->blocked = true;
        g->patch = false;
        g->full = true;
        u->presented = false;
    }
    return r;
}

pn_status_t pn_nonogram_tick(pn_nonogram_t *u, uint64_t now) {
    if (!u || !u->impl)
        return PN_EMPTY;
    no_t *g = u->impl;
    time_update(g, now);
    return g->dirty && u->presented && now >= g->changed && now - g->changed >= 1500 && now >= g->retry ? save(u, now)
                                                                                                        : PN_EMPTY;
}

static pn_status_t start_puzzle(pn_nonogram_t *u, uint64_t now, bool restart) {
    no_t *g = u->impl;
    pn_status_t r = save(u, now);
    if (r != PN_OK)
        return r;
    g->restart = restart;
    memset(&g->candidate, 0, sizeof g->candidate);
    if (restart) {
        g->candidate.puzzle = g->state.puzzle;
    } else {
        r = pn_nonogram_puzzle(g->tier * 30 + g->selected_level, &g->candidate.puzzle);
        if (r != PN_OK)
            return r;
    }
    g->candidate.bank_crc = restart ? g->state.bank_crc : (uint32_t)get_le(pn_nonogram_bank_bytes + 12, 4);
    if (g->has && g->state.bank_crc == g->candidate.bank_crc)
        memcpy(g->candidate.completed, g->state.completed, 12);

    r = storage(g, &g->candidate, true);
    if (r != PN_OK) {
        g->pending_start = true;
        g->screen = PN_NO_ERROR;
        g->notice = save_failure;
        g->full = true;
        u->presented = false;
        return r;
    }
    g->state = g->candidate;
    g->tier = g->state.puzzle.tier;
    g->selected_level = g->state.puzzle.id % 30;
    g->has = true;
    g->pending_start = false;
    g->dirty = false;
    g->notice = NULL;
    g->screen = PN_NO_PLAY;
    g->full = true;
    u->presented = false;
    return PN_OK;
}

pn_status_t pn_nonogram_event(pn_nonogram_t *u, int cmd, uint64_t now) {
    if (!u || !u->impl || !u->active)
        return PN_INVALID;
    no_t *g = u->impl;
    time_update(g, now);

    if (g->pending_start)
        return cmd == PN_NO_RETRY ? start_puzzle(u, now, g->restart) : PN_BUSY;

    if (cmd == PN_NO_BACK || cmd == PN_NO_EXIT) {
        pn_status_t r = pn_nonogram_pause(u, now);
        if (r == PN_OK)
            u->active = false;
        return r;
    }
    if (cmd == PN_NO_RETRY && g->screen == PN_NO_ERROR) {
        pn_status_t r = save(u, now);
        if (r == PN_OK) {
            r = storage(g, &g->candidate, false);
            if (r == PN_OK) {
                g->state = g->candidate;
                g->has = true;
                g->tier = g->state.puzzle.tier;
                g->selected_level = g->state.puzzle.id % 30;
                g->screen = g->state.complete ? PN_NO_COMPLETE : PN_NO_PLAY;
                g->notice = NULL;
                g->full = true;
                u->presented = false;
            } else if (r == PN_EMPTY) {
                g->screen = PN_NO_SELECT;
                g->notice = NULL;
                g->full = true;
                u->presented = false;
            }
        }
        return r;
    }
    if (g->screen == PN_NO_ERROR)
        return PN_EMPTY;
    if (cmd == PN_NO_CLEAN) {
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (cmd == PN_NO_CANCEL || cmd == PN_NO_RESUME) {
        if (!g->has)
            return PN_EMPTY;
        g->tier = g->state.puzzle.tier;
        g->selected_level = g->state.puzzle.id % 30;
        g->screen = g->state.complete ? PN_NO_COMPLETE : PN_NO_PLAY;
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (cmd == PN_NO_CHOOSE || cmd == PN_NO_RESTART) {
        g->running = false;
        if (g->has && !g->state.complete)
            g->dirty = true;
        g->restart = cmd == PN_NO_RESTART;
        g->screen = g->has ? PN_NO_CONFIRM : PN_NO_SELECT;
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (cmd == PN_NO_ACCEPT && g->screen == PN_NO_CONFIRM) {
        if (g->restart)
            return start_puzzle(u, now, true);
        pn_status_t r = save(u, now);
        if (r != PN_OK)
            return r;
        g->screen = PN_NO_SELECT;
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (g->screen == PN_NO_SELECT) {
        if (cmd >= PN_NO_TIER && cmd < PN_NO_TIER + 3) {
            g->tier = (unsigned)(cmd - PN_NO_TIER);
            g->selected_level = 0;
        } else if (cmd == PN_NO_PREV_LEVEL) {
            g->selected_level = (g->selected_level + 29) % 30;
        } else if (cmd == PN_NO_NEXT_LEVEL) {
            g->selected_level = (g->selected_level + 1) % 30;
        } else if (cmd == PN_NO_START) {
            return start_puzzle(u, now, false);
        } else {
            return PN_EMPTY;
        }
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (cmd == PN_NO_NEXT && g->screen == PN_NO_COMPLETE) {
        g->tier = g->state.puzzle.tier;
        g->selected_level = (g->state.puzzle.id % 30 + 1) % 30;
        return start_puzzle(u, now, false);
    }
    if (!u->presented)
        return PN_BUSY;

    pn_status_t r = PN_EMPTY;
    if (cmd == PN_NO_UNDO_MOVE) {
        r = pn_nonogram_undo(&g->state);
    } else if (g->screen == PN_NO_PLAY) {
        unsigned area = (unsigned)g->state.puzzle.width * g->state.puzzle.height;
        if (cmd == PN_NO_FILL_MODE || cmd == PN_NO_CROSS_MODE) {
            bool cross = (cmd == PN_NO_CROSS_MODE);
            if (g->state.cross_mode != cross) {
                g->state.cross_mode = cross;
                r = PN_OK;
            }
        } else if (cmd == PN_NO_HINT) {
            r = pn_nonogram_hint(&g->state);
        } else if (cmd >= PN_NO_CELL && cmd < PN_NO_CELL + (int)area) {
            r = pn_nonogram_act(&g->state, (unsigned)(cmd - PN_NO_CELL));
        } else if (cmd >= PN_NO_STROKE && cmd < PN_NO_STROKE + PN_NO_CELLS * PN_NO_CELLS) {
            unsigned v = (unsigned)(cmd - PN_NO_STROKE);
            r = pn_nonogram_stroke(&g->state, v / PN_NO_CELLS, v % PN_NO_CELLS);
        }
    }
    if (r == PN_OK) {
        bool was_complete = (g->screen == PN_NO_COMPLETE);
        g->screen = g->state.complete ? PN_NO_COMPLETE : PN_NO_PLAY;
        if (g->state.complete) {
            g->state.completed[g->state.puzzle.id / 8] |= (uint8_t)(1u << (g->state.puzzle.id % 8));
            g->running = false;
        }
        if (was_complete != g->state.complete)
            g->full = true;
        g->dirty = true;
        g->changed = now;
        u->presented = false;
    }
    return r;
}

void pn_nonogram_displayed(pn_nonogram_t *u, bool ok) {
    if (!u || !u->impl)
        return;
    no_t *g = u->impl;
    bool running = ok && g->screen == PN_NO_PLAY;
    if (running && !g->running)
        g->fresh_clock = true;
    u->presented = ok;
    g->running = running;
    if (ok) {
        memcpy(g->shown_cells, g->state.cells, sizeof g->shown_cells);
        g->shown_selected = g->state.selected;
        g->shown_cross_mode = g->state.cross_mode;
    }
    g->patch = ok && g->screen == PN_NO_PLAY;
    g->full = !ok;
}

void pn_nonogram_close(pn_nonogram_t *u) {
    if (u) {
        pn_free(u->impl);
        memset(u, 0, sizeof *u);
    }
}

const pn_no_state_t *pn_nonogram_state(const pn_nonogram_t *u) {
    const no_t *g = u ? u->impl : NULL;
    return g && g->has ? &g->state : NULL;
}

pn_no_screen_t pn_nonogram_screen(const pn_nonogram_t *u) {
    return u && u->impl ? ((no_t *)u->impl)->screen : PN_NO_ERROR;
}
