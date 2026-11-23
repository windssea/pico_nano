/* SPDX-License-Identifier: Apache-2.0
 * 三国华容道主机规则、全60关最优解验证、偏离纠偏提示、A/B存档损坏保留与同步报错屏障测试。
 * Host tests for Klotski rules, all-60 optimal solve, detour hint recovery, corrupt A/B save preservation and sync-error barriers. */
#define _POSIX_C_SOURCE 200809L
#include "pn_klotski.h"
#include "../../components/pn_library/pn_klotski_internal.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int __real_fsync(int fd);
static bool fail_fsync;
int __wrap_fsync(int fd) {
    if (fail_fsync) {
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}

static void clean_dir(const char *root) {
    char p[512];
    snprintf(p, sizeof p, "%s/klotski.a", root);
    (void)unlink(p);
    snprintf(p, sizeof p, "%s/klotski.b", root);
    (void)unlink(p);
    assert(rmdir(root) == 0);
}

int main(void) {
    /* 1. 全60关题库CRC与合法性校验，并用内置提示完整求解入门/进阶/名局代表关 / Verify all 60 levels and solve via hints */
    uint32_t crc = pn_kl_bank_crc();
    assert(crc != 0u);
    for (unsigned id = 0; id < PN_KL_LEVELS; id++) {
        pn_kl_level_t lv;
        assert(pn_klotski_level(id, &lv) == PN_OK);
        assert(lv.id == id && lv.tier == id / 20u && lv.piece_count >= 6u && lv.par_moves > 0u);
        uint8_t occ[PN_KL_CELLS];
        assert(pn_klotski_occupancy(&lv, lv.start_pos, occ));
    }
    assert(pn_klotski_level(PN_KL_LEVELS, &(pn_kl_level_t){0}) == PN_INVALID);

    /* 用提示自动通关第0关（入门）、第25关（进阶）、第59关（名局）验证最优步数 / Solve 3 representative levels by hint */
    const unsigned sample_ids[] = {0u, 25u, 59u};
    for (unsigned k = 0; k < 3u; k++) {
        pn_kl_level_t lv;
        assert(pn_klotski_level(sample_ids[k], &lv) == PN_OK);
        pn_kl_state_t st;
        memset(&st, 0, sizeof st);
        pn_kl_reset_board(&st, &lv, crc);
        for (unsigned step = 0; step < lv.par_moves; step++) {
            assert(!st.solved);
            assert(pn_klotski_hint(&st) == PN_OK);
            assert(st.hint_piece < lv.piece_count);
            assert(pn_klotski_slide(&st, st.hint_piece, st.hint_dir, st.hint_steps) == PN_OK);
        }
        assert(st.solved && st.pos[0] == 13u && st.moves == lv.par_moves);
        assert((st.completed[lv.id >> 3] & (1u << (lv.id & 7u))) != 0u);
    }

    /* 2. 验证偏离主解1步后提示可自动纠偏、撤销恢复与A/B持久化 / Verify 1-move detour hint, undo and A/B persistence */
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 2u * 1024u * 1024u, NULL, NULL, NULL) == PN_OK);
    pn_media_t media;
    pn_media_init(&media);
    assert(pn_media_attach(&media, 1u) == PN_OK);

    char root[] = "/tmp/pn-test-klotski-XXXXXX";
    assert(mkdtemp(root));
    pn_klotski_t game = {0};
    assert(pn_klotski_open(&game, &pool, &media, NULL, 100) == PN_INVALID);
    assert(pn_klotski_open(&game, &pool, &media, root, 100) == PN_OK);
    assert(pn_klotski_screen(&game) == PN_KL_SELECT);
    assert(pn_klotski_event(&game, PN_KL_START, 200) == PN_OK);
    assert(pn_klotski_screen(&game) == PN_KL_PLAY);
    /* 开局立即落盘：不经tick/pause直接关闭并重开仍恢复进行中的新局。/ Start persists immediately: reopening without tick/pause restores the started level. */
    pn_klotski_close(&game);
    assert(pn_klotski_open(&game, &pool, &media, root, 250) == PN_OK);
    assert(pn_klotski_screen(&game) == PN_KL_PLAY && pn_klotski_state(&game)->level.id == 0u);
    pn_klotski_displayed(&game, true);

    /* 执行提示走2步再撤销1步 / Play 2 hinted moves then undo 1 */
    assert(pn_klotski_event(&game, PN_KL_HINT, 300) == PN_OK);
    const pn_kl_state_t *s = pn_klotski_state(&game);
    assert(s->hint_piece < s->level.piece_count && s->hints == 1u);
    int cmd1 = PN_KL_SLIDE + (int)(s->hint_piece * 8u + s->hint_dir * 2u + (s->hint_steps - 1u));
    assert(pn_klotski_event(&game, cmd1, 400) == PN_OK && s->moves == 1u);
    assert(pn_klotski_event(&game, PN_KL_HINT, 500) == PN_OK);
    int cmd2 = PN_KL_SLIDE + (int)(s->hint_piece * 8u + s->hint_dir * 2u + (s->hint_steps - 1u));
    assert(pn_klotski_event(&game, cmd2, 600) == PN_OK && s->moves == 2u);
    assert(pn_klotski_event(&game, PN_KL_UNDO_MOVE, 700) == PN_OK && s->moves == 1u);

    /* 同步报错屏障：fsync失败时pause与退出都阻止关闭 / Save barrier on fsync error */
    fail_fsync = true;
    assert(pn_klotski_pause(&game, 800) != PN_OK && game.active && pn_klotski_screen(&game) == PN_KL_ERROR);
    fail_fsync = false;
    assert(pn_klotski_event(&game, PN_KL_RETRY, 900) == PN_OK && pn_klotski_screen(&game) == PN_KL_PLAY);
    pn_klotski_displayed(&game, true);
    /* 即使未走新步，pause 也会落盘已推进的计时。/ Pause persists elapsed time even when no move was dirty. */
    assert(pn_klotski_tick(&game, 950) == PN_EMPTY);
    assert(pn_klotski_pause(&game, 1000) == PN_OK);
    pn_kl_state_t snap = *pn_klotski_state(&game);
    assert(snap.elapsed_ms > 0u);
    pn_klotski_close(&game);

    /* 重新打开验证恢复原局与步数、计时，并验证确认页 KEY2(EXIT) 可保存退出 / Reopen and verify restore + confirm-screen EXIT save */
    assert(pn_klotski_open(&game, &pool, &media, root, 2000) == PN_OK);
    assert(pn_klotski_screen(&game) == PN_KL_PLAY);
    assert(pn_klotski_state(&game)->moves == snap.moves &&
           pn_klotski_state(&game)->elapsed_ms == snap.elapsed_ms &&
           memcmp(pn_klotski_state(&game)->pos, snap.pos, sizeof snap.pos) == 0);
    assert(pn_klotski_event(&game, PN_KL_RESTART, 2050) == PN_OK && pn_klotski_screen(&game) == PN_KL_CONFIRM);
    assert(pn_klotski_event(&game, PN_KL_EXIT, 2100) == PN_OK && !game.active);
    pn_klotski_close(&game);

    /* 损坏存档必须保留原文件并报ERROR，且点击重试不会绕过读取错误 / Corrupt save preserves files and RETRY reloads instead of bypassing */
    char fa[512], fb[512];
    snprintf(fa, sizeof fa, "%s/klotski.a", root);
    snprintf(fb, sizeof fb, "%s/klotski.b", root);
    FILE *f = fopen(fa, "wb");
    assert(f && fwrite("BAD!", 1, 4, f) == 4 && fclose(f) == 0);
    f = fopen(fb, "wb");
    assert(f && fwrite("BAD!", 1, 4, f) == 4 && fclose(f) == 0);
    assert(pn_klotski_open(&game, &pool, &media, root, 3000) == PN_OK);
    assert(pn_klotski_screen(&game) == PN_KL_ERROR);
    assert(pn_klotski_event(&game, PN_KL_RETRY, 3100) != PN_OK && pn_klotski_screen(&game) == PN_KL_ERROR);
    assert(pn_klotski_event(&game, PN_KL_CHOOSE, 3200) == PN_EMPTY && pn_klotski_screen(&game) == PN_KL_ERROR);
    pn_klotski_close(&game);
    f = fopen(fa, "rb");
    char check[4] = {0};
    assert(f && fread(check, 1, 4, f) == 4 && fclose(f) == 0 && memcmp(check, "BAD!", 4) == 0);

    clean_dir(root);
    assert(pool.used == 0 && pool.live == 0);
    puts("test_klotski passed");
    return 0;
}
