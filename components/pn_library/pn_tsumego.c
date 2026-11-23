/* SPDX-License-Identifier: Apache-2.0
 * 围棋死活题引擎：完整气/提子/禁自杀/单劫规则、变化树判题与有界A/B事务存档。
 * Go Tsumego engine: full liberties/capture/suicide/Ko rules, variation-tree grading and bounded A/B saves. */
#include "pn_tsumego_internal.h"
#include "pn_crc32.h"
#include <stdio.h>
#include <string.h>

extern const uint8_t pn_tsumego_bank_bytes[];
extern const size_t pn_tsumego_bank_size;

#define TG_BANK_HDR 16u
#define TG_BANK_REC 56u
#define TG_SAVE_MAGIC 0x31475450u /* "PTG1" */
#define TG_SAVE_VER 1u
#define TG_SAVE_IDLE_MS 2000u
#define TG_SAVE_RETRY_MS 3000u

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

const char *pn_tsumego_goal_label(unsigned goal) {
    static const char *const labels[] = {"黑先杀白", "黑先做活", "黑先吃子", "黑先双活", "黑先打劫"};
    return goal < 5u ? labels[goal] : "黑先";
}

/* 迭代DFS统计棋块与其气数（无递归，栈占用固定） / Iterative DFS counting group stones and liberties. */
static unsigned group_info(const uint8_t board[PN_TG_CELLS], unsigned start, uint8_t out_stones[PN_TG_CELLS],
                           unsigned *out_count) {
    uint8_t color = board[start];
    uint8_t visited[PN_TG_CELLS] = {0};
    uint8_t lib_seen[PN_TG_CELLS] = {0};
    uint8_t stack[PN_TG_CELLS];
    unsigned sp = 0, count = 0, libs = 0;
    visited[start] = 1;
    stack[sp++] = (uint8_t)start;
    while (sp > 0u) {
        unsigned cur = stack[--sp];
        if (out_stones) out_stones[count] = (uint8_t)cur;
        count++;
        unsigned r = cur / PN_TG_SIZE, c = cur % PN_TG_SIZE;
        unsigned nbs[4];
        unsigned nc = 0;
        if (r > 0u) nbs[nc++] = cur - PN_TG_SIZE;
        if (r + 1u < PN_TG_SIZE) nbs[nc++] = cur + PN_TG_SIZE;
        if (c > 0u) nbs[nc++] = cur - 1u;
        if (c + 1u < PN_TG_SIZE) nbs[nc++] = cur + 1u;
        for (unsigned i = 0; i < nc; i++) {
            unsigned nb = nbs[i];
            if (board[nb] == PN_TG_EMPTY) {
                if (!lib_seen[nb]) {
                    lib_seen[nb] = 1;
                    libs++;
                }
            } else if (board[nb] == color && !visited[nb]) {
                visited[nb] = 1;
                stack[sp++] = (uint8_t)nb;
            }
        }
    }
    if (out_count) *out_count = count;
    return libs;
}

bool pn_tsumego_apply_move(const uint8_t board[PN_TG_CELLS], const uint8_t *ko_prev, unsigned pos, uint8_t color,
                           uint8_t out_board[PN_TG_CELLS], unsigned *out_captured) {
    if (!board || !out_board || pos >= PN_TG_CELLS || (color != PN_TG_BLACK && color != PN_TG_WHITE)) return false;
    if (board[pos] != PN_TG_EMPTY) return false;
    uint8_t nxt[PN_TG_CELLS];
    memcpy(nxt, board, PN_TG_CELLS);
    nxt[pos] = color;
    uint8_t opp = (color == PN_TG_BLACK) ? PN_TG_WHITE : PN_TG_BLACK;

    unsigned r = pos / PN_TG_SIZE, c = pos % PN_TG_SIZE;
    unsigned nbs[4];
    unsigned nc = 0;
    if (r > 0u) nbs[nc++] = pos - PN_TG_SIZE;
    if (r + 1u < PN_TG_SIZE) nbs[nc++] = pos + PN_TG_SIZE;
    if (c > 0u) nbs[nc++] = pos - 1u;
    if (c + 1u < PN_TG_SIZE) nbs[nc++] = pos + 1u;

    unsigned captured = 0;
    for (unsigned i = 0; i < nc; i++) {
        unsigned nb = nbs[i];
        if (nxt[nb] == opp) {
            uint8_t grp[PN_TG_CELLS];
            unsigned gcnt = 0;
            if (group_info(nxt, nb, grp, &gcnt) == 0u) {
                for (unsigned k = 0; k < gcnt; k++) nxt[grp[k]] = PN_TG_EMPTY;
                captured += gcnt;
            }
        }
    }
    if (group_info(nxt, pos, NULL, NULL) == 0u) return false; /* 禁自杀 / No suicide */
    if (ko_prev && captured == 1u && memcmp(nxt, ko_prev, PN_TG_CELLS) == 0) return false; /* 禁单劫同形 / No simple Ko */
    memcpy(out_board, nxt, PN_TG_CELLS);
    if (out_captured) *out_captured = captured;
    return true;
}

