/* SPDX-License-Identifier: Apache-2.0
 * 像素仓库与变格绘制，不读文件不触碰硬件。/ Pixel warehouse and changed-cell rendering, without files or hardware
 * effects. */
#include "pn_sokoban_internal.h"
#include "pn_widgets.h"
#include <stdio.h>
static const char *const tiers[] = {"低难度", "中难度", "高难度"};

#define DRAW(x)                                                                                                        \
    do {                                                                                                               \
        pn_status_t r = (x);                                                                                           \
        if (r != PN_OK)                                                                                                \
            return r;                                                                                                  \
    } while (0)
static bool inside(int x, int y, int a, int b, int w, int h) { return x >= a && y >= b && x < a + w && y < b + h; }
static void geometry(const pn_so_level_t *l, int *x, int *y, int *cell) {
    int c = 612 / l->width, h = 684 / l->height;
    if (c > h)
        c = h;
    if (c > 96)
        c = 96;
    c -= c % 2;
    *cell = c;
    *x = (684 - c * l->width) / 2;
    *y = 184 + (684 - c * l->height) / 2;
}
static void stroke(pn_frame_t *p, int x, int y, int w, int h, int n, uint8_t color) {
    pn_frame_rect(p, x, y, w, n, color);
    pn_frame_rect(p, x, y + h - n, w, n, color);
    pn_frame_rect(p, x, y, n, h, color);
    pn_frame_rect(p, x + w - n, y, n, h, color);
}
static void cell(pn_frame_t *p, const pn_so_state_t *s, unsigned pos) {
    int x, y, c;
    geometry(&s->level, &x, &y, &c);
    x += (int)(pos % s->level.width) * c;
    y += (int)(pos / s->level.width) * c;

    pn_frame_rect(p, x, y, c, c, 15);

    if (so_bit(s->level.walls, pos)) {
        pn_frame_rect(p, x + 1, y + 1, c - 2, c - 2, 12);
        pn_frame_rect(p, x + 2, y + c - 4, c - 4, 3, 8);
        stroke(p, x + 1, y + 1, c - 2, c - 2, 1, 7);

        pn_frame_rect(p, x + c / 2, y + 3, 1, c / 2 - 3, 10);
        pn_frame_rect(p, x + 3, y + c / 2, c - 6, 1, 10);
        return;
    }
    if (so_bit(s->level.goals, pos)) {
        int a = c / 5;
        pn_w_round_stroke(p, x + c / 2 - a, y + c / 2 - a, a * 2, a * 2, a, 2, 3);
    }
    if (so_bit(s->boxes, pos)) {
        int a = c / 7, w = c - 2 * a;
        pn_frame_rect(p, x + a + 2, y + a + 3, w, w, 8);
        pn_frame_rect(p, x + a, y + a, w, w, 15);
        stroke(p, x + a, y + a, w, w, 3, 0);

        for (int i = 5; i < w - 5; i++) {
            pn_frame_rect(p, x + a + i, y + a + i, 2, 2, 3);
            pn_frame_rect(p, x + a + w - i - 2, y + a + i, 2, 2, 3);
        }
        if (so_bit(s->level.goals, pos))
            pn_frame_rect(p, x + a + 5, y + a + 5, c / 7, c / 7, 0);
    }
    if (s->player == pos) {
        int head = c / 8;
        pn_w_round_fill(p, x + c / 2 - head, y + c / 5, head * 2, head * 2, head, 0);
        pn_w_round_fill(p, x + c / 3, y + c / 2, c / 3, c / 4, 4, 0);
        pn_frame_rect(p, x + c / 3, y + c * 3 / 4, c / 9, c / 10, 0);
        pn_frame_rect(p, x + c * 5 / 9, y + c * 3 / 4, c / 9, c / 10, 0);
    }
}
static pn_status_t button(pn_font_t *f, pn_frame_t *p, const char *text, int x, int y, int w, int h, bool selected) {
    pn_w_round_fill(p, x + 1, y + 4, w - 2, h - 4, 9, 7);
    pn_w_round_fill(p, x, y, w, h - 4, 9, selected ? 0 : 15);
    pn_w_round_outline(p, x, y, w, h - 4, 9, 1, selected ? 0 : 8);

    DRAW(pn_font_size(f, 26));
    return pn_w_text_ex(f, p, text, x, y + h / 2 + 8, w, PN_ALIGN_CENTER, selected ? 15 : 0, false);
}
static pn_status_t label(pn_font_t *f, pn_frame_t *p, const char *text, int y, int size) {
    DRAW(pn_font_size(f, size));
    return pn_w_text(f, p, text, 32, y, 620, PN_ALIGN_CENTER);
}
pn_status_t pn_sokoban_render_update(const pn_sokoban_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p)
        return PN_INVALID;

    const so_t *g = u->impl;
    if (!g->patch || g->full || g->screen != PN_SO_PLAY)
        return PN_UNSUPPORTED;

    for (unsigned c = 0; c < g->state.level.width * g->state.level.height; c++)
        if (c == g->shown_player || c == g->state.player || so_bit(g->shown_boxes, c) != so_bit(g->state.boxes, c))
            cell(p, &g->state, c);

    return PN_OK;
}
pn_status_t pn_sokoban_render(const pn_sokoban_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p)
        return PN_INVALID;
    const so_t *g = u->impl;
    char text[96];
    pn_frame_clear(p, 15);
    DRAW(pn_w_status(f, p));
    DRAW(pn_w_header(f, p, g->pending_start ? NULL : "返回", "推箱子", NULL));

    if (g->screen == PN_SO_PLAY || g->screen == PN_SO_COMPLETE) {
        snprintf(text, sizeof text, "%s  /  第 %u 关", tiers[g->state.level.tier], g->state.level.id % 30 + 1);
        DRAW(label(f, p, text, 166, 23));

        for (unsigned c = 0; c < g->state.level.width * g->state.level.height; c++)
            cell(p, &g->state, c);

        if (g->screen == PN_SO_COMPLETE) {
            snprintf(text, sizeof text, "完成  ·  %lu 步  /  推箱 %lu 次", (unsigned long)g->state.moves,
                     (unsigned long)g->state.pushes);
            DRAW(label(f, p, text, 915, 28));

            DRAW(button(f, p, "下一关", 36, 960, 292, 80, false));
            DRAW(button(f, p, "选关", 356, 960, 292, 80, false));
            DRAW(button(f, p, "撤销", 36, 1064, 292, 80, false));
            DRAW(button(f, p, "返回娱乐", 356, 1064, 292, 80, false));

        } else {
            static const char *const names[] = {"撤销", "重玩", "选关", "清屏"};
            for (int i = 0; i < 4; i++)
                DRAW(button(f, p, names[i], 36 + i * 156, 880, 144, 64, false));

            DRAW(button(f, p, "上", 244, 960, 196, 72, false));
            DRAW(button(f, p, "左", 36, 1040, 196, 72, false));
            DRAW(button(f, p, "右", 452, 1040, 196, 72, false));
            DRAW(button(f, p, "下", 244, 1120, 196, 72, false));

            DRAW(pn_font_size(f, 19));
            DRAW(pn_w_text(f, p, "点空地走路", 244, 1065, 196, PN_ALIGN_CENTER));
            DRAW(pn_w_text(f, p, "箱子只能推", 244, 1092, 196, PN_ALIGN_CENTER));
        }
    } else if (g->screen == PN_SO_SELECT) {
        DRAW(label(f, p, "把箱子推到圆点上", 254, 34));
        DRAW(label(f, p, "点空地走路，方向键推箱", 310, 25));

        for (int i = 0; i < 3; i++)
            DRAW(button(f, p, tiers[i], 36 + i * 208, 396, 196, 92, (unsigned)i == g->tier));

        snprintf(text, sizeof text, "第 %u / 30 关", g->selected + 1);
        DRAW(label(f, p, text, 660, 42));

        DRAW(button(f, p, "上一关", 36, 728, 292, 80, false));
        DRAW(button(f, p, "下一关", 356, 728, 292, 80, false));
        DRAW(button(f, p, "开始游戏", 36, 908, 612, 88, false));

        if (g->has)
            DRAW(button(f, p, "继续当前棋局", 36, 1030, 612, 80, false));

    } else if (g->screen == PN_SO_CONFIRM) {
        DRAW(label(f, p, g->restart ? "重新开始本关？" : "保存并选择其他关卡？", 468, 34));
        DRAW(button(f, p, "确认", 36, 660, 612, 84, false));
        DRAW(button(f, p, "取消", 36, 772, 612, 84, false));

    } else {
        DRAW(label(f, p, g->pending_start ? "保存尚未确认" : "无法恢复棋局", 468, 34));
        DRAW(button(f, p, "重试", 36, 660, 612, 84, false));
        if (g->pending_start)
            DRAW(label(f, p, "请重试保存后退出", 816, 24));
        else
            DRAW(button(f, p, "返回", 36, 772, 612, 84, false));
    }
    if (g->notice) {
        pn_frame_rect(p, 0, 1194, 684, 22, 15);
        DRAW(label(f, p, g->notice, 1211, 18));
    }
    return PN_OK;
}
int pn_sokoban_hit(const pn_sokoban_t *u, int x, int y) {
    if (!u || !u->impl)
        return 0;
    const so_t *g = u->impl;

    if (!g->pending_start && pn_w_header_hit(x, y, false) == 1)
        return PN_SO_BACK;

    if (g->notice && y >= 1194)
        return PN_SO_RETRY;

    if (g->screen == PN_SO_SELECT) {
        for (int i = 0; i < 3; i++)
            if (inside(x, y, 36 + i * 208, 396, 196, 92))
                return PN_SO_TIER + i;

        if (inside(x, y, 36, 728, 292, 80))
            return PN_SO_PREV_LEVEL;
        if (inside(x, y, 356, 728, 292, 80))
            return PN_SO_NEXT_LEVEL;

        if (inside(x, y, 36, 908, 612, 88))
            return PN_SO_START;
        if (g->has && inside(x, y, 36, 1030, 612, 80))
            return PN_SO_CANCEL;

    } else if (g->screen == PN_SO_CONFIRM || g->screen == PN_SO_ERROR) {
        if (inside(x, y, 36, 660, 612, 84))
            return g->screen == PN_SO_CONFIRM ? PN_SO_ACCEPT : PN_SO_RETRY;
        if (!g->pending_start && inside(x, y, 36, 772, 612, 84))
            return g->screen == PN_SO_CONFIRM ? PN_SO_CANCEL : PN_SO_BACK;

    } else if (g->screen == PN_SO_COMPLETE) {
        if (inside(x, y, 36, 960, 292, 80))
            return PN_SO_NEXT;
        if (inside(x, y, 356, 960, 292, 80))
            return PN_SO_CHOOSE;
        if (inside(x, y, 36, 1064, 292, 80))
            return PN_SO_UNDO_MOVE;
        if (inside(x, y, 356, 1064, 292, 80))
            return PN_SO_EXIT;

    } else if (g->screen == PN_SO_PLAY) {
        if (inside(x, y, 244, 960, 196, 72))
            return PN_SO_UP;
        if (inside(x, y, 36, 1040, 196, 72))
            return PN_SO_LEFT;
        if (inside(x, y, 452, 1040, 196, 72))
            return PN_SO_RIGHT;
        if (inside(x, y, 244, 1120, 196, 72))
            return PN_SO_DOWN;

        static const int cmds[] = {PN_SO_UNDO_MOVE, PN_SO_RESTART, PN_SO_CHOOSE, PN_SO_CLEAN};
        for (int i = 0; i < 4; i++)
            if (inside(x, y, 36 + i * 156, 880, 144, 64))
                return cmds[i];

        int a, b, c;
        geometry(&g->state.level, &a, &b, &c);
        if (inside(x, y, a, b, c * g->state.level.width, c * g->state.level.height))
            return PN_SO_CELL + (y - b) / c * g->state.level.width + (x - a) / c;
    }
    return 0;
}
int pn_sokoban_touch(pn_sokoban_t *u, unsigned count, int x, int y, bool valid) {
    if (!u || !u->impl)
        return 0;

    so_t *g = u->impl;

    if (!valid || count > 1) {
        g->down = false;
        g->blocked = true;
        return 0;
    }
    if (!count) {
        if (g->blocked) {
            g->blocked = false;
            g->down = false;
            return 0;
        }
        if (!g->down)
            return 0;

        g->down = false;
        int dx = g->last_x - g->touch_x, dy = g->last_y - g->touch_y;

        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;

        if (g->screen == PN_SO_PLAY && pn_sokoban_hit(u, g->touch_x, g->touch_y) >= PN_SO_CELL &&
            (ax >= 40 || ay >= 40)) {
            if (ax > ay * 3 / 2)
                return dx > 0 ? PN_SO_RIGHT : PN_SO_LEFT;

            if (ay > ax * 3 / 2)
                return dy > 0 ? PN_SO_DOWN : PN_SO_UP;

            return 0;
        }
        int before = pn_sokoban_hit(u, g->touch_x, g->touch_y), after = pn_sokoban_hit(u, g->last_x, g->last_y);

        return before == after ? before : 0;
    }
    if (g->blocked)
        return 0;

    if (!g->down) {
        g->touch_x = x;
        g->touch_y = y;
        g->down = true;
    }
    g->last_x = x;
    g->last_y = y;
    return 0;
}
