/* SPDX-License-Identifier: Apache-2.0
 * 围棋死活题纯绘制：角/边实线棋盘、高光黑白棋子与逐交叉点局部增量更新。
 * Pure Go Tsumego rendering: true-edge corner/side board, specular stones and per-intersection partial updates. */
#include "pn_tsumego_internal.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <string.h>

static const char *const tiers[] = {"入门", "进阶", "段位"};
static const char *const tier_notes[] = {"1-2手基础死活", "2手连环手筋", "2-4手高阶攻防"};

enum {
    TG_STEP = 68,
    TG_HALF = 34,
    TG_X0 = 70,  /* col 0 center; tile x = 36..104, col 8 tile x = 580..648 */
    TG_Y0 = 228, /* row 0 center; tile y = 194..262, row 8 tile y = 738..806 */
    TG_BX = 36,
    TG_BY = 194,
    TG_BW = 612,
    TG_BH = 612,
    TG_STONE_R = 29
};

#define DRAW(expr)                                                                                                     \
    do {                                                                                                               \
        pn_status_t r_ = (expr);                                                                                       \
        if (r_ != PN_OK)                                                                                               \
            return r_;                                                                                                 \
    } while (0)

static bool inside(int x, int y, int a, int b, int w, int h) { return x >= a && y >= b && x < a + w && y < b + h; }

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

static pn_status_t heading(const tg_t *g, pn_font_t *f, pn_frame_t *p) {
    if (!g->pending_start) {
        cap(p, 36, 48, 148, 48, false);
        DRAW(text_c(f, p, "返回", 36, 80, 148, 26, 0, false));
    }
    DRAW(text_c(f, p, "围棋死活", 210, 82, 264, 32, 0, true));
    if (g->screen == PN_TG_PLAY || g->screen == PN_TG_COMPLETE) {
        cap(p, 500, 48, 148, 48, false);
        DRAW(text_c(f, p, "选关", 500, 80, 148, 26, 0, false));
    }
    return PN_OK;
}

static pn_status_t metadata(const tg_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_tg_state_t *s = &g->state;
    char left[96], right[96];
    pn_frame_rect(p, 32, 110, 620, 46, 15);
    snprintf(left, sizeof left, "%s · 第 %u 题「%s」", tiers[s->level.tier], s->level.id % 20u + 1u, s->level.title);
    DRAW(pn_font_size(f, 22));
    DRAW(pn_w_text_ex(f, p, left, 36, 142, 368, PN_ALIGN_LEFT, 0, true));
    unsigned long long sec = s->elapsed_ms / 1000u;
    snprintf(right, sizeof right, "%s · %u手 · %02llu:%02llu", pn_tsumego_goal_label(s->level.goal),
             s->level.main_moves, sec / 60u, sec % 60u);
    DRAW(pn_font_size(f, 19));
    return pn_w_text_ex(f, p, right, 404, 142, 244, PN_ALIGN_RIGHT, PN_UI_MUTED, false);
}

static pn_status_t status_prompt(const tg_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_tg_state_t *s = &g->state;
    char line[96];
    pn_frame_rect(p, 36, 848, 612, 50, 15);
    if (g->notice) {
        snprintf(line, sizeof line, "%s", g->notice);
    } else if (s->outcome == PN_TG_SOLVED) {
        snprintf(line, sizeof line, "正解！%s成功（共 %u 手）", pn_tsumego_goal_label(s->level.goal), s->moves);
    } else if (s->outcome == PN_TG_FAILED) {
        snprintf(line, sizeof line, "白棋反击破题，点「悔棋」退回重试");
    } else if (s->hint_pos < PN_TG_CELLS) {
        snprintf(line, sizeof line, "提示：请落子于圆环标记急所");
    } else if (s->moves == 0u) {
        snprintf(line, sizeof line, "%s：点击交叉点执黑落子", pn_tsumego_goal_label(s->level.goal));
    } else {
        snprintf(line, sizeof line, "白棋已应手，请继续执黑落子（已走 %u 手）", s->moves);
    }
    return text_c(f, p, line, 36, 882, 612, 23, 0, s->outcome != PN_TG_ONGOING || s->hint_pos < PN_TG_CELLS);
}