static bool board_has_no_dead_groups(const uint8_t board[PN_TG_CELLS]) {
    uint8_t checked[PN_TG_CELLS] = {0};
    for (unsigned i = 0; i < PN_TG_CELLS; i++) {
        if (board[i] > PN_TG_WHITE) return false;
        if (board[i] != PN_TG_EMPTY && !checked[i]) {
            uint8_t grp[PN_TG_CELLS];
            unsigned cnt = 0;
            if (group_info(board, i, grp, &cnt) == 0u) return false;
            for (unsigned k = 0; k < cnt; k++) checked[grp[k]] = 1;
        }
    }
    return true;
}

static bool bank_valid(uint32_t *out_crc) {
    if (pn_tsumego_bank_size < TG_BANK_HDR + (size_t)PN_TG_LEVELS * TG_BANK_REC) return false;
    const uint8_t *b = pn_tsumego_bank_bytes;
    if (memcmp(b, "PNTG", 4) != 0) return false;
    uint16_t ver = rd16(b + 4), count = rd16(b + 6);
    uint32_t crc = rd32(b + 8), res = rd32(b + 12);
    if (ver != 1u || count != PN_TG_LEVELS || res != 0u) return false;
    if (pn_crc32_update(0u, b + TG_BANK_HDR, pn_tsumego_bank_size - TG_BANK_HDR) != crc) return false;
    if (out_crc) *out_crc = crc;
    return true;
}

uint32_t pn_tg_bank_crc(void) {
    uint32_t crc = 0;
    return bank_valid(&crc) ? crc : 0u;
}

pn_status_t pn_tsumego_level(unsigned id, pn_tg_level_t *out) {
    if (!out || id >= PN_TG_LEVELS) return PN_INVALID;
    if (!bank_valid(NULL)) return PN_CORRUPT;
    const uint8_t *rec = pn_tsumego_bank_bytes + TG_BANK_HDR + (size_t)id * TG_BANK_REC;
    size_t pool_off = TG_BANK_HDR + (size_t)PN_TG_LEVELS * TG_BANK_REC;
    size_t total_nodes = (pn_tsumego_bank_size - pool_off) / 4u;
    const uint8_t *pool = pn_tsumego_bank_bytes + pool_off;

    pn_tg_level_t lv;
    memset(&lv, 0, sizeof lv);
    lv.id = rec[0];
    lv.tier = rec[1];
    lv.goal = rec[2];
    lv.edge_mask = rec[3];
    uint16_t noff = rd16(rec + 4);
    lv.node_count = rec[6];
    lv.main_moves = rec[7];
    if (lv.id != id || lv.tier > 2u || lv.goal > 4u || lv.edge_mask == 0u) return PN_CORRUPT;
    if (lv.node_count < 2u || lv.node_count > PN_TG_MAX_NODES || (size_t)noff + lv.node_count > total_nodes)
        return PN_CORRUPT;
    memcpy(lv.title, rec + 8, 23);
    lv.title[23] = '\0';
    for (unsigned i = 0; i < PN_TG_CELLS; i++) {
        lv.board[i] = (uint8_t)((rec[32u + (i >> 2)] >> ((i & 3u) * 2u)) & 3u);
    }
    if (!board_has_no_dead_groups(lv.board)) return PN_CORRUPT;
    for (unsigned k = 0; k < lv.node_count; k++) {
        const uint8_t *np = pool + ((size_t)noff + k) * 4u;
        lv.nodes[k] = (pn_tg_node_t){np[0], np[1], np[2], np[3]};
    }
    if (lv.nodes[0].flags != PN_TG_NODE_ROOT || lv.nodes[0].parent != PN_TG_NONE) return PN_CORRUPT;
    *out = lv;
    return PN_OK;
}

void pn_tg_reset_board(pn_tg_state_t *s, const pn_tg_level_t *lv, uint32_t bank_crc) {
    uint8_t comp[8];
    memcpy(comp, s->completed, sizeof comp);
    memset(s, 0, sizeof *s);
    s->level = *lv;
    memcpy(s->board, lv->board, PN_TG_CELLS);
    memcpy(s->completed, comp, sizeof comp);
    s->last_black = PN_TG_NONE;
    s->last_white = PN_TG_NONE;
    s->hint_pos = PN_TG_NONE;
    s->cursor = PN_TG_NONE;
    s->bank_crc = bank_crc;
}

