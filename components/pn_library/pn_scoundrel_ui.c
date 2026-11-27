/* SPDX-License-Identifier: Apache-2.0
 * 地牢纸牌（Scoundrel）16灰阶纯绘制：矢量花色、2×2大牌、磨损链与逐区增量更新。
 * Pure 16-grayscale Scoundrel rendering: procedural suits, 2x2 cards, weapon wear track and byte-identical partial updates. */
#include "pn_scoundrel_internal.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SC_CARD_W = 294,
    SC_CARD_H = 260,
    SC_GAP_X = 24,
    SC_GAP_Y = 24,
    SC_GRID_X = 36,
    SC_GRID_Y = 204
};

#define DRAW(expr)                                                                                                     \
    do {                                                                                                               \
        pn_status_t r_ = (expr);                                                                                       \
        if (r_ != PN_OK)                                                                                               \
            return r_;                                                                                                 \
    } while (0)

static bool inside(int x, int y, int a, int b, int w, int h) { return x >= a && y >= b && x < a + w && y < b + h; }

static void pixel_fill(pn_frame_t *p, int x, int y, int w, int h, int cut, uint8_t ink) {
    if (w <= 0 || h <= 0) return;
    if (cut * 2 >= w || cut * 2 >= h) {
        pn_frame_rect(p, x, y, w, h, ink);
        return;
    }
    pn_frame_rect(p, x + cut, y, w - cut * 2, h, ink);
    pn_frame_rect(p, x + cut / 2, y + cut / 2, w - cut, h - cut, ink);
    pn_frame_rect(p, x, y + cut, w, h - cut * 2, ink);
}

static void draw_cap(pn_frame_t *p, int x, int y, int w, int h, bool selected, bool disabled) {
    uint8_t shadow = disabled ? 13 : 7;
    uint8_t border = disabled ? 10 : 0;
    uint8_t face = disabled ? 14 : (selected ? 0 : 15);
    pixel_fill(p, x + 2, y + 4, w - 2, h - 4, 6, shadow);
    pixel_fill(p, x, y, w, h - 4, 6, border);
    pixel_fill(p, x + 3, y + 3, w - 6, h - 10, 4, face);
    if (!selected && !disabled) pn_frame_rect(p, x + 8, y + h - 10, w - 16, 3, 11);
}

static void draw_dashed_box(pn_frame_t *p, int x, int y, int w, int h, uint8_t fill, uint8_t ink) {
    pn_frame_rect(p, x, y, w, h, fill);
    for (int px = x; px < x + w; px += 12) {
        int seg = (px + 7 <= x + w) ? 7 : (x + w - px);
        pn_frame_rect(p, px, y, seg, 2, ink);
        pn_frame_rect(p, px, y + h - 2, seg, 2, ink);
    }
    for (int py = y; py < y + h; py += 12) {
        int seg = (py + 7 <= y + h) ? 7 : (y + h - py);
        pn_frame_rect(p, x, py, 2, seg, ink);
        pn_frame_rect(p, x + w - 2, py, 2, seg, ink);
    }
}

static void fill_circle(pn_frame_t *p, int cx, int cy, int r, uint8_t ink) {
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
        pn_frame_rect(p, cx - dx, cy + dy, dx * 2 + 1, 1, ink);
    }
}

/* 纯C几何绘制四花色，彻底避免内置字库缺失扑克符号。 / Pure-C geometric suits avoiding missing Unicode card glyphs. */
static void draw_suit(pn_frame_t *p, unsigned suit, int cx, int cy, int r, uint8_t ink, uint8_t inner_bg) {
    if (r < 4) return;
    if (suit == PN_SC_DIAMOND) {
        int thick = r >= 16 ? 4 : 2;
        for (int dy = -r; dy <= r; dy++) {
            int span = (r - abs(dy)) * 85 / 100;
            if (span < 0) continue;
            pn_frame_rect(p, cx - span, cy + dy, span * 2 + 1, 1, ink);
            int inner = span - thick;
            if (inner > 0 && abs(dy) <= r - thick) {
                uint8_t mid = (r >= 16 && abs(dy) <= r / 2 && inner > 3) ? 12 : inner_bg;
                pn_frame_rect(p, cx - inner, cy + dy, inner * 2 + 1, 1, mid);
            }
        }
        return;
    }
    if (suit == PN_SC_HEART) {
        int thick = r >= 16 ? 4 : 2;
        int lobe_r = r * 48 / 100;
        int lobe_x = r * 42 / 100;
        int lobe_y = -r * 24 / 100;
        fill_circle(p, cx - lobe_x, cy + lobe_y, lobe_r, ink);
        fill_circle(p, cx + lobe_x, cy + lobe_y, lobe_r, ink);
        for (int dy = lobe_y; dy <= r; dy++) {
            int num = (r - dy) * (lobe_x + lobe_r);
            int den = (r - lobe_y);
            int span = den > 0 ? num / den : 0;
            if (span >= 0) pn_frame_rect(p, cx - span, cy + dy, span * 2 + 1, 1, ink);
        }
        if (lobe_r > thick) {
            fill_circle(p, cx - lobe_x, cy + lobe_y, lobe_r - thick, inner_bg);
            fill_circle(p, cx + lobe_x, cy + lobe_y, lobe_r - thick, inner_bg);
            for (int dy = lobe_y; dy <= r - thick - 1; dy++) {
                int num = (r - thick - 1 - dy) * (lobe_x + lobe_r - thick);
                int den = (r - thick - 1 - lobe_y);
                int span = den > 0 ? num / den : 0;
                if (span > 0) pn_frame_rect(p, cx - span, cy + dy, span * 2 + 1, 1, inner_bg);
            }
            if (r >= 16) {
                /* 药水十字标识 / Medical cross inside large heart */
                int cw = r / 3;
                int ct = 4;
                pn_frame_rect(p, cx - cw, cy - ct / 2, cw * 2, ct, ink);
                pn_frame_rect(p, cx - ct / 2, cy - cw, ct, cw * 2, ink);
            }
        }
        return;
    }
    if (suit == PN_SC_SPADE) {
        int lobe_r = r * 42 / 100;
        int lobe_x = r * 38 / 100;
        int lobe_y = r * 14 / 100;
        for (int dy = -r; dy <= lobe_y; dy++) {
            int num = (dy + r) * (lobe_x + lobe_r);
            int den = (r + lobe_y);
            int span = den > 0 ? num / den : 0;
            if (span >= 0) pn_frame_rect(p, cx - span, cy + dy, span * 2 + 1, 1, ink);
        }
        fill_circle(p, cx - lobe_x, cy + lobe_y, lobe_r, ink);
        fill_circle(p, cx + lobe_x, cy + lobe_y, lobe_r, ink);
        for (int dy = r * 20 / 100; dy <= r; dy++) {
            int half = (r / 8) + ((dy - r * 20 / 100) * (r * 36 / 100)) / (r * 80 / 100 + 1);
            pn_frame_rect(p, cx - half, cy + dy, half * 2 + 1, 1, ink);
        }
        return;
    }
    /* PN_SC_CLUB 梅花 / Club */
    int cr = r * 42 / 100;
    fill_circle(p, cx, cy - r * 42 / 100, cr, ink);
    fill_circle(p, cx - r * 42 / 100, cy + r * 10 / 100, cr, ink);
    fill_circle(p, cx + r * 42 / 100, cy + r * 10 / 100, cr, ink);
    fill_circle(p, cx, cy, cr / 2 + 1, ink);
    for (int dy = r * 18 / 100; dy <= r; dy++) {
        int half = (r / 8) + ((dy - r * 18 / 100) * (r * 36 / 100)) / (r * 82 / 100 + 1);
        pn_frame_rect(p, cx - half, cy + dy, half * 2 + 1, 1, ink);
    }
}

