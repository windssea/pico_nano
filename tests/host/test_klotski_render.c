/* SPDX-License-Identifier: Apache-2.0
 * 三国华容道绘制、命中、拖拽滑动与局部增量更新逐像素等价测试。
 * Klotski rendering, hit testing, drag-swiping and pixel-exact partial-update verification. */
#define _POSIX_C_SOURCE 200809L
#include "pn_klotski.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void assert_patch_matches_full(pn_klotski_t *game, pn_font_t *font, pn_frame_t *patch_fb, uint8_t *full_px) {
    pn_frame_t full_fb;
    assert(pn_frame_bind(&full_fb, full_px, 415872u, 684u, 1216u));
    assert(pn_klotski_render_update(game, font, patch_fb) == PN_OK);
    assert(pn_klotski_render(game, font, &full_fb) == PN_OK);
    assert(memcmp(patch_fb->pixels, full_fb.pixels, 415872u) == 0);
    pn_klotski_displayed(game, true);
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

    char root[] = "/tmp/pn-test-klotski-render-XXXXXX";
    assert(mkdtemp(root));
    pn_klotski_t game = {0};
    assert(pn_klotski_open(&game, &pool, &media, root, 100) == PN_OK);
    assert(pn_klotski_render(&game, &font, &fb) == PN_OK);
    pn_klotski_displayed(&game, true);
    assert(pn_klotski_hit(&game, 340, 880) == PN_KL_START);
    assert(pn_klotski_hit(&game, 100, 70) == PN_KL_BACK);

    /* 进入第1关并整页渲染基准 / Enter level 0 and paint full baseline */
    assert(pn_klotski_event(&game, PN_KL_START, 200) == PN_OK);
    assert(pn_klotski_render(&game, &font, &fb) == PN_OK);
    pn_klotski_displayed(&game, true);

    /* 提示 -> 局部更新逐像素校验 / Hint -> pixel-exact partial update */
    assert(pn_klotski_event(&game, PN_KL_HINT, 300) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 按提示拖拽滑动 -> 局部更新逐像素校验 / Drag-swipe hinted piece -> pixel-exact partial update */
    const pn_kl_state_t *st = pn_klotski_state(&game);
    unsigned hp = st->hint_piece, hd = st->hint_dir;
    unsigned pr = st->pos[hp] / PN_KL_COLS, pc = st->pos[hp] % PN_KL_COLS;
    int x0 = 70 + (int)pc * 136 + 68, y0 = 186 + (int)pr * 136 + 68;
    int dx = (hd == PN_KL_DIR_LEFT) ? -80 : (hd == PN_KL_DIR_RIGHT) ? 80 : 0;
    int dy = (hd == PN_KL_DIR_UP) ? -80 : (hd == PN_KL_DIR_DOWN) ? 80 : 0;
    assert(pn_klotski_touch(&game, 1u, x0, y0, true) == 0);
    assert(pn_klotski_touch(&game, 1u, x0 + dx, y0 + dy, true) == 0);
    int swipe_cmd = pn_klotski_touch(&game, 0u, x0 + dx, y0 + dy, true);
    assert(swipe_cmd >= PN_KL_SLIDE);
    assert(pn_klotski_event(&game, swipe_cmd, 400) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 撤销 -> 局部更新逐像素校验 / Undo -> pixel-exact partial update */
    assert(pn_klotski_event(&game, PN_KL_UNDO_MOVE, 500) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 重开确认框渲染 / Confirm dialog render */
    assert(pn_klotski_event(&game, swipe_cmd, 600) == PN_OK);
    assert(pn_klotski_event(&game, PN_KL_RESTART, 700) == PN_OK);
    assert(pn_klotski_screen(&game) == PN_KL_CONFIRM);
    assert(pn_klotski_render(&game, &font, &fb) == PN_OK);
    assert(pn_klotski_hit(&game, 200, 580) == PN_KL_CANCEL);
    assert(pn_klotski_hit(&game, 450, 580) == PN_KL_ACCEPT);

    pn_klotski_close(&game);
    pn_font_close(&font);
    pn_free(px_a);
    pn_free(px_b);
    char p[512];
    snprintf(p, sizeof p, "%s/klotski.a", root);
    (void)unlink(p);
    snprintf(p, sizeof p, "%s/klotski.b", root);
    (void)unlink(p);
    assert(rmdir(root) == 0);
    assert(pool.used == 0 && pool.live == 0);
    puts("test_klotski_render passed");
    return 0;
}
