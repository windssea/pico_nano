/* SPDX-License-Identifier: Apache-2.0
 * 三国华容道规则引擎、内置BFS最优解提示、有界A/B事务存档与状态机。/ Three Kingdoms Klotski engine, BFS optimal hints, bounded A/B saves and controller. */
#include "pn_klotski_internal.h"
#include "pn_crc32.h"
#include <stdio.h>
#include <string.h>

extern const uint8_t pn_klotski_bank_bytes[];
extern const size_t pn_klotski_bank_size;

#define KL_BANK_HDR 16u
#define KL_BANK_REC 40u
#define KL_SAVE_MAGIC 0x31534C4Bu /* "KLS1" */
#define KL_SAVE_VER 1u
#define KL_SAVE_IDLE_MS 2000u
#define KL_SAVE_RETRY_MS 3000u

static const int8_t DX[4] = {0, 0, -1, 1};
static const int8_t DY[4] = {-1, 1, 0, 0};

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

void pn_kl_piece_dims(uint8_t shape, unsigned *w, unsigned *h) {
    *w = (shape == PN_KL_SHAPE_2X1 || shape == PN_KL_SHAPE_2X2) ? 2u : 1u;
    *h = (shape == PN_KL_SHAPE_1X2 || shape == PN_KL_SHAPE_2X2) ? 2u : 1u;
}

static bool bank_valid(uint32_t *out_crc, unsigned *out_count) {
    if (pn_klotski_bank_size < KL_BANK_HDR + KL_BANK_REC) return false;
    const uint8_t *b = pn_klotski_bank_bytes;
    if (memcmp(b, "PNKL", 4) != 0) return false;
    uint16_t ver = rd16(b + 4), count = rd16(b + 6);
    uint32_t crc = rd32(b + 8), res = rd32(b + 12);
    if (ver != 1u || count != PN_KL_LEVELS || res != 0u) return false;
    if (pn_klotski_bank_size < KL_BANK_HDR + (size_t)count * KL_BANK_REC) return false;
    if (pn_crc32_update(0u, b + KL_BANK_HDR, pn_klotski_bank_size - KL_BANK_HDR) != crc) return false;
    if (out_crc) *out_crc = crc;
    if (out_count) *out_count = count;
    return true;
}

uint32_t pn_kl_bank_crc(void) {
    uint32_t crc = 0;
    return bank_valid(&crc, NULL) ? crc : 0u;
}

bool pn_klotski_occupancy(const pn_kl_level_t *lv, const uint8_t pos[PN_KL_MAX_PIECES], uint8_t occ[PN_KL_CELLS]) {
    if (!lv || !pos || !occ || lv->piece_count < 2u || lv->piece_count > PN_KL_MAX_PIECES) return false;
    memset(occ, PN_KL_NONE, PN_KL_CELLS);
    unsigned occupied = 0;
    for (unsigned i = 0; i < lv->piece_count; i++) {
        uint8_t p = pos[i], sh = lv->shapes[i];
        if (p >= PN_KL_CELLS || sh > PN_KL_SHAPE_2X2) return false;
        unsigned w, h;
        pn_kl_piece_dims(sh, &w, &h);
        unsigned r = p / PN_KL_COLS, c = p % PN_KL_COLS;
        if (r + h > PN_KL_ROWS || c + w > PN_KL_COLS) return false;
        for (unsigned dr = 0; dr < h; dr++) {
            for (unsigned dc = 0; dc < w; dc++) {
                unsigned cell = (r + dr) * PN_KL_COLS + (c + dc);
                if (occ[cell] != PN_KL_NONE) return false;
                occ[cell] = (uint8_t)i;
                occupied++;
            }
        }
    }
    return occupied == PN_KL_CELLS - 2u;
}

static const uint8_t *bank_sol_pool(size_t *out_len) {
    size_t off = KL_BANK_HDR + (size_t)PN_KL_LEVELS * KL_BANK_REC;
    if (pn_klotski_bank_size <= off) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    if (out_len) *out_len = pn_klotski_bank_size - off;
    return pn_klotski_bank_bytes + off;
}

pn_status_t pn_klotski_level(unsigned id, pn_kl_level_t *out) {
    if (!out || id >= PN_KL_LEVELS) return PN_INVALID;
    if (!bank_valid(NULL, NULL)) return PN_CORRUPT;
    const uint8_t *rec = pn_klotski_bank_bytes + KL_BANK_HDR + (size_t)id * KL_BANK_REC;
    size_t pool_len = 0;
    const uint8_t *pool = bank_sol_pool(&pool_len);
    pn_kl_level_t lv;
    memset(&lv, 0, sizeof lv);
    lv.id = rec[0];
    lv.tier = rec[1];
    lv.piece_count = rec[2];
    lv.par_moves = rec[3];
    lv.sol_offset = rd16(rec + 4);
    lv.sol_len = rd16(rec + 6);
    if (lv.id != id || lv.tier > 2u || lv.piece_count < 6u || lv.piece_count > PN_KL_MAX_PIECES) return PN_CORRUPT;
    if (!pool || (size_t)lv.sol_offset + lv.sol_len > pool_len || lv.sol_len == 0u) return PN_CORRUPT;
    memcpy(lv.title, rec + 8, 19);
    lv.title[19] = '\0';
    memset(lv.start_pos, PN_KL_NONE, sizeof lv.start_pos);
    for (unsigned i = 0; i < lv.piece_count; i++) {
        uint8_t b = rec[28u + i];
        lv.shapes[i] = (uint8_t)(b >> 6);
        lv.start_pos[i] = (uint8_t)(b & 63u);
    }
    if (lv.shapes[0] != PN_KL_SHAPE_2X2 || lv.start_pos[0] == 13u) return PN_CORRUPT;
    uint8_t occ[PN_KL_CELLS];
    if (!pn_klotski_occupancy(&lv, lv.start_pos, occ)) return PN_CORRUPT;
    *out = lv;
    return PN_OK;
}