static void rank_label(unsigned rank, char buf[8]) {
    if (rank == 11u) strcpy(buf, "J");
    else if (rank == 12u) strcpy(buf, "Q");
    else if (rank == 13u) strcpy(buf, "K");
    else if (rank == 14u) strcpy(buf, "A");
    else snprintf(buf, 8, "%u", rank);
}

static pn_status_t text_c(pn_font_t *f, pn_frame_t *p, const char *s, int x, int y, int w, int size, uint8_t ink,
                          bool bold) {
    DRAW(pn_font_size(f, size));
    return pn_w_text_ex(f, p, s, x, y, w, PN_ALIGN_CENTER, ink, bold);
}

static pn_status_t text_l(pn_font_t *f, pn_frame_t *p, const char *s, int x, int y, int w, int size, uint8_t ink,
                          bool bold) {
    DRAW(pn_font_size(f, size));
    return pn_w_text_ex(f, p, s, x, y, w, PN_ALIGN_LEFT, ink, bold);
}

static pn_status_t text_r(pn_font_t *f, pn_frame_t *p, const char *s, int x, int y, int w, int size, uint8_t ink,
                          bool bold) {
    DRAW(pn_font_size(f, size));
    return pn_w_text_ex(f, p, s, x, y, w, PN_ALIGN_RIGHT, ink, bold);
}

static pn_status_t draw_button(pn_font_t *f, pn_frame_t *p, const char *s, int x, int y, int w, int h, bool selected,
                               bool disabled) {
    draw_cap(p, x, y, w, h, selected, disabled);
    uint8_t ink = disabled ? 9 : (selected ? 15 : 0);
    return text_c(f, p, s, x, y + h / 2 + 9, w - 2, 26, ink, selected);
}

static void slot_rect(unsigned slot, int *ox, int *oy) {
    *ox = SC_GRID_X + (int)(slot & 1u) * (SC_CARD_W + SC_GAP_X);
    *oy = SC_GRID_Y + (int)(slot >> 1) * (SC_CARD_H + SC_GAP_Y);
}

static int selected_preview_damage(const pn_sc_state_t *s) {
    if (s->selected_slot >= PN_SC_ROOM_SLOTS) return 0;
    uint8_t c = s->room[s->selected_slot];
    if (!pn_sc_is_monster(c)) return 0;
    if (pn_scoundrel_can_use_weapon(s, c)) return pn_scoundrel_weapon_damage(s, c);
    return (int)pn_sc_card_rank(c);
}

static pn_status_t draw_header(const sc_t *g, pn_font_t *f, pn_frame_t *p) {
    if (!g->pending_start) {
        draw_cap(p, 36, 48, 148, 48, false, false);
        DRAW(text_c(f, p, "返回", 36, 80, 148, 26, 0, false));
    }
    DRAW(text_c(f, p, "地牢纸牌", 200, 82, 284, 32, 0, true));
    if (g->screen == PN_SC_PLAY || g->screen == PN_SC_COMPLETE) {
        draw_cap(p, 500, 48, 148, 48, false, false);
        DRAW(text_c(f, p, "选局", 500, 80, 148, 26, 0, false));
    }
    return PN_OK;
}

