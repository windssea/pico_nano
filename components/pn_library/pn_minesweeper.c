/* SPDX-License-Identifier: Apache-2.0
 * 确定性首步安全扫雷与小型事务。/ Deterministic first-move-safe Minesweeper and small transactions. */
#include "pn_minesweeper_internal.h"
#include <stdio.h>
#include <string.h>
#define MI_SAVE 160u
static const char save_failure[] = "保存失败，请重试";
static const uint8_t widths[] = {9, 12, 14}, heights[] = {9, 16, 18}, mine_counts[] = {10, 40, 65};
static uint32_t rng(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static unsigned total(const pn_mi_state_t *s) { return (unsigned)s->width * s->height; }
/// 公共纯规则也拒绝零对象和伪造几何，不能先除宽度或访问位图。/ Public pure rules reject zero objects and forged geometry before division or bitmap access.
static bool shape(const pn_mi_state_t *s) {
    return s && s->tier < 3 && s->width == widths[s->tier] && s->height == heights[s->tier] &&
           s->mine_count == mine_counts[s->tier] && s->seed && (unsigned)s->result <= PN_MI_LOST;
}
static bool near(const pn_mi_state_t *s, unsigned a, unsigned b) {
    int dx = (int)(a % s->width) - (int)(b % s->width), dy = (int)(a / s->width) - (int)(b / s->width);
    return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
}
static unsigned bits(const uint8_t *p, unsigned n) { unsigned count = 0; for (unsigned i = 0; i < n; i++) count += mi_bit(p, i); return count; }
unsigned pn_minesweeper_opened(const pn_mi_state_t *s) { return shape(s) ? bits(s->opened, total(s)) : 0; }
unsigned pn_minesweeper_flags(const pn_mi_state_t *s) { return shape(s) ? bits(s->flags, total(s)) : 0; }
unsigned pn_minesweeper_adjacent(const pn_mi_state_t *s, unsigned c) {
    if (!shape(s) || c >= total(s)) return 0;
    unsigned count = 0;
    int x = (int)(c % s->width), y = (int)(c / s->width);
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int a = x + dx, b = y + dy;
        if ((dx || dy) && a >= 0 && b >= 0 && a < s->width && b < s->height)
            count += mi_bit(s->mines, (unsigned)b * s->width + (unsigned)a);
    }
    return count;
}
pn_status_t pn_minesweeper_new(pn_mi_state_t *s, unsigned tier, uint32_t seed) {
    if (!s || tier > 2) return PN_INVALID;
    memset(s, 0, sizeof *s);
    s->width = widths[tier]; s->height = heights[tier]; s->tier = (uint8_t)tier; s->mine_count = mine_counts[tier];
    s->seed = seed ? seed : 0x6d2b79f5u; s->first = s->exploded = PN_MI_CELLS; s->result = PN_MI_WAITING;
    return PN_OK;
}
/// 无重试的候选洗牌，每格至多访问一次；不依赖全局随机状态。/ Candidate shuffle without retries; visit each cell at most once, with no global RNG.
static void place(pn_mi_state_t *s, unsigned first) {
    uint8_t cells[PN_MI_CELLS]; unsigned n = 0;
    for (unsigned c = 0; c < total(s); c++) if (!near(s, first, c)) cells[n++] = (uint8_t)c;
    uint32_t seed = s->seed;
    for (unsigned i = 0; i < s->mine_count; i++) {
        unsigned j = i + rng(&seed) % (n - i); uint8_t c = cells[j]; cells[j] = cells[i]; cells[i] = c; mi_set(s->mines, c, true);
    }
    s->first = (uint16_t)first; s->result = PN_MI_RUNNING;
}
static void won(pn_mi_state_t *s) {
    if (s->result == PN_MI_RUNNING && pn_minesweeper_opened(s) == total(s) - s->mine_count) s->result = PN_MI_WON;
}
pn_status_t pn_minesweeper_flag(pn_mi_state_t *s, unsigned c) {
    if (!shape(s) || c >= total(s)) return PN_INVALID;
    if (s->result >= PN_MI_WON || mi_bit(s->opened, c)) return PN_EMPTY;
    if (!mi_bit(s->flags, c) && pn_minesweeper_flags(s) >= s->mine_count) return PN_LIMIT;
    mi_set(s->flags, c, !mi_bit(s->flags, c)); s->selected = (uint16_t)c; return PN_OK;
}
pn_status_t pn_minesweeper_reveal(pn_mi_state_t *s, unsigned c) {
    if (!shape(s) || c >= total(s)) return PN_INVALID;
    if (s->result >= PN_MI_WON || mi_bit(s->flags, c) || mi_bit(s->opened, c)) return PN_EMPTY;
    if (s->result == PN_MI_WAITING) place(s, c);
    s->selected = (uint16_t)c;
    if (mi_bit(s->mines, c)) { s->exploded = (uint16_t)c; s->result = PN_MI_LOST; return PN_OK; }
    uint8_t queue[PN_MI_CELLS]; unsigned head = 0, tail = 0;
    mi_set(s->opened, c, true); queue[tail++] = (uint8_t)c;
    while (head < tail) {
        unsigned at = queue[head++];
        if (pn_minesweeper_adjacent(s, at)) continue;
        int x = (int)(at % s->width), y = (int)(at / s->width);
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int a = x + dx, b = y + dy;
            if (a < 0 || b < 0 || a >= s->width || b >= s->height) continue;
            unsigned n = (unsigned)b * s->width + (unsigned)a;
            if (!mi_bit(s->opened, n) && !mi_bit(s->flags, n) && !mi_bit(s->mines, n)) {
                mi_set(s->opened, n, true); queue[tail++] = (uint8_t)n;
            }
        }
    }
    won(s); return PN_OK;
}
pn_status_t pn_minesweeper_chord(pn_mi_state_t *s, unsigned c) {
    if (!shape(s) || c >= total(s)) return PN_INVALID;
    if (s->result != PN_MI_RUNNING || !mi_bit(s->opened, c)) return PN_EMPTY;
    unsigned count = 0, number = pn_minesweeper_adjacent(s, c);
    for (unsigned n = 0; n < total(s); n++) if (near(s, c, n) && mi_bit(s->flags, n)) count++;
    if (!number || count != number) return PN_EMPTY;
    bool changed = false;
    for (unsigned n = 0; n < total(s) && s->result == PN_MI_RUNNING; n++) if (near(s, c, n)) {
        pn_status_t r = pn_minesweeper_reveal(s, n); if (r == PN_OK) changed = true;
    }
    if (changed) s->selected = (uint16_t)c;
    return changed ? PN_OK : PN_EMPTY;
}
static void put(uint8_t *p, uint64_t v, unsigned n) { for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(v >> (i * 8)); }
static uint64_t get(const uint8_t *p, unsigned n) { uint64_t v = 0; for (unsigned i = 0; i < n; i++) v |= (uint64_t)p[i] << (i * 8); return v; }
static bool valid(const pn_mi_state_t *s) {
    if (!shape(s) || s->selected >= total(s) || s->elapsed_ms > UINT64_MAX - 86400000u) return false;
    unsigned opened = 0, mines = 0, flags = 0;
    for (unsigned c = 0; c < 256; c++) {
        unsigned mask = 1u << (c % 8); bool m = (s->mines[c / 8] & mask) != 0, o = (s->opened[c / 8] & mask) != 0, f = (s->flags[c / 8] & mask) != 0;
        if ((c >= total(s) && (m || o || f)) || (o && (m || f))) return false;
        opened += o; mines += m; flags += f;
    }
    if (flags > s->mine_count) return false;
    if (s->result == PN_MI_WAITING) return !opened && !mines && s->first == PN_MI_CELLS && s->exploded == PN_MI_CELLS && !s->elapsed_ms;
    if (s->first >= total(s) || mines != s->mine_count || !mi_bit(s->opened, s->first)) return false;
    pn_mi_state_t expected; (void)pn_minesweeper_new(&expected, s->tier, s->seed); place(&expected, s->first);
    if (memcmp(s->mines, expected.mines, PN_MI_BITS)) return false;
    if (s->result == PN_MI_LOST) return opened < total(s) - s->mine_count && s->exploded < total(s) && mi_bit(s->mines, s->exploded) && !mi_bit(s->flags, s->exploded);
    return s->exploded == PN_MI_CELLS && ((s->result == PN_MI_WON) == (opened == total(s) - s->mine_count));
}
static void encode(const pn_mi_state_t *s, uint8_t *b) {
    memset(b, 0, MI_SAVE); memcpy(b, "PNMV", 4); b[4] = 1; b[8] = s->tier; b[9] = (uint8_t)s->result; b[10] = s->flag_mode;
    put(b + 12, s->seed, 4); put(b + 16, s->elapsed_ms, 8); put(b + 24, s->selected, 2); put(b + 26, s->first, 2); put(b + 28, s->exploded, 2);
    memcpy(b + 32, s->mines, 32); memcpy(b + 64, s->opened, 32); memcpy(b + 96, s->flags, 32);
}
static pn_status_t decode(pn_mi_state_t *s, const uint8_t *b, size_t n) {
    if (n != MI_SAVE || memcmp(b, "PNMV", 4) || b[4] != 1 || b[9] > PN_MI_LOST || b[10] > 1 || !get(b + 12, 4)) return PN_CORRUPT;
    for (unsigned i = 5; i < MI_SAVE; i++) if ((i < 8 || i == 11 || (i >= 30 && i < 32) || i >= 128) && b[i]) return PN_CORRUPT;
    if (pn_minesweeper_new(s, b[8], (uint32_t)get(b + 12, 4)) != PN_OK) return PN_CORRUPT;
    s->result = (pn_mi_result_t)b[9]; s->flag_mode = b[10] != 0; s->elapsed_ms = get(b + 16, 8); s->selected = (uint16_t)get(b + 24, 2); s->first = (uint16_t)get(b + 26, 2); s->exploded = (uint16_t)get(b + 28, 2);
    memcpy(s->mines, b + 32, 32); memcpy(s->opened, b + 64, 32); memcpy(s->flags, b + 96, 32);
    return valid(s) ? PN_OK : PN_CORRUPT;
}
static pn_status_t storage(mi_t *g, pn_mi_state_t *s, bool write) {
    pn_media_lease_t lease = {0}; pn_journal_files_t files; pn_journal_io_t io;
    char a[PN_JOURNAL_PATH_MAX], b[PN_JOURNAL_PATH_MAX];
    int n = snprintf(a, sizeof a, "%s/minesweeper.a", g->root), m = snprintf(b, sizeof b, "%s/minesweeper.b", g->root);
    if (n < 0 || m < 0 || n >= (int)sizeof a || m >= (int)sizeof b) return PN_LIMIT;
    pn_status_t r = pn_media_acquire(g->media, write ? PN_MEDIA_WRITE : PN_MEDIA_READ, &lease);
    if (r == PN_OK) r = pn_journal_files_init(&files, g->media, &lease, a, b, &io);
    if (r == PN_OK) {
        if (write) {
            // 写前复核当前载荷，未知未来版本和语义损坏都不能被新局覆盖。/ Recheck current payload before writing; never overwrite future versions or semantic corruption with a new game.
            pn_record_t record; pn_mi_state_t previous; r = pn_journal_load(&io, &record);
            if (r == PN_OK) r = decode(&previous, record.payload, record.size);
            else if (r == PN_EMPTY) r = PN_OK;
            if (r == PN_OK) { uint8_t data[MI_SAVE]; encode(s, data); r = pn_journal_save(&io, data, MI_SAVE); }
        }
        else { pn_record_t record; r = pn_journal_load(&io, &record); if (r == PN_OK) r = decode(s, record.payload, record.size); }
    }
    if (lease.ticket) (void)pn_media_release(g->media, &lease);
    return r;
}
static void time_update(mi_t *g, uint64_t now) {
    // 恢复后的首轮只重设时基，不把锁屏或未呈现等待加入游戏用时。/ First resumed step only resets the clock, excluding lock and unpresented waits.
    if (g->fresh_clock) { g->last = now; g->fresh_clock = false; return; }
    if (g->running && now >= g->last && g->state.result == PN_MI_RUNNING) {
        uint64_t delta = now - g->last;
        g->state.elapsed_ms = delta > UINT64_MAX - 86400000u - g->state.elapsed_ms ? UINT64_MAX - 86400000u : g->state.elapsed_ms + delta;
    }
    g->last = now;
}
static pn_status_t save(pn_minesweeper_t *u, uint64_t now) {
    mi_t *g = u->impl;
    if (!g->has || !g->dirty) return PN_OK;
    pn_status_t r = storage(g, &g->state, true); g->retry = r == PN_OK ? 0 : now + 3000;
    const char *old = g->notice; g->notice = r == PN_OK ? NULL : save_failure;
    if (r == PN_OK) g->dirty = false;
    if (old != g->notice) u->presented = false;
    return r;
}
pn_status_t pn_minesweeper_open(pn_minesweeper_t *u, pn_pool_t *pool, pn_media_t *media, const char *root, uint64_t now) {
    if (!u || u->impl || !pool || !media || !root || !*root || strlen(root) >= PN_JOURNAL_PATH_MAX - 20) return PN_INVALID;
    mi_t *g = pn_alloc_tagged(pool, PN_MEM_UI, sizeof *g); if (!g) return PN_NO_MEMORY;
    memset(g, 0, sizeof *g); g->pool = pool; g->media = media; strcpy(g->root, root); g->last = now; g->next_seed = (uint32_t)now ^ 0xa5f1523du; if (!g->next_seed) g->next_seed = 1;
    u->impl = g; u->active = true; u->presented = false;
    pn_status_t r = storage(g, &g->candidate, false);
    if (r == PN_OK) { g->state = g->candidate; g->has = true; g->tier = g->state.tier; g->next_seed ^= g->state.seed; if (!g->next_seed) g->next_seed = 1; g->screen = g->state.result >= PN_MI_WON ? PN_MI_COMPLETE : PN_MI_PLAY; }
    else if (r == PN_EMPTY) g->screen = PN_MI_SELECT;
    else { g->screen = PN_MI_ERROR; g->notice = "棋局读取失败，原记录保留"; }
    return PN_OK;
}
pn_status_t pn_minesweeper_pause(pn_minesweeper_t *u, uint64_t now) {
    if (!u || !u->impl) return PN_OK;
    mi_t *g = u->impl;
    if (g->pending_start) return PN_BUSY;
    time_update(g, now); if (g->has && g->state.result == PN_MI_RUNNING) g->dirty = true;
    pn_status_t r = save(u, now); if (r == PN_OK) { g->running = false; u->presented = false; } return r;
}
pn_status_t pn_minesweeper_tick(pn_minesweeper_t *u, uint64_t now) {
    if (!u || !u->impl) return PN_EMPTY;
    mi_t *g = u->impl; time_update(g, now);
    return g->dirty && u->presented && now >= g->changed && now - g->changed >= 1500 && now >= g->retry ? save(u, now) : PN_EMPTY;
}
static pn_status_t commit(pn_minesweeper_t *u) {
    mi_t *g = u->impl; pn_status_t r = storage(g, &g->candidate, true);
    if (r != PN_OK) { g->pending_start = true; g->screen = PN_MI_ERROR; g->notice = save_failure; u->presented = false; return r; }
    g->state = g->candidate; g->has = true; g->dirty = false; g->pending_start = false; g->screen = PN_MI_PLAY; g->notice = NULL; u->presented = false; return PN_OK;
}
pn_status_t pn_minesweeper_event(pn_minesweeper_t *u, int cmd, uint64_t now) {
    if (!u || !u->impl || !u->active) return PN_INVALID;
    mi_t *g = u->impl; time_update(g, now);
    if (g->pending_start) return cmd == PN_MI_RETRY ? commit(u) : PN_BUSY;
    if (cmd == PN_MI_BACK || cmd == PN_MI_EXIT) { pn_status_t r = pn_minesweeper_pause(u, now); if (r == PN_OK) u->active = false; return r; }
    if (cmd == PN_MI_RETRY && g->screen == PN_MI_ERROR) {
        pn_status_t r = storage(g, &g->candidate, false);
        if (r == PN_OK) { g->state = g->candidate; g->has = true; g->tier = g->state.tier; g->screen = g->state.result >= PN_MI_WON ? PN_MI_COMPLETE : PN_MI_PLAY; }
        else if (r == PN_EMPTY) g->screen = PN_MI_SELECT;
        if (r == PN_OK || r == PN_EMPTY) { g->notice = NULL; u->presented = false; } return r;
    }
    if (g->screen == PN_MI_ERROR) return PN_EMPTY;
    if (cmd == PN_MI_CLEAN) { u->presented = false; return PN_OK; }
    if ((g->screen == PN_MI_PLAY || g->screen == PN_MI_COMPLETE) && cmd >= PN_MI_TIER && cmd < PN_MI_TIER + 3) {
        if (!u->presented) return PN_BUSY;
        unsigned requested = (unsigned)(cmd - PN_MI_TIER);
        if (requested == g->state.tier) return PN_EMPTY;
        // 只暂存请求；取消不得污染旧局和下一次新局的默认难度。/ Stage the request only; cancellation must preserve the old game and the next New default.
        g->pending_tier = requested; g->running = false;
        if (g->state.result == PN_MI_RUNNING) g->dirty = true;
        g->screen = PN_MI_CONFIRM; u->presented = false; return PN_OK;
    }
    if (cmd == PN_MI_NEW) {
        g->pending_tier = g->tier; g->running = false;
        if (g->has && g->state.result == PN_MI_RUNNING) g->dirty = true;
        g->screen = g->has ? PN_MI_CONFIRM : PN_MI_SELECT; u->presented = false; return PN_OK;
    }
    if (cmd == PN_MI_CANCEL || cmd == PN_MI_RESUME) { if (!g->has) return PN_EMPTY; g->pending_tier = g->tier; g->screen = g->state.result >= PN_MI_WON ? PN_MI_COMPLETE : PN_MI_PLAY; u->presented = false; return PN_OK; }
    if (cmd == PN_MI_ACCEPT && g->screen == PN_MI_CONFIRM) { pn_status_t r = save(u, now); if (r != PN_OK) return r; g->tier = g->pending_tier; g->screen = PN_MI_SELECT; u->presented = false; return PN_OK; }
    if (g->screen == PN_MI_SELECT) {
        if (cmd >= PN_MI_TIER && cmd < PN_MI_TIER + 3) { g->tier = (unsigned)(cmd - PN_MI_TIER); u->presented = false; return PN_OK; }
        if (cmd == PN_MI_START) { pn_status_t r = save(u, now); if (r != PN_OK) return r; (void)pn_minesweeper_new(&g->candidate, g->tier, rng(&g->next_seed)); return commit(u); }
        return PN_EMPTY;
    }
    if (g->screen != PN_MI_PLAY) return PN_EMPTY;
    if (!u->presented) return PN_BUSY;
    pn_status_t r = PN_EMPTY;
    if (cmd == PN_MI_REVEAL_MODE || cmd == PN_MI_FLAG_MODE) { bool flag = cmd == PN_MI_FLAG_MODE; if (g->state.flag_mode != flag) { g->state.flag_mode = flag; r = PN_OK; } }
    else if (cmd >= PN_MI_CELL && cmd < PN_MI_CELL + (int)total(&g->state)) {
        unsigned c = (unsigned)(cmd - PN_MI_CELL);
        r = g->state.flag_mode ? pn_minesweeper_flag(&g->state, c) : mi_bit(g->state.opened, c) ? pn_minesweeper_chord(&g->state, c) : pn_minesweeper_reveal(&g->state, c);
    }
    if (r == PN_OK) { g->dirty = true; g->changed = now; u->presented = false; g->screen = g->state.result >= PN_MI_WON ? PN_MI_COMPLETE : PN_MI_PLAY; if (g->state.result >= PN_MI_WON) g->running = false; }
    return r;
}
void pn_minesweeper_displayed(pn_minesweeper_t *u, bool ok) {
    if (u && u->impl) {
        mi_t *g = u->impl; bool running = ok && g->screen == PN_MI_PLAY;
        if (running && !g->running) g->fresh_clock = true;
        u->presented = ok; g->running = running;
    }
}
void pn_minesweeper_close(pn_minesweeper_t *u) { if (u) { pn_free(u->impl); memset(u, 0, sizeof *u); } }
const pn_mi_state_t *pn_minesweeper_state(const pn_minesweeper_t *u) { const mi_t *g = u ? u->impl : NULL; return g && g->has ? &g->state : NULL; }
pn_mi_screen_t pn_minesweeper_screen(const pn_minesweeper_t *u) { return u && u->impl ? ((mi_t *)u->impl)->screen : PN_MI_ERROR; }