const char *pn_klotski_piece_label(const pn_kl_level_t *lv, unsigned piece) {
    static const char *const v_names[] = {"张飞", "赵云", "马超", "黄忠", "魏延", "周仓"};
    static const char *const h_names[] = {"关羽", "甘宁", "姜维", "太史", "庞德"};
    if (!lv || piece >= lv->piece_count) return "";
    uint8_t sh = lv->shapes[piece];
    if (sh == PN_KL_SHAPE_2X2) return "曹操";
    if (sh == PN_KL_SHAPE_1X1) return "卒";
    unsigned rank = 0;
    for (unsigned i = 0; i < piece; i++) {
        if (lv->shapes[i] == sh) rank++;
    }
    if (sh == PN_KL_SHAPE_2X1) return h_names[rank % (sizeof h_names / sizeof *h_names)];
    return v_names[rank % (sizeof v_names / sizeof *v_names)];
}

static bool can_slide_pos(const pn_kl_level_t *lv, const uint8_t pos[PN_KL_MAX_PIECES], unsigned piece, unsigned dir,
                          unsigned steps) {
    if (!lv || piece >= lv->piece_count || dir >= 4u || (steps != 1u && steps != 2u)) return false;
    uint8_t occ[PN_KL_CELLS];
    if (!pn_klotski_occupancy(lv, pos, occ)) return false;
    unsigned w, h;
    pn_kl_piece_dims(lv->shapes[piece], &w, &h);
    int r0 = (int)(pos[piece] / PN_KL_COLS), c0 = (int)(pos[piece] % PN_KL_COLS);
    for (unsigned st = 1; st <= steps; st++) {
        int nr = r0 + (int)DY[dir] * (int)st;
        int nc = c0 + (int)DX[dir] * (int)st;
        if (nr < 0 || nc < 0 || nr + (int)h > PN_KL_ROWS || nc + (int)w > PN_KL_COLS) return false;
        for (unsigned dr = 0; dr < h; dr++) {
            for (unsigned dc = 0; dc < w; dc++) {
                uint8_t who = occ[(unsigned)(nr + (int)dr) * PN_KL_COLS + (unsigned)(nc + (int)dc)];
                if (who != PN_KL_NONE && who != piece) return false;
            }
        }
    }
    return true;
}

bool pn_klotski_can_slide(const pn_kl_state_t *s, unsigned piece, unsigned dir, unsigned steps) {
    if (!s || s->solved) return false;
    return can_slide_pos(&s->level, s->pos, piece, dir, steps);
}

static uint8_t moved_pos(uint8_t p, unsigned dir, unsigned steps) {
    int r = (int)(p / PN_KL_COLS) + (int)DY[dir] * (int)steps;
    int c = (int)(p % PN_KL_COLS) + (int)DX[dir] * (int)steps;
    return (uint8_t)(r * PN_KL_COLS + c);
}

pn_status_t pn_klotski_slide(pn_kl_state_t *s, unsigned piece, unsigned dir, unsigned steps) {
    if (!s || !pn_klotski_can_slide(s, piece, dir, steps)) return PN_INVALID;
    uint8_t from = s->pos[piece];
    uint8_t to = moved_pos(from, dir, steps);
    if (s->undo_count < PN_KL_UNDO) {
        s->undo[s->undo_count++] = (pn_kl_undo_t){(uint8_t)piece, from, to, 0};
    } else {
        memmove(s->undo, s->undo + 1, (PN_KL_UNDO - 1u) * sizeof s->undo[0]);
        s->undo[PN_KL_UNDO - 1u] = (pn_kl_undo_t){(uint8_t)piece, from, to, 0};
    }
    s->pos[piece] = to;
    if (s->moves < 65535u) s->moves++;
    s->selected = PN_KL_NONE;
    s->hint_piece = PN_KL_NONE;
    if (s->pos[0] == 13u) {
        s->solved = true;
        s->completed[s->level.id >> 3] |= (uint8_t)(1u << (s->level.id & 7u));
    }
    return PN_OK;
}

pn_status_t pn_klotski_undo(pn_kl_state_t *s) {
    if (!s || s->undo_count == 0u) return PN_EMPTY;
    pn_kl_undo_t u = s->undo[--s->undo_count];
    if (u.piece >= s->level.piece_count) return PN_CORRUPT;
    s->pos[u.piece] = u.from_pos;
    if (s->moves > 0u) s->moves--;
    s->solved = false;
    s->selected = PN_KL_NONE;
    s->hint_piece = PN_KL_NONE;
    return PN_OK;
}

