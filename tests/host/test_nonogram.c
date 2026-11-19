/* SPDX-License-Identifier: Apache-2.0
 * 数织90关唯一解核验、直线批涂、有界撤销与A/B事务故障回归。/ Nonogram 90-puzzle verification, line strokes, bounded undo and A/B fault regressions. */
#define _POSIX_C_SOURCE 200809L
#include "../../components/pn_library/pn_nonogram_internal.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool fail_sync;
int __real_fsync(int);
int __wrap_fsync(int fd) {
    int r = __real_fsync(fd);
    if (r == 0 && fail_sync) {
        errno = EIO;
        return -1;
    }
    return r;
}

static void clear_files(const char *root) {
    char path[512];
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/nonogram.%c", root, 'a' + i);
        unlink(path);
    }
}

static void forge_record(const char *root, pn_pool_t *pool, pn_media_t *media, unsigned mode) {
    pn_media_lease_t lease = {0};
    pn_journal_files_t files;
    pn_journal_io_t io;
    uint8_t buf[NO_SAVE_BYTES];
    size_t sz = 0;
    char a[512], b[512];
    snprintf(a, sizeof a, "%s/nonogram.a", root);
    snprintf(b, sizeof b, "%s/nonogram.b", root);
    assert(pn_media_acquire(media, PN_MEDIA_WRITE, &lease) == PN_OK);
    assert(pn_journal_files_init(&files, media, &lease, a, b, &io) == PN_OK);
    assert(pn_blob_load_bounded(&io, pool, buf, NO_SAVE_BYTES, &sz) == PN_OK && sz == NO_SAVE_BYTES);
    if (mode == 0) buf[4] = 9;          /* 非法版本号 / Bad version */
    if (mode == 1) buf[128] = 99;       /* 非法宽度 / Out-of-bounds width */
    if (mode == 2) buf[44] = 0xFF;      /* 非法2位格值3 / Illegal cell state 3 */
    if (mode == 3) buf[160] ^= 0x01;    /* 篡改答案破坏唯一解或目标计数 / Corrupt puzzle solution */
    assert(pn_blob_save_bounded(&io, pool, buf, sz, NO_SAVE_BYTES) == PN_OK);
    assert(pn_media_release(media, &lease) == PN_OK);
}

static void test_bank_and_rules(void) {
    pn_no_puzzle_t bad;
    assert(pn_nonogram_puzzle(PN_NO_PUZZLES, &bad) == PN_INVALID);

    for (unsigned id = 0; id < PN_NO_PUZZLES; id++) {
        pn_no_puzzle_t p;
        assert(pn_nonogram_puzzle(id, &p) == PN_OK);
        assert(p.id == id && p.tier == id / 30u && p.title[0] != '\0');
        if (p.tier == 0) assert((p.width == 5 && p.height == 5) || (p.width == 8 && p.height == 8));
        if (p.tier == 1) assert(p.width == 10 && p.height == 10);
        if (p.tier == 2) assert(p.width == 15 && p.height == 15);
    }

    pn_no_state_t s = {0};
    assert(pn_nonogram_puzzle(0, &s.puzzle) == PN_OK);
    unsigned w = s.puzzle.width, area = w * s.puzzle.height;

    /* 交叉保护测试：叉号格不会被填涂笔刷覆盖，涂黑格不会被叉号笔刷覆盖。/ Cross protection: strokes never clobber opposite marks. */
    s.cross_mode = true;
    assert(pn_nonogram_act(&s, 1) == PN_OK && pn_nonogram_cell(&s, 1) == PN_NO_MARK);
    s.cross_mode = false;
    assert(pn_nonogram_stroke(&s, 0, w - 1u) == PN_OK);
    assert(pn_nonogram_cell(&s, 0) == PN_NO_FILL && pn_nonogram_cell(&s, 1) == PN_NO_MARK && pn_nonogram_cell(&s, 2) == PN_NO_FILL);
    assert(pn_nonogram_undo(&s) == PN_OK);
    assert(pn_nonogram_cell(&s, 0) == PN_NO_BLANK && pn_nonogram_cell(&s, 1) == PN_NO_MARK && pn_nonogram_cell(&s, 2) == PN_NO_BLANK);
    assert(pn_nonogram_undo(&s) == PN_OK && pn_nonogram_cell(&s, 1) == PN_NO_BLANK);
    assert(pn_nonogram_undo(&s) == PN_EMPTY);

    /* 64步有界撤销环与提示求解测试。/ 64-step bounded undo ring and hint completion test. */
    for (unsigned i = 0; i < 80; i++) assert(pn_nonogram_act(&s, i % area) == PN_OK);
    assert(s.undo_count == PN_NO_UNDO);
    for (unsigned i = 0; i < PN_NO_UNDO; i++) assert(pn_nonogram_undo(&s) == PN_OK);
    assert(pn_nonogram_undo(&s) == PN_EMPTY);

    memset(&s, 0, sizeof s);
    assert(pn_nonogram_puzzle(35, &s.puzzle) == PN_OK);
    while (!s.complete) assert(pn_nonogram_hint(&s) == PN_OK);
    assert(pn_nonogram_filled_count(&s) == s.puzzle.target_count);
    for (unsigned r = 0; r < s.puzzle.height; r++) assert(pn_nonogram_row_satisfied(&s, r));
    for (unsigned c = 0; c < s.puzzle.width; c++) assert(pn_nonogram_col_satisfied(&s, c));
    assert(pn_nonogram_act(&s, 0) == PN_EMPTY && pn_nonogram_hint(&s) == PN_EMPTY);
}

