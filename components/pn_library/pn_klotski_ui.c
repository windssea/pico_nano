/* SPDX-License-Identifier: Apache-2.0
 * 三国华容道纯绘制、刻纹木块、华容道出口与变格局部增量更新。/ Pure Klotski rendering, carved blocks, escape gate and changed-cell partial updates. */
#include "pn_klotski_internal.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const tiers[] = {"入门", "进阶", "名局"};
static const char *const tier_notes[] = {"11-26步热身", "27-58步巧移", "60-130步经典"};
static const char *const dir_names[] = {"上", "下", "左", "右"};

enum {
    KL_CELL = 136,
    KL_GX = 70,  /* (684 - 4*136)/2 */
    KL_GY = 186, /* 5*136 = 680 -> bottom 866 */
    KL_GW = 544,
    KL_GH = 680
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

static pn_status_t heading(const kl_t *g, pn_font_t *f, pn_frame_t *p) {
    if (!g->pending_start) {
        cap(p, 36, 48, 148, 48, false);
        DRAW(text_c(f, p, "返回", 36, 80, 148, 26, 0, false));
    }
    DRAW(text_c(f, p, "三国华容道", 200, 82, 284, 32, 0, true));
    if (g->screen == PN_KL_PLAY || g->screen == PN_KL_COMPLETE) {
        cap(p, 500, 48, 148, 48, false);
        DRAW(text_c(f, p, "选关", 500, 80, 148, 26, 0, false));
    }
    return PN_OK;
}

static pn_status_t metadata(const kl_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_kl_state_t *s = &g->state;
    char left[96], right[96];
    pn_frame_rect(p, 32, 110, 620, 46, 15);
    snprintf(left, sizeof left, "%s · 第 %u 关「%s」", tiers[s->level.tier], s->level.id % 20u + 1u, s->level.title);
    DRAW(pn_font_size(f, 22));
    DRAW(pn_w_text_ex(f, p, left, 36, 142, 368, PN_ALIGN_LEFT, 0, true));
    unsigned long long sec = s->elapsed_ms / 1000u;
    snprintf(right, sizeof right, "步 %u · 最优 %u · %02llu:%02llu", s->moves, s->level.par_moves, sec / 60u,
             sec % 60u);
    DRAW(pn_font_size(f, 19));
    return pn_w_text_ex(f, p, right, 404, 142, 244, PN_ALIGN_RIGHT, PN_UI_MUTED, false);
}

static pn_status_t status_prompt(const kl_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_kl_state_t *s = &g->state;
    char line[96];
    pn_frame_rect(p, 36, 896, 612, 46, 15);
    if (g->notice) {
        snprintf(line, sizeof line, "%s", g->notice);
    } else if (s->solved) {
        snprintf(line, sizeof line, "曹操成功突围！共 %u 步（最优 %u 步）", s->moves, s->level.par_moves);
    } else if (s->hint_piece != PN_KL_NONE && s->hint_piece < s->level.piece_count) {
        snprintf(line, sizeof line, "提示：将「%s」向%s移 %u 格", pn_klotski_piece_label(&s->level, s->hint_piece),
                 dir_names[s->hint_dir & 3u], s->hint_steps);
    } else if (s->selected != PN_KL_NONE && s->selected < s->level.piece_count) {
        snprintf(line, sizeof line, "已选「%s」，点击空位或拖动滑行", pn_klotski_piece_label(&s->level, s->selected));
    } else {
        snprintf(line, sizeof line, "点击棋块自动滑行，或拖动棋块助曹操抵达出口");
    }
    return text_c(f, p, line, 36, 928, 612, 22, 0, s->solved || s->hint_piece != PN_KL_NONE);
}

static void draw_empty_cell(const pn_kl_state_t *s, pn_frame_t *p, unsigned cell) {
    unsigned r = cell / PN_KL_COLS, c = cell % PN_KL_COLS;
    int x = KL_GX + (int)c * KL_CELL, y = KL_GY + (int)r * KL_CELL;
    pn_frame_rect(p, x, y, KL_CELL, KL_CELL, 14);
    /* 极细格线 / Subtle cell border */
    pn_frame_rect(p, x, y, KL_CELL, 1, 12);
    pn_frame_rect(p, x, y, 1, KL_CELL, 12);

    /* 若选中块可直达该空位，在格心画清晰方向箭头 / Draw directional indicator if selected piece can slide here. */
    if (s->selected != PN_KL_NONE && s->selected < s->level.piece_count && !s->solved) {
        unsigned w, h;
        pn_kl_piece_dims(s->level.shapes[s->selected], &w, &h);
        int pr = (int)(s->pos[s->selected] / PN_KL_COLS), pc = (int)(s->pos[s->selected] % PN_KL_COLS);
        int tr = (int)r, tc = (int)c;
        unsigned dir = 4u, steps = 0u;
        if (tc >= pc && tc < pc + (int)w) {
            if (tr < pr) { dir = PN_KL_DIR_UP; steps = (unsigned)(pr - tr); }
            else if (tr >= pr + (int)h) { dir = PN_KL_DIR_DOWN; steps = (unsigned)(tr - (pr + (int)h) + 1); }
        } else if (tr >= pr && tr < pr + (int)h) {
            if (tc < pc) { dir = PN_KL_DIR_LEFT; steps = (unsigned)(pc - tc); }
            else if (tc >= pc + (int)w) { dir = PN_KL_DIR_RIGHT; steps = (unsigned)(tc - (pc + (int)w) + 1); }
        }
        if (dir < 4u && steps >= 1u && steps <= 2u && pn_klotski_can_slide(s, s->selected, dir, steps)) {
            int cx = x + KL_CELL / 2, cy = y + KL_CELL / 2;
            uint8_t ink = (s->hint_piece == s->selected && s->hint_dir == dir) ? 0 : 6;
            for (int k = 0; k < 14; k++) {
                if (dir == PN_KL_DIR_UP) pn_frame_rect(p, cx - k, cy - 8 + k, k * 2 + 1, 1, ink);
                else if (dir == PN_KL_DIR_DOWN) pn_frame_rect(p, cx - k, cy + 8 - k, k * 2 + 1, 1, ink);
                else if (dir == PN_KL_DIR_LEFT) pn_frame_rect(p, cx - 8 + k, cy - k, 1, k * 2 + 1, ink);
                else pn_frame_rect(p, cx + 8 - k, cy - k, 1, k * 2 + 1, ink);
            }
        }
    }
}

static pn_status_t draw_piece(const pn_kl_state_t *s, pn_font_t *f, pn_frame_t *p, unsigned idx) {
    uint8_t pos = s->pos[idx], sh = s->level.shapes[idx];
    unsigned w, h;
    pn_kl_piece_dims(sh, &w, &h);
    unsigned r = pos / PN_KL_COLS, c = pos % PN_KL_COLS;
    int gx = KL_GX + (int)c * KL_CELL, gy = KL_GY + (int)r * KL_CELL;
    int gw = (int)w * KL_CELL, gh = (int)h * KL_CELL;

    /* 先清所占整格背景，确保与整绘逐像素一致 / Clear occupied cell background for exact full/patch match. */
    pn_frame_rect(p, gx, gy, gw, gh, 14);

    int x = gx + 5, y = gy + 5, bw = gw - 10, bh = gh - 10;
    bool sel = (s->selected == idx);
    bool hinted = (s->hint_piece == idx);
    uint8_t fill = (sh == PN_KL_SHAPE_2X2) ? 2 : (sh == PN_KL_SHAPE_2X1) ? 5 : (sh == PN_KL_SHAPE_1X2) ? 12 : 15;
    uint8_t text_ink = (fill <= 5) ? 15 : 0;

    /* 立体阴影与木块主体 / Tactile shadow and carved woodblock body */
    pixel_fill(p, x + 2, y + 3, bw - 2, bh - 3, 6, 8);
    pixel_fill(p, x, y, bw - 2, bh - 3, 6, 0);
    pixel_fill(p, x + 3, y + 3, bw - 8, bh - 9, 4, fill);

    /* 曹操双线刻纹内框，或选中加粗高亮框 / Double inner trim for Cao Cao, or thick highlight on selection */
    if (sh == PN_KL_SHAPE_2X2) {
        pn_frame_rect(p, x + 11, y + 11, bw - 24, 2, 13);
        pn_frame_rect(p, x + 11, y + bh - 16, bw - 24, 2, 13);
        pn_frame_rect(p, x + 11, y + 11, 2, bh - 25, 13);
        pn_frame_rect(p, x + bw - 15, y + 11, 2, bh - 25, 13);
    } else if (sh == PN_KL_SHAPE_1X2 || sh == PN_KL_SHAPE_2X1) {
        uint8_t trim = (fill <= 5) ? 11 : 7;
        pn_frame_rect(p, x + 8, y + 8, bw - 18, 1, trim);
        pn_frame_rect(p, x + 8, y + bh - 12, bw - 18, 1, trim);
        pn_frame_rect(p, x + 8, y + 8, 1, bh - 19, trim);
        pn_frame_rect(p, x + bw - 11, y + 8, 1, bh - 19, trim);
    }
    if (sel || hinted) {
        /* 选中/提示四角加粗标框 / Bold corner highlight for selected or hinted piece */
        uint8_t hi = (fill <= 5) ? 15 : 0;
        int m = 6, len = 24;
        pn_frame_rect(p, x + m, y + m, len, 4, hi);
        pn_frame_rect(p, x + m, y + m, 4, len, hi);
        pn_frame_rect(p, x + bw - 2 - m - len, y + m, len, 4, hi);
        pn_frame_rect(p, x + bw - 2 - m - 4, y + m, 4, len, hi);
        pn_frame_rect(p, x + m, y + bh - 3 - m - 4, len, 4, hi);
        pn_frame_rect(p, x + m, y + bh - 3 - m - len, 4, len, hi);
        pn_frame_rect(p, x + bw - 2 - m - len, y + bh - 3 - m - 4, len, 4, hi);
        pn_frame_rect(p, x + bw - 2 - m - 4, y + bh - 3 - m - len, 4, len, hi);
    }

    const char *label = pn_klotski_piece_label(&s->level, idx);
    if (sh == PN_KL_SHAPE_2X2) {
        DRAW(text_c(f, p, label, x, y + bh / 2 + 18, bw - 2, 52, text_ink, true));
    } else if (sh == PN_KL_SHAPE_1X2 && strlen(label) == 6u) {
        /* 竖将两字纵向排布 / Vertical two-character layout for 1x2 generals */
        char c1[4] = {label[0], label[1], label[2], '\0'};
        char c2[4] = {label[3], label[4], label[5], '\0'};
        DRAW(text_c(f, p, c1, x, y + bh / 2 - 22, bw - 2, 42, text_ink, true));
        DRAW(text_c(f, p, c2, x, y + bh / 2 + 52, bw - 2, 42, text_ink, true));
    } else if (sh == PN_KL_SHAPE_2X1) {
        DRAW(text_c(f, p, label, x, y + bh / 2 + 14, bw - 2, 40, text_ink, true));
    } else {
        DRAW(text_c(f, p, label, x, y + bh / 2 + 14, bw - 2, 40, text_ink, false));
    }
    return PN_OK;
}

static pn_status_t draw_frame_border(pn_font_t *f, pn_frame_t *p) {
    /* 粗墨外框，底部中央开口作为华容道出口 / Thick outer wooden border with open bottom-center escape gate */
    int ox = KL_GX - 12, oy = KL_GY - 12, ow = KL_GW + 24, oh = KL_GH + 24;
    pn_frame_rect(p, ox, oy, ow, 12, 0);
    pn_frame_rect(p, ox, oy, 12, oh, 0);
    pn_frame_rect(p, ox + ow - 12, oy, 12, oh, 0);
    /* 底部左右框，中间留宽 2*KL_CELL 出口 / Bottom left & right walls leaving 2-cell gate */
    pn_frame_rect(p, ox, oy + oh - 12, KL_CELL + 12, 12, 0);
    pn_frame_rect(p, KL_GX + 3 * KL_CELL, oy + oh - 12, KL_CELL + 12, 12, 0);
    pn_frame_rect(p, KL_GX + KL_CELL, KL_GY + KL_GH, 2 * KL_CELL, 18, 15);
    DRAW(text_c(f, p, "▼ 华 容 道 出 口 ▼", KL_GX + KL_CELL, KL_GY + KL_GH + 20, 2 * KL_CELL, 18, 4, true));
    return PN_OK;
}

static pn_status_t draw_play_buttons(const kl_t *g, pn_font_t *f, pn_frame_t *p) {
    if (g->screen == PN_KL_COMPLETE) {
        DRAW(button(f, p, "下一关", 36, 976, 196, 68, true));
        DRAW(button(f, p, "重玩本关", 244, 976, 196, 68, false));
        DRAW(button(f, p, "选关", 452, 976, 196, 68, false));
        return PN_OK;
    }
    DRAW(button(f, p, "撤销", 36, 976, 144, 68, false));
    DRAW(button(f, p, "提示", 192, 976, 144, 68, false));
    DRAW(button(f, p, "重开", 348, 976, 144, 68, false));
    DRAW(button(f, p, "清屏", 504, 976, 144, 68, false));
    return PN_OK;
}

static pn_status_t render_select(const kl_t *g, pn_font_t *f, pn_frame_t *p) {
    DRAW(heading(g, f, p));
    unsigned solved_total = 0;
    for (unsigned i = 0; i < PN_KL_LEVELS; i++)
        if (g->state.completed[i >> 3] & (1u << (i & 7u))) solved_total++;
    char sub[80];
    snprintf(sub, sizeof sub, "经典三国华容道 60 关 · 已通关 %u / 60", solved_total);
    DRAW(text_c(f, p, sub, 36, 134, 612, 22, PN_UI_MUTED, false));

    /* 三档选项卡 / Three tier tabs */
    for (unsigned t = 0; t < 3u; t++) {
        int bx = 36 + (int)t * 208;
        bool act = (g->tier == t);
        cap(p, bx, 160, 196, 80, act);
        DRAW(text_c(f, p, tiers[t], bx, 196, 196, 26, act ? 15 : 0, true));
        DRAW(text_c(f, p, tier_notes[t], bx, 224, 196, 17, act ? 13 : PN_UI_MUTED, false));
    }

    /* 5×4 关卡网格（当前档20关） / 5x4 level grid for active tier */
    unsigned base = g->tier * 20u;
    for (unsigned i = 0; i < 20u; i++) {
        unsigned id = base + i;
        int col = (int)(i % 5u), row = (int)(i / 5u);
        int x = 36 + col * 124, y = 266 + row * 124;
        bool done = (g->state.completed[id >> 3] & (1u << (id & 7u))) != 0u;
        bool sel = (g->selected_level == id);
        cap(p, x, y, 114, 112, sel);
        pn_kl_level_t lv;
        char num[16];
        snprintf(num, sizeof num, "%u%s", i + 1u, done ? " ✓" : "");
        DRAW(text_c(f, p, num, x, y + 48, 114, 24, sel ? 15 : 0, true));
        if (pn_klotski_level(id, &lv) == PN_OK) {
            DRAW(text_c(f, p, lv.title, x + 2, y + 86, 110, 17, sel ? 14 : (done ? 4 : PN_UI_MUTED), false));
        }
    }

    /* 底部按钮 / Bottom start & resume buttons */
    pn_kl_level_t sel_lv;
    if (pn_klotski_level(g->selected_level, &sel_lv) == PN_OK) {
        char info[96];
        snprintf(info, sizeof info, "已选「%s」· 最优 %u 步", sel_lv.title, sel_lv.par_moves);
        DRAW(text_c(f, p, info, 36, 800, 612, 22, 0, true));
    }
    if (g->has && !g->state.solved) {
        DRAW(button(f, p, "继续当前局", 36, 846, 296, 72, false));
        DRAW(button(f, p, "开始所选关", 352, 846, 296, 72, true));
    } else {
        DRAW(button(f, p, "开始挑战", 164, 846, 356, 72, true));
    }
    return PN_OK;
}

static pn_status_t render_dialog(const kl_t *g, pn_font_t *f, pn_frame_t *p, const char *title, const char *body,
                                 const char *left_btn, const char *right_btn) {
    DRAW(heading(g, f, p));
    cap(p, 56, 320, 572, 340, false);
    DRAW(text_c(f, p, title, 80, 396, 524, 32, 0, true));
    DRAW(text_c(f, p, body, 80, 470, 524, 24, PN_UI_MUTED, false));
    DRAW(button(f, p, left_btn, 92, 548, 232, 72, false));
    DRAW(button(f, p, right_btn, 360, 548, 232, 72, true));
    return PN_OK;
}

pn_status_t pn_klotski_render(const pn_klotski_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p) return PN_INVALID;
    const kl_t *g = u->impl;
    pn_frame_clear(p, 15);
    if (g->screen == PN_KL_SELECT) return render_select(g, f, p);
    if (g->screen == PN_KL_CONFIRM)
        return render_dialog(g, f, p, "重新开始本关？", "将重置当前棋盘步数并保留通关记录", "取消", "确认重开");
    if (g->screen == PN_KL_ERROR)
        return render_dialog(g, f, p, "存储提示", g->notice ? g->notice : "存取异常", "去选关", "重试");