static pn_status_t draw_hud(const sc_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_sc_state_t *s = &g->state;
    pn_frame_rect(p, 32, 110, 620, 80, 15);
    pn_frame_rect(p, 36, 112, 612, 76, 0);
    pn_frame_rect(p, 38, 114, 608, 72, 14);

    int prev_dmg = (g->screen == PN_SC_PLAY) ? selected_preview_damage(s) : 0;
    char hp_str[40], right1[48], left2[48], right2[64];
    if (prev_dmg > 0) {
        int nxt = (int)s->hp - prev_dmg;
        if (nxt < 0) nxt = 0;
        snprintf(hp_str, sizeof hp_str, "生命 %d->%d", (int)s->hp, nxt);
    } else {
        snprintf(hp_str, sizeof hp_str, "生命 %d/20", (int)s->hp);
    }
    DRAW(text_l(f, p, hp_str, 50, 144, 156, 22, 0, true));

    /* 20格分段血条（中灰预览即将扣除的血量） / 20-segment HP bar with mid-gray damage preview */
    for (int i = 0; i < PN_SC_MAX_HP; i++) {
        int bx = 212 + i * 11;
        int by = 128;
        pn_frame_rect(p, bx, by, 8, 18, (i < (int)s->hp) ? 0 : 10);
        uint8_t fill = 15;
        if (i < (int)s->hp - prev_dmg) fill = 0;
        else if (i < (int)s->hp) fill = 8;
        pn_frame_rect(p, bx + 1, by + 1, 6, 16, fill);
    }

    snprintf(right1, sizeof right1, "牌堆 %u/44 · 第%u房", s->deck_count, s->room_no);
    DRAW(text_r(f, p, right1, 434, 144, 200, 19, 3, true));

    snprintf(left2, sizeof left2, "%s · #%04X", s->mode == PN_SC_MODE_HARDCORE ? "硬核(<)" : "标准(<=)",
             (unsigned)(s->seed & 0xFFFFu));
    DRAW(text_l(f, p, left2, 50, 176, 240, 18, 6, false));

    bool flee_ok = (s->can_flee && !s->fled_last && s->room_cleared == 0u && s->deck_count > 0u);
    snprintf(right2, sizeof right2, "本房 %u/3 · 药水%s · 逃跑%s", s->room_cleared > 3u ? 3u : s->room_cleared,
             s->potion_used ? "已饮" : "可用", flee_ok ? "可用" : "不可");
    DRAW(text_r(f, p, right2, 290, 176, 344, 18, 4, false));
    return PN_OK;
}

static pn_status_t draw_room_slot(const sc_t *g, pn_font_t *f, pn_frame_t *p, unsigned slot) {
    const pn_sc_state_t *s = &g->state;
    int x, y;
    slot_rect(slot, &x, &y);
    int w = SC_CARD_W, h = SC_CARD_H;

    /* 先整槽清白，确保局部更新与整绘逐字节一致 / Clear entire slot for byte-identical partial updates */
    pn_frame_rect(p, x, y, w, h, 15);

    uint8_t card = s->room[slot];
    if (card == PN_SC_CARD_NONE) {
        draw_dashed_box(p, x + 2, y + 2, w - 4, h - 4, 14, 10);
        return text_c(f, p, "已清除", x, y + h / 2 + 8, w, 24, 8, true);
    }

    bool sel = (g->screen == PN_SC_PLAY && s->selected_slot == slot);
    unsigned suit = pn_sc_card_suit(card);
    unsigned rank = pn_sc_card_rank(card);
    bool is_m = pn_sc_is_monster(card);
    bool is_w = pn_sc_is_weapon(card);
    bool is_p = pn_sc_is_potion(card);
    bool wasted_p = is_p && (s->potion_used != 0u || s->hp >= PN_SC_MAX_HP);

    /* 卡片阴影与边框 / Tactile shadow and frame */
    pixel_fill(p, x + 4, y + 5, w - 4, h - 5, 8, sel ? 4 : 8);
    pixel_fill(p, x, y, w - 4, h - 5, 8, 0);
    int bw = sel ? 6 : 3;
    pixel_fill(p, x + bw, y + bw, w - 4 - bw * 2, h - 5 - bw * 2, 5, 15);

    /* 顶部花色标题带 / Header band */
    int hx = x + bw, hy = y + bw, hw = w - 4 - bw * 2, hh = 46 - bw;
    uint8_t head_bg = is_m ? 2 : (is_w ? 13 : 14);
    uint8_t head_fg = is_m ? 15 : (wasted_p ? 7 : 0);
    pn_frame_rect(p, hx, hy, hw, hh, head_bg);
    pn_frame_rect(p, hx, hy + hh, hw, 2, 0);

    draw_suit(p, suit, x + 26, y + 24, 10, head_fg, head_bg);
    char rbuf[8], tl[24];
    rank_label(rank, rbuf);
    snprintf(tl, sizeof tl, "%s", rbuf);
    DRAW(text_l(f, p, tl, x + 44, y + 34, 80, 24, head_fg, true));
    DRAW(text_r(f, p, is_m ? "怪物" : (is_w ? "武器" : "血瓶"), x + 124, y + 34, w - 144, 20, head_fg, true));

    if (sel) {
        pn_frame_rect(p, x + w - 92, y + 54, 78, 26, 0);
        DRAW(text_c(f, p, "已选中", x + w - 92, y + 73, 78, 16, 15, true));
    }

    /* 中央大花色徽章与大号数值 / Center suit artwork and large value */
    draw_suit(p, suit, x + 84, y + 126, 30, wasted_p ? 8 : 0, 15);
    char big[16];
    if (is_m) snprintf(big, sizeof big, "%u", rank);
    else if (is_w) snprintf(big, sizeof big, "攻%u", rank);
    else snprintf(big, sizeof big, "+%u", rank);
    DRAW(text_c(f, p, big, x + 126, y + 146, 148, is_m ? 52 : 44, wasted_p ? 8 : 0, true));

    /* 底部战术预判条 / Bottom tactical preview strip */
    int fy = y + 200;
    int fh = (y + h - 5 - bw) - fy;
    pn_frame_rect(p, hx, fy, hw, 2, 10);
    if (is_m) {
        bool can_w = pn_scoundrel_can_use_weapon(s, card);
        if (can_w) {
            int wd = pn_scoundrel_weapon_damage(s, card);
            int half = hw / 2;
            pn_frame_rect(p, hx, fy + 2, half, fh - 2, sel ? 0 : 13);
            pn_frame_rect(p, hx + half, fy + 2, 2, fh - 2, 10);
            char fl[24], fr[24];
            snprintf(fl, sizeof fl, "剑 -%d血", wd);
            snprintf(fr, sizeof fr, "手 -%u血", rank);
            DRAW(text_c(f, p, fl, hx, fy + 32, half, 19, sel ? 15 : 0, true));
            DRAW(text_c(f, p, fr, hx + half + 2, fy + 32, hw - half - 2, 19, 4, false));
        } else {
            pn_frame_rect(p, hx, fy + 2, hw, fh - 2, 14);
            char ft[36];
            if ((int)s->hp <= (int)rank) snprintf(ft, sizeof ft, "仅空手 -%u血 (致命)", rank);
            else snprintf(ft, sizeof ft, "仅空手 -%u血", rank);
            DRAW(text_c(f, p, ft, hx, fy + 32, hw, 19, 0, true));
        }
    } else if (is_w) {
        char ft[36];
        if (s->weapon != PN_SC_CARD_NONE) snprintf(ft, sizeof ft, "装备并替换 攻%u", pn_sc_card_rank(s->weapon));
        else snprintf(ft, sizeof ft, "点击直接装备");
        DRAW(text_c(f, p, ft, hx, fy + 32, hw, 19, 2, true));
    } else {
        pn_frame_rect(p, hx, fy + 2, hw, fh - 2, wasted_p ? 14 : 15);
        const char *ft = s->potion_used ? "本房已饮 · 选则弃置"
                         : (s->hp >= PN_SC_MAX_HP) ? "生命已满 · 选则弃置"
                                                   : "饮用回血 (上限20)";
        DRAW(text_c(f, p, ft, hx, fy + 32, hw, 19, wasted_p ? 7 : 2, !wasted_p));
    }
    return PN_OK;
}

