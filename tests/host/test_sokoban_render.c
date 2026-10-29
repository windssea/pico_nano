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
/// 小型固定布局分别观察人物、砖、空靶和落靶箱，不以整张截图哈希代替语义。/ A fixed small layout isolates the worker, masonry, empty goal and occupied goal instead of relying on a whole-screenshot hash.
static void icon_checks(pn_font_t *f, pn_frame_t *p) {
    so_t g = {.screen = PN_SO_PLAY, .has = true};
    g.state.level.width = g.state.level.height = 6;
    g.state.player = 7;
    so_set(g.state.level.walls, 0, true);
    so_set(g.state.level.goals, 8, true);
    so_set(g.state.boxes, 9, true);
    so_set(g.state.level.goals, 10, true);
    so_set(g.state.boxes, 10, true);
    pn_sokoban_t u = {.impl = &g, .active = true};
    pn_so_state_t before = g.state;
    assert(pn_sokoban_render(&u, f, p) == PN_OK && !memcmp(&before, &g.state, sizeof before));
    // 6×6布局使用96px格；16×20工人为4px整倍数，帽檐、白脸双眼与并立双腿分别可见。
    // The 6x6 layout uses 96px cells and a 16x20 worker at 4x scale, exposing the brim, white face, two eyes and parallel legs.
    const int left = 166, top = 342, unit = 4;
    assert(pn_frame_get(p, left + 2 * unit + 1, top + 4 * unit + 1) == 0);
    assert(pn_frame_get(p, left + 4 * unit + 1, top + 5 * unit + 1) == 15);
    assert(pn_frame_get(p, left + 5 * unit + 1, top + 6 * unit + 1) == 0);
    assert(pn_frame_get(p, left + 10 * unit + 1, top + 6 * unit + 1) == 0);
    assert(pn_frame_get(p, left + 7 * unit + 1, top + 6 * unit + 1) == 15);
    assert(pn_frame_get(p, left + 5 * unit + 1, top + 18 * unit + 1) == 0);
    assert(pn_frame_get(p, left + 9 * unit + 1, top + 18 * unit + 1) == 0);
    assert(pn_frame_get(p, left + 7 * unit + 1, top + 18 * unit + 1) == 15);
    // 墙主体深于亮沿，错缝为黑；箱子双框/粗X，落靶用反白X，空靶保留白心环与中心点。
    // Walls are darker than their top edges with black staggered joints; double-framed crates have thick X braces inverted on goals, while empty goals retain a white ring interior and center dot.
    assert(pn_frame_get(p, 64, 241) == 11 && pn_frame_get(p, 64, 248) == 5 && pn_frame_get(p, 118, 248) == 0);
    assert(pn_frame_get(p, 294, 358) == 0 && pn_frame_get(p, 302, 382) == 15 && pn_frame_get(p, 294, 382) == 0);
    assert(pn_frame_get(p, 355, 347) == 0 && pn_frame_get(p, 360, 352) == 0);
    assert(pn_frame_get(p, 390, 382) == 0 && pn_frame_get(p, 486, 382) == 15);
    assert(pn_frame_get(p, 54, 942) == 8);
    // 四个方向使用同一像素三角的旋转；三角外侧留白，命中坐标保持原四方向语义。
    // All directions rotate the same pixel triangle; outside pixels stay white and original hit coordinates retain their directional meaning.
    assert(pn_frame_get(p, 340, 984) == 0 && pn_frame_get(p, 337, 984) == 15 && pn_frame_get(p, 340, 981) == 15);
    assert(pn_frame_get(p, 123, 1073) == 0 && pn_frame_get(p, 557, 1073) == 0 && pn_frame_get(p, 340, 1162) == 0);
    assert(pn_sokoban_hit(&u, 340, 992) == PN_SO_UP && pn_sokoban_hit(&u, 130, 1070) == PN_SO_LEFT);
    assert(pn_sokoban_hit(&u, 550, 1070) == PN_SO_RIGHT && pn_sokoban_hit(&u, 340, 1150) == PN_SO_DOWN);
}
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
    icon_checks(&font, &a);
    for (unsigned id = 0; id < 90; id++) {
        so_t g = {.pool = &pool, .screen = PN_SO_PLAY, .has = true};
        pn_sokoban_t u = {.impl = &g, .active = true};
        assert(pn_sokoban_level(id, &g.state.level) == PN_OK);
        memcpy(g.state.boxes, g.state.level.initial_boxes, 16);
        g.state.player = g.state.level.initial_player;
        paint(&u, &font, &a, &b);
        // 操作键有留白阶梯角和粗黑外沿，不依赖阴影识别。/ Controls retain white stepped corners and strong ink edges, independent of their shadows.
        assert(pn_frame_get(&a, 36, 880) == 15 && pn_frame_get(&a, 42, 880) == 0);
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