pn_status_t pn_tsumego_play(pn_tg_state_t *s, unsigned pos) {
    if (!s || pos >= PN_TG_CELLS || s->outcome != PN_TG_ONGOING) return PN_INVALID;
    uint8_t after_b[PN_TG_CELLS];
    unsigned cap_b = 0;
    if (!pn_tsumego_apply_move(s->board, s->has_ko ? s->ko_board : NULL, pos, PN_TG_BLACK, after_b, &cap_b))
        return PN_INVALID;

    /* 记录悔棋快照 / Push turn undo snapshot */
    if (s->undo_count < PN_TG_UNDO) {
        pn_tg_undo_t *u = &s->undo[s->undo_count++];
        memcpy(u->board, s->board, PN_TG_CELLS);
        memcpy(u->ko_board, s->ko_board, PN_TG_CELLS);
        u->node_idx = s->node_idx;
        u->last_black = s->last_black;
        u->last_white = s->last_white;
        u->has_ko = s->has_ko;
    }

    uint8_t prev_board[PN_TG_CELLS];
    memcpy(prev_board, s->board, PN_TG_CELLS);
    memcpy(s->board, after_b, PN_TG_CELLS);
    s->captured_white = (uint16_t)(s->captured_white + cap_b);
    s->last_black = (uint8_t)pos;
    s->last_white = PN_TG_NONE;
    s->hint_pos = PN_TG_NONE;
    if (s->moves < 65535u) s->moves++;

    /* 在当前变化树节点的直接子节点中查找匹配着法 / Match against direct children of current node. */
    int matched = -1;
    for (unsigned k = 1u; k < s->level.node_count; k++) {
        if (s->level.nodes[k].parent == s->node_idx && s->level.nodes[k].black_pos == pos) {
            matched = (int)k;
            break;
        }
    }

    if (matched >= 0) {
        const pn_tg_node_t *nd = &s->level.nodes[matched];
        if (nd->flags == PN_TG_NODE_CORRECT_LEAF) {
            s->node_idx = (uint8_t)matched;
            s->outcome = PN_TG_SOLVED;
            s->completed[s->level.id >> 3] |= (uint8_t)(1u << (s->level.id & 7u));
            memcpy(s->ko_board, prev_board, PN_TG_CELLS);
            s->has_ko = 1u;
            return PN_OK;
        }
        if (nd->flags == PN_TG_NODE_CORRECT_CONT) {
            uint8_t after_w[PN_TG_CELLS];
            unsigned cap_w = 0;
            if (nd->white_reply < PN_TG_CELLS &&
                pn_tsumego_apply_move(s->board, prev_board, nd->white_reply, PN_TG_WHITE, after_w, &cap_w)) {
                memcpy(s->ko_board, s->board, PN_TG_CELLS);
                s->has_ko = 1u;
                memcpy(s->board, after_w, PN_TG_CELLS);
                s->captured_black = (uint16_t)(s->captured_black + cap_w);
                s->last_white = nd->white_reply;
            }
            s->node_idx = (uint8_t)matched;
            return PN_OK;
        }
        if (nd->flags == PN_TG_NODE_WRONG_REFUTE) {
            uint8_t after_w[PN_TG_CELLS];
            unsigned cap_w = 0;
            if (nd->white_reply < PN_TG_CELLS &&
                pn_tsumego_apply_move(s->board, prev_board, nd->white_reply, PN_TG_WHITE, after_w, &cap_w)) {
                memcpy(s->ko_board, s->board, PN_TG_CELLS);
                s->has_ko = 1u;
                memcpy(s->board, after_w, PN_TG_CELLS);
                s->captured_black = (uint16_t)(s->captured_black + cap_w);
                s->last_white = nd->white_reply;
            }
            s->outcome = PN_TG_FAILED;
            return PN_OK;
        }
    }

    /* 未命中正解分支：白棋执行当前节点默认反击点（若合法）或判落子偏缓 / Off-tree move: play default refutation if legal. */
    uint8_t def_w = (s->node_idx == 0u) ? s->level.nodes[0].white_reply : PN_TG_NONE;
    if (def_w == PN_TG_NONE) {
        /* 用当前节点的正解急所作为白棋反抢要点 / White seizes the vital point if legal */
        for (unsigned k = 1u; k < s->level.node_count; k++) {
            if (s->level.nodes[k].parent == s->node_idx &&
                (s->level.nodes[k].flags == PN_TG_NODE_CORRECT_LEAF ||
                 s->level.nodes[k].flags == PN_TG_NODE_CORRECT_CONT)) {
                def_w = s->level.nodes[k].black_pos;
                break;
            }
        }
    }
    if (def_w < PN_TG_CELLS) {
        uint8_t after_w[PN_TG_CELLS];
        unsigned cap_w = 0;
        if (pn_tsumego_apply_move(s->board, prev_board, def_w, PN_TG_WHITE, after_w, &cap_w)) {
            memcpy(s->ko_board, s->board, PN_TG_CELLS);
            s->has_ko = 1u;
            memcpy(s->board, after_w, PN_TG_CELLS);
            s->captured_black = (uint16_t)(s->captured_black + cap_w);
            s->last_white = def_w;
        }
    }
    s->outcome = PN_TG_FAILED;
    return PN_OK;
}