static pn_status_t draw_equip(const sc_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_sc_state_t *s = &g->state;
    pn_frame_rect(p, 36, 764, 612, 140, 15);
    pn_frame_rect(p, 36, 764, 612, 140, 0);
    pn_frame_rect(p, 38, 766, 608, 136, 14);

    /* 左侧当前武器槽 / Left weapon box */
    if (s->weapon != PN_SC_CARD_NONE) {
        pn_frame_rect(p, 48, 776, 176, 116, 0);
        pn_frame_rect(p, 50, 778, 172, 112, 15);
        pn_frame_rect(p, 50, 778, 172, 30, 13);
        pn_frame_rect(p, 50, 808, 172, 1, 9);
        draw_suit(p, PN_SC_DIAMOND, 66, 793, 7, 0, 13);
        DRAW(text_l(f, p, "当前武器", 80, 800, 136, 18, 0, true));
        char atk[16], cap[24];
        snprintf(atk, sizeof atk, "攻 %u", pn_sc_card_rank(s->weapon));
        DRAW(text_c(f, p, atk, 50, 850, 172, 32, 0, true));
        if (s->slain_count == 0u) snprintf(cap, sizeof cap, "上限: 全新任意");
        else snprintf(cap, sizeof cap, "上限 %s %u", s->mode == PN_SC_MODE_HARDCORE ? "<" : "<=",
                      pn_sc_card_rank(s->slain[s->slain_count - 1u]));
        DRAW(text_c(f, p, cap, 50, 880, 172, 18, 4, true));
    } else {
        draw_dashed_box(p, 48, 776, 176, 116, 14, 10);
        DRAW(text_c(f, p, "未装备武器", 48, 830, 176, 20, 6, true));
        DRAW(text_c(f, p, "怪物须空手扛", 48, 860, 176, 16, 8, false));
    }

    /* 右侧武器磨损链 / Right slain monster track */
    char title[72];
    bool sel_weapon_ok = (g->screen == PN_SC_PLAY && s->selected_slot < PN_SC_ROOM_SLOTS &&
                          pn_scoundrel_can_use_weapon(s, s->room[s->selected_slot]));
    if (sel_weapon_ok) {
        unsigned r = pn_sc_card_rank(s->room[s->selected_slot]);
        snprintf(title, sizeof title, "磨损链（若斩此怪 -> 新上限 %s %u）",
                 s->mode == PN_SC_MODE_HARDCORE ? "<" : "<=", r);
    } else if (s->weapon == PN_SC_CARD_NONE) {
        snprintf(title, sizeof title, "武器磨损链（拾取方块牌即可装备武器）");
    } else if (s->slain_count == 0u) {
        snprintf(title, sizeof title, "武器磨损链（全新 · 可斩任意点数怪物）");
    } else {
        unsigned last_r = pn_sc_card_rank(s->slain[s->slain_count - 1u]);
        snprintf(title, sizeof title, "武器磨损链（下只怪须 %s %u · 已斩 %u）",
                 s->mode == PN_SC_MODE_HARDCORE ? "<" : "<=", last_r, s->slain_count);
    }
    DRAW(text_l(f, p, title, 238, 798, 396, 18, 0, true));

    if (s->slain_count == 0u && !sel_weapon_ok) {
        draw_dashed_box(p, 238, 810, 396, 80, 15, 11);
        DRAW(text_c(f, p, s->weapon != PN_SC_CARD_NONE ? "尚未斩杀怪物 · 保留最高上限" : "空手状态", 238, 858, 396, 18,
                    7, false));
    } else {
        unsigned max_vis = sel_weapon_ok ? 5u : 6u;
        unsigned start = s->slain_count > max_vis ? (s->slain_count - max_vis) : 0u;
        int cur_x = 238;
        for (unsigned i = start; i < s->slain_count; i++) {
            uint8_t mc = s->slain[i];
            bool is_last = (i + 1u == s->slain_count);
            pn_frame_rect(p, cur_x, 812, 58, 76, 0);
            pn_frame_rect(p, cur_x + (is_last ? 3 : 2), 812 + (is_last ? 3 : 2), 58 - (is_last ? 6 : 4),
                          76 - (is_last ? 6 : 4), is_last ? 2 : 15);
            draw_suit(p, pn_sc_card_suit(mc), cur_x + 17, 832, 7, is_last ? 15 : 0, is_last ? 2 : 15);
            char rb[8], rv[8];
            rank_label(pn_sc_card_rank(mc), rb);
            snprintf(rv, sizeof rv, "%u", pn_sc_card_rank(mc));
            DRAW(text_l(f, p, rb, cur_x + 28, 838, 26, 17, is_last ? 15 : 4, true));
            DRAW(text_c(f, p, rv, cur_x, 872, 58, 24, is_last ? 15 : 0, true));
            cur_x += 54;
        }
        if (sel_weapon_ok) {
            uint8_t sc = s->room[s->selected_slot];
            int px = cur_x + (s->slain_count > 0u ? 10 : 0);
            draw_dashed_box(p, px, 812, 66, 76, 13, 0);
            char rb[8], rv[12];
            rank_label(pn_sc_card_rank(sc), rb);
            snprintf(rv, sizeof rv, "+%s", rb);
            DRAW(text_c(f, p, rv, px, 842, 66, 18, 0, true));
            snprintf(rv, sizeof rv, "%u", pn_sc_card_rank(sc));
            DRAW(text_c(f, p, rv, px, 872, 66, 22, 0, true));
        }
    }
    return PN_OK;
}

