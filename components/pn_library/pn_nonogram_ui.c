/* SPDX-License-Identifier: Apache-2.0
 * 数织纯绘制、五格分组提示栏与同行/列变格增量更新。/ Nonogram pure rendering, 5-cell grouped clues and partial updates. */
#include "pn_nonogram_internal.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <string.h>

static const char *const tiers[] = {"简单", "中等", "困难"};
static const char *const tier_notes[] = {"5×5 · 8×8", "10×10", "15×15"};

#define DRAW(expr)                                                                                                     \
    do {                                                                                                               \
        pn_status_t r_ = (expr);                                                                                       \
        if (r_ != PN_OK)                                                                                               \
            return r_;                                                                                                 \
    } while (0)

static bool inside(int x, int y, int a, int b, int w, int h) { return x >= a && y >= b && x < a + w && y < b + h; }

typedef struct {
    int bx, by;     ///< 含提示区的整体起点 / Outer origin including clue headers
    int gx, gy;     ///< 棋盘网格起点 / Puzzle grid origin
    int clue_w;     ///< 左侧行提示宽 / Left row-clue width
    int clue_h;     ///< 顶部列提示高 / Top col-clue height
    int cell;       ///< 单格像素边长 / Single cell side in pixels
} no_geom_t;

static no_geom_t geometry(const pn_no_puzzle_t *p) {
    no_geom_t m;
    if (p->width <= 5) {
        m.clue_w = 132;
        m.clue_h = 132;
        m.cell = 76;
    } else if (p->width <= 8) {
        m.clue_w = 140;
        m.clue_h = 140;
        m.cell = 56;
    } else if (p->width <= 10) {
        m.clue_w = 152;
        m.clue_h = 152;
        m.cell = 44;
    } else {
        m.clue_w = 162;
        m.clue_h = 162;
        m.cell = 30;
    }
    int total_w = m.clue_w + (int)p->width * m.cell;
    int total_h = m.clue_h + (int)p->height * m.cell;
    m.bx = (684 - total_w) / 2;
    m.by = 166 + (764 - total_h) / 2;
    m.gx = m.bx + m.clue_w;
    m.gy = m.by + m.clue_h;
    return m;
}

static void pixel_fill(pn_frame_t *p, int x, int y, int w, int h, int cut, uint8_t ink) {
    pn_frame_rect(p, x + cut, y, w - cut * 2, h, ink);
    pn_frame_rect(p, x + cut / 2, y + cut / 2, w - cut, h - cut, ink);
    pn_frame_rect(p, x, y + cut, w, h - cut * 2, ink);
}

static void cap(pn_frame_t *p, int x, int y, int w, int h, bool selected) {
    pixel_fill(p, x + 2, y + 4, w - 2, h - 4, 6, 7);
    pixel_fill(p, x, y, w, h - 4, 6, 0);
    pixel_fill(p, x + 3, y + 3, w - 6, h - 10, 4, selected ? 0 : 15);
    if (!selected)
        pn_frame_rect(p, x + 8, y + h - 10, w - 16, 3, 11);
}

static pn_status_t text_c(pn_font_t *f, pn_frame_t *p, const char *s, int x, int y, int w, int size, uint8_t ink,
                          bool bold) {
    DRAW(pn_font_size(f, size));
    return pn_w_text_ex(f, p, s, x, y, w, PN_ALIGN_CENTER, ink, bold);
}

static pn_status_t button(pn_font_t *f, pn_frame_t *p, const char *s, int x, int y, int w, int h, bool selected) {
    cap(p, x, y, w, h, selected);
    return text_c(f, p, s, x, y + h / 2 + 9, w - 2, 27, selected ? 15 : 0, selected);
}

static pn_status_t heading(const no_t *g, pn_font_t *f, pn_frame_t *p) {
    if (!g->pending_start) {
        cap(p, 36, 48, 148, 48, false);
        DRAW(text_c(f, p, "返回", 36, 80, 148, 26, 0, false));
    }
    DRAW(text_c(f, p, "数织", 210, 82, 264, 32, 0, true));
    if (g->screen == PN_NO_PLAY || g->screen == PN_NO_COMPLETE) {
        cap(p, 500, 48, 148, 48, false);
        DRAW(text_c(f, p, "选关", 500, 80, 148, 26, 0, false));
    }
    return PN_OK;
}

