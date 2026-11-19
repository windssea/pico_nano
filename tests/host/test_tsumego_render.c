/* SPDX-License-Identifier: Apache-2.0
 * 围棋死活题绘制、命中与逐交叉点局部增量更新逐像素等价测试。
 * Go Tsumego rendering, hit testing and pixel-exact per-intersection partial update verification. */
#define _POSIX_C_SOURCE 200809L
#include "pn_tsumego.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void assert_patch_matches_full(pn_tsumego_t *game, pn_font_t *font, pn_frame_t *patch_fb, uint8_t *full_px) {
    pn_frame_t full_fb;
    assert(pn_frame_bind(&full_fb, full_px, 415872u, 684u, 1216u));
    assert(pn_tsumego_render_update(game, font, patch_fb) == PN_OK);
    assert(pn_tsumego_render(game, font, &full_fb) == PN_OK);
    assert(memcmp(patch_fb->pixels, full_fb.pixels, 415872u) == 0);
    pn_tsumego_displayed(game, true);
}

int main(void) {
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 4u * 1024u * 1024u, NULL, NULL, NULL) == PN_OK);
    pn_media_t media;
    pn_media_init(&media);
    assert(pn_media_attach(&media, 1u) == PN_OK);

    pn_font_t font = {0};
    pn_text_source_t src = pn_font_builtin_source();
    assert(pn_font_open(&font, &pool, &src, 24) == PN_OK);

    uint8_t *px_a = pn_alloc_tagged(&pool, PN_MEM_FRAME, 415872u);
    uint8_t *px_b = pn_alloc_tagged(&pool, PN_MEM_FRAME, 415872u);
    assert(px_a && px_b);
    pn_frame_t fb;
    assert(pn_frame_bind(&fb, px_a, 415872u, 684u, 1216u));

    char root[] = "/tmp/pn-test-tsumego-render-XXXXXX";
    assert(mkdtemp(root));
    pn_tsumego_t game = {0};
    assert(pn_tsumego_open(&game, &pool, &media, root, 100) == PN_OK);
    assert(pn_tsumego_render(&game, &font, &fb) == PN_OK);
    pn_tsumego_displayed(&game, true);
    assert(pn_tsumego_hit(&game, 340, 880) == PN_TG_START);
    assert(pn_tsumego_hit(&game, 100, 70) == PN_TG_BACK);

    /* 进入第13题（2手倒扑提子），测试提示/落子/提子/败局/悔棋的局部与整绘逐像素一致 / Test 2-move snapback capture patch parity */
    assert(pn_tsumego_event(&game, PN_TG_CELL + 12, 200) == PN_OK);
    assert(pn_tsumego_render(&game, &font, &fb) == PN_OK);
    pn_tsumego_displayed(&game, true);
    /* 验证交叉点(0,0)和(8,8)命中坐标 / Verify corner intersection hit coords */
    assert(pn_tsumego_hit(&game, 70, 228) == PN_TG_CELL + 0);
    assert(pn_tsumego_hit(&game, 614, 772) == PN_TG_CELL + 80);

    /* 提示 -> 局部更新逐像素一致 */
    assert(pn_tsumego_event(&game, PN_TG_HINT, 300) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 走1手触发白棋提吃黑饵子 -> 局部更新逐像素一致 */
    uint8_t mv1 = pn_tsumego_state(&game)->hint_pos;
    assert(pn_tsumego_event(&game, PN_TG_CELL + mv1, 400) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 故意下错触发白反击失败 -> 局部更新逐像素一致 */
    assert(pn_tsumego_event(&game, PN_TG_CELL + 80, 500) == PN_OK);
    assert(pn_tsumego_state(&game)->outcome == PN_TG_FAILED);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 悔棋回退 -> 局部更新逐像素一致 */
    assert(pn_tsumego_event(&game, PN_TG_UNDO_MOVE, 600) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 切到段位题（含TL左角/TR右角）确认多方位棋盘整绘通过 / Render TL/TR tier-2 board */
    assert(pn_tsumego_event(&game, PN_TG_CHOOSE, 700) == PN_OK);
    assert(pn_tsumego_event(&game, PN_TG_TIER + 2, 800) == PN_OK);
    assert(pn_tsumego_event(&game, PN_TG_CELL + 18, 900) == PN_OK);
    assert(pn_tsumego_render(&game, &font, &fb) == PN_OK);

    pn_tsumego_close(&game);
    pn_font_close(&font);
    pn_free(px_a);
    pn_free(px_b);
    char p[512];
    snprintf(p, sizeof p, "%s/tsumego.a", root);
    (void)unlink(p);
    snprintf(p, sizeof p, "%s/tsumego.b", root);
    (void)unlink(p);
    assert(rmdir(root) == 0);
    assert(pool.used == 0 && pool.live == 0);
    puts("test_tsumego_render passed");
    return 0;
}