static pn_status_t draw_prompt(const sc_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_sc_state_t *s = &g->state;
    pn_frame_rect(p, 36, 920, 612, 104, 15);

    if (g->screen == PN_SC_PLAY && s->selected_slot < PN_SC_ROOM_SLOTS && pn_sc_is_monster(s->room[s->selected_slot])) {
        uint8_t mc = s->room[s->selected_slot];
        unsigned r = pn_sc_card_rank(mc);
        bool can_w = pn_scoundrel_can_use_weapon(s, mc);
        if (can_w) {
            int wd = pn_scoundrel_weapon_damage(s, mc);
            char l1[40], l2[40], r1[40];
            snprintf(l1, sizeof l1, "武器斩杀 (-%d血)", wd);
            snprintf(l2, sizeof l2, "攻%u · 新上限 %s %u", pn_sc_card_rank(s->weapon),
                     s->mode == PN_SC_MODE_HARDCORE ? "<" : "<=", r);
            snprintf(r1, sizeof r1, "空手硬扛 (-%u血)", r);
            draw_cap(p, 36, 926, 296, 92, true, false);
            DRAW(text_c(f, p, l1, 36, 964, 296, 24, 15, true));
            DRAW(text_c(f, p, l2, 36, 996, 296, 17, 12, false));
            draw_cap(p, 352, 926, 296, 92, false, false);
            DRAW(text_c(f, p, r1, 352, 964, 296, 24, 0, true));
            DRAW(text_c(f, p, "保留当前武器上限", 352, 996, 296, 17, 6, false));
            return PN_OK;
        }
        /* 仅可空手且会致死：提供防误触二次确认 / Lethal barehanded-only confirmation */
        char r1[40], r2[40];
        snprintf(r1, sizeof r1, "空手硬扛 (-%u血)", r);
        snprintf(r2, sizeof r2, "生命不足，此步将阵亡");
        draw_cap(p, 36, 926, 296, 92, false, false);
        DRAW(text_c(f, p, "取消选择", 36, 978, 296, 25, 0, true));
        draw_cap(p, 352, 926, 296, 92, true, false);
        DRAW(text_c(f, p, r1, 352, 964, 296, 24, 15, true));
        DRAW(text_c(f, p, r2, 352, 996, 296, 17, 12, false));
        return PN_OK;
    }

    if (g->screen == PN_SC_COMPLETE) {
        pn_frame_rect(p, 36, 924, 612, 96, 0);
        pn_frame_rect(p, 38, 926, 608, 92, 14);
        char l1[96], l2[96];
        if (s->outcome == PN_SC_VICTORY) {
            snprintf(l1, sizeof l1, "地牢通关！本局得分 +%d（剩余生命 %d/20）", (int)s->score, (int)s->hp);
        } else {
            snprintf(l1, sizeof l1, "冒险阵亡 · 得分 %d（未清怪物总伤 %d）", (int)s->score, -(int)s->score);
        }
        unsigned long long sec = s->elapsed_ms / 1000u;
        if (s->has_best) {
            snprintf(l2, sizeof l2, "历史最高 %+d · 种子 #%04X · 用时 %02llu:%02llu", (int)s->best_score,
                     (unsigned)(s->seed & 0xFFFFu), sec / 60u, sec % 60u);
        } else {
            snprintf(l2, sizeof l2, "种子 #%04X · 清 %u 房 · 用时 %02llu:%02llu", (unsigned)(s->seed & 0xFFFFu),
                     s->room_no, sec / 60u, sec % 60u);
        }
        DRAW(text_c(f, p, l1, 44, 962, 596, 22, 0, true));
        return text_c(f, p, l2, 44, 996, 596, 18, 5, false);
    }

    pn_frame_rect(p, 36, 924, 612, 96, 12);
    pn_frame_rect(p, 38, 926, 608, 92, 15);
    char l1[96];
    if (g->notice) {
        snprintf(l1, sizeof l1, "%s", g->notice);
    } else if (s->deck_count == 0u) {
        snprintf(l1, sizeof l1, "最终房：清空桌上剩余全部牌即可通关");
    } else {
        unsigned need = s->room_cleared < 3u ? (3u - s->room_cleared) : 0u;
        snprintf(l1, sizeof l1, "本房再选 %u 张牌进入下房（余 1 张带入下房）", need);
    }
    DRAW(text_c(f, p, l1, 44, 964, 596, 21, 0, g->notice != NULL));
    const char *sub = (s->mode == PN_SC_MODE_HARDCORE)
                          ? "硬核模式：武器须严格小于上次怪点数，禁用撤销"
                          : "点怪物可选武器斩杀/空手硬扛 · 本房内最多撤销 2 步";
    return text_c(f, p, sub, 44, 996, 596, 17, 6, false);
}