static void shape_key(const pn_kl_level_t *lv, const uint8_t pos[PN_KL_MAX_PIECES], uint8_t key[PN_KL_CELLS]) {
    memset(key, 0, PN_KL_CELLS);
    for (unsigned i = 0; i < lv->piece_count; i++) {
        unsigned w, h;
        pn_kl_piece_dims(lv->shapes[i], &w, &h);
        unsigned r = pos[i] / PN_KL_COLS, c = pos[i] % PN_KL_COLS;
        uint8_t tag = (uint8_t)(lv->shapes[i] + 1u);
        for (unsigned dr = 0; dr < h; dr++)
            for (unsigned dc = 0; dc < w; dc++)
                key[(r + dr) * PN_KL_COLS + (c + dc)] = tag;
    }
}

/* 在最优解序列中查找与目标形制盘面匹配的最深步骤 / Find deepest step on optimal path matching target shape grid. */
static int find_on_optimal_path(const pn_kl_level_t *lv, const uint8_t target_key[PN_KL_CELLS],
                                uint8_t *out_src_pos, uint8_t *out_dir, uint8_t *out_steps) {
    size_t pool_len = 0;
    const uint8_t *pool = bank_sol_pool(&pool_len);
    if (!pool || (size_t)lv->sol_offset + lv->sol_len > pool_len) return -1;
    const uint8_t *sol = pool + lv->sol_offset;
    uint8_t cur[PN_KL_MAX_PIECES];
    memcpy(cur, lv->start_pos, sizeof cur);
    int best_k = -1;
    uint8_t best_src = 0, best_dir = 0, best_steps = 1;
    for (unsigned k = 0; k < lv->sol_len; k++) {
        uint8_t kgrid[PN_KL_CELLS];
        shape_key(lv, cur, kgrid);
        uint8_t code = sol[k];
        uint8_t idx = (uint8_t)((code >> 4) & 0x0Fu);
        uint8_t dir = (uint8_t)((code >> 2) & 0x03u);
        uint8_t steps = (uint8_t)((code & 0x03u) + 1u);
        if (idx >= lv->piece_count) return -1;
        if (memcmp(kgrid, target_key, PN_KL_CELLS) == 0) {
            best_k = (int)k;
            best_src = cur[idx];
            best_dir = dir;
            best_steps = steps;
        }
        cur[idx] = moved_pos(cur[idx], dir, steps);
    }
    if (best_k >= 0) {
        if (out_src_pos) *out_src_pos = best_src;
        if (out_dir) *out_dir = best_dir;
        if (out_steps) *out_steps = best_steps;
    }
    return best_k;
}

pn_status_t pn_klotski_hint(pn_kl_state_t *s) {
    if (!s || s->solved) return PN_EMPTY;
    uint8_t cur_key[PN_KL_CELLS];
    shape_key(&s->level, s->pos, cur_key);
    uint8_t src_pos = 0, dir = 0, steps = 1;
    int step = find_on_optimal_path(&s->level, cur_key, &src_pos, &dir, &steps);
    if (step >= 0) {
        for (unsigned i = 0; i < s->level.piece_count; i++) {
            if (s->pos[i] == src_pos && can_slide_pos(&s->level, s->pos, i, dir, steps)) {
                s->hint_piece = (uint8_t)i;
                s->hint_dir = dir;
                s->hint_steps = steps;
                s->selected = (uint8_t)i;
                if (s->hints < 65535u) s->hints++;
                return PN_OK;
            }
        }
    }
    /* 若偏离主解1步，自动找回最优解路径的纠偏步 / If 1 move off-path, find a recovery move back onto the optimal path. */
    int best_k = -1;
    uint8_t best_p = PN_KL_NONE, best_d = 0, best_s = 1;
    for (unsigned i = 0; i < s->level.piece_count; i++) {
        for (unsigned d = 0; d < 4u; d++) {
            for (unsigned st = 1; st <= 2u; st++) {
                if (!can_slide_pos(&s->level, s->pos, i, d, st)) continue;
                uint8_t tmp[PN_KL_MAX_PIECES];
                memcpy(tmp, s->pos, sizeof tmp);
                tmp[i] = moved_pos(tmp[i], d, st);
                if (i == 0 && tmp[0] == 13u) {
                    s->hint_piece = 0;
                    s->hint_dir = (uint8_t)d;
                    s->hint_steps = (uint8_t)st;
                    s->selected = 0;
                    if (s->hints < 65535u) s->hints++;
                    return PN_OK;
                }
                uint8_t tkey[PN_KL_CELLS];
                shape_key(&s->level, tmp, tkey);
                int k = find_on_optimal_path(&s->level, tkey, NULL, NULL, NULL);
                if (k > best_k) {
                    best_k = k;
                    best_p = (uint8_t)i;
                    best_d = (uint8_t)d;
                    best_s = (uint8_t)st;
                }
            }
        }
    }
    if (best_k >= 0 && best_p != PN_KL_NONE) {
        s->hint_piece = best_p;
        s->hint_dir = best_d;
        s->hint_steps = best_s;
        s->selected = best_p;
        if (s->hints < 65535u) s->hints++;
        return PN_OK;
    }
    s->hint_piece = PN_KL_NONE;
    return PN_EMPTY;
}

void pn_kl_reset_board(pn_kl_state_t *s, const pn_kl_level_t *lv, uint32_t bank_crc) {
    uint8_t comp[8];
    memcpy(comp, s->completed, sizeof comp);
    memset(s, 0, sizeof *s);
    s->level = *lv;
    memcpy(s->pos, lv->start_pos, sizeof s->pos);
    memcpy(s->completed, comp, sizeof comp);
    s->selected = PN_KL_NONE;
    s->hint_piece = PN_KL_NONE;
    s->bank_crc = bank_crc;
}

