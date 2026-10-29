/* SPDX-License-Identifier: Apache-2.0
 * 全难度界面纯度、边缘命中与失败基准回归。/ All-tier rendering purity, edge hits and failed-base regressions. */
#include "../../components/pn_library/pn_minesweeper_internal.h"
#include "pn_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    pn_pool_t pool; assert(pn_pool_init(&pool, 2u * 1024u * 1024u, NULL, NULL, NULL) == PN_OK);
    pn_font_t font = {0}; pn_text_source_t src = pn_font_builtin_source(); assert(pn_font_open(&font, &pool, &src, 28) == PN_OK); pn_w_set_clock("12:00");
    uint8_t *pixels = malloc(415872), *copy = malloc(415872); assert(pixels && copy); pn_frame_t frame; assert(pn_frame_bind(&frame, pixels, 415872, 684, 1216));
    for (unsigned tier = 0; tier < 3; tier++) {
        mi_t g = {.pool = &pool, .screen = PN_MI_PLAY, .has = true}; pn_minesweeper_t u = {.impl = &g, .active = true};
        assert(pn_minesweeper_new(&g.state, tier, 1234) == PN_OK); assert(pn_minesweeper_reveal(&g.state, 0) == PN_OK);
        int x, y, c; mi_geometry(&g.state, &x, &y, &c); assert(x >= 36 && y >= 244 && x + c * g.state.width <= 648 && y + c * g.state.height <= 1000 && c >= 42);
        assert(pn_minesweeper_hit(&u, x, y) == PN_MI_CELL); assert(pn_minesweeper_hit(&u, x + c * g.state.width - 1, y + c * g.state.height - 1) == PN_MI_CELL + g.state.width * g.state.height - 1);
        assert(!pn_minesweeper_hit(&u, x - 1, y) && !pn_minesweeper_hit(&u, x + c * g.state.width, y));
        assert(pn_minesweeper_hit(&u, 36, 1036) == PN_MI_REVEAL_MODE && pn_minesweeper_hit(&u, 356, 1036) == PN_MI_FLAG_MODE && !pn_minesweeper_hit(&u, 340, 1060));
        // 可见难度页签共享点击范围；页签之间留白不触发。/ Visible difficulty tabs share hit bounds; gaps do not activate them.
        for (int t = 0; t < 3; t++) assert(pn_minesweeper_hit(&u, 36 + t * 208, 120) == PN_MI_TIER + t);
        assert(!pn_minesweeper_hit(&u, 238, 150) && !pn_minesweeper_hit(&u, 446, 150));
        for (unsigned screen = PN_MI_SELECT; screen <= PN_MI_ERROR; screen++) {
            g.screen = (pn_mi_screen_t)screen; g.tier = tier; mi_t before = g; assert(pn_minesweeper_render(&u, &font, &frame) == PN_OK); assert(!memcmp(&before, &g, sizeof g));
            memcpy(copy, pixels, 415872); assert(pn_minesweeper_render(&u, &font, &frame) == PN_OK && !memcmp(copy, pixels, 415872));
        }
        g.screen = PN_MI_PLAY; pn_minesweeper_displayed(&u, false); assert(pn_minesweeper_render_update(&u, &font, &frame) == PN_UNSUPPORTED);
        for (unsigned n = 0; n < (unsigned)g.state.width * g.state.height; n++) if (mi_bit(g.state.mines, n)) { assert(pn_minesweeper_reveal(&g.state, n) == PN_OK); break; }
        g.screen = PN_MI_COMPLETE; assert(pn_minesweeper_render(&u, &font, &frame) == PN_OK); assert(pn_minesweeper_hit(&u, 36, 1036) == PN_MI_NEW);
    }
    free(pixels); free(copy); pn_font_close(&font); assert(!pool.used && !pool.live); printf("minesweeper render: three tiers, all pages, deterministic pure frames, geometry and loss glyphs passed\n"); return 0;
}