static pn_status_t draw_toolbar(const sc_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_sc_state_t *s = &g->state;
    pn_frame_rect(p, 32, 1036, 620, 144, 15);
    if (g->screen == PN_SC_COMPLETE) {
        DRAW(draw_button(f, p, "再来一局", 36, 1040, 196, 68, true, false));
        DRAW(draw_button(f, p, "重打同种子", 244, 1040, 196, 68, false, false));
        DRAW(draw_button(f, p, "选模式", 452, 1040, 196, 68, false, false));
    } else {
        bool flee_ok = (s->can_flee && !s->fled_last && s->room_cleared == 0u && s->deck_count > 0u);
        bool undo_ok = (s->selected_slot != PN_SC_NONE) || (s->mode == PN_SC_MODE_STANDARD && s->undo_count > 0u);
        DRAW(draw_button(f, p, "逃跑", 36, 1040, 144, 68, false, !flee_ok));
        DRAW(draw_button(f, p, s->selected_slot != PN_SC_NONE ? "取消" : "撤销", 192, 1040, 144, 68, false, !undo_ok));
        DRAW(draw_button(f, p, "重开", 348, 1040, 144, 68, false, false));
        DRAW(draw_button(f, p, "清屏", 504, 1040, 144, 68, false, false));
    }
    return text_c(f, p, "左键：逃跑  ·  中键：保存退出  ·  右键：撤销", 36, 1152, 612, 17, 7, false);
}

static pn_status_t render_select(const sc_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_sc_state_t *s = &g->state;
    DRAW(draw_header(g, f, p));
    DRAW(text_c(f, p, "44 张扑克单人地牢 · 20 生命 · 清空牌堆即胜", 36, 138, 612, 21, 6, false));

    bool std_sel = (g->chosen_mode == PN_SC_MODE_STANDARD);
    draw_cap(p, 36, 176, 296, 126, std_sel, false);
    DRAW(text_c(f, p, "标准规则 (<=)", 36, 224, 296, 26, std_sel ? 15 : 0, true));
    DRAW(text_c(f, p, "武器可斩 <= 上次怪点数", 36, 258, 296, 18, std_sel ? 12 : 5, false));
    DRAW(text_c(f, p, "本房内撤销 2 步 · 官方原版", 36, 284, 296, 17, std_sel ? 11 : 7, false));

    draw_cap(p, 352, 176, 296, 126, !std_sel, false);
    DRAW(text_c(f, p, "硬核规则 (<)", 352, 224, 296, 26, !std_sel ? 15 : 0, true));
    DRAW(text_c(f, p, "武器须严格 < 上次怪点数", 352, 258, 296, 18, !std_sel ? 12 : 5, false));
    DRAW(text_c(f, p, "禁用撤销 · 极限算牌挑战", 352, 284, 296, 17, !std_sel ? 11 : 7, false));

    /* 战绩与当前进度 / Stats and active run summary */
    pn_frame_rect(p, 36, 332, 612, 166, 0);
    pn_frame_rect(p, 38, 334, 608, 162, 14);
    DRAW(text_l(f, p, "冒险战绩", 60, 374, 560, 24, 0, true));
    char line[96];
    if (s->total_games > 0u) {
        unsigned pct = (unsigned)((s->total_wins * 100u) / s->total_games);
        if (s->has_best) {
            snprintf(line, sizeof line, "通关 %u / %u 局（%u%%） · 最高分 %+d", (unsigned)s->total_wins,
                     (unsigned)s->total_games, pct, (int)s->best_score);
        } else {
            snprintf(line, sizeof line, "通关 %u / %u 局（%u%%）", (unsigned)s->total_wins, (unsigned)s->total_games, pct);
        }
    } else {
        snprintf(line, sizeof line, "尚无完赛记录 · 准备好进入地牢吧");
    }
    DRAW(text_l(f, p, line, 60, 418, 560, 21, 3, false));
    if (g->has && s->outcome == PN_SC_ONGOING) {
        snprintf(line, sizeof line, "进行中：%s · 种子 #%04X · 生命 %d/20 · 牌堆余 %u",
                 s->mode == PN_SC_MODE_HARDCORE ? "硬核" : "标准", (unsigned)(s->seed & 0xFFFFu), (int)s->hp,
                 s->deck_count);
    } else if (g->has) {
        snprintf(line, sizeof line, "上局：%s · 种子 #%04X · %s（得分 %+d）",
                 s->mode == PN_SC_MODE_HARDCORE ? "硬核" : "标准", (unsigned)(s->seed & 0xFFFFu),
                 s->outcome == PN_SC_VICTORY ? "通关" : "阵亡", (int)s->score);
    } else {
        snprintf(line, sizeof line, "每局由随机种子洗牌，随时暂停自动保存");
    }
    DRAW(text_l(f, p, line, 60, 460, 560, 19, 5, false));

    /* 简明规则卡片 / Concise rule reference */
    pn_frame_rect(p, 36, 526, 612, 268, 10);
    pn_frame_rect(p, 38, 528, 608, 264, 15);
    DRAW(text_l(f, p, "简明规则", 60, 568, 560, 23, 0, true));
    DRAW(text_l(f, p, "· 黑桃/梅花 怪物(2-14)：用武器减伤，或空手全额硬扛", 60, 610, 564, 19, 2, false));
    DRAW(text_l(f, p, "· 方块 武器(2-10)：拿取必替换；斩怪后上限降至该怪点数", 60, 648, 564, 19, 2, false));
    DRAW(text_l(f, p, "· 红桃 血瓶(2-10)：回血上限 20；每间房仅第 1 瓶生效", 60, 686, 564, 19, 2, false));
    DRAW(text_l(f, p, "· 每房清 3 张余 1 张带入下房；逃跑将 4 牌沉底且不可连逃", 60, 724, 564, 19, 2, false));
    DRAW(text_l(f, p, "· 实体按键：左键逃跑 · 中键保存退出 · 右键本房撤销", 60, 764, 564, 18, 6, false));

    if (g->has && s->outcome == PN_SC_ONGOING) {
        DRAW(draw_button(f, p, "继续当前局", 36, 840, 296, 76, false, false));
        DRAW(draw_button(f, p, "开始新局", 352, 840, 296, 76, true, false));
        DRAW(draw_button(f, p, "重打当前种子", 36, 940, 612, 68, false, false));
    } else if (g->has && s->seed != 0u) {
        DRAW(draw_button(f, p, "开始新局", 164, 840, 356, 76, true, false));
        DRAW(draw_button(f, p, "重打上局种子", 164, 940, 356, 68, false, false));
    } else {
        DRAW(draw_button(f, p, "进入地牢", 164, 840, 356, 76, true, false));
    }
    return PN_OK;
}