static pn_status_t metadata(const no_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_no_state_t *s = &g->state;
    char left[96], right[96];
    pn_frame_rect(p, 32, 112, 620, 46, 15);
    if (s->complete)
        snprintf(left, sizeof left, "%s · 第 %u 关「%s」", tiers[s->puzzle.tier], s->puzzle.id % 30 + 1, s->puzzle.title);
    else
        snprintf(left, sizeof left, "%s · 第 %u 关 (%ux%u)", tiers[s->puzzle.tier], s->puzzle.id % 30 + 1,
                 s->puzzle.width, s->puzzle.height);
    DRAW(pn_font_size(f, 22));
    DRAW(pn_w_text_ex(f, p, left, 36, 144, 356, PN_ALIGN_LEFT, 0, true));

    unsigned filled = pn_nonogram_filled_count(s);
    unsigned long long sec = s->elapsed_ms / 1000u;
    snprintf(right, sizeof right, "涂 %u/%u · %02llu:%02llu", filled, s->puzzle.target_count, sec / 60, sec % 60);
    DRAW(pn_font_size(f, 20));
    return pn_w_text_ex(f, p, right, 396, 144, 252, PN_ALIGN_RIGHT, PN_UI_MUTED, false);
}

static pn_status_t draw_row_clue(const pn_no_state_t *s, const no_geom_t *m, unsigned r, pn_font_t *f, pn_frame_t *p) {
    uint8_t clues[PN_NO_MAX_CLUES];
    unsigned n = pn_nonogram_row_clues(&s->puzzle, r, clues);
    bool done = pn_nonogram_row_satisfied(s, r);
    int y = m->gy + (int)r * m->cell;
    pn_frame_rect(p, m->bx + 2, y + 1, m->clue_w - 4, m->cell - 2, done ? 14 : 13);
    if ((r + 1) % 5 == 0 && r + 1 < s->puzzle.height)
        pn_frame_rect(p, m->bx + 3, y + m->cell - 2, m->clue_w - 6, 2, 4);
    else if (r + 1 < s->puzzle.height)
        pn_frame_rect(p, m->bx + 3, y + m->cell - 1, m->clue_w - 6, 1, 10);

    int font_px = s->puzzle.width >= 15 ? 18 : s->puzzle.width >= 10 ? 20 : 24;
    int slot_w = (m->clue_w - 10) / (int)(n ? n : 1);
    if (slot_w > 34)
        slot_w = 34;
    int start_x = m->gx - 6 - (int)n * slot_w;
    for (unsigned i = 0; i < n; i++) {
        char buf[8];
        snprintf(buf, sizeof buf, "%u", clues[i]);
        DRAW(text_c(f, p, buf, start_x + (int)i * slot_w, y + m->cell / 2 + font_px / 3 + 1, slot_w, font_px,
                    done ? 9 : 0, !done));
    }
    return PN_OK;
}

static pn_status_t draw_col_clue(const pn_no_state_t *s, const no_geom_t *m, unsigned c, pn_font_t *f, pn_frame_t *p) {
    uint8_t clues[PN_NO_MAX_CLUES];
    unsigned n = pn_nonogram_col_clues(&s->puzzle, c, clues);
    bool done = pn_nonogram_col_satisfied(s, c);
    int x = m->gx + (int)c * m->cell;
    pn_frame_rect(p, x + 1, m->by + 2, m->cell - 2, m->clue_h - 4, done ? 14 : 13);
    if ((c + 1) % 5 == 0 && c + 1 < s->puzzle.width)
        pn_frame_rect(p, x + m->cell - 2, m->by + 3, 2, m->clue_h - 6, 4);
    else if (c + 1 < s->puzzle.width)
        pn_frame_rect(p, x + m->cell - 1, m->by + 3, 1, m->clue_h - 6, 10);

    int font_px = s->puzzle.height >= 15 ? 18 : s->puzzle.height >= 10 ? 20 : 24;
    int slot_h = (m->clue_h - 10) / (int)(n ? n : 1);
    if (slot_h > 30)
        slot_h = 30;
    int start_y = m->gy - 6 - (int)n * slot_h;
    for (unsigned i = 0; i < n; i++) {
        char buf[8];
        snprintf(buf, sizeof buf, "%u", clues[i]);
        DRAW(text_c(f, p, buf, x, start_y + (int)i * slot_h + slot_h / 2 + font_px / 3, m->cell, font_px,
                    done ? 9 : 0, !done));
    }
    return PN_OK;
}