pn_status_t pn_tsumego_undo(pn_tg_state_t *s) {
    if (!s || s->undo_count == 0u) return PN_EMPTY;
    const pn_tg_undo_t *u = &s->undo[--s->undo_count];
    memcpy(s->board, u->board, PN_TG_CELLS);
    memcpy(s->ko_board, u->ko_board, PN_TG_CELLS);
    s->node_idx = u->node_idx;
    s->last_black = u->last_black;
    s->last_white = u->last_white;
    s->has_ko = u->has_ko;
    s->outcome = PN_TG_ONGOING;
    s->hint_pos = PN_TG_NONE;
    if (s->moves > 0u) s->moves--;
    return PN_OK;
}

pn_status_t pn_tsumego_hint(pn_tg_state_t *s) {
    if (!s || s->outcome != PN_TG_ONGOING) return PN_EMPTY;
    for (unsigned k = 1u; k < s->level.node_count; k++) {
        if (s->level.nodes[k].parent == s->node_idx &&
            (s->level.nodes[k].flags == PN_TG_NODE_CORRECT_LEAF ||
             s->level.nodes[k].flags == PN_TG_NODE_CORRECT_CONT)) {
            s->hint_pos = s->level.nodes[k].black_pos;
            if (s->hints < 65535u) s->hints++;
            return PN_OK;
        }
    }
    return PN_EMPTY;
}

pn_status_t pn_tg_encode_state(const pn_tg_state_t *s, const uint8_t played[PN_TG_UNDO], bool has_saved,
                               uint8_t out[PN_TG_SAVE_BYTES]) {
    if (!s || !played || !out) return PN_INVALID;
    memset(out, 0, PN_TG_SAVE_BYTES);
    wr32(out + 0, TG_SAVE_MAGIC);
    wr16(out + 4, TG_SAVE_VER);
    out[6] = has_saved ? 1u : 0u;
    out[7] = s->level.id;
    out[8] = s->undo_count;
    out[9] = s->outcome;
    wr16(out + 10, s->hints);
    wr32(out + 12, s->bank_crc);
    wr64(out + 16, s->elapsed_ms);
    memcpy(out + 24, s->completed, 8);
    memcpy(out + 32, played, PN_TG_UNDO);
    return PN_OK;
}

pn_status_t pn_tg_decode_state(const uint8_t *in, size_t n, uint32_t current_crc, pn_tg_state_t *out,
                               uint8_t played[PN_TG_UNDO], bool *has_saved) {
    if (!in || !out || !played || !has_saved || n != PN_TG_SAVE_BYTES) return PN_CORRUPT;
    if (rd32(in + 0) != TG_SAVE_MAGIC || rd16(in + 4) != TG_SAVE_VER) return PN_CORRUPT;
    uint8_t has = in[6], id = in[7], ucnt = in[8], outcome = in[9];
    if (has > 1u || id >= PN_TG_LEVELS || ucnt > PN_TG_UNDO || outcome > PN_TG_FAILED) return PN_CORRUPT;
    pn_tg_level_t lv;
    if (pn_tsumego_level(id, &lv) != PN_OK) return PN_CORRUPT;

    pn_tg_state_t st;
    memset(&st, 0, sizeof st);
    pn_tg_reset_board(&st, &lv, current_crc);
    uint32_t saved_crc = rd32(in + 12);
    if (saved_crc == current_crc) memcpy(st.completed, in + 24, 8);
    memset(played, PN_TG_NONE, PN_TG_UNDO);

    if (has) {
        for (unsigned i = 0; i < ucnt; i++) {
            uint8_t mv = in[32u + i];
            if (mv >= PN_TG_CELLS) return PN_CORRUPT;
            if (pn_tsumego_play(&st, mv) != PN_OK) return PN_CORRUPT;
            played[i] = mv;
        }
        if (st.outcome != outcome) return PN_CORRUPT;
        st.hints = rd16(in + 10);
        st.elapsed_ms = rd64(in + 16);
    }
    *out = st;
    *has_saved = (has != 0u);
    return PN_OK;
}

static bool save_paths(const tg_t *g, char a[PN_JOURNAL_PATH_MAX], char b[PN_JOURNAL_PATH_MAX]) {
    if (!g->root[0]) return false;
    int na = snprintf(a, PN_JOURNAL_PATH_MAX, "%s/tsumego.a", g->root);
    int nb = snprintf(b, PN_JOURNAL_PATH_MAX, "%s/tsumego.b", g->root);
    return na > 0 && (size_t)na < PN_JOURNAL_PATH_MAX && nb > 0 && (size_t)nb < PN_JOURNAL_PATH_MAX;
}