static bool is_star_point(uint8_t mask, unsigned r, unsigned c) {
    if (mask == (PN_TG_EDGE_TOP | PN_TG_EDGE_RIGHT)) return r == 3u && c == 5u;
    if (mask == (PN_TG_EDGE_TOP | PN_TG_EDGE_LEFT)) return r == 3u && c == 3u;
    if (mask == PN_TG_EDGE_TOP) return r == 3u && (c == 2u || c == 6u);
    return (r == 2u || r == 6u) && (c == 2u || c == 6u);
}

static void draw_stone(pn_frame_t *p, int cx, int cy, uint8_t color, bool is_last) {
    int r2 = TG_STONE_R * TG_STONE_R;
    int inner2 = (TG_STONE_R - 3) * (TG_STONE_R - 3);
    for (int dy = -TG_STONE_R; dy <= TG_STONE_R; dy++) {
        for (int dx = -TG_STONE_R; dx <= TG_STONE_R; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > r2) continue;
            uint8_t ink;
            if (color == PN_TG_BLACK) {
                /* 黑子带左上角月牙高光，相邻黑子边界清晰 / Black stone with top-left crescent highlight */
                int hx = dx + 9, hy = dy + 9;
                int hd2 = hx * hx + hy * hy;
                if (hd2 <= 36 && dx < -3 && dy < -3) ink = 12;
                else if (hd2 <= 64 && dx < -1 && dy < -1) ink = 6;
                else ink = 0;
            } else {
                /* 白子粗墨外框+右下微影 / White stone with bold black rim and bottom-right shade */
                if (d2 >= inner2) ink = 0;
                else if (dx + dy > 16 && d2 >= (TG_STONE_R - 8) * (TG_STONE_R - 8)) ink = 12;
                else ink = 15;
            }
            pn_frame_rect(p, cx + dx, cy + dy, 1, 1, ink);
        }
    }
    if (is_last) {
        /* 末手圆环标记（黑子白环、白子黑环） / Last-move contrasting ring */
        uint8_t ring = (color == PN_TG_BLACK) ? 15 : 0;
        for (int dy = -12; dy <= 12; dy++) {
            for (int dx = -12; dx <= 12; dx++) {
                int d2 = dx * dx + dy * dy;
                if (d2 >= 72 && d2 <= 144) pn_frame_rect(p, cx + dx, cy + dy, 1, 1, ring);
            }
        }
    }
}