static void draw_cell(const pn_no_state_t *s, const no_geom_t *m, unsigned pos, pn_frame_t *p) {
    unsigned r = pos / s->puzzle.width, c = pos % s->puzzle.width;
    int x = m->gx + (int)c * m->cell, y = m->gy + (int)r * m->cell, side = m->cell;
    pn_frame_rect(p, x, y, side, side, 15);
    pn_w_outline(p, x, y, side, side, 1, 9);
    if (c > 0 && c % 5 == 0)
        pn_frame_rect(p, x, y, 2, side, 0);
    if (r > 0 && r % 5 == 0)
        pn_frame_rect(p, x, y, side, 2, 0);
    if ((c + 1) % 5 == 0 && c + 1 < s->puzzle.width)
        pn_frame_rect(p, x + side - 1, y, 1, side, 0);
    if ((r + 1) % 5 == 0 && r + 1 < s->puzzle.height)
        pn_frame_rect(p, x, y + side - 1, side, 1, 0);

    uint8_t state = no_cell_get(s->cells, pos);
    if (state == PN_NO_FILL) {
        int pad = s->complete ? 1 : (side >= 44 ? 4 : 3);
        pn_frame_rect(p, x + pad, y + pad, side - pad * 2, side - pad * 2, 0);
    } else if (state == PN_NO_MARK && !s->complete) {
        int pad = side >= 56 ? 14 : side >= 44 ? 10 : 7;
        int thick = side >= 44 ? 3 : 2;
        for (int k = pad; k < side - pad - thick + 1; k++) {
            pn_frame_rect(p, x + k, y + k, thick, thick, 3);
            pn_frame_rect(p, x + side - k - thick, y + k, thick, thick, 3);
        }
    }
    if (!s->complete && pos == s->selected) {
        int b = side >= 44 ? 3 : 2;
        pn_w_outline(p, x + 2, y + 2, side - 4, side - 4, b, state == PN_NO_FILL ? 15 : 0);
    }
}

static void draw_board_borders(const pn_no_state_t *s, const no_geom_t *m, pn_frame_t *p) {
    int tw = m->clue_w + (int)s->puzzle.width * m->cell;
    int th = m->clue_h + (int)s->puzzle.height * m->cell;
    pn_w_outline(p, m->bx, m->by, tw, th, 3, 0);
    pn_frame_rect(p, m->gx - 2, m->by, 3, th, 0);
    pn_frame_rect(p, m->bx, m->gy - 2, tw, 3, 0);
}

static pn_status_t draw_mode_keys(const pn_no_state_t *s, pn_font_t *f, pn_frame_t *p) {
    pn_frame_rect(p, 32, 950, 620, 92, 15);
    DRAW(button(f, p, "填涂 ■", 36, 954, 296, 80, !s->cross_mode));
    return button(f, p, "标记 ×", 352, 954, 296, 80, s->cross_mode);
}