    DRAW(heading(g, f, p));
    DRAW(metadata(g, f, p));
    DRAW(draw_frame_border(f, p));
    uint8_t occ[PN_KL_CELLS];
    if (!pn_klotski_occupancy(&g->state.level, g->state.pos, occ)) return PN_CORRUPT;
    for (unsigned c = 0; c < PN_KL_CELLS; c++)
        if (occ[c] == PN_KL_NONE) draw_empty_cell(&g->state, p, c);
    for (unsigned i = 0; i < g->state.level.piece_count; i++) DRAW(draw_piece(&g->state, f, p, i));
    DRAW(status_prompt(g, f, p));
    return draw_play_buttons(g, f, p);
}

pn_status_t pn_klotski_render_update(const pn_klotski_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p) return PN_INVALID;
    const kl_t *g = u->impl;
    if (g->screen != PN_KL_PLAY || g->full || !g->patch) return PN_UNSUPPORTED;

    const pn_kl_state_t *s = &g->state;
    uint8_t old_occ[PN_KL_CELLS], new_occ[PN_KL_CELLS];
    if (!pn_klotski_occupancy(&s->level, g->shown_pos, old_occ) ||
        !pn_klotski_occupancy(&s->level, s->pos, new_occ))
        return PN_UNSUPPORTED;

    bool redraw_piece[PN_KL_MAX_PIECES] = {false};
    for (unsigned i = 0; i < s->level.piece_count; i++) {
        if (g->shown_pos[i] != s->pos[i] || i == g->shown_selected || i == s->selected ||
            i == g->shown_hint_piece || i == s->hint_piece)
            redraw_piece[i] = true;
    }
    /* 重画空位（含方向箭头变化）与变动棋块 / Redraw empty cells (including arrow changes) and changed pieces. */
    for (unsigned c = 0; c < PN_KL_CELLS; c++) {
        if (new_occ[c] == PN_KL_NONE) draw_empty_cell(s, p, c);
    }
    for (unsigned i = 0; i < s->level.piece_count; i++) {
        if (redraw_piece[i]) DRAW(draw_piece(s, f, p, i));
    }
    DRAW(metadata(g, f, p));
    return status_prompt(g, f, p);
}