static void tick_clock(tg_t *g, uint64_t now) {
    if (g->running && g->state.outcome == PN_TG_ONGOING) {
        if (!g->fresh_clock && now > g->last) g->state.elapsed_ms += now - g->last;
        g->last = now;
        g->fresh_clock = false;
    }
}

static pn_status_t tg_storage(tg_t *g, pn_tg_state_t *s, uint8_t played[PN_TG_UNDO], bool *has_saved, bool write) {
    char a[PN_JOURNAL_PATH_MAX], b[PN_JOURNAL_PATH_MAX];
    if (!save_paths(g, a, b)) return PN_IO;
    uint32_t crc = pn_tg_bank_crc();
    if (!crc) return PN_CORRUPT;
    uint8_t *buf = pn_alloc_tagged(g->pool, PN_MEM_UI, PN_TG_SAVE_BYTES);
    if (!buf) return PN_NO_MEMORY;
    pn_media_lease_t lease = {0};
    pn_journal_files_t files;
    pn_journal_io_t io;
    pn_status_t r = pn_media_acquire(g->media, write ? PN_MEDIA_WRITE : PN_MEDIA_READ, &lease);
    if (r == PN_OK) r = pn_journal_files_init(&files, g->media, &lease, a, b, &io);
    if (r == PN_OK) {
        if (write) {
            r = pn_tg_encode_state(s, played, g->has, buf);
            if (r == PN_OK) r = pn_blob_save_bounded(&io, g->pool, buf, PN_TG_SAVE_BYTES, PN_TG_SAVE_BYTES);
        } else {
            size_t got = 0;
            r = pn_blob_load_bounded(&io, g->pool, buf, PN_TG_SAVE_BYTES, &got);
            if (r == PN_OK) r = pn_tg_decode_state(buf, got, crc, s, played, has_saved);
        }
    }
    if (lease.ticket) (void)pn_media_release(g->media, &lease);
    pn_free(buf);
    return r;
}

static pn_status_t save_now(tg_t *g, uint64_t now) {
    if (!g->dirty) return PN_OK;
    if (!g->media || !g->root[0]) {
        g->dirty = false;
        return PN_OK;
    }
    pn_status_t s = tg_storage(g, &g->state, g->played_moves, NULL, true);
    if (s == PN_OK) g->dirty = false;
    else g->retry = now;
    return s;
}

static void mark_full(pn_tsumego_t *u, tg_t *g) {
    g->full = true;
    g->patch = false;
    u->presented = false;
}

static void mark_patch(pn_tsumego_t *u, tg_t *g) {
    if (!g->full) g->patch = true;
    u->presented = false;
}

static void snapshot_shown(tg_t *g) {
    memcpy(g->shown_board, g->state.board, PN_TG_CELLS);
    g->shown_last_black = g->state.last_black;
    g->shown_last_white = g->state.last_white;
    g->shown_hint = g->state.hint_pos;
    g->shown_cursor = g->state.cursor;
}

static pn_status_t start_level(pn_tsumego_t *u, tg_t *g, unsigned id, uint64_t now) {
    pn_tg_level_t lv;
    pn_status_t s = pn_tsumego_level(id, &lv);
    if (s != PN_OK) return s;
    pn_tg_reset_board(&g->state, &lv, pn_tg_bank_crc());
    memset(g->played_moves, PN_TG_NONE, sizeof g->played_moves);
    g->has = true;
    g->dirty = true;
    g->changed = now;
    g->running = true;
    g->fresh_clock = true;
    g->last = now;
    g->tier = lv.tier;
    g->selected_level = lv.id;
    g->screen = PN_TG_PLAY;
    g->notice = NULL;
    g->pending_start = false;
    /* 开题/重开立即落盘，避免2秒空闲窗内断电丢局。/ Persist immediately on start/restart so power loss within the idle window keeps the new puzzle. */
    if (save_now(g, now) != PN_OK) g->notice = "保存失败，退出前将重试";
    mark_full(u, g);
    return PN_OK;
}