pn_status_t pn_nonogram_render_update(const pn_nonogram_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p)
        return PN_INVALID;
    const no_t *g = u->impl;
    if (!g->patch || g->full || g->screen != PN_NO_PLAY)
        return PN_UNSUPPORTED;

    int saved_px = f->pixels;
    const pn_no_state_t *s = &g->state;
    no_geom_t m = geometry(&s->puzzle);
    pn_status_t status = metadata(g, f, p);
    if (status == PN_OK && g->shown_cross_mode != s->cross_mode)
        status = draw_mode_keys(s, f, p);

    bool dirty_row[PN_NO_MAX_SIDE] = {0}, dirty_col[PN_NO_MAX_SIDE] = {0};
    unsigned area = (unsigned)s->puzzle.width * s->puzzle.height;
    for (unsigned pos = 0; status == PN_OK && pos < area; pos++) {
        uint8_t prev = no_cell_get(g->shown_cells, pos), cur = no_cell_get(s->cells, pos);
        if (prev != cur || pos == g->shown_selected || pos == s->selected) {
            draw_cell(s, &m, pos, p);
            if (prev != cur) {
                dirty_row[pos / s->puzzle.width] = true;
                dirty_col[pos % s->puzzle.width] = true;
            }
        }
    }
    for (unsigned r = 0; status == PN_OK && r < s->puzzle.height; r++)
        if (dirty_row[r])
            status = draw_row_clue(s, &m, r, f, p);
    for (unsigned c = 0; status == PN_OK && c < s->puzzle.width; c++)
        if (dirty_col[c])
            status = draw_col_clue(s, &m, c, f, p);
    if (status == PN_OK)
        draw_board_borders(s, &m, p);
    pn_status_t restored = pn_font_size(f, saved_px);
    return status == PN_OK ? restored : status;
}

