/* SPDX-License-Identifier: Apache-2.0
 * 中国象棋残局纯绘制、双色刻纹棋子、楚河汉界棋盘与交叉点局部更新。/ Pure Xiangqi rendering, dual-ring stones, river board and intersection partial updates. */
#include "pn_xiangqi_internal.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <string.h>

static const char *const tiers[] = {"初级", "中级", "高级"};
static const char *const tier_notes[] = {"1-2步绝杀", "2-3步弃子巧杀", "3-4步深算连杀"};

enum {
    XQ_STEP = 66,
    XQ_TX0 = 45,
    XQ_TY_TOP = 168,
    XQ_RIVER_Y = 498,
    XQ_RIVER_H = 28,
    XQ_TY_BOT = 526
};

#define DRAW(expr)                                                                                                     \
    do {                                                                                                               \
        pn_status_t r_ = (expr);                                                                                       \
        if (r_ != PN_OK)                                                                                               \
            return r_;                                                                                                 \
    } while (0)

static bool inside(int x, int y, int a, int b, int w, int h) { return x >= a && y >= b && x < a + w && y < b + h; }

static void tile_origin(unsigned pos, int *tx, int *ty) {
    unsigned col = pos % 9u, row = pos / 9u;
    *tx = XQ_TX0 + (int)col * XQ_STEP;
    *ty = (row < 5u ? XQ_TY_TOP : XQ_TY_BOT) + (int)(row % 5u) * XQ_STEP;
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

static pn_status_t heading(const xq_t *g, pn_font_t *f, pn_frame_t *p) {
    if (!g->pending_start) {
        cap(p, 36, 48, 148, 48, false);
        DRAW(text_c(f, p, "返回", 36, 80, 148, 26, 0, false));
    }
    DRAW(text_c(f, p, "象棋残局", 210, 82, 264, 32, 0, true));
    if (g->screen == PN_XQ_PLAY || g->screen == PN_XQ_COMPLETE) {
        cap(p, 500, 48, 148, 48, false);
        DRAW(text_c(f, p, "选关", 500, 80, 148, 26, 0, false));
    }
    return PN_OK;
}

static pn_status_t metadata(const xq_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_xq_state_t *s = &g->state;
    char left[96], right[96];
    pn_frame_rect(p, 32, 110, 620, 46, 15);
    snprintf(left, sizeof left, "%s · 第 %u 局「%s」", tiers[s->level.tier], s->level.id % 20u + 1u, s->level.title);
    DRAW(pn_font_size(f, 22));
    DRAW(pn_w_text_ex(f, p, left, 36, 142, 362, PN_ALIGN_LEFT, 0, true));

    unsigned long long sec = s->elapsed_ms / 1000u;
    snprintf(right, sizeof right, "步 %u/%u · %u步杀 · %02llu:%02llu", s->moves, s->level.max_moves,
             s->level.mate_moves, sec / 60u, sec % 60u);
    DRAW(pn_font_size(f, 19));
    return pn_w_text_ex(f, p, right, 400, 142, 248, PN_ALIGN_RIGHT, PN_UI_MUTED, false);
}

static pn_status_t status_prompt(const xq_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_xq_state_t *s = &g->state;
    char line[96];
    pn_frame_rect(p, 32, 870, 620, 48, 15);
    if (s->hint_from != PN_XQ_NONE_SQ && s->hint_to != PN_XQ_NONE_SQ) {
        char mv[24];
        pn_xiangqi_move_text(s->board, s->hint_from, s->hint_to, mv);
        snprintf(line, sizeof line, "锦囊提示：%s", mv[0] ? mv : "按高亮落子");
    } else if (pn_xiangqi_in_check(s->board, 0u)) {
        snprintf(line, sizeof line, "红帅受将！请立即应将解杀");
    } else if (pn_xiangqi_in_check(s->board, 1u)) {
        snprintf(line, sizeof line, "将军！乘胜追击破阵");
    } else if (s->selected != PN_XQ_NONE_SQ) {
        snprintf(line, sizeof line, "已选红子 · 点击圆点或框标落子");
    } else if (s->last_from != PN_XQ_NONE_SQ) {
        snprintf(line, sizeof line, "黑方已应着 · 请走红子");
    } else {
        snprintf(line, sizeof line, "红先破阵 · 点选红子查看着法");
    }
    return text_c(f, p, line, 36, 904, 612, 23, 0, true);
}

static bool is_fiducial(unsigned col, unsigned row) {
    if ((row == 2u || row == 7u) && (col == 1u || col == 7u)) return true;
    if ((row == 3u || row == 6u) && (col % 2u) == 0u) return true;
    return false;
}

static void draw_corners(pn_frame_t *p, int x, int y, int side, int arm, int thick, uint8_t shade) {
    pn_frame_rect(p, x, y, arm, thick, shade);
    pn_frame_rect(p, x, y, thick, arm, shade);
    pn_frame_rect(p, x + side - arm, y, arm, thick, shade);
    pn_frame_rect(p, x + side - thick, y, thick, arm, shade);
    pn_frame_rect(p, x, y + side - thick, arm, thick, shade);
    pn_frame_rect(p, x, y + side - arm, thick, arm, shade);
    pn_frame_rect(p, x + side - arm, y + side - thick, arm, thick, shade);
    pn_frame_rect(p, x + side - thick, y + side - arm, thick, arm, shade);
}

static const char *piece_glyph(uint8_t p) {
    static const char *const red[8] = {"", "帅", "仕", "相", "马", "车", "炮", "兵"};
    static const char *const blk[8] = {"", "将", "士", "象", "马", "车", "炮", "卒"};
    unsigned k = p & 7u;
    if (k < 1u || k > 7u) return "";
    return (p & PN_XQ_BLACK) ? blk[k] : red[k];
}

static pn_status_t draw_tile(const pn_xq_state_t *s, const uint8_t legal[PN_XQ_CELLS], unsigned pos, pn_font_t *f,
                             pn_frame_t *p) {
    unsigned col = pos % 9u, row = pos / 9u;
    int tx, ty;
    tile_origin(pos, &tx, &ty);
    int cx = tx + XQ_STEP / 2, cy = ty + XQ_STEP / 2;

    pn_frame_rect(p, tx, ty, XQ_STEP, XQ_STEP, 15);

    /* 横纵交叉线与河界边线。/ Orthogonal intersection lines and riverbank edges. */
    int h_thick = (row == 0u || row == 4u || row == 5u || row == 9u) ? 2 : 1;
    if (col > 0u) pn_frame_rect(p, tx, cy - h_thick / 2, XQ_STEP / 2 + 1, h_thick, 2);
    if (col < 8u) pn_frame_rect(p, cx, cy - h_thick / 2, XQ_STEP / 2, h_thick, 2);

    int v_thick = (col == 0u || col == 8u) ? 2 : 1;
    if (row > 0u && (row != 5u || col == 0u || col == 8u))
        pn_frame_rect(p, cx - v_thick / 2, ty, v_thick, XQ_STEP / 2 + 1, 2);
    if (row < 9u && (row != 4u || col == 0u || col == 8u))
        pn_frame_rect(p, cx - v_thick / 2, cy, v_thick, XQ_STEP / 2, 2);

    /* 九宫45度斜线（严格落在单格66×66像素内）。/ Palace 45-degree diagonals strictly bounded within the 66x66 tile. */
    bool nw = (col == 4u && (row == 1u || row == 8u)) || (col == 5u && (row == 2u || row == 9u));
    bool ne = (col == 4u && (row == 1u || row == 8u)) || (col == 3u && (row == 2u || row == 9u));
    bool sw = (col == 4u && (row == 1u || row == 8u)) || (col == 5u && (row == 0u || row == 7u));
    bool se = (col == 4u && (row == 1u || row == 8u)) || (col == 3u && (row == 0u || row == 7u));
    for (int k = 0; k < XQ_STEP / 2; k++) {
        if (nw) pn_frame_rect(p, cx - k, cy - k, 2, 1, 5);
        if (ne && cx + k + 1 < tx + XQ_STEP) pn_frame_rect(p, cx + k, cy - k, 2, 1, 5);
        if (sw) pn_frame_rect(p, cx - k, cy + k, 2, 1, 5);
        if (se && cx + k + 1 < tx + XQ_STEP) pn_frame_rect(p, cx + k, cy + k, 2, 1, 5);
    }

    /* 炮兵花位直角标。/ Cannon and soldier fiducial corner marks. */
    if (is_fiducial(col, row)) {
        int off = 5, arm = 7;
        if (col > 0u) {
            pn_frame_rect(p, cx - off - arm, cy - off, arm, 2, 5);
            pn_frame_rect(p, cx - off, cy - off - arm, 2, arm, 5);
            pn_frame_rect(p, cx - off - arm, cy + off - 1, arm, 2, 5);
            pn_frame_rect(p, cx - off, cy + off, 2, arm, 5);
        }
        if (col < 8u) {
            pn_frame_rect(p, cx + off + 1, cy - off, arm, 2, 5);
            pn_frame_rect(p, cx + off - 1, cy - off - arm, 2, arm, 5);
            pn_frame_rect(p, cx + off + 1, cy + off - 1, arm, 2, 5);
            pn_frame_rect(p, cx + off - 1, cy + off, 2, arm, 5);
        }
    }

    if (s->outcome == PN_XQ_ONGOING) {
        if (pos == s->selected || pos == s->hint_from) {
            draw_corners(p, tx + 2, ty + 2, XQ_STEP - 4, 13, 3, 0);
        } else if (pos == s->last_from || pos == s->last_to) {
            draw_corners(p, tx + 4, ty + 4, XQ_STEP - 8, 9, 2, 7);
        }
    }

    uint8_t piece = s->board[pos];
    if (piece != 0) {
        bool blk = (piece & PN_XQ_BLACK) != 0;
        pn_w_dot(p, (float)(cx + 1), (float)(cy + 2), 26.2f, 9);
        if (!blk) {
            pn_w_dot(p, (float)cx, (float)cy, 26.2f, 0);
            pn_w_dot(p, (float)cx, (float)cy, 23.8f, 15);
            pn_w_ring(p, (float)cx, (float)cy, 20.0f, 1.6f, 2);
            DRAW(text_c(f, p, piece_glyph(piece), tx, cy + 9, XQ_STEP, 26, 0, true));
        } else {
            pn_w_dot(p, (float)cx, (float)cy, 26.2f, 0);
            pn_w_ring(p, (float)cx, (float)cy, 21.0f, 1.8f, 15);
            DRAW(text_c(f, p, piece_glyph(piece), tx, cy + 9, XQ_STEP, 26, 15, true));
        }
    }

    if (s->outcome == PN_XQ_ONGOING) {
        if (pos == s->hint_to) {
            if (piece == 0) {
                pn_w_ring(p, (float)cx, (float)cy, 13.5f, 2.2f, 0);
                pn_w_dot(p, (float)cx, (float)cy, 6.0f, 0);
            } else {
                draw_corners(p, tx + 2, ty + 2, XQ_STEP - 4, 15, 3, 0);
            }
        } else if (legal[pos]) {
            if (piece == 0) {
                pn_w_dot(p, (float)cx, (float)cy, 7.2f, 2);
                pn_w_dot(p, (float)cx, (float)cy, 2.8f, 15);
            } else {
                draw_corners(p, tx + 3, ty + 3, XQ_STEP - 6, 12, 3, 0);
            }
        }
    }
    return PN_OK;
}

static pn_status_t draw_river_and_frame(pn_font_t *f, pn_frame_t *p) {
    pn_w_outline(p, XQ_TX0 - 6, XQ_TY_TOP - 6, 9 * XQ_STEP + 12, 10 * XQ_STEP + XQ_RIVER_H + 12, 3, 0);
    pn_frame_rect(p, XQ_TX0, XQ_RIVER_Y, 9 * XQ_STEP, XQ_RIVER_H, 15);
    pn_frame_rect(p, XQ_TX0 + XQ_STEP / 2 - 1, XQ_RIVER_Y, 2, XQ_RIVER_H, 2);
    pn_frame_rect(p, XQ_TX0 + 8 * XQ_STEP + XQ_STEP / 2 - 1, XQ_RIVER_Y, 2, XQ_RIVER_H, 2);
    DRAW(text_c(f, p, "楚  河", XQ_TX0 + XQ_STEP, XQ_RIVER_Y + 21, 3 * XQ_STEP, 20, 3, true));
    return text_c(f, p, "汉  界", XQ_TX0 + 5 * XQ_STEP, XQ_RIVER_Y + 21, 3 * XQ_STEP, 20, 3, true);
}

pn_status_t pn_xiangqi_render_update(const pn_xiangqi_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p) return PN_INVALID;
    const xq_t *g = u->impl;
    if (!g->patch || g->full || g->screen != PN_XQ_PLAY) return PN_UNSUPPORTED;

    int saved_px = f->pixels;
    const pn_xq_state_t *s = &g->state;
    uint8_t cur_legal[PN_XQ_CELLS];
    pn_xq_compute_legal_dests(s, cur_legal);

    pn_status_t status = metadata(g, f, p);
    if (status == PN_OK) status = status_prompt(g, f, p);
    for (unsigned pos = 0; status == PN_OK && pos < PN_XQ_CELLS; pos++) {
        bool was_hl = (pos == g->shown_selected || pos == g->shown_last_from || pos == g->shown_last_to ||
                       pos == g->shown_hint_from || pos == g->shown_hint_to);
        bool cur_hl = (pos == s->selected || pos == s->last_from || pos == s->last_to || pos == s->hint_from ||
                       pos == s->hint_to);
        if (g->shown_board[pos] != s->board[pos] || g->shown_legal[pos] != cur_legal[pos] || was_hl || cur_hl)
            status = draw_tile(s, cur_legal, pos, f, p);
    }
    pn_status_t restored = pn_font_size(f, saved_px);
    return status == PN_OK ? restored : status;
}