pn_status_t pn_tsumego_open(pn_tsumego_t *u, pn_pool_t *pool, pn_media_t *media, const char *root, uint64_t now) {
    if (!u || !pool || !media || !root || !*root || strlen(root) >= PN_JOURNAL_PATH_MAX - 20u) return PN_INVALID;
    if (u->impl) return PN_BUSY;
    uint32_t crc = pn_tg_bank_crc();
    if (!crc) return PN_CORRUPT;
    pn_tg_level_t first;
    pn_status_t s = pn_tsumego_level(0, &first);
    if (s != PN_OK) return s;

    tg_t *g = pn_alloc_tagged(pool, PN_MEM_UI, sizeof *g);
    if (!g) return PN_NO_MEMORY;
    memset(g, 0, sizeof *g);
    g->pool = pool;
    g->media = media;
    if (root && strlen(root) < sizeof g->root) strcpy(g->root, root);
    pn_tg_reset_board(&g->state, &first, crc);
    memset(g->played_moves, PN_TG_NONE, sizeof g->played_moves);
    g->screen = PN_TG_SELECT;
    g->full = true;
    g->last = now;
    g->fresh_clock = true;

    if (media && g->root[0]) {
        bool has = false;
        s = tg_storage(g, &g->state, g->played_moves, &has, false);
        if (s == PN_OK) {
            g->has = has;
            g->tier = g->state.level.tier;
            g->selected_level = g->state.level.id;
            if (has && g->state.outcome != PN_TG_SOLVED) {
                g->screen = PN_TG_PLAY;
                g->running = (g->state.outcome == PN_TG_ONGOING);
            }
        } else if (s == PN_CORRUPT) {
            g->load_error = true;
            g->screen = PN_TG_ERROR;
            g->notice = "存档已损坏，已保留原文件";
        } else if (s != PN_EMPTY) {
            g->load_error = true;
            g->screen = PN_TG_ERROR;
            g->notice = "存档读取失败，请重试";
        }
    }
    u->impl = g;
    u->active = true;
    u->presented = false;
    return PN_OK;
}