pn_status_t pn_kl_encode_state(const pn_kl_state_t *s, bool has_saved, uint8_t out[PN_KL_SAVE_BYTES]) {
    if (!s || !out) return PN_INVALID;
    memset(out, 0, PN_KL_SAVE_BYTES);
    wr32(out + 0, KL_SAVE_MAGIC);
    wr16(out + 4, KL_SAVE_VER);
    out[6] = has_saved ? 1u : 0u;
    out[7] = s->level.id;
    out[8] = s->level.piece_count;
    out[9] = s->selected;
    out[10] = s->undo_count;
    out[11] = s->solved ? 1u : 0u;
    wr16(out + 12, s->moves);
    wr16(out + 14, s->hints);
    wr32(out + 16, s->bank_crc);
    wr64(out + 20, s->elapsed_ms);
    memcpy(out + 28, s->completed, 8);
    memcpy(out + 36, s->pos, PN_KL_MAX_PIECES);
    for (unsigned i = 0; i < s->undo_count && i < PN_KL_UNDO; i++) {
        size_t off = 48u + (size_t)i * 4u;
        out[off + 0] = s->undo[i].piece;
        out[off + 1] = s->undo[i].from_pos;
        out[off + 2] = s->undo[i].to_pos;
        out[off + 3] = 0u;
    }
    return PN_OK;
}

pn_status_t pn_kl_decode_state(const uint8_t *in, size_t n, uint32_t current_crc, pn_kl_state_t *out, bool *has_saved) {
    if (!in || !out || !has_saved || n != PN_KL_SAVE_BYTES) return PN_CORRUPT;
    if (rd32(in + 0) != KL_SAVE_MAGIC || rd16(in + 4) != KL_SAVE_VER) return PN_CORRUPT;
    uint8_t has = in[6], id = in[7], pcount = in[8], sel = in[9], ucnt = in[10], solved = in[11];
    if (has > 1u || id >= PN_KL_LEVELS || ucnt > PN_KL_UNDO || solved > 1u) return PN_CORRUPT;
    pn_kl_level_t lv;
    if (pn_klotski_level(id, &lv) != PN_OK) return PN_CORRUPT;
    if (has && pcount != lv.piece_count) return PN_CORRUPT;
    if (sel != PN_KL_NONE && sel >= lv.piece_count) return PN_CORRUPT;
    pn_kl_state_t st;
    memset(&st, 0, sizeof st);
    st.level = lv;
    st.bank_crc = rd32(in + 16);
    if (st.bank_crc == current_crc) {
        memcpy(st.completed, in + 28, 8);
    } else {
        st.bank_crc = current_crc;
    }
    if (has) {
        memcpy(st.pos, in + 36, PN_KL_MAX_PIECES);
        uint8_t occ[PN_KL_CELLS];
        if (!pn_klotski_occupancy(&lv, st.pos, occ)) return PN_CORRUPT;
        st.selected = sel;
        st.undo_count = ucnt;
        st.solved = (solved != 0u) || (st.pos[0] == 13u);
        st.moves = rd16(in + 12);
        st.hints = rd16(in + 14);
        st.elapsed_ms = rd64(in + 20);
        for (unsigned i = 0; i < ucnt; i++) {
            size_t off = 48u + (size_t)i * 4u;
            if (in[off] >= lv.piece_count || in[off + 1] >= PN_KL_CELLS || in[off + 2] >= PN_KL_CELLS)
                return PN_CORRUPT;
            st.undo[i] = (pn_kl_undo_t){in[off], in[off + 1], in[off + 2], 0};
        }
    } else {
        memcpy(st.pos, lv.start_pos, sizeof st.pos);
        st.selected = PN_KL_NONE;
    }
    st.hint_piece = PN_KL_NONE;
    *out = st;
    *has_saved = (has != 0u);
    return PN_OK;
}

static bool save_paths(const kl_t *g, char a[PN_JOURNAL_PATH_MAX], char b[PN_JOURNAL_PATH_MAX]) {
    if (!g->root[0]) return false;
    int na = snprintf(a, PN_JOURNAL_PATH_MAX, "%s/klotski.a", g->root);
    int nb = snprintf(b, PN_JOURNAL_PATH_MAX, "%s/klotski.b", g->root);
    return na > 0 && (size_t)na < PN_JOURNAL_PATH_MAX && nb > 0 && (size_t)nb < PN_JOURNAL_PATH_MAX;
}

static void tick_clock(kl_t *g, uint64_t now) {
    if (g->running && !g->state.solved) {
        if (!g->fresh_clock && now > g->last) g->state.elapsed_ms += now - g->last;
        g->last = now;
        g->fresh_clock = false;
    }
}

