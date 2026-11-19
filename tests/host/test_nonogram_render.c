/* SPDX-License-Identifier: Apache-2.0
 * 数织三档纯绘制确定性、手势命中与局部变格一致性回归。/ Nonogram pure rendering determinism, touch gestures and partial-patch equivalence. */
#include "../../components/pn_library/pn_nonogram_internal.h"
#include "pn_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned patches, frames;

static void paint(pn_nonogram_t *u, pn_font_t *f, pn_frame_t *a, pn_frame_t *b) {
    pn_status_t r = pn_nonogram_render_update(u, f, a);
    if (r == PN_UNSUPPORTED) {
        assert(pn_nonogram_render(u, f, a) == PN_OK);
    } else {
        assert(r == PN_OK);
        patches++;
    }
    assert(pn_nonogram_render(u, f, b) == PN_OK);
    assert(!memcmp(a->pixels, b->pixels, 415872));
    pn_nonogram_displayed(u, true);
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

    static const unsigned sample_ids[] = {0, 15, 30, 45, 60, 89};
    for (unsigned idx = 0; idx < sizeof sample_ids / sizeof sample_ids[0]; idx++) {
        no_t g = {.pool = &pool, .screen = PN_NO_PLAY, .has = true, .full = true};
        pn_nonogram_t u = {.impl = &g, .active = true};
        assert(pn_nonogram_puzzle(sample_ids[idx], &g.state.puzzle) == PN_OK);
        g.tier = g.state.puzzle.tier;

        for (unsigned scr = PN_NO_SELECT; scr <= PN_NO_ERROR; scr++) {
            g.screen = (pn_no_screen_t)scr;
            no_t snap = g;
            assert(pn_nonogram_render(&u, &font, &a) == PN_OK && font.pixels == 28);
            assert(!memcmp(&snap, &g, sizeof g));
            assert(pn_nonogram_render(&u, &font, &b) == PN_OK && !memcmp(pa, pb, 415872));
        }

        g.screen = PN_NO_PLAY;
        g.full = true;
        paint(&u, &font, &a, &b);

        /* 验证点按与同行/列拖涂手势映射。/ Verify single-cell tap and straight-line drag touch mapping. */
        int cell = g.state.puzzle.width <= 5 ? 76 : g.state.puzzle.width <= 8 ? 56 : g.state.puzzle.width <= 10 ? 44 : 30;
        int clue = g.state.puzzle.width <= 5 ? 132 : g.state.puzzle.width <= 8 ? 140 : g.state.puzzle.width <= 10 ? 152 : 162;
        int gx = (684 - (clue + (int)g.state.puzzle.width * cell)) / 2 + clue;
        int gy = 166 + (764 - (clue + (int)g.state.puzzle.height * cell)) / 2 + clue;
        assert(pn_nonogram_touch(&u, 1, gx + 4, gy + 4, true) == 0);
        assert(pn_nonogram_touch(&u, 1, gx + 3 * cell + 4, gy + 4, true) == 0);
        int stroke_cmd = pn_nonogram_touch(&u, 0, gx + 3 * cell + 4, gy + 4, true);
        assert(stroke_cmd == PN_NO_STROKE + 3);
        assert(pn_nonogram_event(&u, stroke_cmd, 10) == PN_OK);
        paint(&u, &font, &a, &b);

        assert(pn_nonogram_event(&u, PN_NO_CROSS_MODE, 11) == PN_OK);
        paint(&u, &font, &a, &b);
        assert(pn_nonogram_event(&u, PN_NO_CELL + (int)g.state.puzzle.width, 12) == PN_OK);
        paint(&u, &font, &a, &b);
        assert(pn_nonogram_event(&u, PN_NO_UNDO_MOVE, 13) == PN_OK);
        paint(&u, &font, &a, &b);
        assert(pn_nonogram_event(&u, PN_NO_UNDO_MOVE, 14) == PN_OK);
        paint(&u, &font, &a, &b);

        for (unsigned step = 0; step < 6 && !g.state.complete; step++) {
            assert(pn_nonogram_event(&u, PN_NO_HINT, 20 + step) == PN_OK);
            paint(&u, &font, &a, &b);
        }
        pn_nonogram_displayed(&u, false);
        assert(pn_nonogram_render_update(&u, &font, &a) == PN_UNSUPPORTED);
    }

    free(pa);
    free(pb);
    pn_w_font_scope_end(&scope);
    pn_font_close(&font);
    assert(!pool.used && !pool.live);
    printf("Nonogram %u full-or-patch frames identical, %u patches, pool peak=%zu\n", frames, patches, pool.peak);
    return 0;
}
