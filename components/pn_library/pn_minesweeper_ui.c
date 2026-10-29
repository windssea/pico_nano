/* SPDX-License-Identifier: Apache-2.0
 * 黑白像素棋盘，共用点击几何且绘制无副作用。/ Monochrome pixel board with shared hit geometry and pure drawing. */
#include "pn_minesweeper_internal.h"
#include "pn_widgets.h"
#include <stdio.h>
static const char *const tiers[] = {"简单", "中等", "困难"};
#define DRAW(expr) do { pn_status_t r = (expr); if (r != PN_OK) return r; } while (0)
static bool inside(int x, int y, int a, int b, int w, int h) { return x >= a && y >= b && x < a + w && y < b + h; }
void mi_geometry(const pn_mi_state_t *s, int *x, int *y, int *c) {
    int cell = 588 / s->width, h = 756 / s->height;
    if (cell > h) cell = h;
    if (cell > 64) cell = 64;
    cell -= cell % 2; *c = cell; *x = (684 - cell * s->width) / 2; *y = 244 + (756 - cell * s->height) / 2;
}
/// 整数阶梯边与短灰底沿，保持设计图的白面键帽。/ Integer stepped edges and a short gray base preserve the reference's white key faces.
static void pixel_fill(pn_frame_t *p, int x, int y, int w, int h, int cut, uint8_t ink) {
    pn_frame_rect(p, x + cut, y, w - cut * 2, h, ink);
    pn_frame_rect(p, x + cut / 2, y + cut / 2, w - cut, h - cut, ink);
    pn_frame_rect(p, x, y + cut, w, h - cut * 2, ink);
}
static void cap(pn_frame_t *p, int x, int y, int w, int h, bool selected) {
    pixel_fill(p, x + 2, y + 4, w - 2, h - 4, 6, 6);
    pixel_fill(p, x, y, w, h - 4, 6, 0);
    pixel_fill(p, x + 3, y + 3, w - 6, h - 10, 4, selected ? 0 : 15);
    if (!selected) pn_frame_rect(p, x + 8, y + h - 10, w - 16, 3, 11);
}
static pn_status_t text(pn_font_t *f, pn_frame_t *p, const char *s, int x, int y, int w, int size, bool invert) {
    DRAW(pn_font_size(f, size)); return pn_w_text_ex(f, p, s, x, y, w, PN_ALIGN_CENTER, invert ? 15 : 0, false);
}
static pn_status_t button(pn_font_t *f, pn_frame_t *p, const char *s, int x, int y, int w, int h, bool selected) {
    cap(p, x, y, w, h, selected); return text(f, p, s, x, y + h / 2 + 8, w - 3, 28, selected);
}
enum { MI_ICON_FLAG, MI_ICON_OPEN, MI_ICON_CLOCK, MI_ICON_BACK };
/// 同一个16点图形整数缩放用于格内、模式键和计数器；旗面向右，避免像数字1。/ The same 16-dot shape scales into cells, mode keys and counters; a right-facing banner avoids resembling digit one.
static void icon(pn_frame_t *p, int x, int y, unsigned kind, int unit, uint8_t ink) {
    static const char shapes[4][16][17] = {
        {"................","...##...........","...####.........","...#######......","...##########...","...###########..","...##########...","...#######......","...####.........","...##...........","...##...........","...##...........","...##...........","...##...........",".######.........",".######........."},
        {"................","....##########..","...##........#..","..##.........#..","..#..........#..","..#..##......#..","..#..##......#..","..#..........#..","..#..........#..","..#..........#..","..#..........#..","..#..........#..","..############..",".##..........#..",".#############..","................"},
        {"......####......","......####......",".......##.......","....########....","...##......##...","..##........##..",".##.....#....##.",".#......#.....#.",".#......#.....#.",".#......####..#.",".#............#.",".##..........##.","..##........##..","...##......##...","....########....","................"},
        {"................","......##........",".....###........","....####........","...#####........","..############..",".#############..","##############..","##############..",".#############..","..############..","...#####........","....####........",".....###........","......##........","................"}
    };
    for (int row = 0; row < 16; row++) for (int col = 0; col < 16; col++)
        if (shapes[kind][row][col] == '#') pn_frame_rect(p, x + col * unit, y + row * unit, unit, unit, ink);
}
static void flag(pn_frame_t *p, int x, int y, int c, bool wrong) {
    int unit = (c - 10) / 16, at = (c - unit * 16) / 2;
    icon(p, x + at, y + at, MI_ICON_FLAG, unit, 0);
    if (wrong) for (int k = 6; k < c - 6; k++) pn_frame_rect(p, x + k, y + k, 2, 2, 0);
}
static void mine(pn_frame_t *p, int x, int y, int c, bool exploded) {
    int a = c / 4, cx = x + c / 2, cy = y + c / 2;
    if (exploded) pn_w_outline(p, x + 3, y + 3, c - 6, c - 6, 3, 0);
    pn_frame_rect(p, cx - a, cy - a / 2, a * 2, a, 0); pn_frame_rect(p, cx - a / 2, cy - a, a, a * 2, 0);
    pn_frame_rect(p, cx - 1, cy - a - 4, 2, a * 2 + 8, 0); pn_frame_rect(p, cx - a - 4, cy - 1, a * 2 + 8, 2, 0);
    pn_frame_rect(p, cx - a / 2, cy - a / 2, 3, 3, 15);
}
static pn_status_t board(const mi_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_mi_state_t *s = &g->state; int x, y, c; mi_geometry(s, &x, &y, &c);
    for (unsigned n = 0; n < (unsigned)s->width * s->height; n++) {
        int a = x + (int)(n % s->width) * c, b = y + (int)(n / s->width) * c;
        bool open = mi_bit(s->opened, n), show_mine = s->result >= PN_MI_WON && mi_bit(s->mines, n);
        pn_frame_rect(p, a, b, c, c, 15);
        if (!open && !show_mine) {
            pn_frame_rect(p, a + 1, b + 1, c - 2, c - 2, 10);
            pn_frame_rect(p, a + 2, b + 2, c - 4, 3, 15); pn_frame_rect(p, a + 2, b + 2, 3, c - 4, 15);
            pn_frame_rect(p, a + 3, b + c - 5, c - 5, 3, 5); pn_frame_rect(p, a + c - 5, b + 3, 3, c - 5, 5);
        }
        pn_w_outline(p, a, b, c, c, 1, 0);
        if (show_mine) mine(p, a, b, c, n == s->exploded);
        else if (mi_bit(s->flags, n)) flag(p, a, b, c, s->result == PN_MI_LOST);
        else if (open) { unsigned count = pn_minesweeper_adjacent(s, n); if (count) { char digit[2] = {(char)('0' + count), 0}; DRAW(text(f, p, digit, a, b + c / 2 + c / 4, c, c * 2 / 3, false)); } }
        if (s->result < PN_MI_WON && n == s->selected) pn_w_outline(p, a + 5, b + 5, c - 10, c - 10, 2, 0);
    }
    pn_w_outline(p, x - 3, y - 3, s->width * c + 6, s->height * c + 6, 3, 0);
    return PN_OK;
}
pn_status_t pn_minesweeper_render_update(const pn_minesweeper_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p) return PN_INVALID;
    return PN_UNSUPPORTED;
}
/// 标题和图标共用键帽边缘，不借用圆角胶囊。/ Heading icons share the keycap edge rather than the rounded global pill.
static pn_status_t heading(const mi_t *g, pn_font_t *f, pn_frame_t *p) {
    if (!g->pending_start) {
        cap(p, 36, 48, 152, 48, false); icon(p, 45, 55, MI_ICON_BACK, 2, 0);
        DRAW(text(f, p, "返回", 82, 80, 98, 28, false));
    }
    DRAW(pn_font_size(f, 32)); DRAW(pn_w_text_ex(f, p, "扫雷", 220, 82, 244, PN_ALIGN_CENTER, 0, true));
    if (g->screen == PN_MI_PLAY || g->screen == PN_MI_COMPLETE) {
        cap(p, 516, 48, 132, 48, false); DRAW(text(f, p, "新局", 516, 80, 132, 26, false));
    }
    return PN_OK;
}
/// 模式键的翻牌/旗帜在反白状态仍保留轮廓。/ Reveal and flag silhouettes remain visible on inverted mode keys.
static pn_status_t mode_button(pn_font_t *f, pn_frame_t *p, int x, bool flag_mode, bool selected) {
    cap(p, x, 1036, 292, 84, selected);
    icon(p, x + 76, 1057, flag_mode ? MI_ICON_FLAG : MI_ICON_OPEN, 2, selected ? 15 : 0);
    return text(f, p, flag_mode ? "插旗" : "翻开", x + 124, 1086, 96, 28, selected);
}
/// 难度页签、旗子计数、静态进度和计时器保持设计图的位置层次。/ Difficulty tabs, flag count, static progress and timer follow the reference hierarchy.
static pn_status_t metadata(const mi_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_mi_state_t *s = &g->state; char line[64];
    static const char *const dimensions[] = {"9×9 / 10雷", "12×16 / 40雷", "14×18 / 65雷"};
    for (unsigned tier = 0; tier < 3; tier++) {
        int x = 36 + (int)tier * 208; bool selected = s->tier == tier;
        cap(p, x, 120, 196, 68, selected);
        DRAW(text(f, p, tiers[tier], x, 147, 196, 24, selected));
        DRAW(text(f, p, dimensions[tier], x, 174, 196, 18, selected));
    }
    icon(p, 36, 200, MI_ICON_FLAG, 2, 0);
    snprintf(line, sizeof line, "%u 雷", s->mine_count - pn_minesweeper_flags(s)); DRAW(text(f, p, line, 76, 227, 136, 28, false));
    unsigned opened = pn_minesweeper_opened(s), safe = (unsigned)s->width * s->height - s->mine_count;
    pn_w_outline(p, 230, 197, 210, 20, 2, 0);
    for (unsigned n = 0; n < 20; n++) pn_frame_rect(p, 234 + (int)n * 10, 201, 8, 12, n * safe < opened * 20 ? 0 : 12);
    snprintf(line, sizeof line, "%u / %u", opened, safe); DRAW(text(f, p, line, 230, 234, 210, 16, false));
    icon(p, 478, 199, MI_ICON_CLOCK, 2, 0); unsigned long long sec = s->elapsed_ms / 1000;
    snprintf(line, sizeof line, "%02llu:%02llu", sec / 60, sec % 60); DRAW(text(f, p, line, 516, 227, 132, 24, false));
    return PN_OK;
}
pn_status_t pn_minesweeper_render(const pn_minesweeper_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p) return PN_INVALID;
    const mi_t *g = u->impl; pn_frame_clear(p, 15); DRAW(pn_w_status(f, p)); DRAW(heading(g, f, p));
    if (g->screen == PN_MI_PLAY || g->screen == PN_MI_COMPLETE) {
        const pn_mi_state_t *s = &g->state; DRAW(metadata(g, f, p)); DRAW(board(g, f, p));
        if (g->screen == PN_MI_PLAY) {
            DRAW(mode_button(f, p, 36, false, !s->flag_mode)); DRAW(mode_button(f, p, 356, true, s->flag_mode));
            DRAW(text(f, p, "首步安全 · 点数字可展开", 36, 1168, 612, 22, false));
        } else { DRAW(button(f, p, "再来一局", 36, 1036, 292, 84, true)); DRAW(button(f, p, "返回娱乐", 356, 1036, 292, 84, false)); DRAW(text(f, p, s->result == PN_MI_WON ? "排雷成功" : "踩到地雷", 36, 1168, 612, 26, false)); }
    } else if (g->screen == PN_MI_SELECT) {
        DRAW(text(f, p, "找出地雷，留下安全的一格", 36, 254, 612, 30, false));
        static const char *const detail[] = {"简单 · 9 × 9 · 10 雷", "中等 · 12 × 16 · 40 雷", "困难 · 14 × 18 · 65 雷"};
        for (unsigned i = 0; i < 3; i++) DRAW(button(f, p, detail[i], 36, 362 + (int)i * 150, 612, 112, i == g->tier));
        DRAW(text(f, p, "首步及周围八格无雷", 36, 866, 612, 26, false));
        DRAW(button(f, p, "开始游戏", 36, 940, 612, 88, true));
        if (g->has) DRAW(button(f, p, "继续当前棋局", 36, 1060, 612, 88, false));
    } else if (g->screen == PN_MI_CONFIRM) {
        DRAW(text(f, p, "替换当前棋局？", 36, 478, 612, 36, false)); DRAW(text(f, p, "确认后可重新选择难度", 36, 550, 612, 27, false));
        DRAW(button(f, p, "确认", 36, 708, 612, 80, true)); DRAW(button(f, p, "取消", 36, 812, 612, 80, false));
    } else {
        DRAW(text(f, p, g->pending_start ? "新局尚未确认保存" : "无法恢复棋局", 36, 478, 612, 34, false));
        DRAW(text(f, p, "原记录保留，请重试", 36, 550, 612, 27, false)); DRAW(button(f, p, "重试", 36, 708, 612, 80, true));
        if (!g->pending_start) DRAW(button(f, p, "返回", 36, 812, 612, 80, false));
    }
    if (g->notice) { pn_frame_rect(p, 0, 1186, 684, 30, 15); DRAW(text(f, p, g->notice, 36, 1210, 612, 20, false)); }
    return PN_OK;
}
int pn_minesweeper_hit(const pn_minesweeper_t *u, int x, int y) {
    if (!u || !u->impl) return 0;
    const mi_t *g = u->impl;
    if (!g->pending_start && inside(x, y, 28, 40, 160, 60)) return PN_MI_BACK;
    if (g->screen == PN_MI_PLAY || g->screen == PN_MI_COMPLETE) {
        if (inside(x, y, 508, 40, 160, 60)) return PN_MI_NEW;
        for (int tier = 0; tier < 3; tier++) if (inside(x, y, 36 + tier * 208, 120, 196, 68)) return PN_MI_TIER + tier;
        int a, b, c; mi_geometry(&g->state, &a, &b, &c);
        if (g->screen == PN_MI_PLAY && inside(x, y, a, b, g->state.width * c, g->state.height * c)) return PN_MI_CELL + (y - b) / c * g->state.width + (x - a) / c;
        if (inside(x, y, 36, 1036, 292, 84)) return g->screen == PN_MI_PLAY ? PN_MI_REVEAL_MODE : PN_MI_NEW;
        if (inside(x, y, 356, 1036, 292, 84)) return g->screen == PN_MI_PLAY ? PN_MI_FLAG_MODE : PN_MI_EXIT;
    } else if (g->screen == PN_MI_SELECT) {
        for (int i = 0; i < 3; i++) if (inside(x, y, 36, 362 + i * 150, 612, 112)) return PN_MI_TIER + i;
        if (inside(x, y, 36, 940, 612, 88)) return PN_MI_START;
        if (g->has && inside(x, y, 36, 1060, 612, 88)) return PN_MI_RESUME;
    } else if (g->screen == PN_MI_CONFIRM) {
        if (inside(x, y, 36, 708, 612, 80)) return PN_MI_ACCEPT;
        if (inside(x, y, 36, 812, 612, 80)) return PN_MI_CANCEL;
    } else if (g->screen == PN_MI_ERROR) {
        if (inside(x, y, 36, 708, 612, 80)) return PN_MI_RETRY;
        if (!g->pending_start && inside(x, y, 36, 812, 612, 80)) return PN_MI_BACK;
    }
    return 0;
}