/* 绘制单个68×68交叉点瓦片，确保整绘与局部刷新逐像素一致 / Paint one non-overlapping 68x68 intersection tile. */
static void draw_point(const pn_tg_state_t *s, pn_frame_t *p, unsigned pos) {
    unsigned r = pos / PN_TG_SIZE, c = pos % PN_TG_SIZE;
    int cx = TG_X0 + (int)c * TG_STEP;
    int cy = TG_Y0 + (int)r * TG_STEP;
    int tx = cx - TG_HALF, ty = cy - TG_HALF;
    uint8_t mask = s->level.edge_mask;

    /* 浅木纹底色 / Light board paper background */
    pn_frame_rect(p, tx, ty, TG_STEP, TG_STEP, 14);

    /* 根据真边掩码决定横/纵线在瓦片内的起止，真边加粗 / Grid lines clipped to true Go edges */
    bool top_edge = (r == 0u) && ((mask & PN_TG_EDGE_TOP) != 0u);
    bool bot_edge = (r == PN_TG_SIZE - 1u) && ((mask & PN_TG_EDGE_BOTTOM) != 0u);
    bool left_edge = (c == 0u) && ((mask & PN_TG_EDGE_LEFT) != 0u);
    bool right_edge = (c == PN_TG_SIZE - 1u) && ((mask & PN_TG_EDGE_RIGHT) != 0u);

    int hx0 = left_edge ? cx : tx;
    int hx1 = right_edge ? cx + 1 : tx + TG_STEP;
    int vy0 = top_edge ? cy : ty;
    int vy1 = bot_edge ? cy + 1 : ty + TG_STEP;

    int h_thick = (top_edge || bot_edge) ? 4 : 2;
    int v_thick = (left_edge || right_edge) ? 4 : 2;
    int h_y = top_edge ? cy - 2 : (bot_edge ? cy - 1 : cy - 1);
    int v_x = left_edge ? cx - 2 : (right_edge ? cx - 1 : cx - 1);

    pn_frame_rect(p, hx0, h_y, hx1 - hx0, h_thick, 3);
    pn_frame_rect(p, v_x, vy0, v_thick, vy1 - vy0, 3);

    /* 真边外侧延伸留白与角点衔接 / Clean corner join on true edges */
    if (top_edge && right_edge) pn_frame_rect(p, cx - 1, cy - 2, 4, 4, 3);
    if (top_edge && left_edge) pn_frame_rect(p, cx - 2, cy - 2, 4, 4, 3);
    if (is_star_point(mask, r, c)) pn_frame_rect(p, cx - 4, cy - 4, 9, 9, 0);

    uint8_t stone = s->board[pos];
    if (stone == PN_TG_BLACK || stone == PN_TG_WHITE) {
        bool is_last = (pos == s->last_white) || (s->last_white == PN_TG_NONE && pos == s->last_black);
        draw_stone(p, cx, cy, stone, is_last);
    } else if (pos == s->hint_pos) {
        /* 提示急所：双环清晰标靶 / Hint vital point: crisp concentric target ring */
        for (int dy = -18; dy <= 18; dy++) {
            for (int dx = -18; dx <= 18; dx++) {
                int d2 = dx * dx + dy * dy;
                if ((d2 >= 210 && d2 <= 324) || d2 <= 25) pn_frame_rect(p, cx + dx, cy + dy, 1, 1, 0);
            }
        }
    }

    if (pos == s->cursor) {
        int m = 3, len = 12;
        pn_frame_rect(p, tx + m, ty + m, len, 3, 0);
        pn_frame_rect(p, tx + m, ty + m, 3, len, 0);
        pn_frame_rect(p, tx + TG_STEP - m - len, ty + m, len, 3, 0);
        pn_frame_rect(p, tx + TG_STEP - m - 3, ty + m, 3, len, 0);
        pn_frame_rect(p, tx + m, ty + TG_STEP - m - 3, len, 3, 0);
        pn_frame_rect(p, tx + m, ty + TG_STEP - m - len, 3, len, 0);
        pn_frame_rect(p, tx + TG_STEP - m - len, ty + TG_STEP - m - 3, len, 3, 0);
        pn_frame_rect(p, tx + TG_STEP - m - 3, ty + TG_STEP - m - len, 3, len, 0);
    }
}

static void draw_board_outer_frame(pn_frame_t *p) {
    /* 棋盘木框外描边 / Crisp outer board frame */
    pn_frame_rect(p, TG_BX - 6, TG_BY - 6, TG_BW + 12, 6, 0);
    pn_frame_rect(p, TG_BX - 6, TG_BY + TG_BH, TG_BW + 12, 6, 0);
    pn_frame_rect(p, TG_BX - 6, TG_BY - 6, 6, TG_BH + 12, 0);
    pn_frame_rect(p, TG_BX + TG_BW, TG_BY - 6, 6, TG_BH + 12, 0);
}

static pn_status_t draw_play_buttons(const tg_t *g, pn_font_t *f, pn_frame_t *p) {
    if (g->screen == PN_TG_COMPLETE) {
        DRAW(button(f, p, "下一题", 36, 948, 196, 68, true));
        DRAW(button(f, p, "重做本题", 244, 948, 196, 68, false));
        DRAW(button(f, p, "选关", 452, 948, 196, 68, false));
        return PN_OK;
    }
    DRAW(button(f, p, "悔棋", 36, 948, 144, 68, g->state.outcome == PN_TG_FAILED));
    DRAW(button(f, p, "提示", 192, 948, 144, 68, false));
    DRAW(button(f, p, "重开", 348, 948, 144, 68, false));
    DRAW(button(f, p, "清屏", 504, 948, 144, 68, false));
    return PN_OK;
}

