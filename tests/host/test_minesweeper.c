/* SPDX-License-Identifier: Apache-2.0
 * 跨种子规则、首步安全与事务故障回归。/ Cross-seed rules, first-move safety and transaction fault regressions. */
#define _POSIX_C_SOURCE 200809L
#include "../../components/pn_library/pn_minesweeper_internal.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static bool fail_sync;
int __real_fsync(int);
int __wrap_fsync(int fd) { int r = __real_fsync(fd); if (r == 0 && fail_sync) { errno = EIO; return -1; } return r; }
static void clear(const char *root) { char path[512]; for (int i = 0; i < 2; i++) { snprintf(path, sizeof path, "%s/minesweeper.%c", root, 'a' + i); unlink(path); } }
static void forge(const char *root, pn_media_t *media, unsigned mode) {
    pn_media_lease_t lease = {0}; pn_journal_files_t files; pn_journal_io_t io; pn_record_t record;
    char a[512], b[512]; snprintf(a, sizeof a, "%s/minesweeper.a", root); snprintf(b, sizeof b, "%s/minesweeper.b", root);
    assert(pn_media_acquire(media, PN_MEDIA_WRITE, &lease) == PN_OK); assert(pn_journal_files_init(&files, media, &lease, a, b, &io) == PN_OK);
    assert(pn_journal_load(&io, &record) == PN_OK);
    if (mode == 0) record.payload[4] = 2;
    if (mode == 1) memset(record.payload + 12, 0, 4);
    if (mode == 2) record.payload[26] = 0;
    if (mode == 3) record.payload[127] |= 128;
    if (mode == 4) record.payload[32] ^= 1;
    assert(pn_journal_save(&io, record.payload, record.size) == PN_OK); assert(pn_media_release(media, &lease) == PN_OK);
}
static unsigned cells(const pn_mi_state_t *s) { return (unsigned)s->width * s->height; }
static bool near(const pn_mi_state_t *s, unsigned a, unsigned b) { int dx = (int)(a % s->width) - (int)(b % s->width), dy = (int)(a / s->width) - (int)(b / s->width); return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1; }
static void rules(void) {
    pn_mi_state_t invalid = {0};
    for (unsigned i = 0; i < 4; i++) {
        assert(pn_minesweeper_reveal(&invalid, 0) == PN_INVALID && pn_minesweeper_flag(&invalid, 0) == PN_INVALID && pn_minesweeper_chord(&invalid, 0) == PN_INVALID);
        assert(!pn_minesweeper_adjacent(&invalid, 0) && !pn_minesweeper_opened(&invalid) && !pn_minesweeper_flags(&invalid));
        invalid.width = 255; invalid.height = 255; invalid.tier = (uint8_t)(i == 3 ? 255 : i);
    }
    unsigned tested = 0;
    for (unsigned tier = 0; tier < 3; tier++) for (unsigned seed = 1; seed <= 8; seed++) {
        pn_mi_state_t s; assert(pn_minesweeper_new(&s, tier, seed) == PN_OK); unsigned n = cells(&s);
        for (unsigned first = 0; first < n; first++) {
            assert(pn_minesweeper_new(&s, tier, seed) == PN_OK); assert(pn_minesweeper_reveal(&s, first) == PN_OK);
            assert(s.result != PN_MI_LOST && s.first == first && pn_minesweeper_adjacent(&s, first) == 0);
            unsigned mines = 0;
            for (unsigned c = 0; c < n; c++) { mines += mi_bit(s.mines, c); if (near(&s, first, c)) assert(!mi_bit(s.mines, c)); assert(!mi_bit(s.mines, c) || !mi_bit(s.opened, c)); }
            assert(mines == s.mine_count); pn_mi_state_t again; assert(pn_minesweeper_new(&again, tier, seed) == PN_OK); assert(pn_minesweeper_reveal(&again, first) == PN_OK); assert(!memcmp(&s, &again, sizeof s));
            for (unsigned c = 0; c < n; c++) if (!mi_bit(s.mines, c)) (void)pn_minesweeper_reveal(&s, c);
            assert(s.result == PN_MI_WON && pn_minesweeper_opened(&s) == n - s.mine_count);
            assert(pn_minesweeper_reveal(&s, 0) == PN_EMPTY && pn_minesweeper_flag(&s, 0) == PN_EMPTY); tested++;
        }
    }
    pn_mi_state_t s; assert(pn_minesweeper_new(&s, 3, 1) == PN_INVALID); assert(pn_minesweeper_new(&s, 0, 1) == PN_OK);
    assert(pn_minesweeper_flag(&s, 0) == PN_OK && pn_minesweeper_reveal(&s, 0) == PN_EMPTY && s.result == PN_MI_WAITING);
    assert(pn_minesweeper_flag(&s, 0) == PN_OK); assert(pn_minesweeper_reveal(&s, 0) == PN_OK);
    unsigned mine = 0; while (!mi_bit(s.mines, mine)) mine++; assert(pn_minesweeper_reveal(&s, mine) == PN_OK && s.result == PN_MI_LOST && s.exploded == mine);
    bool chord_tested = false;
    for (unsigned seed = 1; seed < 100 && !chord_tested; seed++) {
        assert(pn_minesweeper_new(&s, 1, seed) == PN_OK); assert(pn_minesweeper_reveal(&s, 70) == PN_OK);
        for (unsigned c = 0; c < cells(&s) && !chord_tested; c++) if (mi_bit(s.opened, c) && pn_minesweeper_adjacent(&s, c)) {
            unsigned safe = PN_MI_CELLS, mine_at = PN_MI_CELLS;
            for (unsigned b = 0; b < cells(&s); b++) if (near(&s, c, b) && !mi_bit(s.opened, b)) { if (mi_bit(s.mines, b)) mine_at = b; else safe = b; }
            if (safe == PN_MI_CELLS || mine_at == PN_MI_CELLS) continue;
            pn_mi_state_t good = s, bad = s;
            for (unsigned b = 0; b < cells(&s); b++) if (near(&s, c, b) && mi_bit(s.mines, b)) { assert(pn_minesweeper_flag(&good, b) == PN_OK); if (b != mine_at) assert(pn_minesweeper_flag(&bad, b) == PN_OK); }
            assert(pn_minesweeper_chord(&good, c) == PN_OK && good.result != PN_MI_LOST && mi_bit(good.opened, safe));
            assert(pn_minesweeper_flag(&bad, safe) == PN_OK); assert(pn_minesweeper_chord(&bad, c) == PN_OK && bad.result == PN_MI_LOST); chord_tested = true;
        }
    }
    assert(chord_tested); printf("minesweeper rules: %u seed/first-cell boards, all tiers, exact mines, flood/win/loss/chord passed\n", tested);
}
static void tier_switch(const char *root, pn_pool_t *pool, pn_media_t *media) {
    pn_minesweeper_t u = {0};
    assert(pn_minesweeper_open(&u, pool, media, root, 100) == PN_OK);
    assert(pn_minesweeper_event(&u, PN_MI_TIER + 1, 100) == PN_OK && pn_minesweeper_event(&u, PN_MI_START, 100) == PN_OK);
    mi_t *g = u.impl;
    // 未显示的页不能接收切档，当前档也不得借此触发确认。/ An unshown page rejects tier input, including its current tier.
    assert(pn_minesweeper_event(&u, PN_MI_TIER, 100) == PN_BUSY && pn_minesweeper_event(&u, PN_MI_TIER + 1, 100) == PN_BUSY);
    pn_minesweeper_displayed(&u, true);
    assert(pn_minesweeper_event(&u, PN_MI_CELL, 100) == PN_OK); pn_minesweeper_displayed(&u, true);
    assert(pn_minesweeper_event(&u, PN_MI_TIER + 1, 100) == PN_EMPTY && u.presented && g->screen == PN_MI_PLAY);
    pn_mi_state_t old = g->state;
    assert(pn_minesweeper_event(&u, PN_MI_TIER + 2, 100) == PN_OK && g->screen == PN_MI_CONFIRM && g->tier == 1 && g->pending_tier == 2);
    assert(!memcmp(&old, &g->state, sizeof old));
    assert(pn_minesweeper_event(&u, PN_MI_CANCEL, 10000000) == PN_OK && g->screen == PN_MI_PLAY && g->tier == 1 && g->pending_tier == 1);
    pn_minesweeper_displayed(&u, true); assert(pn_minesweeper_tick(&u, 10000001) == PN_OK); assert(!memcmp(&old, &g->state, sizeof old));
    assert(pn_minesweeper_tick(&u, 10000101) == PN_EMPTY && g->state.elapsed_ms == old.elapsed_ms + 100); old = g->state;
    assert(pn_minesweeper_event(&u, PN_MI_TIER, 10000101) == PN_OK && g->tier == 1 && g->pending_tier == 0);
    fail_sync = true;
    assert(pn_minesweeper_event(&u, PN_MI_ACCEPT, 20000101) != PN_OK && g->screen == PN_MI_CONFIRM && g->tier == 1 && g->pending_tier == 0);
    assert(!memcmp(&old, &g->state, sizeof old)); fail_sync = false;
    assert(pn_minesweeper_event(&u, PN_MI_CANCEL, 20000101) == PN_OK && g->tier == 1 && g->screen == PN_MI_PLAY);
    // 普通新局继承原选择，不继承取消掉的切档请求。/ Ordinary New inherits the old selection, never a cancelled tier request.
    assert(pn_minesweeper_event(&u, PN_MI_NEW, 20000101) == PN_OK && g->pending_tier == 1);
    assert(pn_minesweeper_event(&u, PN_MI_ACCEPT, 20000101) == PN_OK && g->screen == PN_MI_SELECT && g->tier == 1 && !memcmp(&old, &g->state, sizeof old));
    assert(pn_minesweeper_event(&u, PN_MI_RESUME, 20000101) == PN_OK); pn_minesweeper_displayed(&u, true);
    for (unsigned c = 0; c < cells(&g->state) && g->state.result != PN_MI_WON; c++) if (!mi_bit(g->state.mines, c) && !mi_bit(g->state.opened, c)) {
        assert(pn_minesweeper_event(&u, PN_MI_CELL + (int)c, 20000101) == PN_OK); pn_minesweeper_displayed(&u, true);
    }
    assert(g->screen == PN_MI_COMPLETE && g->state.result == PN_MI_WON); old = g->state;
    assert(pn_minesweeper_event(&u, PN_MI_TIER + 1, 20000101) == PN_EMPTY && u.presented);
    assert(pn_minesweeper_event(&u, PN_MI_TIER + 2, 20000101) == PN_OK); assert(pn_minesweeper_event(&u, PN_MI_CANCEL, 20000101) == PN_OK && g->screen == PN_MI_COMPLETE && g->tier == 1);
    pn_minesweeper_displayed(&u, true); assert(pn_minesweeper_event(&u, PN_MI_TIER, 20000101) == PN_OK);
    assert(pn_minesweeper_event(&u, PN_MI_ACCEPT, 20000101) == PN_OK && g->screen == PN_MI_SELECT && g->tier == 0 && !memcmp(&old, &g->state, sizeof old));
    pn_minesweeper_close(&u);
    assert(pn_minesweeper_open(&u, pool, media, root, 30000000) == PN_OK && pn_minesweeper_screen(&u) == PN_MI_COMPLETE && !memcmp(&old, pn_minesweeper_state(&u), sizeof old));
    pn_minesweeper_close(&u); clear(root);
    assert(!pool->used && !pool->live && !pn_media_active(media));
    printf("minesweeper tier chips: current/hidden/cancel/confirm/save failure/completed-game preservation passed\n");
}
int main(void) {
    rules(); char root[] = "/tmp/pn-mines-XXXXXX"; assert(mkdtemp(root)); pn_pool_t pool; assert(pn_pool_init(&pool, 32 * 1024, NULL, NULL, NULL) == PN_OK);
    pn_media_t media; pn_media_init(&media); assert(pn_media_attach(&media, 1) == PN_OK); pn_minesweeper_t u = {0};
    assert(pn_minesweeper_open(&u, &pool, &media, root, 100) == PN_OK && pn_minesweeper_screen(&u) == PN_MI_SELECT);
    assert(pn_minesweeper_event(&u, PN_MI_TIER + 2, 101) == PN_OK); assert(pn_minesweeper_event(&u, PN_MI_START, 102) == PN_OK);
    assert(pn_minesweeper_screen(&u) == PN_MI_PLAY && pn_minesweeper_state(&u)->tier == 2);
    assert(pn_minesweeper_event(&u, PN_MI_CELL, 103) == PN_BUSY); pn_minesweeper_displayed(&u, true);
    assert(pn_minesweeper_event(&u, PN_MI_CELL, 104) == PN_OK); pn_minesweeper_displayed(&u, true);
    assert(pn_minesweeper_tick(&u, 2000) == PN_OK); assert(u.presented); size_t used = pool.used;
    assert(pn_minesweeper_tick(&u, 3000) == PN_EMPTY && u.presented && pool.used == used);
    assert(pn_minesweeper_pause(&u, 3100) == PN_OK); pn_mi_state_t expected = *pn_minesweeper_state(&u); assert(expected.elapsed_ms > 0); pn_minesweeper_close(&u); assert(!pool.used);
    assert(pn_minesweeper_open(&u, &pool, &media, root, 100000) == PN_OK && !memcmp(&expected, pn_minesweeper_state(&u), sizeof expected));
    pn_minesweeper_displayed(&u, true); assert(pn_minesweeper_tick(&u, 100000) == PN_EMPTY);
    assert(pn_minesweeper_pause(&u, 100100) == PN_OK); uint64_t before_lock = pn_minesweeper_state(&u)->elapsed_ms;
    pn_minesweeper_displayed(&u, true); assert(pn_minesweeper_tick(&u, 36100100) == PN_EMPTY);
    assert(pn_minesweeper_state(&u)->elapsed_ms == before_lock); assert(pn_minesweeper_tick(&u, 36100200) == PN_EMPTY);
    assert(pn_minesweeper_state(&u)->elapsed_ms == before_lock + 100);
    assert(pn_minesweeper_event(&u, PN_MI_NEW, 36100201) == PN_OK); assert(pn_minesweeper_event(&u, PN_MI_CANCEL, 36100202) == PN_OK && pn_minesweeper_screen(&u) == PN_MI_PLAY);
    pn_minesweeper_displayed(&u, true); assert(pn_minesweeper_event(&u, PN_MI_FLAG_MODE, 36100203) == PN_OK); pn_minesweeper_displayed(&u, true);
    fail_sync = true; assert(pn_minesweeper_event(&u, PN_MI_EXIT, 36100204) != PN_OK && u.active); fail_sync = false;
    assert(pn_minesweeper_event(&u, PN_MI_EXIT, 36100205) == PN_OK && !u.active); pn_minesweeper_close(&u);
    assert(pn_minesweeper_open(&u, &pool, &media, root, 200000) == PN_OK && pn_minesweeper_state(&u)->flag_mode);
    assert(pn_minesweeper_event(&u, PN_MI_NEW, 200001) == PN_OK); assert(pn_minesweeper_event(&u, PN_MI_ACCEPT, 200002) == PN_OK);
    uint32_t old_seed = pn_minesweeper_state(&u)->seed; fail_sync = true; assert(pn_minesweeper_event(&u, PN_MI_START, 200003) != PN_OK); assert(pn_minesweeper_screen(&u) == PN_MI_ERROR && pn_minesweeper_state(&u)->seed == old_seed);
    assert(pn_minesweeper_event(&u, PN_MI_CANCEL, 200004) == PN_BUSY && pn_minesweeper_pause(&u, 200005) == PN_BUSY); fail_sync = false;
    assert(pn_minesweeper_event(&u, PN_MI_RETRY, 200006) == PN_OK && pn_minesweeper_state(&u)->seed != old_seed); pn_minesweeper_close(&u); clear(root);
    for (unsigned mode = 0; mode < 5; mode++) {
        assert(pn_minesweeper_open(&u, &pool, &media, root, 10) == PN_OK); assert(pn_minesweeper_event(&u, PN_MI_START, 11) == PN_OK);
        pn_minesweeper_close(&u); forge(root, &media, mode);
        assert(pn_minesweeper_open(&u, &pool, &media, root, 12) == PN_OK && pn_minesweeper_screen(&u) == PN_MI_ERROR);
        assert(pn_minesweeper_event(&u, PN_MI_START, 13) == PN_EMPTY); pn_minesweeper_close(&u); clear(root);
    }
    for (int i = 0; i < 2; i++) { char path[512]; snprintf(path, sizeof path, "%s/minesweeper.%c", root, 'a' + i); FILE *f = fopen(path, "wb"); assert(f && fwrite("bad", 1, 3, f) == 3 && fclose(f) == 0); }
    assert(pn_minesweeper_open(&u, &pool, &media, root, 300000) == PN_OK && pn_minesweeper_screen(&u) == PN_MI_ERROR); assert(pn_minesweeper_event(&u, PN_MI_START, 300001) == PN_EMPTY); pn_minesweeper_close(&u); clear(root);
    tier_switch(root, &pool, &media);
    pool.fail_at = pool.attempts + 1; assert(pn_minesweeper_open(&u, &pool, &media, root, 1) == PN_NO_MEMORY); pool.fail_at = 0;
    assert(!pool.used && !pool.live && !pn_media_active(&media)); assert(rmdir(root) == 0); printf("minesweeper save/restore/failure/exit barriers; controller=%zu peak=%zu, state=%zu\n", used, pool.peak, sizeof(pn_mi_state_t)); return 0;
}