int pn_klotski_hit(const pn_klotski_t *u, int x, int y) {
    if (!u || !u->impl) return 0;
    const kl_t *g = u->impl;
    if (!g->pending_start && inside(x, y, 36, 48, 148, 48)) return PN_KL_BACK;

    if (g->screen == PN_KL_ERROR) {
        if (inside(x, y, 92, 548, 232, 72)) return PN_KL_CHOOSE;
        if (inside(x, y, 360, 548, 232, 72)) return PN_KL_RETRY;
        return 0;
    }
    if (g->screen == PN_KL_CONFIRM) {
        if (inside(x, y, 92, 548, 232, 72)) return PN_KL_CANCEL;
        if (inside(x, y, 360, 548, 232, 72)) return PN_KL_ACCEPT;
        return 0;
    }
    if (g->screen == PN_KL_SELECT) {
        for (unsigned t = 0; t < 3u; t++)
            if (inside(x, y, 36 + (int)t * 208, 160, 196, 80)) return PN_KL_TIER + (int)t;
        for (unsigned i = 0; i < 20u; i++) {
            int col = (int)(i % 5u), row = (int)(i / 5u);
            if (inside(x, y, 36 + col * 124, 266 + row * 124, 114, 112)) return PN_KL_CELL + (int)i;
        }
        if (g->has && !g->state.solved) {
            if (inside(x, y, 36, 846, 296, 72)) return PN_KL_RESUME;
            if (inside(x, y, 352, 846, 296, 72)) return PN_KL_START;
        } else if (inside(x, y, 164, 846, 356, 72)) {
            return PN_KL_START;
        }
        return 0;
    }
    if (inside(x, y, 500, 48, 148, 48)) return PN_KL_CHOOSE;
    if (g->screen == PN_KL_COMPLETE) {
        if (inside(x, y, 36, 976, 196, 68)) return PN_KL_NEXT;
        if (inside(x, y, 244, 976, 196, 68)) return PN_KL_RESTART;
        if (inside(x, y, 452, 976, 196, 68)) return PN_KL_CHOOSE;
        return 0;
    }
    if (inside(x, y, KL_GX, KL_GY, KL_GW, KL_GH)) {
        int col = (x - KL_GX) / KL_CELL, row = (y - KL_GY) / KL_CELL;
        if (col >= 0 && col < PN_KL_COLS && row >= 0 && row < PN_KL_ROWS)
            return PN_KL_CELL + row * PN_KL_COLS + col;
    }
    if (inside(x, y, 36, 976, 144, 68)) return PN_KL_UNDO_MOVE;
    if (inside(x, y, 192, 976, 144, 68)) return PN_KL_HINT;
    if (inside(x, y, 348, 976, 144, 68)) return PN_KL_RESTART;
    if (inside(x, y, 504, 976, 144, 68)) return PN_KL_CLEAN;
    return 0;
}