static pn_status_t render_select(const tg_t *g, pn_font_t *f, pn_frame_t *p) {
    DRAW(heading(g, f, p));
    unsigned solved_total = 0;
    for (unsigned i = 0; i < PN_TG_LEVELS; i++)
        if (g->state.completed[i >> 3] & (1u << (i & 7u))) solved_total++;
    char sub[80];
    snprintf(sub, sizeof sub, "围棋经典死活 60 题 · 已攻克 %u / 60", solved_total);
    DRAW(text_c(f, p, sub, 36, 134, 612, 22, PN_UI_MUTED, false));

    for (unsigned t = 0; t < 3u; t++) {
        int bx = 36 + (int)t * 208;
        bool act = (g->tier == t);
        cap(p, bx, 160, 196, 80, act);
        DRAW(text_c(f, p, tiers[t], bx, 196, 196, 26, act ? 15 : 0, true));
        DRAW(text_c(f, p, tier_notes[t], bx, 224, 196, 17, act ? 13 : PN_UI_MUTED, false));
    }

    unsigned base = g->tier * 20u;
    for (unsigned i = 0; i < 20u; i++) {
        unsigned id = base + i;
        int col = (int)(i % 5u), row = (int)(i / 5u);
        int x = 36 + col * 124, y = 266 + row * 124;
        bool done = (g->state.completed[id >> 3] & (1u << (id & 7u))) != 0u;
        bool sel = (g->selected_level == id);
        cap(p, x, y, 114, 112, sel);
        pn_tg_level_t lv;
        char num[16];
        snprintf(num, sizeof num, "%u%s", i + 1u, done ? " ✓" : "");
        DRAW(text_c(f, p, num, x, y + 48, 114, 24, sel ? 15 : 0, true));
        if (pn_tsumego_level(id, &lv) == PN_OK) {
            DRAW(text_c(f, p, lv.title, x + 2, y + 86, 110, 16, sel ? 14 : (done ? 4 : PN_UI_MUTED), false));
        }
    }

    pn_tg_level_t sel_lv;
    if (pn_tsumego_level(g->selected_level, &sel_lv) == PN_OK) {
        char info[96];
        snprintf(info, sizeof info, "已选「%s」· %s · %u手正解", sel_lv.title,
                 pn_tsumego_goal_label(sel_lv.goal), sel_lv.main_moves);
        DRAW(text_c(f, p, info, 36, 800, 612, 22, 0, true));
    }
    if (g->has && g->state.outcome != PN_TG_SOLVED) {
        DRAW(button(f, p, "继续当前题", 36, 846, 296, 72, false));
        DRAW(button(f, p, "开始所选题", 352, 846, 296, 72, true));
    } else {
        DRAW(button(f, p, "开始答题", 164, 846, 356, 72, true));
    }
    return PN_OK;
}

static pn_status_t render_dialog(const tg_t *g, pn_font_t *f, pn_frame_t *p, const char *title, const char *body,
                                 const char *left_btn, const char *right_btn) {
    DRAW(heading(g, f, p));
    cap(p, 56, 320, 572, 340, false);
    DRAW(text_c(f, p, title, 80, 396, 524, 32, 0, true));
    DRAW(text_c(f, p, body, 80, 470, 524, 24, PN_UI_MUTED, false));
    DRAW(button(f, p, left_btn, 92, 548, 232, 72, false));
    DRAW(button(f, p, right_btn, 360, 548, 232, 72, true));
    return PN_OK;
}

pn_status_t pn_tsumego_render(const pn_tsumego_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p) return PN_INVALID;
    const tg_t *g = u->impl;
    pn_frame_clear(p, 15);
    if (g->screen == PN_TG_SELECT) return render_select(g, f, p);
    if (g->screen == PN_TG_CONFIRM)
        return render_dialog(g, f, p, "重做本题？", "将重置当前棋盘并保留已通关记录", "取消", "确认重开");
    if (g->screen == PN_TG_ERROR)
        return render_dialog(g, f, p, "存储提示", g->notice ? g->notice : "存取异常", "去选关", "重试");

    DRAW(heading(g, f, p));
    DRAW(metadata(g, f, p));
    draw_board_outer_frame(p);
    for (unsigned pos = 0; pos < PN_TG_CELLS; pos++) draw_point(&g->state, p, pos);
    DRAW(status_prompt(g, f, p));
    return draw_play_buttons(g, f, p);
}