static pn_status_t render_dialog(const sc_t *g, pn_font_t *f, pn_frame_t *p, const char *title, const char *body,
                                 const char *left_btn, const char *right_btn) {
    DRAW(draw_header(g, f, p));
    draw_cap(p, 56, 320, 572, 340, false, false);
    DRAW(text_c(f, p, title, 80, 396, 524, 32, 0, true));
    DRAW(text_c(f, p, body, 80, 470, 524, 24, PN_UI_MUTED, false));
    DRAW(draw_button(f, p, left_btn, 92, 548, 232, 72, false, false));
    DRAW(draw_button(f, p, right_btn, 360, 548, 232, 72, true, false));
    return PN_OK;
}

static pn_status_t draw_end_banner(const sc_t *g, pn_font_t *f, pn_frame_t *p) {
    const pn_sc_state_t *s = &g->state;
    pn_frame_rect(p, 108, 364, 468, 224, 0);
    pn_frame_rect(p, 112, 368, 460, 216, 15);
    pn_frame_rect(p, 120, 376, 444, 200, 14);
    if (s->outcome == PN_SC_VICTORY) {
        DRAW(text_c(f, p, "地 牢 通 关", 120, 444, 444, 38, 0, true));
        char sc[48];
        snprintf(sc, sizeof sc, "最终得分 +%d（剩余生命 %d/20）", (int)s->score, (int)s->hp);
        DRAW(text_c(f, p, sc, 120, 496, 444, 23, 2, true));
    } else {
        DRAW(text_c(f, p, "冒 险 阵 亡", 120, 444, 444, 38, 0, true));
        char sc[48];
        snprintf(sc, sizeof sc, "最终得分 %d（已斩 %u 怪）", (int)s->score, s->monsters_slain);
        DRAW(text_c(f, p, sc, 120, 496, 444, 23, 2, true));
    }
    char sub[64];
    snprintf(sub, sizeof sub, "清 %u 房 · 逃跑 %u 次 · 共 %u 步", s->room_no, s->flees_used, s->actions);
    return text_c(f, p, sub, 120, 540, 444, 19, 6, false);
}

pn_status_t pn_scoundrel_render(const pn_scoundrel_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p) return PN_INVALID;
    const sc_t *g = u->impl;
    pn_frame_clear(p, 15);
    if (g->screen == PN_SC_SELECT) return render_select(g, f, p);
    if (g->screen == PN_SC_CONFIRM)
        return render_dialog(g, f, p, "重新开始新地牢？", "将放弃当前进度并洗牌开始新局", "取消", "确认重开");
    if (g->screen == PN_SC_ERROR)
        return render_dialog(g, f, p, "存储提示", g->notice ? g->notice : "存取异常", "去选局", "重试");

    DRAW(draw_header(g, f, p));
    DRAW(draw_hud(g, f, p));
    for (unsigned i = 0; i < PN_SC_ROOM_SLOTS; i++) DRAW(draw_room_slot(g, f, p, i));
    if (g->screen == PN_SC_COMPLETE) DRAW(draw_end_banner(g, f, p));
    DRAW(draw_equip(g, f, p));
    DRAW(draw_prompt(g, f, p));
    return draw_toolbar(g, f, p);
}