static pn_status_t kl_storage(kl_t *g, pn_kl_state_t *s, bool *has_saved, bool write) {
    char a[PN_JOURNAL_PATH_MAX], b[PN_JOURNAL_PATH_MAX];
    if (!save_paths(g, a, b)) return PN_IO;
    uint32_t crc = pn_kl_bank_crc();
    if (!crc) return PN_CORRUPT;
    uint8_t *buf = pn_alloc_tagged(g->pool, PN_MEM_UI, PN_KL_SAVE_BYTES);
    if (!buf) return PN_NO_MEMORY;
    pn_media_lease_t lease = {0};
    pn_journal_files_t files;
    pn_journal_io_t io;
    pn_status_t r = pn_media_acquire(g->media, write ? PN_MEDIA_WRITE : PN_MEDIA_READ, &lease);
    if (r == PN_OK) r = pn_journal_files_init(&files, g->media, &lease, a, b, &io);
    if (r == PN_OK) {
        if (write) {
            r = pn_kl_encode_state(s, g->has, buf);
            if (r == PN_OK) r = pn_blob_save_bounded(&io, g->pool, buf, PN_KL_SAVE_BYTES, PN_KL_SAVE_BYTES);
        } else {
            size_t got = 0;
            r = pn_blob_load_bounded(&io, g->pool, buf, PN_KL_SAVE_BYTES, &got);
            if (r == PN_OK) r = pn_kl_decode_state(buf, got, crc, s, has_saved);
        }
    }
    if (lease.ticket) (void)pn_media_release(g->media, &lease);
    pn_free(buf);
    return r;
}

static pn_status_t save_now(kl_t *g, uint64_t now) {
    if (!g->dirty) return PN_OK;
    if (!g->media || !g->root[0]) {
        g->dirty = false;
        return PN_OK;
    }
    pn_status_t s = kl_storage(g, &g->state, NULL, true);
    if (s == PN_OK) {
        g->dirty = false;
    } else {
        g->retry = now;
    }
    return s;
}

static void mark_full(pn_klotski_t *u, kl_t *g) {
    g->full = true;
    g->patch = false;
    u->presented = false;
}

static void mark_patch(pn_klotski_t *u, kl_t *g) {
    if (!g->full) g->patch = true;
    u->presented = false;
}

static void snapshot_shown(kl_t *g) {
    memcpy(g->shown_pos, g->state.pos, sizeof g->shown_pos);
    g->shown_selected = g->state.selected;
    g->shown_hint_piece = g->state.hint_piece;
    g->shown_hint_dir = g->state.hint_dir;
    g->shown_hint_steps = g->state.hint_steps;
}

static pn_status_t start_level(pn_klotski_t *u, kl_t *g, unsigned id, uint64_t now) {
    pn_kl_level_t lv;
    pn_status_t s = pn_klotski_level(id, &lv);
    if (s != PN_OK) return s;
    pn_kl_reset_board(&g->state, &lv, pn_kl_bank_crc());
    g->has = true;
    g->dirty = true;
    g->changed = now;
    g->running = true;
    g->fresh_clock = true;
    g->last = now;
    g->tier = lv.tier;
    g->selected_level = lv.id;
    g->screen = PN_KL_PLAY;
    g->notice = NULL;
    g->pending_start = false;
    /* 开局/重开立即落盘，避免2秒空闲窗内断电丢局。/ Persist immediately on start/restart so power loss within the idle window keeps the new level. */
    if (save_now(g, now) != PN_OK) g->notice = "保存失败，退出前将重试";
    mark_full(u, g);
    return PN_OK;
}

pn_status_t pn_klotski_open(pn_klotski_t *u, pn_pool_t *pool, pn_media_t *media, const char *root, uint64_t now) {
    if (!u || !pool || !media || !root || !*root || strlen(root) >= PN_JOURNAL_PATH_MAX - 20u) return PN_INVALID;
    if (u->impl) return PN_BUSY;
    uint32_t crc = pn_kl_bank_crc();
    if (!crc) return PN_CORRUPT;
    pn_kl_level_t first;
    pn_status_t s = pn_klotski_level(0, &first);
    if (s != PN_OK) return s;

    kl_t *g = pn_alloc_tagged(pool, PN_MEM_UI, sizeof *g);
    if (!g) return PN_NO_MEMORY;
    memset(g, 0, sizeof *g);
    g->pool = pool;
    g->media = media;
    if (root && strlen(root) < sizeof g->root) strcpy(g->root, root);
    pn_kl_reset_board(&g->state, &first, crc);
    g->screen = PN_KL_SELECT;
    g->tier = 0;
    g->selected_level = 0;
    g->full = true;
    g->last = now;
    g->fresh_clock = true;

    if (media && g->root[0]) {
        bool has = false;
        s = kl_storage(g, &g->state, &has, false);
        if (s == PN_OK) {
            g->has = has;
            g->tier = g->state.level.tier;
            g->selected_level = g->state.level.id;
            if (has && !g->state.solved) {
                g->screen = PN_KL_PLAY;
                g->running = true;
            }
        } else if (s == PN_CORRUPT) {
            g->load_error = true;
            g->screen = PN_KL_ERROR;
            g->notice = "存档已损坏，已保留原文件";
        } else if (s != PN_EMPTY) {
            g->load_error = true;
            g->screen = PN_KL_ERROR;
            g->notice = "存档读取失败，请重试";
        }
    }
    u->impl = g;
    u->active = true;
    u->presented = false;
    return PN_OK;
}