int main(void) {
    test_bank_and_rules();

    char root[] = "/tmp/pn-nonogram-XXXXXX";
    assert(mkdtemp(root));
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 64 * 1024, NULL, NULL, NULL) == PN_OK);
    pn_media_t media;
    pn_media_init(&media);
    assert(pn_media_attach(&media, 1) == PN_OK);

    pn_nonogram_t u = {0};
    assert(pn_nonogram_open(&u, &pool, &media, root, 100) == PN_OK && pn_nonogram_screen(&u) == PN_NO_SELECT);
    assert(pn_nonogram_event(&u, PN_NO_TIER + 1, 101) == PN_OK);
    assert(pn_nonogram_event(&u, PN_NO_NEXT_LEVEL, 102) == PN_OK);
    assert(pn_nonogram_event(&u, PN_NO_START, 103) == PN_OK && pn_nonogram_screen(&u) == PN_NO_PLAY);
    assert(pn_nonogram_state(&u)->puzzle.id == 31);
    assert(pn_nonogram_event(&u, PN_NO_CELL, 104) == PN_BUSY);
    pn_nonogram_displayed(&u, true);

    assert(pn_nonogram_event(&u, PN_NO_STROKE + 0 * PN_NO_CELLS + 4, 110) == PN_OK);
    pn_nonogram_displayed(&u, true);
    assert(pn_nonogram_tick(&u, 2000) == PN_OK);
    size_t active_used = pool.used;
    assert(pn_nonogram_tick(&u, 3000) == PN_EMPTY && pool.used == active_used);

    assert(pn_nonogram_pause(&u, 3200) == PN_OK);
    pn_no_state_t saved = *pn_nonogram_state(&u);
    assert(saved.elapsed_ms > 0);
    pn_nonogram_close(&u);
    assert(!pool.used);

    assert(pn_nonogram_open(&u, &pool, &media, root, 50000) == PN_OK);
    assert(!memcmp(&saved, pn_nonogram_state(&u), sizeof saved));
    pn_nonogram_displayed(&u, true);

    /* 保存失败屏障：阻止退出、锁屏与覆盖新局。/ Save-failure barriers block exit, lock and new-level overwrite. */
    assert(pn_nonogram_event(&u, PN_NO_CROSS_MODE, 50010) == PN_OK);
    pn_nonogram_displayed(&u, true);
    fail_sync = true;
    assert(pn_nonogram_event(&u, PN_NO_EXIT, 50020) != PN_OK && u.active);
    assert(pn_nonogram_pause(&u, 50025) != PN_OK);
    fail_sync = false;
    assert(pn_nonogram_event(&u, PN_NO_EXIT, 50030) == PN_OK && !u.active);
    pn_nonogram_close(&u);

    assert(pn_nonogram_open(&u, &pool, &media, root, 60000) == PN_OK && pn_nonogram_state(&u)->cross_mode);
    assert(pn_nonogram_event(&u, PN_NO_CHOOSE, 60001) == PN_OK && pn_nonogram_screen(&u) == PN_NO_CONFIRM);
    assert(pn_nonogram_event(&u, PN_NO_ACCEPT, 60002) == PN_OK && pn_nonogram_screen(&u) == PN_NO_SELECT);
    fail_sync = true;
    assert(pn_nonogram_event(&u, PN_NO_START, 60003) != PN_OK && pn_nonogram_screen(&u) == PN_NO_ERROR);
    assert(pn_nonogram_event(&u, PN_NO_BACK, 60004) == PN_BUSY && pn_nonogram_pause(&u, 60005) == PN_BUSY);
    fail_sync = false;
    assert(pn_nonogram_event(&u, PN_NO_RETRY, 60006) == PN_OK && pn_nonogram_screen(&u) == PN_NO_PLAY);
    pn_nonogram_close(&u);
    clear_files(root);

    for (unsigned mode = 0; mode < 4; mode++) {
        assert(pn_nonogram_open(&u, &pool, &media, root, 10) == PN_OK);
        assert(pn_nonogram_event(&u, PN_NO_START, 11) == PN_OK);
        pn_nonogram_close(&u);
        forge_record(root, &pool, &media, mode);
        assert(pn_nonogram_open(&u, &pool, &media, root, 12) == PN_OK && pn_nonogram_screen(&u) == PN_NO_ERROR);
        assert(pn_nonogram_event(&u, PN_NO_START, 13) == PN_EMPTY);
        pn_nonogram_close(&u);
        clear_files(root);
    }

    pool.fail_at = pool.attempts + 1;
    assert(pn_nonogram_open(&u, &pool, &media, root, 1) == PN_NO_MEMORY);
    pool.fail_at = 0;
    assert(!pool.used && !pool.live && !pn_media_active(&media));
    assert(rmdir(root) == 0);
    printf("nonogram rules/bank/stroke/undo/A-B barriers passed; controller=%zu peak=%zu state=%zu\n", active_used,
           pool.peak, sizeof(pn_no_state_t));
    return 0;
}