pn_status_t pn_tsumego_render_update(const pn_tsumego_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p) return PN_INVALID;
    const tg_t *g = u->impl;
    if (g->screen != PN_TG_PLAY || g->full || !g->patch) return PN_UNSUPPORTED;
    const pn_tg_state_t *s = &g->state;
    for (unsigned pos = 0; pos < PN_TG_CELLS; pos++) {
        if (g->shown_board[pos] != s->board[pos] || pos == g->shown_last_black || pos == s->last_black ||
            pos == g->shown_last_white || pos == s->last_white || pos == g->shown_hint || pos == s->hint_pos ||
            pos == g->shown_cursor || pos == s->cursor) {
            draw_point(s, p, pos);
        }
    }
    DRAW(metadata(g, f, p));
    DRAW(status_prompt(g, f, p));
    return draw_play_buttons(g, f, p);
}

int pn_tsumego_hit(const pn_tsumego_t *u, int x, int y) {
    if (!u || !u->impl) return 0;
    const tg_t *g = u->impl;
    if (!g->pending_start && inside(x, y, 36, 48, 148, 48)) return PN_TG_BACK;

    if (g->screen == PN_TG_ERROR) {
        if (inside(x, y, 92, 548, 232, 72)) return PN_TG_CHOOSE;
        if (inside(x, y, 360, 548, 232, 72)) return PN_TG_RETRY;
        return 0;
    }
    if (g->screen == PN_TG_CONFIRM) {
        if (inside(x, y, 92, 548, 232, 72)) return PN_TG_CANCEL;
        if (inside(x, y, 360, 548, 232, 72)) return PN_TG_ACCEPT;
        return 0;
    }
    if (g->screen == PN_TG_SELECT) {
        for (unsigned t = 0; t < 3u; t++)
            if (inside(x, y, 36 + (int)t * 208, 160, 196, 80)) return PN_TG_TIER + (int)t;
        for (unsigned i = 0; i < 20u; i++) {
            int col = (int)(i % 5u), row = (int)(i / 5u);
            if (inside(x, y, 36 + col * 124, 266 + row * 124, 114, 112)) return PN_TG_CELL + (int)i;
        }
        if (g->has && g->state.outcome != PN_TG_SOLVED) {
            if (inside(x, y, 36, 846, 296, 72)) return PN_TG_RESUME;
            if (inside(x, y, 352, 846, 296, 72)) return PN_TG_START;
        } else if (inside(x, y, 164, 846, 356, 72)) {
            return PN_TG_START;
        }
        return 0;
    }
    if (inside(x, y, 500, 48, 148, 48)) return PN_TG_CHOOSE;
    if (g->screen == PN_TG_COMPLETE) {
        if (inside(x, y, 36, 948, 196, 68)) return PN_TG_NEXT;
        if (inside(x, y, 244, 948, 196, 68)) return PN_TG_RESTART;
        if (inside(x, y, 452, 948, 196, 68)) return PN_TG_CHOOSE;
        return 0;
    }
    if (inside(x, y, TG_BX, TG_BY, TG_BW, TG_BH)) {
        int col = (x - TG_BX) / TG_STEP, row = (y - TG_BY) / TG_STEP;
        if (col >= 0 && col < PN_TG_SIZE && row >= 0 && row < PN_TG_SIZE)
            return PN_TG_CELL + row * PN_TG_SIZE + col;
    }
    if (inside(x, y, 36, 948, 144, 68)) return PN_TG_UNDO_MOVE;
    if (inside(x, y, 192, 948, 144, 68)) return PN_TG_HINT;
    if (inside(x, y, 348, 948, 144, 68)) return PN_TG_RESTART;
    if (inside(x, y, 504, 948, 144, 68)) return PN_TG_CLEAN;
    return 0;
}
