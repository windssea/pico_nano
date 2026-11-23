/* SPDX-License-Identifier: Apache-2.0
 * 围棋死活题主机测试：完整围棋规则（气/提子/禁自杀/单劫）、全60题求解、败局悔棋、A/B存档与同步屏障。
 * Host tests for Go Tsumego: full Go rules (liberties/capture/suicide/Ko), all-60 solve, undo from fail, A/B save and sync barrier. */
#define _POSIX_C_SOURCE 200809L
#include "pn_tsumego.h"
#include "../../components/pn_library/pn_tsumego_internal.h"
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
    snprintf(p, sizeof p, "%s/tsumego.a", root);
    (void)unlink(p);
    snprintf(p, sizeof p, "%s/tsumego.b", root);
    (void)unlink(p);
    assert(rmdir(root) == 0);
}

int main(void) {
    /* 1. 验证围棋核心规则：提子、禁自杀与单劫禁止立即回提 / Verify Go rules: capture, no-suicide, simple Ko */
    {
        uint8_t b[PN_TG_CELLS] = {0}, b1[PN_TG_CELLS] = {0}, b2[PN_TG_CELLS] = {0};
        /* 禁自杀：白子围住(0,0)，黑下(0,0)被拒 / No suicide at corner (0,0) */
        b[1] = PN_TG_WHITE;
        b[9] = PN_TG_WHITE;
        unsigned cap = 0;
        assert(!pn_tsumego_apply_move(b, NULL, 0, PN_TG_BLACK, b1, &cap));

        /* 单劫校验：经典一路单劫，黑提白一子后白不可立刻回提同形 / Simple Ko check */
        memset(b, 0, sizeof b);
        /* (0,3)=O, (1,4)=O, (0,5)=O, (1,5)=X, (0,6)=X -> O(0,5)仅剩(0,4)一口气 */
        b[3] = PN_TG_WHITE;
        b[13] = PN_TG_WHITE;
        b[5] = PN_TG_WHITE;
        b[14] = PN_TG_BLACK;
        b[6] = PN_TG_BLACK;
        assert(pn_tsumego_apply_move(b, NULL, 4, PN_TG_BLACK, b1, &cap) && cap == 1u && b1[5] == PN_TG_EMPTY);
        /* 立刻在(5)回提还原b必须被单劫规则拒绝 / Immediate Ko recapture must be rejected */
        assert(!pn_tsumego_apply_move(b1, b, 5, PN_TG_WHITE, b2, &cap));
        /* 不带ko_prev时同一手合法 / Without ko_prev same move is legal */
        assert(pn_tsumego_apply_move(b1, NULL, 5, PN_TG_WHITE, b2, &cap) && cap == 1u);
    }

    /* 2. 全60道死活题均可加载并通过提示完整走通至PN_TG_SOLVED / Load and solve ALL 60 puzzles via hints */
    uint32_t crc = pn_tg_bank_crc();
    assert(crc != 0u);
    for (unsigned id = 0; id < PN_TG_LEVELS; id++) {
        pn_tg_level_t lv;
        assert(pn_tsumego_level(id, &lv) == PN_OK);
        assert(lv.id == id && lv.tier == id / 20u && lv.main_moves >= 1u);
        pn_tg_state_t st;
        memset(&st, 0, sizeof st);
        pn_tg_reset_board(&st, &lv, crc);
        unsigned steps = 0;
        while (st.outcome == PN_TG_ONGOING && steps < 8u) {
            assert(pn_tsumego_hint(&st) == PN_OK && st.hint_pos < PN_TG_CELLS);
            assert(pn_tsumego_play(&st, st.hint_pos) == PN_OK);
            steps++;
        }
        assert(st.outcome == PN_TG_SOLVED && steps == lv.main_moves);
        assert((st.completed[id >> 3] & (1u << (id & 7u))) != 0u);
    }
    assert(pn_tsumego_level(PN_TG_LEVELS, &(pn_tg_level_t){0}) == PN_INVALID);

    /* 3. 验证误着反击 -> 败局直接悔棋 -> 存档恢复 -> 同步报错屏障 -> 坏档保留 / Refute, undo, save, barrier, corrupt */
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 2u * 1024u * 1024u, NULL, NULL, NULL) == PN_OK);
    pn_media_t media;
    pn_media_init(&media);
    assert(pn_media_attach(&media, 1u) == PN_OK);

    char root[] = "/tmp/pn-test-tsumego-XXXXXX";
    assert(mkdtemp(root));
    pn_tsumego_t game = {0};
    assert(pn_tsumego_open(&game, &pool, &media, NULL, 100) == PN_INVALID);
    assert(pn_tsumego_open(&game, &pool, &media, root, 100) == PN_OK);
    assert(pn_tsumego_screen(&game) == PN_TG_SELECT);
    /* 选择第13题（倒扑2手）进入答题 / Enter puzzle #12 (2-move snapback) */
    assert(pn_tsumego_event(&game, PN_TG_CELL + 12, 200) == PN_OK);
    assert(pn_tsumego_screen(&game) == PN_TG_PLAY);
    /* 开题立即落盘：不经tick/pause直接重开仍恢复该题。/ Start persists immediately: reopening restores the started puzzle. */
    pn_tsumego_close(&game);
    assert(pn_tsumego_open(&game, &pool, &media, root, 250) == PN_OK);
    assert(pn_tsumego_screen(&game) == PN_TG_PLAY && pn_tsumego_state(&game)->level.id == 12u);
    pn_tsumego_displayed(&game, true);

    /* 先故意下错一手触发白棋反击PN_TG_FAILED，再悔棋回退 / Play wrong move -> FAILED -> undo */
    assert(pn_tsumego_event(&game, PN_TG_CELL + 80, 300) == PN_OK);
    assert(pn_tsumego_state(&game)->outcome == PN_TG_FAILED);
    assert(pn_tsumego_event(&game, PN_TG_UNDO_MOVE, 400) == PN_OK);
    assert(pn_tsumego_state(&game)->outcome == PN_TG_ONGOING && pn_tsumego_state(&game)->moves == 0u);

    /* 走第1手正解并测试fsync报错屏障与恢复 / Play 1st correct move, test fsync barrier & persistence */
    assert(pn_tsumego_event(&game, PN_TG_HINT, 500) == PN_OK);
    uint8_t first_mv = pn_tsumego_state(&game)->hint_pos;
    assert(pn_tsumego_event(&game, PN_TG_CELL + first_mv, 600) == PN_OK);
    assert(pn_tsumego_state(&game)->outcome == PN_TG_ONGOING && pn_tsumego_state(&game)->moves == 1u);

    fail_fsync = true;
    assert(pn_tsumego_pause(&game, 700) != PN_OK && game.active && pn_tsumego_screen(&game) == PN_TG_ERROR);
    fail_fsync = false;
    assert(pn_tsumego_event(&game, PN_TG_RETRY, 800) == PN_OK && pn_tsumego_screen(&game) == PN_TG_PLAY);
    pn_tsumego_displayed(&game, true);
    assert(pn_tsumego_tick(&game, 850) == PN_EMPTY);
    assert(pn_tsumego_pause(&game, 900) == PN_OK);
    uint64_t saved_elapsed = pn_tsumego_state(&game)->elapsed_ms;
    assert(saved_elapsed > 0u);
    uint8_t saved_board[PN_TG_CELLS];
    memcpy(saved_board, pn_tsumego_state(&game)->board, PN_TG_CELLS);
    pn_tsumego_close(&game);

    assert(pn_tsumego_open(&game, &pool, &media, root, 1500) == PN_OK);
    assert(pn_tsumego_screen(&game) == PN_TG_PLAY && pn_tsumego_state(&game)->moves == 1u);
    assert(pn_tsumego_state(&game)->elapsed_ms == saved_elapsed);
    assert(memcmp(pn_tsumego_state(&game)->board, saved_board, PN_TG_CELLS) == 0);
    /* 验证确认页按 EXIT 仍可保存退出，不吞按键 / Confirm-screen EXIT saves and exits */
    assert(pn_tsumego_event(&game, PN_TG_RESTART, 1520) == PN_OK && pn_tsumego_screen(&game) == PN_TG_CONFIRM);
    assert(pn_tsumego_event(&game, PN_TG_EXIT, 1540) == PN_OK && !game.active);
    pn_tsumego_close(&game);
    assert(pn_tsumego_open(&game, &pool, &media, root, 1560) == PN_OK && pn_tsumego_screen(&game) == PN_TG_PLAY);
    /* 走完第2手通关 / Finish 2nd move to solve */
    assert(pn_tsumego_event(&game, PN_TG_HINT, 1600) == PN_OK);
    assert(pn_tsumego_event(&game, PN_TG_CELL + pn_tsumego_state(&game)->hint_pos, 1700) == PN_OK);
    assert(pn_tsumego_screen(&game) == PN_TG_COMPLETE);
    assert(pn_tsumego_event(&game, PN_TG_BACK, 1800) == PN_OK && !game.active);
    pn_tsumego_close(&game);

    /* 坏档保留校验，且重试仍保持读取错误 / Corrupt save preservation and load-error RETRY */
    char fa[512], fb[512];
    snprintf(fa, sizeof fa, "%s/tsumego.a", root);
    snprintf(fb, sizeof fb, "%s/tsumego.b", root);
    FILE *f = fopen(fa, "wb");
    assert(f && fwrite("CORR", 1, 4, f) == 4 && fclose(f) == 0);
    f = fopen(fb, "wb");
    assert(f && fwrite("CORR", 1, 4, f) == 4 && fclose(f) == 0);
    assert(pn_tsumego_open(&game, &pool, &media, root, 2500) == PN_OK);
    assert(pn_tsumego_screen(&game) == PN_TG_ERROR);
    assert(pn_tsumego_event(&game, PN_TG_RETRY, 2600) != PN_OK && pn_tsumego_screen(&game) == PN_TG_ERROR);
    assert(pn_tsumego_event(&game, PN_TG_CHOOSE, 2700) == PN_EMPTY && pn_tsumego_screen(&game) == PN_TG_ERROR);
    pn_tsumego_close(&game);
    f = fopen(fa, "rb");
    char check[4] = {0};
    assert(f && fread(check, 1, 4, f) == 4 && fclose(f) == 0 && memcmp(check, "CORR", 4) == 0);

    clean_dir(root);
    assert(pool.used == 0 && pool.live == 0);
    puts("test_tsumego passed");
    return 0;
}