int pn_klotski_swipe_command(const pn_klotski_t *u, int x0, int y0, int x1, int y1) {
    if (!u || !u->impl) return 0;
    const kl_t *g = u->impl;
    if (g->screen != PN_KL_PLAY || g->state.solved) return 0;
    if (!inside(x0, y0, KL_GX, KL_GY, KL_GW, KL_GH)) return 0;
    int dx = x1 - x0, dy = y1 - y0;
    int ax = abs(dx), ay = abs(dy);
    if (ax < 36 && ay < 36) return 0;
    int col = (x0 - KL_GX) / KL_CELL, row = (y0 - KL_GY) / KL_CELL;
    if (col < 0 || col >= PN_KL_COLS || row < 0 || row >= PN_KL_ROWS) return 0;
    uint8_t occ[PN_KL_CELLS];
    if (!pn_klotski_occupancy(&g->state.level, g->state.pos, occ)) return 0;
    uint8_t piece = occ[row * PN_KL_COLS + col];
    if (piece == PN_KL_NONE) return 0;
    unsigned dir = (ax >= ay) ? (dx < 0 ? PN_KL_DIR_LEFT : PN_KL_DIR_RIGHT)
                              : (dy < 0 ? PN_KL_DIR_UP : PN_KL_DIR_DOWN);
    int dist = ax >= ay ? ax : ay;
    unsigned steps = (dist >= KL_CELL + KL_CELL / 3) ? 2u : 1u;
    return PN_KL_SLIDE + (int)(piece * 8u + dir * 2u + (steps - 1u));
}

int pn_klotski_touch(pn_klotski_t *u, unsigned count, int x, int y, bool valid) {
    if (!u || !u->impl) return 0;
    kl_t *g = u->impl;
    if (!valid) {
        g->touch_cancel = true;
        return 0;
    }
    if (count > 1u) {
        g->touch_cancel = true;
        return 0;
    }
    if (count == 1u) {
        if (!g->touch_down) {
            g->touch_down = true;
            g->touch_cancel = false;
            g->touch_x0 = x;
            g->touch_y0 = y;
        }
        g->touch_x1 = x;
        g->touch_y1 = y;
        return 0;
    }
    if (!g->touch_down) return 0;
    bool cancelled = g->touch_cancel;
    g->touch_down = false;
    g->touch_cancel = false;
    if (cancelled) return 0;
    int swipe = pn_klotski_swipe_command(u, g->touch_x0, g->touch_y0, g->touch_x1, g->touch_y1);
    if (swipe > 0) return swipe;
    int h0 = pn_klotski_hit(u, g->touch_x0, g->touch_y0);
    int h1 = pn_klotski_hit(u, g->touch_x1, g->touch_y1);
    return (h0 > 0 && h0 == h1) ? h1 : 0;
}