static pn_status_t handle_cell_tap(pn_klotski_t *u, kl_t *g, unsigned cell, uint64_t now) {
    if (cell >= PN_KL_CELLS || g->state.solved) return PN_EMPTY;
    uint8_t occ[PN_KL_CELLS];
    if (!pn_klotski_occupancy(&g->state.level, g->state.pos, occ)) return PN_CORRUPT;
    uint8_t hit = occ[cell];

    /* 点击空白格：若已有选中块且可沿直线滑向该空位，立即滑行 / Tap empty cell: slide selected piece toward it if legal. */
    if (hit == PN_KL_NONE) {
        if (g->state.selected == PN_KL_NONE) return PN_EMPTY;
        unsigned p = g->state.selected;
        unsigned w, h;
        pn_kl_piece_dims(g->state.level.shapes[p], &w, &h);
        int pr = (int)(g->state.pos[p] / PN_KL_COLS), pc = (int)(g->state.pos[p] % PN_KL_COLS);
        int tr = (int)(cell / PN_KL_COLS), tc = (int)(cell % PN_KL_COLS);
        unsigned dir = 4u, steps = 0u;
        if (tc >= pc && tc < pc + (int)w) {
            if (tr < pr) { dir = PN_KL_DIR_UP; steps = (unsigned)(pr - tr); }
            else if (tr >= pr + (int)h) { dir = PN_KL_DIR_DOWN; steps = (unsigned)(tr - (pr + (int)h) + 1); }
        } else if (tr >= pr && tr < pr + (int)h) {
            if (tc < pc) { dir = PN_KL_DIR_LEFT; steps = (unsigned)(pc - tc); }
            else if (tc >= pc + (int)w) { dir = PN_KL_DIR_RIGHT; steps = (unsigned)(tc - (pc + (int)w) + 1); }
        }
        if (dir < 4u && steps >= 1u && steps <= 2u && pn_klotski_can_slide(&g->state, p, dir, steps)) {
            pn_klotski_slide(&g->state, p, dir, steps);
            g->dirty = true;
            g->changed = now;
            g->notice = NULL;
            if (g->state.solved) {
                g->running = false;
                g->screen = PN_KL_COMPLETE;
                if (save_now(g, now) != PN_OK) g->notice = "保存失败，请重试";
                mark_full(u, g);
            } else {
                mark_patch(u, g);
            }
            return PN_OK;
        }
        return PN_EMPTY;
    }

    /* 点击棋块：若仅有一个合法滑行方向则一键直滑；多方向则选中/取消选中 / Tap piece: auto-slide if single direction, else toggle selection. */
    unsigned dir_count = 0, only_dir = 0;
    for (unsigned d = 0; d < 4u; d++) {
        if (pn_klotski_can_slide(&g->state, hit, d, 1u)) {
            dir_count++;
            only_dir = d;
        }
    }
    if (dir_count == 1u) {
        pn_klotski_slide(&g->state, hit, only_dir, 1u);
        g->dirty = true;
        g->changed = now;
        g->notice = NULL;
        if (g->state.solved) {
            g->running = false;
            g->screen = PN_KL_COMPLETE;
            if (save_now(g, now) != PN_OK) g->notice = "保存失败，请重试";
            mark_full(u, g);
        } else {
            mark_patch(u, g);
        }
        return PN_OK;
    }
    if (dir_count > 1u) {
        g->state.selected = (g->state.selected == hit) ? PN_KL_NONE : hit;
        g->state.hint_piece = PN_KL_NONE;
        g->notice = NULL;
        mark_patch(u, g);
        return PN_OK;
    }
    if (g->state.selected != PN_KL_NONE) {
        g->state.selected = PN_KL_NONE;
        mark_patch(u, g);
        return PN_OK;
    }
    return PN_EMPTY;
}