pn_status_t pn_xiangqi_render(const pn_xiangqi_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p) return PN_INVALID;
    const xq_t *g = u->impl;
    int saved_px = f->pixels;
    pn_frame_clear(p, 15);
    pn_status_t status = pn_w_status(f, p);
    if (status == PN_OK) status = heading(g, f, p);

    if (status == PN_OK && (g->screen == PN_XQ_PLAY || g->screen == PN_XQ_COMPLETE)) {
        const pn_xq_state_t *s = &g->state;
        uint8_t legal[PN_XQ_CELLS];
        pn_xq_compute_legal_dests(s, legal);
        status = metadata(g, f, p);
        if (status == PN_OK) status = draw_river_and_frame(f, p);
        for (unsigned pos = 0; status == PN_OK && pos < PN_XQ_CELLS; pos++)
            status = draw_tile(s, legal, pos, f, p);

        if (status == PN_OK) {
            if (g->screen == PN_XQ_PLAY) {
                status = status_prompt(g, f, p);
                static const char *const tools[] = {"悔棋", "提示", "重摆", "清屏"};
                for (int i = 0; status == PN_OK && i < 4; i++)
                    status = button(f, p, tools[i], 36 + i * 156, 944, 144, 76, false);
                if (status == PN_OK)
                    status = text_c(f, p, "白底双圈为红方 · 黑底白圈为黑方", 36, 1084, 612, 21, PN_UI_MUTED, false);
            } else {
                char msg[96];
                if (s->outcome == PN_XQ_WON) {
                    snprintf(msg, sizeof msg, "破阵成功！「%s」 · 共 %u 回合", s->level.title, s->moves);
                    status = text_c(f, p, msg, 36, 908, 612, 27, 0, true);
                    if (status == PN_OK) status = button(f, p, "下一局", 36, 944, 296, 78, true);
                    if (status == PN_OK) status = button(f, p, "选关", 352, 944, 296, 78, false);
                    if (status == PN_OK) status = button(f, p, "重摆本局", 36, 1044, 296, 78, false);
                    if (status == PN_OK) status = button(f, p, "返回娱乐", 352, 1044, 296, 78, false);
                } else {
                    snprintf(msg, sizeof msg, "%s",
                             s->moves >= s->level.max_moves ? "步数耗尽，未能破阵" : "红帅遇险，破阵受挫");
                    status = text_c(f, p, msg, 36, 908, 612, 27, 0, true);
                    if (status == PN_OK) status = button(f, p, "悔棋一步", 36, 944, 296, 78, true);
                    if (status == PN_OK) status = button(f, p, "重摆本局", 352, 944, 296, 78, false);
                    if (status == PN_OK) status = button(f, p, "选关", 36, 1044, 296, 78, false);
                    if (status == PN_OK) status = button(f, p, "返回娱乐", 352, 1044, 296, 78, false);
                }
            }
        }
    } else if (status == PN_OK && g->screen == PN_XQ_SELECT) {
        status = text_c(f, p, "红先强制连杀 · 顽强防守实战对弈", 36, 232, 612, 30, 0, true);
        if (status == PN_OK)
            status = text_c(f, p, "精选古谱杀型与弃子妙手，每局皆有严密正着", 36, 284, 612, 22, PN_UI_MUTED, false);
        for (int i = 0; status == PN_OK && i < 3; i++) {
            int x = 36 + i * 208;
            bool sel = (unsigned)i == g->tier;
            cap(p, x, 352, 196, 104, sel);
            status = text_c(f, p, tiers[i], x, 396, 196, 28, sel ? 15 : 0, true);
            if (status == PN_OK)
                status = text_c(f, p, tier_notes[i], x, 434, 196, 19, sel ? 15 : PN_UI_MUTED, false);
        }
        if (status == PN_OK) {
            unsigned id = g->tier * 20u + g->selected_level;
            pn_xq_level_t lv = {0};
            bool ok_lv = (pn_xiangqi_level(id, &lv) == PN_OK);
            bool solved = ((g->state.completed[id / 8u] >> (id % 8u)) & 1u) != 0;
            char line1[96], line2[96];
            snprintf(line1, sizeof line1, "第 %u / 20 局 · 「%s」", g->selected_level + 1u, ok_lv ? lv.title : "");
            snprintf(line2, sizeof line2, "约 %u 步绝杀%s", ok_lv ? lv.mate_moves : 1u, solved ? " · 已破阵" : "");
            status = text_c(f, p, line1, 36, 586, 612, 34, 0, true);
            if (status == PN_OK) status = text_c(f, p, line2, 36, 634, 612, 24, PN_UI_MUTED, false);
        }
        if (status == PN_OK) status = button(f, p, "上一局", 36, 682, 296, 80, false);
        if (status == PN_OK) status = button(f, p, "下一局", 352, 682, 296, 80, false);
        if (status == PN_OK) status = button(f, p, "摆阵开局", 36, 846, 612, 88, true);
        if (status == PN_OK && g->has)
            status = button(f, p, "继续当前残局", 36, 964, 612, 84, false);
    } else if (status == PN_OK && g->screen == PN_XQ_CONFIRM) {
        status = text_c(f, p, g->restart ? "重新摆设本局棋势？" : "保存并选择其他残局？", 36, 470, 612, 34, 0, true);
        if (status == PN_OK) status = button(f, p, "确认", 36, 660, 612, 84, true);
        if (status == PN_OK) status = button(f, p, "取消", 36, 772, 612, 84, false);
    } else if (status == PN_OK) {
        status = text_c(f, p, g->pending_start ? "新局保存尚未确认" : "无法恢复残局记录", 36, 470, 612, 34, 0, true);
        if (status == PN_OK) status = text_c(f, p, "原记录保留，请重试", 36, 536, 612, 26, PN_UI_MUTED, false);
        if (status == PN_OK) status = button(f, p, "重试", 36, 660, 612, 84, true);
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

int pn_xiangqi_hit(const pn_xiangqi_t *u, int x, int y) {
    if (!u || !u->impl) return 0;
    const xq_t *g = u->impl;
    if (!g->pending_start && inside(x, y, 28, 40, 164, 60)) return PN_XQ_BACK;
    if (g->notice && y >= 1188) return PN_XQ_RETRY;

    if (g->screen == PN_XQ_PLAY || g->screen == PN_XQ_COMPLETE) {
        if (inside(x, y, 492, 40, 164, 60)) return PN_XQ_CHOOSE;
        if (g->screen == PN_XQ_PLAY) {
            if (inside(x, y, XQ_TX0, XQ_TY_TOP, 9 * XQ_STEP, 5 * XQ_STEP))
                return PN_XQ_CELL + (y - XQ_TY_TOP) / XQ_STEP * 9 + (x - XQ_TX0) / XQ_STEP;
            if (inside(x, y, XQ_TX0, XQ_TY_BOT, 9 * XQ_STEP, 5 * XQ_STEP))
                return PN_XQ_CELL + (5 + (y - XQ_TY_BOT) / XQ_STEP) * 9 + (x - XQ_TX0) / XQ_STEP;
            /* 点击楚河汉界缝隙时就近归入第4行或第5行交叉点。/ Snap river-seam taps to the nearer riverbank rank. */
            if (inside(x, y, XQ_TX0, XQ_RIVER_Y, 9 * XQ_STEP, XQ_RIVER_H)) {
                int row = (y < XQ_RIVER_Y + XQ_RIVER_H / 2) ? 4 : 5;
                return PN_XQ_CELL + row * 9 + (x - XQ_TX0) / XQ_STEP;
            }
            static const int cmds[] = {PN_XQ_UNDO_MOVE, PN_XQ_HINT, PN_XQ_RESTART, PN_XQ_CLEAN};
            for (int i = 0; i < 4; i++)
                if (inside(x, y, 36 + i * 156, 944, 144, 76)) return cmds[i];
        } else if (g->state.outcome == PN_XQ_WON) {
            if (inside(x, y, 36, 944, 296, 78)) return PN_XQ_NEXT;
            if (inside(x, y, 352, 944, 296, 78)) return PN_XQ_CHOOSE;
            if (inside(x, y, 36, 1044, 296, 78)) return PN_XQ_RESTART;
            if (inside(x, y, 352, 1044, 296, 78)) return PN_XQ_EXIT;
        } else {
            if (inside(x, y, 36, 944, 296, 78)) return PN_XQ_UNDO_MOVE;
            if (inside(x, y, 352, 944, 296, 78)) return PN_XQ_RESTART;
            if (inside(x, y, 36, 1044, 296, 78)) return PN_XQ_CHOOSE;
            if (inside(x, y, 352, 1044, 296, 78)) return PN_XQ_EXIT;
        }
    } else if (g->screen == PN_XQ_SELECT) {
        for (int i = 0; i < 3; i++)
            if (inside(x, y, 36 + i * 208, 352, 196, 104)) return PN_XQ_TIER + i;
        if (inside(x, y, 36, 682, 296, 80)) return PN_XQ_PREV_LEVEL;
        if (inside(x, y, 352, 682, 296, 80)) return PN_XQ_NEXT_LEVEL;
        if (inside(x, y, 36, 846, 612, 88)) return PN_XQ_START;
        if (g->has && inside(x, y, 36, 964, 612, 84)) return PN_XQ_RESUME;
    } else if (g->screen == PN_XQ_CONFIRM) {
        if (inside(x, y, 36, 660, 612, 84)) return PN_XQ_ACCEPT;
        if (inside(x, y, 36, 772, 612, 84)) return PN_XQ_CANCEL;
    } else if (g->screen == PN_XQ_ERROR) {
        if (inside(x, y, 36, 660, 612, 84)) return PN_XQ_RETRY;
        if (!g->pending_start && inside(x, y, 36, 772, 612, 84)) return PN_XQ_BACK;
    }
    return 0;
}
