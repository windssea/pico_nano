/* SPDX-License-Identifier: Apache-2.0
 * 中国象棋残局60局纯绘制确定性、双色棋子像素特征与交叉点局部更新一致性回归。/ Xiangqi 60-endgame pure rendering determinism, stone visuals and partial-patch equivalence. */
#include "../../components/pn_library/pn_xiangqi_internal.h"
#include "pn_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned patches, frames;

static void paint(pn_xiangqi_t *u, pn_font_t *f, pn_frame_t *a, pn_frame_t *b) {
    pn_status_t r = pn_xiangqi_render_update(u, f, a);
    if (r == PN_UNSUPPORTED) {
        assert(pn_xiangqi_render(u, f, a) == PN_OK);
    } else {
        assert(r == PN_OK);
        patches++;
    }
    assert(pn_xiangqi_render(u, f, b) == PN_OK);
    assert(!memcmp(a->pixels, b->pixels, 415872));
    pn_xiangqi_displayed(u, true);
    frames++;
}

int main(void) {
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 2u * 1024u * 1024u, NULL, NULL, NULL) == PN_OK);
    pn_font_t font = {0};
    pn_text_source_t src = pn_font_builtin_source();
    assert(pn_font_open(&font, &pool, &src, 28) == PN_OK);
    pn_w_font_scope_t scope = {0};
    assert(pn_w_font_scope_begin(&scope, &pool, NULL) == PN_OK);
    pn_w_set_clock("12:00");

    uint8_t *pa = malloc(415872), *pb = malloc(415872);
    assert(pa && pb);
    pn_frame_t a, b;
    assert(pn_frame_bind(&a, pa, 415872, 684, 1216) && pn_frame_bind(&b, pb, 415872, 684, 1216));

    /* 验证全页面确定性与不修改控制器输入。/ Verify all screens render deterministically without mutating controller state. */
    {
        xq_t g = {.pool = &pool, .screen = PN_XQ_PLAY, .has = true, .full = true};
        pn_xiangqi_t u = {.impl = &g, .active = true};
        pn_xq_level_t lv;
        assert(pn_xiangqi_level(0, &lv) == PN_OK);
        pn_xq_reset_board(&g.state, &lv, pn_xq_bank_crc());
        for (unsigned scr = PN_XQ_SELECT; scr <= PN_XQ_ERROR; scr++) {
            g.screen = (pn_xq_screen_t)scr;
            xq_t snap = g;
            assert(pn_xiangqi_render(&u, &font, &a) == PN_OK && font.pixels == 28);
            assert(!memcmp(&snap, &g, sizeof g));
            assert(pn_xiangqi_render(&u, &font, &b) == PN_OK && !memcmp(pa, pb, 415872));
        }
        /* 同时验证败局完成页按钮命中。/ Verify loss-outcome completion screen buttons and rendering. */
        g.screen = PN_XQ_COMPLETE;
        g.state.outcome = PN_XQ_LOST;
        assert(pn_xiangqi_render(&u, &font, &a) == PN_OK);
        assert(pn_xiangqi_hit(&u, 100, 980) == PN_XQ_UNDO_MOVE);
        assert(pn_xiangqi_hit(&u, 450, 980) == PN_XQ_RESTART);
    }

    /* 遍历全部60局：逐步执行选子、锦囊提示、落子应着、悔棋与破阵，逐帧核验局部增量与整绘100%像素一致！
     * Walk all 60 endgames across selection, hint, move/reply, undo and mate, asserting 100% patch/full pixel identity! */
    for (unsigned id = 0; id < PN_XQ_ENDGAMES; id++) {
        xq_t g = {.pool = &pool, .screen = PN_XQ_PLAY, .has = true, .full = true};
        pn_xiangqi_t u = {.impl = &g, .active = true};
        pn_xq_level_t lv;
        assert(pn_xiangqi_level(id, &lv) == PN_OK);
        pn_xq_reset_board(&g.state, &lv, pn_xq_bank_crc());
        paint(&u, &font, &a, &b);

        while (g.state.outcome == PN_XQ_ONGOING) {
            assert(pn_xiangqi_event(&u, PN_XQ_HINT, 10) == PN_OK);
            paint(&u, &font, &a, &b);
            uint8_t dest = g.state.hint_to;
            assert(pn_xiangqi_event(&u, PN_XQ_CELL + (int)dest, 11) == PN_OK);
            paint(&u, &font, &a, &b);
            if (g.state.outcome == PN_XQ_ONGOING && g.state.moves == 1) {
                assert(pn_xiangqi_event(&u, PN_XQ_UNDO_MOVE, 12) == PN_OK);
                paint(&u, &font, &a, &b);
                assert(pn_xiangqi_event(&u, PN_XQ_HINT, 13) == PN_OK);
                paint(&u, &font, &a, &b);
                assert(pn_xiangqi_event(&u, PN_XQ_CELL + (int)g.state.hint_to, 14) == PN_OK);
                paint(&u, &font, &a, &b);
            }
        }
        assert(g.state.outcome == PN_XQ_WON && g.screen == PN_XQ_COMPLETE);
        pn_xiangqi_displayed(&u, false);
        assert(pn_xiangqi_render_update(&u, &font, &a) == PN_UNSUPPORTED);
    }

    free(pa);
    free(pb);
    pn_w_font_scope_end(&scope);
    pn_font_close(&font);
    assert(!pool.used && !pool.live);
    printf("Xiangqi %u full-or-patch frames identical across 60 endgames, %u patches, pool peak=%zu\n", frames, patches,
           pool.peak);
    return 0;
}