pn_status_t pn_klotski_event(pn_klotski_t *u, int cmd, uint64_t now) {
    if (!u || !u->impl) return PN_INVALID;
    kl_t *g = u->impl;
    tick_clock(g, now);

    if (g->screen == PN_KL_ERROR) {
        if (cmd == PN_KL_BACK || cmd == PN_KL_EXIT) {
            if (g->dirty) {
                pn_status_t s = save_now(g, now);
                if (s != PN_OK) {
                    g->notice = "存储仍不可用，请重试";
                    mark_full(u, g);
                    return s;
                }
            }
            u->active = false;
            return PN_OK;
        }
        if (cmd == PN_KL_RETRY || cmd == PN_KL_START) {
            if (g->load_error) {
                bool has = false;
                pn_status_t s = kl_storage(g, &g->state, &has, false);
                if (s == PN_OK) {
                    g->load_error = false;
                    g->has = has;
                    g->tier = g->state.level.tier;
                    g->selected_level = g->state.level.id;
                    g->screen = (has && !g->state.solved) ? PN_KL_PLAY : PN_KL_SELECT;
                    g->running = (g->screen == PN_KL_PLAY);
                    g->fresh_clock = true;
                    g->notice = NULL;
                    mark_full(u, g);
                    return PN_OK;
                }
                if (s == PN_EMPTY) {
                    g->load_error = false;
                    g->has = false;
                    g->screen = PN_KL_SELECT;
                    g->notice = NULL;
                    mark_full(u, g);
                    return PN_OK;
                }
                g->notice = (s == PN_CORRUPT) ? "存档已损坏，已保留原文件" : "存档读取失败，请重试";
                mark_full(u, g);
                return s;
            }
            pn_status_t s = save_now(g, now);
            if (s != PN_OK) {
                g->notice = "存储仍不可用，请重试";
                mark_full(u, g);
                return s;
            }
            g->screen = g->has && !g->state.solved ? PN_KL_PLAY : PN_KL_SELECT;
            g->notice = NULL;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_KL_CHOOSE && !g->load_error) {
            g->screen = PN_KL_SELECT;
            g->notice = NULL;
            mark_full(u, g);
            return PN_OK;
        }
        return PN_EMPTY;
    }

    if (g->screen == PN_KL_CONFIRM) {
        if (cmd == PN_KL_EXIT) {
            g->running = false;
            g->restart = false;
            pn_status_t s = save_now(g, now);
            if (s != PN_OK) {
                g->screen = PN_KL_ERROR;
                g->notice = "退出前保存失败，请重试";
                mark_full(u, g);
                return s;
            }
            u->active = false;
            return PN_OK;
        }
        if (cmd == PN_KL_CANCEL || cmd == PN_KL_BACK) {
            g->screen = g->has ? PN_KL_PLAY : PN_KL_SELECT;
            g->running = (g->screen == PN_KL_PLAY && !g->state.solved);
            g->fresh_clock = true;
            g->restart = false;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_KL_ACCEPT) {
            unsigned target = g->restart ? g->state.level.id : g->selected_level;
            g->restart = false;
            return start_level(u, g, target, now);
        }
        return PN_EMPTY;
    }

    if (g->screen == PN_KL_SELECT) {
        if (cmd == PN_KL_BACK || cmd == PN_KL_EXIT) {
            pn_status_t s = save_now(g, now);
            if (s != PN_OK) {
                g->screen = PN_KL_ERROR;
                g->notice = "退出前保存失败，请重试";
                mark_full(u, g);
                return s;
            }
            u->active = false;
            return PN_OK;
        }
        if (cmd >= PN_KL_TIER && cmd < PN_KL_TIER + 3) {
            g->tier = (unsigned)(cmd - PN_KL_TIER);
            g->selected_level = g->tier * 20u;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_KL_PREV_LEVEL || cmd == PN_KL_LEFT) {
            unsigned base = g->tier * 20u;
            g->selected_level = (g->selected_level > base) ? g->selected_level - 1u : base + 19u;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_KL_NEXT_LEVEL || cmd == PN_KL_RIGHT) {
            unsigned base = g->tier * 20u;
            g->selected_level = base + ((g->selected_level - base + 1u) % 20u);
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_KL_UP) {
            unsigned base = g->tier * 20u, off = g->selected_level - base;
            g->selected_level = base + (off >= 5u ? off - 5u : off + 15u);
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_KL_DOWN) {
            unsigned base = g->tier * 20u, off = g->selected_level - base;
            g->selected_level = base + ((off + 5u) % 20u);
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd >= PN_KL_CELL && cmd < PN_KL_CELL + 20) {
            g->selected_level = g->tier * 20u + (unsigned)(cmd - PN_KL_CELL);
            if (g->has && !g->state.solved && g->state.level.id == g->selected_level) {
                g->screen = PN_KL_PLAY;
                g->running = true;
                g->fresh_clock = true;
                mark_full(u, g);
                return PN_OK;
            }
            return start_level(u, g, g->selected_level, now);
        }
        if (cmd == PN_KL_START || cmd == PN_KL_ACTIVATE) {
            if (g->has && !g->state.solved && g->state.level.id == g->selected_level) {
                g->screen = PN_KL_PLAY;
                g->running = true;
                g->fresh_clock = true;
                mark_full(u, g);
                return PN_OK;
            }
            return start_level(u, g, g->selected_level, now);
        }
        if (cmd == PN_KL_RESUME && g->has && !g->state.solved) {
            g->screen = PN_KL_PLAY;
            g->running = true;
            g->fresh_clock = true;
            mark_full(u, g);
            return PN_OK;
        }
        return PN_EMPTY;
    }

    if (g->screen == PN_KL_COMPLETE) {
        if (cmd == PN_KL_BACK || cmd == PN_KL_EXIT) {
            pn_status_t s = save_now(g, now);
            if (s != PN_OK) {
                g->screen = PN_KL_ERROR;
                g->notice = "退出前保存失败，请重试";
                mark_full(u, g);
                return s;
            }
            u->active = false;
            return PN_OK;
        }
        if (cmd == PN_KL_CHOOSE) {
            g->screen = PN_KL_SELECT;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_KL_RESTART) return start_level(u, g, g->state.level.id, now);
        if (cmd == PN_KL_NEXT || cmd == PN_KL_ACTIVATE) {
            unsigned nxt = (g->state.level.id + 1u) % PN_KL_LEVELS;
            return start_level(u, g, nxt, now);
        }
        return PN_EMPTY;
    }

    /* PN_KL_PLAY */
    if (cmd == PN_KL_BACK || cmd == PN_KL_EXIT) {
        g->running = false;
        pn_status_t s = save_now(g, now);
        if (s != PN_OK) {
            g->screen = PN_KL_ERROR;
            g->notice = "退出前保存失败，请重试";
            mark_full(u, g);
            return s;
        }
        u->active = false;
        return PN_OK;
    }
    if (cmd == PN_KL_CHOOSE) {
        g->running = false;
        g->notice = (save_now(g, now) != PN_OK) ? "保存失败，请重试" : NULL;
        g->screen = PN_KL_SELECT;
        g->tier = g->state.level.tier;
        g->selected_level = g->state.level.id;
        mark_full(u, g);
        return PN_OK;
    }
    if (cmd == PN_KL_RESTART) {
        if (g->state.moves == 0u) return PN_EMPTY;
        g->running = false;
        g->restart = true;
        g->screen = PN_KL_CONFIRM;
        mark_full(u, g);
        return PN_OK;
    }
    if (cmd == PN_KL_CLEAN) {
        mark_full(u, g);
        return PN_OK;
    }
    if (cmd == PN_KL_UNDO_MOVE) {
        if (pn_klotski_undo(&g->state) != PN_OK) return PN_EMPTY;
        g->dirty = true;
        g->changed = now;
        g->notice = NULL;
        mark_patch(u, g);
        return PN_OK;
    }
    if (cmd == PN_KL_HINT) {
        pn_status_t s = pn_klotski_hint(&g->state);
        if (s == PN_OK) {
            g->dirty = true;
            g->changed = now;
            g->notice = NULL;
        } else {
            g->notice = "已偏离官解，可点撤销回退或重开";
        }
        mark_patch(u, g);
        return PN_OK;
    }
    if (cmd >= PN_KL_SLIDE && cmd < PN_KL_SLIDE + PN_KL_MAX_PIECES * 8) {
        unsigned code = (unsigned)(cmd - PN_KL_SLIDE);
        unsigned piece = code >> 3;
        unsigned dir = (code >> 1) & 3u;
        unsigned steps = (code & 1u) + 1u;
        if (steps == 2u && !pn_klotski_can_slide(&g->state, piece, dir, 2u)) steps = 1u;
        if (pn_klotski_slide(&g->state, piece, dir, steps) != PN_OK) return PN_EMPTY;
        g->dirty = true;
        g->changed = now;
        g->notice = NULL;
        if (g->state.solved) {
            g->running = false;
            g->screen = PN_KL_COMPLETE;
            if (save_now(g, now) != PN_OK) g->notice = "保存失败，请重试";
            mark_full(u, g);
        } else {
            mark_patch(u, g);
        }
        return PN_OK;
    }
    if (cmd >= PN_KL_CELL && cmd < PN_KL_CELL + PN_KL_CELLS) {
        return handle_cell_tap(u, g, (unsigned)(cmd - PN_KL_CELL), now);
    }
    if (cmd == PN_KL_UP || cmd == PN_KL_DOWN || cmd == PN_KL_LEFT || cmd == PN_KL_RIGHT) {
        unsigned dir = cmd == PN_KL_UP ? PN_KL_DIR_UP : cmd == PN_KL_DOWN ? PN_KL_DIR_DOWN
                     : cmd == PN_KL_LEFT ? PN_KL_DIR_LEFT : PN_KL_DIR_RIGHT;
        if (g->state.selected != PN_KL_NONE && pn_klotski_can_slide(&g->state, g->state.selected, dir, 1u)) {
            pn_klotski_slide(&g->state, g->state.selected, dir, 1u);
            g->dirty = true;
            g->changed = now;
            g->notice = NULL;
            if (g->state.solved) {
                g->running = false;
                g->screen = PN_KL_COMPLETE;
                if (save_now(g, now) != PN_OK) g->notice = "保存失败，请重试";
                mark_full(u, g);
            } else {
                mark_patch(u, g);
            }
            return PN_OK;
        }
        return PN_EMPTY;
    }
    return PN_EMPTY;
}