pn_status_t pn_scoundrel_render_update(const pn_scoundrel_t *u, pn_font_t *f, pn_frame_t *p) {
    if (!u || !u->impl || !f || !p) return PN_INVALID;
    const sc_t *g = u->impl;
    if (g->screen != PN_SC_PLAY || g->full || !g->patch) return PN_UNSUPPORTED;

    const pn_sc_state_t *s = &g->state;
    bool weapon_changed = (g->shown_weapon != s->weapon || g->shown_slain_count != s->slain_count ||
                           (s->slain_count > 0u && memcmp(g->shown_slain, s->slain, s->slain_count) != 0));
    bool hp_changed = (g->shown_hp != s->hp);
    bool sel_changed = (g->shown_selected != s->selected_slot);
    bool potion_changed = (g->shown_potion_used != s->potion_used);
    bool hud_changed = (hp_changed || sel_changed || potion_changed || g->shown_deck_count != s->deck_count ||
                        g->shown_room_no != s->room_no || g->shown_room_cleared != s->room_cleared ||
                        g->shown_can_flee != s->can_flee);

    if (hud_changed) DRAW(draw_hud(g, f, p));

    for (unsigned i = 0; i < PN_SC_ROOM_SLOTS; i++) {
        bool slot_dirty = (g->shown_room[i] != s->room[i] || i == g->shown_selected || i == s->selected_slot);
        if (!slot_dirty && s->room[i] != PN_SC_CARD_NONE) {
            if (pn_sc_is_monster(s->room[i]) && (weapon_changed || hp_changed)) slot_dirty = true;
            else if (pn_sc_is_weapon(s->room[i]) && weapon_changed) slot_dirty = true;
            else if (pn_sc_is_potion(s->room[i]) && (potion_changed || hp_changed)) slot_dirty = true;
        }
        if (slot_dirty) DRAW(draw_room_slot(g, f, p, i));
    }

    if (weapon_changed || sel_changed) DRAW(draw_equip(g, f, p));
    DRAW(draw_prompt(g, f, p));

    bool bar_changed = (g->shown_can_flee != s->can_flee || g->shown_undo_count != s->undo_count || sel_changed ||
                        g->shown_deck_count != s->deck_count || g->shown_room_cleared != s->room_cleared);
    if (bar_changed) DRAW(draw_toolbar(g, f, p));
    return PN_OK;
}

int pn_scoundrel_hit(const pn_scoundrel_t *u, int x, int y) {
    if (!u || !u->impl) return 0;
    const sc_t *g = u->impl;
    const pn_sc_state_t *s = &g->state;
    if (!g->pending_start && inside(x, y, 36, 48, 148, 48)) return PN_SC_BACK;

    if (g->screen == PN_SC_ERROR) {
        if (inside(x, y, 92, 548, 232, 72)) return PN_SC_CHOOSE;
        if (inside(x, y, 360, 548, 232, 72)) return PN_SC_RETRY;
        return 0;
    }
    if (g->screen == PN_SC_CONFIRM) {
        if (inside(x, y, 92, 548, 232, 72)) return PN_SC_CANCEL;
        if (inside(x, y, 360, 548, 232, 72)) return PN_SC_ACCEPT;
        return 0;
    }
    if (g->screen == PN_SC_SELECT) {
        if (inside(x, y, 36, 176, 296, 126)) return PN_SC_MODE_BASE + PN_SC_MODE_STANDARD;
        if (inside(x, y, 352, 176, 296, 126)) return PN_SC_MODE_BASE + PN_SC_MODE_HARDCORE;
        if (g->has && s->outcome == PN_SC_ONGOING) {
            if (inside(x, y, 36, 840, 296, 76)) return PN_SC_RESUME;
            if (inside(x, y, 352, 840, 296, 76)) return PN_SC_START;
            if (inside(x, y, 36, 940, 612, 68)) return PN_SC_REPLAY_SEED;
        } else if (g->has && s->seed != 0u) {
            if (inside(x, y, 164, 840, 356, 76)) return PN_SC_START;
            if (inside(x, y, 164, 940, 356, 68)) return PN_SC_REPLAY_SEED;
        } else {
            if (inside(x, y, 164, 840, 356, 76)) return PN_SC_START;
        }
        return 0;
    }

    if (inside(x, y, 500, 48, 148, 48)) return PN_SC_CHOOSE;

    if (g->screen == PN_SC_COMPLETE) {
        if (inside(x, y, 36, 1040, 196, 68)) return PN_SC_START;
        if (inside(x, y, 244, 1040, 196, 68)) return PN_SC_REPLAY_SEED;
        if (inside(x, y, 452, 1040, 196, 68)) return PN_SC_CHOOSE;
        return 0;
    }

    /* PN_SC_PLAY 命中 / Play-screen hit test */
    for (unsigned i = 0; i < PN_SC_ROOM_SLOTS; i++) {
        int sx, sy;
        slot_rect(i, &sx, &sy);
        if (inside(x, y, sx, sy, SC_CARD_W, SC_CARD_H)) return PN_SC_SLOT_BASE + (int)i;
    }

    /* 同页怪物二选一操作带 / Inline monster combat choice buttons */
    if (s->selected_slot < PN_SC_ROOM_SLOTS && pn_sc_is_monster(s->room[s->selected_slot])) {
        bool can_w = pn_scoundrel_can_use_weapon(s, s->room[s->selected_slot]);
        if (inside(x, y, 36, 926, 296, 92)) return can_w ? PN_SC_FIGHT_WEAPON : PN_SC_DESELECT;
        if (inside(x, y, 352, 926, 296, 92)) return PN_SC_FIGHT_BARE;
    }

    /* 底部四键帽 / Bottom four tool buttons */
    if (inside(x, y, 36, 1040, 144, 68)) return PN_SC_FLEE;
    if (inside(x, y, 192, 1040, 144, 68)) return PN_SC_UNDO_MOVE;
    if (inside(x, y, 348, 1040, 144, 68)) return PN_SC_RESTART;
    if (inside(x, y, 504, 1040, 144, 68)) return PN_SC_CLEAN;

    /* 选怪状态下点击非按钮空白区域取消选中 / Tap blank area while monster selected to deselect */
    if (s->selected_slot != PN_SC_NONE) return PN_SC_DESELECT;
    return 0;
}