pn_status_t pn_tsumego_event(pn_tsumego_t *u, int cmd, uint64_t now) {
    if (!u || !u->impl) return PN_INVALID;
    tg_t *g = u->impl;
    tick_clock(g, now);

    if (g->screen == PN_TG_ERROR) {
        if (cmd == PN_TG_BACK || cmd == PN_TG_EXIT) {
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
        if (cmd == PN_TG_RETRY || cmd == PN_TG_START) {
            if (g->load_error) {
                bool has = false;
                pn_status_t s = tg_storage(g, &g->state, g->played_moves, &has, false);
                if (s == PN_OK) {
                    g->load_error = false;
                    g->has = has;
                    g->tier = g->state.level.tier;
                    g->selected_level = g->state.level.id;
                    g->screen = (has && g->state.outcome != PN_TG_SOLVED) ? PN_TG_PLAY : PN_TG_SELECT;
                    g->running = (g->screen == PN_TG_PLAY && g->state.outcome == PN_TG_ONGOING);
                    g->fresh_clock = true;
                    g->notice = NULL;
                    mark_full(u, g);
                    return PN_OK;
                }
                if (s == PN_EMPTY) {
                    g->load_error = false;
                    g->has = false;
                    g->screen = PN_TG_SELECT;
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
            g->screen = (g->has && g->state.outcome != PN_TG_SOLVED) ? PN_TG_PLAY : PN_TG_SELECT;
            g->notice = NULL;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_TG_CHOOSE && !g->load_error) {
            g->screen = PN_TG_SELECT;
            g->notice = NULL;
            mark_full(u, g);
            return PN_OK;
        }
        return PN_EMPTY;
    }

    if (g->screen == PN_TG_CONFIRM) {
        if (cmd == PN_TG_EXIT) {
            g->running = false;
            g->restart = false;
            pn_status_t s = save_now(g, now);
            if (s != PN_OK) {
                g->screen = PN_TG_ERROR;
                g->notice = "退出前保存失败，请重试";
                mark_full(u, g);
                return s;
            }
            u->active = false;
            return PN_OK;
        }
        if (cmd == PN_TG_CANCEL || cmd == PN_TG_BACK) {
            g->screen = g->has ? PN_TG_PLAY : PN_TG_SELECT;
            g->running = (g->screen == PN_TG_PLAY && g->state.outcome == PN_TG_ONGOING);
            g->fresh_clock = true;
            g->restart = false;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_TG_ACCEPT) {
            unsigned target = g->restart ? g->state.level.id : g->selected_level;
            g->restart = false;
            return start_level(u, g, target, now);
        }
        return PN_EMPTY;
    }

    if (g->screen == PN_TG_SELECT) {
        if (cmd == PN_TG_BACK || cmd == PN_TG_EXIT) {
            pn_status_t s = save_now(g, now);
            if (s != PN_OK) {
                g->screen = PN_TG_ERROR;
                g->notice = "退出前保存失败，请重试";
                mark_full(u, g);
                return s;
            }
            u->active = false;
            return PN_OK;
        }
        if (cmd >= PN_TG_TIER && cmd < PN_TG_TIER + 3) {
            g->tier = (unsigned)(cmd - PN_TG_TIER);
            g->selected_level = g->tier * 20u;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_TG_PREV_LEVEL || cmd == PN_TG_LEFT) {
            unsigned base = g->tier * 20u;
            g->selected_level = (g->selected_level > base) ? g->selected_level - 1u : base + 19u;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_TG_NEXT_LEVEL || cmd == PN_TG_RIGHT) {
            unsigned base = g->tier * 20u;
            g->selected_level = base + ((g->selected_level - base + 1u) % 20u);
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_TG_UP) {
            unsigned base = g->tier * 20u, off = g->selected_level - base;
            g->selected_level = base + (off >= 5u ? off - 5u : off + 15u);
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_TG_DOWN) {
            unsigned base = g->tier * 20u, off = g->selected_level - base;
            g->selected_level = base + ((off + 5u) % 20u);
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd >= PN_TG_CELL && cmd < PN_TG_CELL + 20) {
            g->selected_level = g->tier * 20u + (unsigned)(cmd - PN_TG_CELL);
            if (g->has && g->state.outcome != PN_TG_SOLVED && g->state.level.id == g->selected_level) {
                g->screen = PN_TG_PLAY;
                g->running = (g->state.outcome == PN_TG_ONGOING);
                g->fresh_clock = true;
                mark_full(u, g);
                return PN_OK;
            }
            return start_level(u, g, g->selected_level, now);
        }
        if (cmd == PN_TG_START || cmd == PN_TG_ACTIVATE) {
            if (g->has && g->state.outcome != PN_TG_SOLVED && g->state.level.id == g->selected_level) {
                g->screen = PN_TG_PLAY;
                g->running = (g->state.outcome == PN_TG_ONGOING);
                g->fresh_clock = true;
                mark_full(u, g);
                return PN_OK;
            }
            return start_level(u, g, g->selected_level, now);
        }
        if (cmd == PN_TG_RESUME && g->has && g->state.outcome != PN_TG_SOLVED) {
            g->screen = PN_TG_PLAY;
            g->running = (g->state.outcome == PN_TG_ONGOING);
            g->fresh_clock = true;
            mark_full(u, g);
            return PN_OK;
        }
        return PN_EMPTY;
    }

    if (g->screen == PN_TG_COMPLETE) {
        if (cmd == PN_TG_BACK || cmd == PN_TG_EXIT) {
            pn_status_t s = save_now(g, now);
            if (s != PN_OK) {
                g->screen = PN_TG_ERROR;
                g->notice = "退出前保存失败，请重试";
                mark_full(u, g);
                return s;
            }
            u->active = false;
            return PN_OK;
        }
        if (cmd == PN_TG_CHOOSE) {
            g->screen = PN_TG_SELECT;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_TG_RESTART) return start_level(u, g, g->state.level.id, now);
        if (cmd == PN_TG_NEXT || cmd == PN_TG_ACTIVATE) {
            unsigned nxt = (g->state.level.id + 1u) % PN_TG_LEVELS;
            return start_level(u, g, nxt, now);
        }
        return PN_EMPTY;
    }

    /* PN_TG_PLAY */
    if (cmd == PN_TG_BACK || cmd == PN_TG_EXIT) {
        g->running = false;
        pn_status_t s = save_now(g, now);
        if (s != PN_OK) {
            g->screen = PN_TG_ERROR;
            g->notice = "退出前保存失败，请重试";
            mark_full(u, g);
            return s;
        }
        u->active = false;
        return PN_OK;
    }
    if (cmd == PN_TG_CHOOSE) {
        g->running = false;
        g->notice = (save_now(g, now) != PN_OK) ? "保存失败，请重试" : NULL;
        g->screen = PN_TG_SELECT;
        g->tier = g->state.level.tier;
        g->selected_level = g->state.level.id;
        mark_full(u, g);
        return PN_OK;
    }
    if (cmd == PN_TG_RESTART) {
        if (g->state.moves == 0u) return PN_EMPTY;
        g->running = false;
        g->restart = true;
        g->screen = PN_TG_CONFIRM;
        mark_full(u, g);
        return PN_OK;
    }
    if (cmd == PN_TG_CLEAN) {
        mark_full(u, g);
        return PN_OK;
    }
    if (cmd == PN_TG_UNDO_MOVE) {
        if (pn_tsumego_undo(&g->state) != PN_OK) return PN_EMPTY;
        if (g->state.undo_count < PN_TG_UNDO) g->played_moves[g->state.undo_count] = PN_TG_NONE;
        g->running = true;
        g->fresh_clock = true;
        g->dirty = true;
        g->changed = now;
        g->notice = NULL;
        mark_patch(u, g);
        return PN_OK;
    }
    if (cmd == PN_TG_HINT) {
        if (g->state.outcome == PN_TG_FAILED) {
            g->notice = "请先点「悔棋」退回后再看提示";
            mark_patch(u, g);
            return PN_OK;
        }
        if (pn_tsumego_hint(&g->state) != PN_OK) return PN_EMPTY;
        g->dirty = true;
        g->changed = now;
        g->notice = NULL;
        mark_patch(u, g);
        return PN_OK;
    }
    if (cmd == PN_TG_UP || cmd == PN_TG_DOWN || cmd == PN_TG_LEFT || cmd == PN_TG_RIGHT) {
        unsigned cur = (g->state.cursor < PN_TG_CELLS) ? g->state.cursor : 40u;
        unsigned r = cur / PN_TG_SIZE, c = cur % PN_TG_SIZE;
        if (cmd == PN_TG_UP) r = (r + PN_TG_SIZE - 1u) % PN_TG_SIZE;
        else if (cmd == PN_TG_DOWN) r = (r + 1u) % PN_TG_SIZE;
        else if (cmd == PN_TG_LEFT) c = (c + PN_TG_SIZE - 1u) % PN_TG_SIZE;
        else c = (c + 1u) % PN_TG_SIZE;
        g->state.cursor = (uint8_t)(r * PN_TG_SIZE + c);
        mark_patch(u, g);
        return PN_OK;
    }
    if (cmd == PN_TG_ACTIVATE) {
        if (g->state.cursor < PN_TG_CELLS) cmd = PN_TG_CELL + (int)g->state.cursor;
        else return PN_EMPTY;
    }
    if (cmd >= PN_TG_CELL && cmd < PN_TG_CELL + PN_TG_CELLS) {
        unsigned pos = (unsigned)(cmd - PN_TG_CELL);
        if (g->state.outcome == PN_TG_FAILED) {
            g->notice = "落子失误，请点「悔棋」或「重开」";
            mark_patch(u, g);
            return PN_OK;
        }
        unsigned idx_before = g->state.undo_count;
        pn_status_t s = pn_tsumego_play(&g->state, pos);
        if (s != PN_OK) {
            g->notice = "禁着点（非空/禁自杀/打劫）";
            mark_patch(u, g);
            return PN_OK;
        }
        if (idx_before < PN_TG_UNDO) g->played_moves[idx_before] = (uint8_t)pos;
        g->dirty = true;
        g->changed = now;
        g->notice = NULL;
        if (g->state.outcome == PN_TG_SOLVED) {
            g->running = false;
            g->screen = PN_TG_COMPLETE;
            if (save_now(g, now) != PN_OK) g->notice = "保存失败，请重试";
            mark_full(u, g);
        } else {
            if (g->state.outcome == PN_TG_FAILED) g->running = false;
            mark_patch(u, g);
        }
        return PN_OK;
    }
    return PN_EMPTY;
}

pn_status_t pn_tsumego_tick(pn_tsumego_t *u, uint64_t now) {
    if (!u || !u->impl) return PN_INVALID;
    tg_t *g = u->impl;
    tick_clock(g, now);
    if (!g->dirty) return PN_EMPTY;
    if (now >= g->changed && now - g->changed < TG_SAVE_IDLE_MS) return PN_EMPTY;
    if (g->retry && now >= g->retry && now - g->retry < TG_SAVE_RETRY_MS) return PN_EMPTY;
    return save_now(g, now);
}

pn_status_t pn_tsumego_pause(pn_tsumego_t *u, uint64_t now) {
    if (!u || !u->impl) return PN_INVALID;
    tg_t *g = u->impl;
    uint64_t before = g->state.elapsed_ms;
    tick_clock(g, now);
    if (g->has && g->state.outcome == PN_TG_ONGOING && g->state.elapsed_ms != before) g->dirty = true;
    g->running = false;
    g->fresh_clock = true;
    pn_status_t s = save_now(g, now);
    if (s != PN_OK) {
        g->screen = PN_TG_ERROR;
        g->notice = "保存失败，已阻止退出";
        mark_full(u, g);
    }
    return s;
}

void pn_tsumego_displayed(pn_tsumego_t *u, bool ok) {
    if (!u || !u->impl) return;
    tg_t *g = u->impl;
    bool running = ok && g->screen == PN_TG_PLAY && g->state.outcome == PN_TG_ONGOING;
    if (running && !g->running) g->fresh_clock = true;
    g->running = running;
    u->presented = ok;
    if (ok) {
        if (g->screen == PN_TG_PLAY) snapshot_shown(g);
        g->full = false;
        g->patch = false;
    }
}

void pn_tsumego_close(pn_tsumego_t *u) {
    if (!u) return;
    if (u->impl) {
        pn_free(u->impl);
        u->impl = NULL;
    }
    u->active = false;
    u->presented = false;
}

const pn_tg_state_t *pn_tsumego_state(const pn_tsumego_t *u) {
    return (u && u->impl) ? &((const tg_t *)u->impl)->state : NULL;
}

pn_tg_screen_t pn_tsumego_screen(const pn_tsumego_t *u) {
    return (u && u->impl) ? ((const tg_t *)u->impl)->screen : PN_TG_SELECT;
}