pn_status_t pn_klotski_tick(pn_klotski_t *u, uint64_t now) {
    if (!u || !u->impl) return PN_INVALID;
    kl_t *g = u->impl;
    tick_clock(g, now);
    if (!g->dirty) return PN_EMPTY;
    if (now >= g->changed && now - g->changed < KL_SAVE_IDLE_MS) return PN_EMPTY;
    if (g->retry && now >= g->retry && now - g->retry < KL_SAVE_RETRY_MS) return PN_EMPTY;
    return save_now(g, now);
}

pn_status_t pn_klotski_pause(pn_klotski_t *u, uint64_t now) {
    if (!u || !u->impl) return PN_INVALID;
    kl_t *g = u->impl;
    uint64_t before = g->state.elapsed_ms;
    tick_clock(g, now);
    if (g->has && !g->state.solved && g->state.elapsed_ms != before) g->dirty = true;
    g->running = false;
    g->fresh_clock = true;
    pn_status_t s = save_now(g, now);
    if (s != PN_OK) {
        g->screen = PN_KL_ERROR;
        g->notice = "保存失败，已阻止退出";
        mark_full(u, g);
    }
    return s;
}

void pn_klotski_displayed(pn_klotski_t *u, bool ok) {
    if (!u || !u->impl) return;
    kl_t *g = u->impl;
    bool running = ok && g->screen == PN_KL_PLAY && !g->state.solved;
    if (running && !g->running) g->fresh_clock = true;
    g->running = running;
    u->presented = ok;
    if (ok) {
        if (g->screen == PN_KL_PLAY) snapshot_shown(g);
        g->full = false;
        g->patch = false;
    }
}

void pn_klotski_close(pn_klotski_t *u) {
    if (!u) return;
    if (u->impl) {
        pn_free(u->impl);
        u->impl = NULL;
    }
    u->active = false;
    u->presented = false;
}

const pn_kl_state_t *pn_klotski_state(const pn_klotski_t *u) {
    return (u && u->impl) ? &((const kl_t *)u->impl)->state : NULL;
}

pn_kl_screen_t pn_klotski_screen(const pn_klotski_t *u) {
    return (u && u->impl) ? ((const kl_t *)u->impl)->screen : PN_KL_SELECT;
}