pn_status_t pn_nonogram_render(const pn_nonogram_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p)
        return PN_INVALID;
    const no_t *g = u->impl;
    int saved_px = f->pixels;
    pn_frame_clear(p, 15);
    pn_status_t status = pn_w_status(f, p);
    if (status == PN_OK)
        status = heading(g, f, p);

    if (status == PN_OK && (g->screen == PN_NO_PLAY || g->screen == PN_NO_COMPLETE)) {
        const pn_no_state_t *s = &g->state;
        no_geom_t m = geometry(&s->puzzle);
        status = metadata(g, f, p);
        if (status == PN_OK) {
            pn_frame_rect(p, m.bx + 2, m.by + 2, m.clue_w - 4, m.clue_h - 4, 14);
            /* 左上角装饰小像素纹。/ Subtle top-left corner mosaic motif. */
            int cx = m.bx + m.clue_w / 2 - 14, cy = m.by + m.clue_h / 2 - 14;
            pn_w_outline(p, cx, cy, 28, 28, 2, 6);
            pn_frame_rect(p, cx + 4, cy + 4, 9, 9, 4);
            pn_frame_rect(p, cx + 15, cy + 15, 9, 9, 4);
        }
        for (unsigned r = 0; status == PN_OK && r < s->puzzle.height; r++)
            status = draw_row_clue(s, &m, r, f, p);
        for (unsigned c = 0; status == PN_OK && c < s->puzzle.width; c++)
            status = draw_col_clue(s, &m, c, f, p);
        if (status == PN_OK) {
            unsigned area = (unsigned)s->puzzle.width * s->puzzle.height;
            for (unsigned pos = 0; pos < area; pos++)
                draw_cell(s, &m, pos, p);
            draw_board_borders(s, &m, p);
        }
        if (status == PN_OK) {
            if (g->screen == PN_NO_PLAY) {
                status = draw_mode_keys(s, f, p);
                static const char *const tools[] = {"撤销", "提示", "重玩", "清屏"};
                for (int i = 0; status == PN_OK && i < 4; i++)
                    status = button(f, p, tools[i], 36 + i * 156, 1054, 144, 68, false);
                if (status == PN_OK)
                    status = text_c(f, p, "点按或同行同列拖涂 · 满行自动变淡", 36, 1168, 612, 21, PN_UI_MUTED, false);
            } else {
                char msg[96];
                snprintf(msg, sizeof msg, "解谜完成 · 「%s」", s->puzzle.title);
                status = text_c(f, p, msg, 36, 962, 612, 28, 0, true);
                if (status == PN_OK)
                    status = button(f, p, "下一关", 36, 992, 296, 78, true);
                if (status == PN_OK)
                    status = button(f, p, "选关", 352, 992, 296, 78, false);
                if (status == PN_OK)
                    status = button(f, p, "重玩本关", 36, 1090, 296, 78, false);
                if (status == PN_OK)
                    status = button(f, p, "返回娱乐", 352, 1090, 296, 78, false);
            }
        }
    } else if (status == PN_OK && g->screen == PN_NO_SELECT) {
        status = text_c(f, p, "依行列数字推理，还原像素画卷", 36, 236, 612, 30, 0, true);
        if (status == PN_OK)
            status = text_c(f, p, "全部谜题纯逻辑唯一解，无需猜测", 36, 288, 612, 23, PN_UI_MUTED, false);
        for (int i = 0; status == PN_OK && i < 3; i++) {
            int x = 36 + i * 208;
            bool sel = (unsigned)i == g->tier;
            cap(p, x, 356, 196, 104, sel);
            status = text_c(f, p, tiers[i], x, 400, 196, 28, sel ? 15 : 0, true);
            if (status == PN_OK)
                status = text_c(f, p, tier_notes[i], x, 438, 196, 20, sel ? 15 : PN_UI_MUTED, false);
        }
        if (status == PN_OK) {
            unsigned id = g->tier * 30 + g->selected_level;
            bool solved_before = g->has && ((g->state.completed[id / 8] >> (id % 8)) & 1u) != 0;
            char line[96];
            snprintf(line, sizeof line, "第 %u / 30 关%s", g->selected_level + 1, solved_before ? " · 已通关" : "");
            status = text_c(f, p, line, 36, 608, 612, 38, 0, true);
        }
        if (status == PN_OK)
            status = button(f, p, "上一关", 36, 676, 296, 80, false);
        if (status == PN_OK)
            status = button(f, p, "下一关", 352, 676, 296, 80, false);
        if (status == PN_OK)
            status = button(f, p, "开始解谜", 36, 846, 612, 88, true);
        if (status == PN_OK && g->has)
            status = button(f, p, "继续当前谜题", 36, 964, 612, 84, false);
    } else if (status == PN_OK && g->screen == PN_NO_CONFIRM) {
        status = text_c(f, p, g->restart ? "清空并重玩本关？" : "保存并选择其他关卡？", 36, 470, 612, 34, 0, true);
        if (status == PN_OK)
            status = button(f, p, "确认", 36, 660, 612, 84, true);
        if (status == PN_OK)
            status = button(f, p, "取消", 36, 772, 612, 84, false);
    } else if (status == PN_OK) {
        status = text_c(f, p, g->pending_start ? "新局保存尚未确认" : "无法恢复谜题记录", 36, 470, 612, 34, 0, true);
        if (status == PN_OK)
            status = text_c(f, p, "原记录保留，请重试", 36, 536, 612, 26, PN_UI_MUTED, false);
        if (status == PN_OK)
            status = button(f, p, "重试", 36, 660, 612, 84, true);
        if (status == PN_OK && !g->pending_start)
            status = button(f, p, "返回", 36, 772, 612, 84, false);
    }

    if (status == PN_OK && g->notice) {
        pn_frame_rect(p, 0, 1188, 684, 28, 15);
        status = text_c(f, p, g->notice, 36, 1210, 612, 20, 0, false);
    }
    pn_status_t restored = pn_font_size(f, saved_px);
    return status == PN_OK ? restored : status;
}

