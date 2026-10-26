/* SPDX-License-Identifier: Apache-2.0
 * 同一90关逐动作比较变格与整绘，失败后禁止复用基准。/ Compare patch/full pixels through 90 levels, rejecting failed
 * bases. */
#include "pn_sokoban_internal.h"
#include "pn_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned patches, frames;
static void paint(pn_sokoban_t *u, pn_font_t *f, pn_frame_t *a, pn_frame_t *b) {
    pn_status_t r = pn_sokoban_render_update(u, f, a);
    if (r == PN_UNSUPPORTED)
        assert(pn_sokoban_render(u, f, a) == PN_OK);
    else {
        assert(r == PN_OK);
        patches++;
    }
    assert(pn_sokoban_render(u, f, b) == PN_OK);
    assert(!memcmp(a->pixels, b->pixels, 415872));
    pn_sokoban_displayed(u, true);
    frames++;
}
int main(void) {
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 2u * 1024u * 1024u, NULL, NULL, NULL) == PN_OK);
    pn_font_t font = {0};
    pn_text_source_t src = pn_font_builtin_source();
    assert(pn_font_open(&font, &pool, &src, 28) == PN_OK);
    pn_w_set_clock("12:00");
    pn_frame_t a, b;
    uint8_t *pa = malloc(415872), *pb = malloc(415872);
    assert(pa && pb);
    assert(pn_frame_bind(&a, pa, 415872, 684, 1216) && pn_frame_bind(&b, pb, 415872, 684, 1216));
    for (unsigned id = 0; id < 90; id++) {
        so_t g = {.pool = &pool, .screen = PN_SO_PLAY, .has = true};
        pn_sokoban_t u = {.impl = &g, .active = true};
        assert(pn_sokoban_level(id, &g.state.level) == PN_OK);
        memcpy(g.state.boxes, g.state.level.initial_boxes, 16);
        g.state.player = g.state.level.initial_player;
        paint(&u, &font, &a, &b);
        for (unsigned j = 0; j < g.state.level.proof_count; j++) {
            unsigned box = g.state.level.solution[j * 2], d = g.state.level.solution[j * 2 + 1];
            int stand = so_neighbor(&g.state.level, box, (d + 2) % 4);
            if (g.state.player != (unsigned)stand) {
                assert(pn_sokoban_event(&u, PN_SO_CELL + stand, j + 1) == PN_OK);
                paint(&u, &font, &a, &b);
            }
            assert(pn_sokoban_event(&u, PN_SO_UP + (int)d, j + 1) == PN_OK);
            assert(pn_sokoban_event(&u, PN_SO_UP + (int)d, j + 1) == PN_BUSY);
            paint(&u, &font, &a, &b);
            assert(pn_sokoban_event(&u, PN_SO_UNDO_MOVE, j + 1) == PN_OK);
            paint(&u, &font, &a, &b);
            assert(pn_sokoban_event(&u, PN_SO_UP + (int)d, j + 1) == PN_OK);
            paint(&u, &font, &a, &b);
        }
        assert(g.state.complete);
        pn_sokoban_displayed(&u, false);
        assert(pn_sokoban_render_update(&u, &font, &a) == PN_UNSUPPORTED);
    }
    free(pa);
    free(pb);
    pn_font_close(&font);
    assert(!pool.used && !pool.live);
    printf("Sokoban %u full-or-patch frames identical, %u patches, shared-font pool peak=%zu\n", frames, patches,
           pool.peak);
    return 0;
}