int pn_nonogram_hit(const pn_nonogram_t *u, int x, int y) {
    if (!u || !u->impl)
        return 0;
    const no_t *g = u->impl;
    if (!g->pending_start && inside(x, y, 28, 40, 164, 60))
        return PN_NO_BACK;
    if (g->notice && y >= 1188)
        return PN_NO_RETRY;

    if (g->screen == PN_NO_PLAY || g->screen == PN_NO_COMPLETE) {
        if (inside(x, y, 492, 40, 164, 60))
            return PN_NO_CHOOSE;
        if (g->screen == PN_NO_PLAY) {
            no_geom_t m = geometry(&g->state.puzzle);
            int gw = (int)g->state.puzzle.width * m.cell, gh = (int)g->state.puzzle.height * m.cell;
            if (inside(x, y, m.gx, m.gy, gw, gh))
                return PN_NO_CELL + (y - m.gy) / m.cell * (int)g->state.puzzle.width + (x - m.gx) / m.cell;
            if (inside(x, y, 36, 954, 296, 80))
                return PN_NO_FILL_MODE;
            if (inside(x, y, 352, 954, 296, 80))
                return PN_NO_CROSS_MODE;
            static const int cmds[] = {PN_NO_UNDO_MOVE, PN_NO_HINT, PN_NO_RESTART, PN_NO_CLEAN};
            for (int i = 0; i < 4; i++)
                if (inside(x, y, 36 + i * 156, 1054, 144, 68))
                    return cmds[i];
        } else {
            if (inside(x, y, 36, 992, 296, 78))
                return PN_NO_NEXT;
            if (inside(x, y, 352, 992, 296, 78))
                return PN_NO_CHOOSE;
            if (inside(x, y, 36, 1090, 296, 78))
                return PN_NO_RESTART;
            if (inside(x, y, 352, 1090, 296, 78))
                return PN_NO_EXIT;
        }
    } else if (g->screen == PN_NO_SELECT) {
        for (int i = 0; i < 3; i++)
            if (inside(x, y, 36 + i * 208, 356, 196, 104))
                return PN_NO_TIER + i;
        if (inside(x, y, 36, 676, 296, 80))
            return PN_NO_PREV_LEVEL;
        if (inside(x, y, 352, 676, 296, 80))
            return PN_NO_NEXT_LEVEL;
        if (inside(x, y, 36, 846, 612, 88))
            return PN_NO_START;
        if (g->has && inside(x, y, 36, 964, 612, 84))
            return PN_NO_RESUME;
    } else if (g->screen == PN_NO_CONFIRM) {
        if (inside(x, y, 36, 660, 612, 84))
            return PN_NO_ACCEPT;
        if (inside(x, y, 36, 772, 612, 84))
            return PN_NO_CANCEL;
    } else if (g->screen == PN_NO_ERROR) {
        if (inside(x, y, 36, 660, 612, 84))
            return PN_NO_RETRY;
        if (!g->pending_start && inside(x, y, 36, 772, 612, 84))
            return PN_NO_BACK;
    }
    return 0;
}

int pn_nonogram_touch(pn_nonogram_t *u, unsigned count, int x, int y, bool valid) {
    if (!u || !u->impl)
        return 0;
    no_t *g = u->impl;
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

        int first = pn_nonogram_hit(u, g->touch_x, g->touch_y);
        int last = pn_nonogram_hit(u, g->last_x, g->last_y);
        if (g->screen == PN_NO_PLAY && first >= PN_NO_CELL) {
            unsigned from = (unsigned)(first - PN_NO_CELL);
            no_geom_t m = geometry(&g->state.puzzle);
            int w = (int)g->state.puzzle.width, h = (int)g->state.puzzle.height;
            int dx = g->last_x - g->touch_x, dy = g->last_y - g->touch_y;
            int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            if (ax < m.cell / 2 && ay < m.cell / 2)
                return first;
            int col = (g->last_x - m.gx) / m.cell;
            int row = (g->last_y - m.gy) / m.cell;
            if (col < 0)
                col = 0;
            if (col >= w)
                col = w - 1;
            if (row < 0)
                row = 0;
            if (row >= h)
                row = h - 1;
            unsigned to = ax >= ay ? (from / (unsigned)w) * (unsigned)w + (unsigned)col
                                   : (unsigned)row * (unsigned)w + (from % (unsigned)w);
            return to == from ? first : PN_NO_STROKE + (int)(from * PN_NO_CELLS + to);
        }
        return first == last ? first : 0;
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
