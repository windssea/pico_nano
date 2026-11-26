/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：综合验证阅读核心交互与排版打磨：可视化状态栏、屏幕手势/触控区、三键与自动翻页、半屏实时阅读线微调、按句高亮摘录及词典/书签联动。
 * English: end-to-end verification of visual status bar, screen gestures/tap zones, 3-key & auto-turn, live reading-line half-sheet, and sentence highlight excerpts with dict & bookmarks.
 */
#define _POSIX_C_SOURCE 200809L
#include "test_remove_indexes.h"
#include "pn_auto_turn.h"
#include "pn_bookmark_ui.h"
#include "pn_dict_ui.h"
#include "pn_excerpts_ui.h"
#include "pn_footnote_ui.h"
#include "pn_highlights.h"
#include "pn_input_prefs.h"
#include "pn_menu_cache.h"
#include "pn_reader_chrome.h"
#include "pn_reader_input.h"
#include "pn_shake_turn.h"
#include "pn_settings_ui.h"
#include "pn_style_ui.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char *capture_name;
    uint8_t last_pixels[684u * 1216u / 2u];
    pn_frame_t last_frame;
    bool has_frame;
    unsigned calls;
} screen_t;

typedef struct {
    pn_menu_cache_t cache;
    unsigned hits, misses;
} set_cache_probe_t;

static uint8_t *probe_cache_begin(void *ctx, uint64_t key, uint64_t aux, bool *hit, bool *same_aux) {
    set_cache_probe_t *p = ctx;
    uint8_t *px = pn_menu_cache_begin(&p->cache, key, aux, hit, same_aux);
    if (hit && *hit) p->hits++;
    else p->misses++;
    return px;
}
static void probe_cache_commit(void *ctx, uint64_t key, uint64_t aux) {
    pn_menu_cache_commit(&((set_cache_probe_t *)ctx)->cache, key, aux);
}
static void probe_cache_abort(void *ctx) {
    pn_menu_cache_abort(&((set_cache_probe_t *)ctx)->cache);
}

static pn_status_t present(void *ctx, const pn_frame_t *frame, pn_refresh_t profile) {
    (void)profile;
    screen_t *s = ctx;
    assert(frame && frame->pixels && frame->width == 684 && frame->height == 1216);
    s->calls++;
    for (int y = 0; y < 1216; y++) {
        memcpy(s->last_pixels + (size_t)y * 342u, frame->pixels + (size_t)y * frame->stride, 342u);
    }
    pn_frame_bind(&s->last_frame, s->last_pixels, sizeof s->last_pixels, 684, 1216);
    s->has_frame = true;
    const char *dir = getenv("PN_UX_CAPTURE_DIR");
    if (dir && *dir && s->capture_name && *s->capture_name) {
        char path[512];
        int n = snprintf(path, sizeof path, "%s/%s.pgm", dir, s->capture_name);
        assert(n > 0 && (size_t)n < sizeof path);
        FILE *f = fopen(path, "wb");
        assert(f && fprintf(f, "P5\n684 1216\n255\n") > 0);
        for (int y = 0; y < 1216; y++) {
            uint8_t row[684];
            for (int x = 0; x < 684; x++) row[x] = (uint8_t)(pn_frame_get(frame, x, y) * 17);
            assert(fwrite(row, 1, sizeof row, f) == sizeof row);
        }
        assert(fclose(f) == 0);
        s->capture_name = NULL;
    }
    return PN_OK;
}

static void rm_tree(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char child[1024];
        int n = snprintf(child, sizeof child, "%s/%s", path, e->d_name);
        assert(n > 0 && (size_t)n < sizeof child);
        struct stat st;
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) rm_tree(child);
        else assert(unlink(child) == 0);
    }
    assert(closedir(dir) == 0 && rmdir(path) == 0);
}

int main(void) {
    char root[] = "/tmp/pn-reading-ux-XXXXXX";
    assert(mkdtemp(root));
    char state[384], book[384];
    snprintf(state, sizeof state, "%s/state", root);
    snprintf(book, sizeof book, "%s/book.txt", root);
    assert(mkdir(state, 0700) == 0);

    FILE *f = fopen(book, "wb");
    assert(f);
    for (unsigned i = 0; i < 120; i++) {
        assert(fputs("    夜色落进山谷，风吹过松林发出清脆的声响。人们停下脚步，抬头望向远方微亮的灯火！这段旅程才刚刚开始。\n", f) >= 0);
    }
    assert(fclose(f) == 0);

    pn_pool_t pool;
    assert(pn_pool_init(&pool, 4u * 1024u * 1024u, NULL, NULL, NULL) == 0);
    pn_media_t state_media;
    pn_media_init(&state_media);
    assert(pn_media_attach(&state_media, 1) == PN_OK);
    screen_t *scr = calloc(1, sizeof *scr);
    assert(scr);

    /* 1. 可视化状态栏设置、屏幕手势/触控区与三键/摘录/自动翻页设置。
     * 1. Visual Status Bar, Screen Gestures, Tap Zones, and 3-Key/Highlight/Auto-Turn settings. */
    pn_settings_ui_t set = {0};
    scr->capture_name = "ux_settings_main";
    assert(pn_settings_ui_open(&set, &pool, &state_media, state, present, scr) == PN_OK);

    // 从设置一级页「显示与字体 → 状态栏设置」直接进入，返回直接回到设置一级页。
    // Enter Status Bar directly from Settings root "Display & Fonts", and Back returns directly to Settings root.
    assert(pn_settings_ui_hit(&set, 200, 440) == PN_SETUI_STATUS_BAR);
    scr->capture_name = "ux_status_bar_default";
    assert(pn_settings_ui_event(&set, PN_SETUI_STATUS_BAR, present, scr) == PN_OK);
    assert(set.status_bar.enabled == 0 && set.status_bar.position == PN_SBAR_POS_DUAL);
    // 默认双栏下可直接配置上下两栏的格子（顶栏第2格=书名，底栏第3格=节号）。
    // Dual bar supports configuring both top and bottom slots (top slot 1 = book, bottom slot 2 = section).
    assert(pn_settings_ui_event(&set, PN_SETUI_SBAR_SLOT + 1, present, scr) == PN_OK);
    assert(pn_settings_ui_event(&set, PN_SETUI_SBAR_PICK + PN_SBAR_SLOT_BOOK, present, scr) == PN_OK);
    assert(pn_settings_ui_event(&set, PN_SETUI_SBAR_BSLOT + 2, present, scr) == PN_OK);
    assert(pn_settings_ui_event(&set, PN_SETUI_SBAR_PICK + PN_SBAR_SLOT_SECTION, present, scr) == PN_OK);
    assert(set.status_bar.enabled == 1 && set.status_bar.position == PN_SBAR_POS_DUAL);
    assert(set.status_bar.slots[1] == PN_SBAR_SLOT_BOOK && set.status_bar.bottom_slots[2] == PN_SBAR_SLOT_SECTION);
    {
        pn_status_bar_prefs_t dual_reloaded = {0};
        assert(pn_settings_load_status_bar(&state_media, state, &dual_reloaded) == PN_OK);
        assert(dual_reloaded.enabled == 1 && dual_reloaded.position == PN_SBAR_POS_DUAL);
        assert(dual_reloaded.slots[1] == PN_SBAR_SLOT_BOOK && dual_reloaded.bottom_slots[2] == PN_SBAR_SLOT_SECTION);
        pn_status_bar_set_active(&dual_reloaded);
        int dt = 0, db = 0;
        pn_status_bar_insets(false, &dt, &db);
        assert(dt > 40 && db > 40);
    }
    scr->capture_name = "ux_status_bar";
    assert(pn_settings_ui_event(&set, PN_SETUI_SBAR_POS + PN_SBAR_POS_TOP, present, scr) == PN_OK);
    assert(pn_settings_ui_event(&set, PN_SETUI_SBAR_PROG + PN_SBAR_PROG_BOOK, present, scr) == PN_OK);
    assert(pn_settings_ui_event(&set, PN_SETUI_SBAR_MARGIN_INC, present, scr) == PN_OK);
    assert(set.status_bar.enabled == 1 && set.status_bar.position == PN_SBAR_POS_TOP && set.status_bar.margin_px == 10);
    pn_status_bar_prefs_t sb_reloaded = {0};
    assert(pn_settings_load_status_bar(&state_media, state, &sb_reloaded) == PN_OK);
    assert(sb_reloaded.enabled == 1 && sb_reloaded.position == PN_SBAR_POS_TOP && sb_reloaded.margin_px == 10);
    pn_status_bar_set_active(&sb_reloaded);
    int inset_top = 0, inset_bot = 0;
    pn_status_bar_insets(false, &inset_top, &inset_bot);
    assert(inset_top > 40 && inset_bot == 0);
    // 顶部单栏激活返回条时：底部预留 56px 避免遮挡正文，且「< 返回跳转前位置」固定在底侧左下角命中。
    // Under top-only bar with active return bar: bottom reserves 56 px to prevent overlap, and return hits at bottom-left.
    pn_status_bar_set_return_bar(true);
    pn_status_bar_insets(false, &inset_top, &inset_bot);
    assert(inset_top > 40 && inset_bot == PN_RETURN_BAR_HEIGHT);
    assert(pn_reader_bar_hit(100, 1190, false, true) == 11);
    assert(pn_reader_bar_hit(100, 20, false, true) == 14);
    assert(pn_reader_bar_hit(500, 20, false, false) == 14);
    pn_status_bar_set_return_bar(false);
    assert(pn_settings_ui_event(&set, PN_SETUI_BACK, present, scr) == PN_OK && pn_settings_ui_screen(&set) == 0);

    // 进入 按键与手势 → 屏幕手势（含浮层动作选择器）、阅读触控区域、三键/摘录/自动翻页。
    // Enter Keys & Gestures -> Gestures (with modal), Tap Zones, 3-Key/Highlight/Auto-Turn.
    assert(pn_settings_ui_event(&set, PN_SETUI_KEYS, present, scr) == PN_OK);
    scr->capture_name = "ux_gestures";
    assert(pn_settings_ui_event(&set, PN_SETUI_GESTURES, present, scr) == PN_OK);
    scr->capture_name = "ux_gesture_modal";
    assert(pn_settings_ui_event(&set, PN_SETUI_GSLOT + PN_GSLOT_SWIPE_DOWN, present, scr) == PN_OK);
    assert(pn_settings_ui_event(&set, PN_SETUI_GACT_PICK + PN_GACT_TOC, present, scr) == PN_OK);
    assert(set.gestures.slots[PN_GSLOT_SWIPE_DOWN] == PN_GACT_TOC);
    assert(pn_settings_ui_event(&set, PN_SETUI_BACK, present, scr) == PN_OK);

    scr->capture_name = "ux_tap_zones";
    assert(pn_settings_ui_event(&set, PN_SETUI_TAP_ZONES, present, scr) == PN_OK);
    assert(pn_settings_ui_event(&set, PN_SETUI_TAP_ZONE_PICK + PN_TAP_ZONE_ONE_HAND_R, present, scr) == PN_OK);
    assert(set.gestures.tap_zone == PN_TAP_ZONE_ONE_HAND_R);
    assert(pn_settings_ui_event(&set, PN_SETUI_BACK, present, scr) == PN_OK);

    scr->capture_name = "ux_key_highlight";
    assert(pn_settings_ui_event(&set, PN_SETUI_KEY_HIGHLIGHT, present, scr) == PN_OK);
    assert(set.gestures.key2_short_action == 0); // 默认返回首页 / Default: Home
    assert(pn_settings_ui_event(&set, PN_SETUI_KEY_MAP + PN_KEY_MAP_SWAP_LR, present, scr) == PN_OK);
    assert(pn_settings_ui_event(&set, PN_SETUI_KEY2_SHORT + 4, present, scr) == PN_OK); // 切到自动翻页 / Switch to auto-turn
    assert(set.gestures.key2_short_action == 4);
    assert(pn_settings_ui_event(&set, PN_SETUI_KEY2_SHORT + 0, present, scr) == PN_OK); // 可设回默认返回首页 / Can restore default Home
    assert(set.gestures.key2_short_action == 0);
    assert(pn_settings_ui_event(&set, PN_SETUI_KEY2_SHORT + 3, present, scr) == PN_OK);
    assert(pn_settings_ui_event(&set, PN_SETUI_LP_MODE + PN_LP_HIGHLIGHT, present, scr) == PN_OK);
    assert(pn_settings_ui_event(&set, PN_SETUI_AUTO_TURN_INC, present, scr) == PN_OK);
    assert(set.gestures.auto_turn_sec == 20);
    pn_gesture_prefs_t g_reloaded = {0};
    assert(pn_settings_load_gestures(&state_media, state, &g_reloaded) == PN_OK);
    assert(g_reloaded.slots[PN_GSLOT_SWIPE_DOWN] == PN_GACT_TOC);
    assert(g_reloaded.tap_zone == PN_TAP_ZONE_ONE_HAND_R);
    assert(g_reloaded.key_map == PN_KEY_MAP_SWAP_LR);
    assert(g_reloaded.key2_short_action == 3);
    assert(g_reloaded.long_press_mode == PN_LP_HIGHLIGHT);
    assert(g_reloaded.auto_turn_sec == 20);
    pn_settings_ui_close(&set);

    /* 2. 手势引擎（自定义屏幕内下滑→目录、单手右手触控区、长按摘录）与自动翻页状态机。
     * 2. Gesture engine (center-down -> TOC, one-hand right tap zones, long-press highlight) & auto-turn timer. */
    pn_reader_input_t inp = {0};
    inp.config = (pn_reader_input_config_t){.use_gestures = true, .gestures = g_reloaded};
    pn_reader_action_t act = PN_APP_OPEN;
    assert(!pn_reader_input_feed_timed(&inp, 1, 340, 420, true, 100, &act));
    assert(!pn_reader_input_feed_timed(&inp, 1, 340, 620, true, 180, &act));
    (void)pn_reader_input_feed_timed(&inp, 0, 0, 0, true, 200, &act);
    assert(pn_reader_input_feed_timed(&inp, 0, 0, 0, true, 210, &act) && act == PN_APP_TOC);

    // 单手右手触控区：右下点击下一页，右上/左半点击上一页。/ One-hand right tap zone: bottom-right -> NEXT, top-right/left -> PREVIOUS.
    assert(!pn_reader_input_feed_timed(&inp, 1, 560, 820, true, 500, &act));
    (void)pn_reader_input_feed_timed(&inp, 0, 0, 0, true, 550, &act);
    assert(pn_reader_input_feed_timed(&inp, 0, 0, 0, true, 560, &act) && act == PN_APP_NEXT);
    assert(!pn_reader_input_feed_timed(&inp, 1, 560, 320, true, 620, &act));
    (void)pn_reader_input_feed_timed(&inp, 0, 0, 0, true, 650, &act);
    assert(pn_reader_input_feed_timed(&inp, 0, 0, 0, true, 660, &act) && act == PN_APP_PREVIOUS);
    assert(!pn_reader_input_feed_timed(&inp, 1, 80, 820, true, 700, &act));
    (void)pn_reader_input_feed_timed(&inp, 0, 0, 0, true, 750, &act);
    assert(pn_reader_input_feed_timed(&inp, 0, 0, 0, true, 760, &act) && act == PN_APP_PREVIOUS);

    // 长按摘录模式（PN_LP_HIGHLIGHT）。/ Long-press highlight mode.
    assert(!pn_reader_input_feed_timed(&inp, 1, 320, 400, true, 1000, &act));
    assert(pn_reader_input_feed_timed(&inp, 1, 322, 401, true, 1600, &act) && act == PN_APP_HIGHLIGHT);

    // 自动翻页状态机。/ Auto page-turn timer state machine.
    pn_auto_turn_t at = {0};
    pn_auto_turn_start(&at, 15, 1000);
    assert(at.active && !pn_auto_turn_due(&at, 15999) && pn_auto_turn_due(&at, 16000));
    pn_auto_turn_receipt(&at, 16000, true);
    assert(!pn_auto_turn_due(&at, 30999) && pn_auto_turn_due(&at, 31000));
    pn_auto_turn_stop(&at);
    assert(!at.active && !pn_auto_turn_due(&at, 99999));

    /* 3. 半屏阅读线实时微调 + 自定义虚线长度/间隔持久化（schema 15）+ 实线支持。
     * 3. Live half-screen reading-line micro-editor + custom dash/gap persistence (schema 15) + solid style. */
    pn_reader_app_t app = {0};
    assert(pn_reader_app_open(&app, &pool, book, NULL, state, 44, 0) == PN_OK);
    assert(pn_reader_app_step(&app, PN_APP_OPEN, 10, present, scr) == PN_OK);

    pn_style_ui_t sui = {0};
    assert(pn_style_ui_open_guide(&sui, &app, 20, present, scr) == PN_OK && sui.active && sui.guide_sheet);
    assert(pn_style_ui_event(&sui, PN_SUI_GUIDE_STYLE + 3, 21, present, scr) == PN_OK && sui.draft.reading_line == 3);
    assert(pn_style_ui_event(&sui, PN_SUI_GUIDE_STYLE + 1, 22, present, scr) == PN_OK && sui.draft.reading_line == 1);
    assert(pn_style_ui_event(&sui, PN_SUI_GUIDE_ADJ + 1, 23, present, scr) == PN_OK && sui.draft.reading_line_thickness_extra == 1);
    assert(pn_style_ui_event(&sui, PN_SUI_GUIDE_ADJ + 5, 24, present, scr) == PN_OK && sui.draft.reading_line_dash == 14);
    assert(pn_style_ui_event(&sui, PN_SUI_GUIDE_ADJ + 7, 25, present, scr) == PN_OK && sui.draft.reading_line_gap == 12);
    scr->capture_name = "ux_guide_sheet";
    assert(pn_style_ui_event(&sui, PN_SUI_GUIDE_ADJ + 8, 26, present, scr) == PN_OK && sui.draft.reading_line_offset == -2);
    assert(pn_style_ui_event(&sui, PN_SUI_APPLY, 27, present, scr) == PN_OK && !sui.active);

    // 关闭重开验证 schema 15 持久化。/ Close & reopen to verify schema 15 persistence.
    assert(pn_reader_app_close(&app, 30) == PN_OK);
    assert(pn_reader_app_open(&app, &pool, book, NULL, state, 44, 31) == PN_OK);
    pn_style_t reloaded_style = {0};
    assert(pn_reader_app_style_get(&app, &reloaded_style) == PN_OK);
    assert(reloaded_style.reading_line == 1 && reloaded_style.reading_line_dash == 14 && reloaded_style.reading_line_gap == 12 && reloaded_style.reading_line_offset == -2);
    assert(pn_reader_app_step(&app, PN_APP_OPEN, 32, present, scr) == PN_OK);

    /* 4. 按句高亮摘录 + 词典弹窗「摘录」联动 + 书签/高亮摘录双页签。
     * 4. Sentence highlight excerpts + dictionary "摘录" toggle + bookmark/excerpts tabs. */
    bool is_hl = false;
    assert(pn_reader_app_highlight_at(&app, 260, 180, &is_hl) == PN_OK && !is_hl);
    bool added = false;
    char excerpt[128] = {0};
    scr->capture_name = "ux_highlighted_page";
    assert(pn_reader_app_highlight_toggle(&app, 260, 180, 40, present, scr, &added, excerpt, sizeof excerpt) == PN_OK);
    assert(added && excerpt[0] != '\0');
    assert(pn_reader_app_highlight_at(&app, 260, 180, &is_hl) == PN_OK && is_hl);

    // 打开词典浮层并验证「已摘录」按钮可切换高亮状态。/ Open dict card and toggle sentence highlight via pill.
    char cands[2][PN_DICT_WORD_MAX] = {"夜色", "山谷"};
    pn_dict_ui_t dui = {0};
    pn_dict_ui_options_t dopt = {.anchor_x = 260, .anchor_y = 180, .page_frame = &scr->last_frame, .pref_media = &state_media, .pref_dir = state};
    scr->capture_name = "ux_dict_highlight";
    assert(pn_dict_ui_open_ex(&dui, &pool, &app, NULL, NULL, (const char (*)[PN_DICT_WORD_MAX])cands, 2, false, &dopt, present, scr) == PN_OK);
    assert(pn_dict_ui_event(&dui, PN_DUI_TOGGLE_HIGHLIGHT, 45, present, scr) == PN_OK);
    assert(pn_reader_app_highlight_at(&app, 260, 180, &is_hl) == PN_OK && !is_hl);
    assert(pn_dict_ui_event(&dui, PN_DUI_TOGGLE_HIGHLIGHT, 46, present, scr) == PN_OK);
    assert(pn_reader_app_highlight_at(&app, 260, 180, &is_hl) == PN_OK && is_hl);
    assert(pn_dict_ui_event(&dui, PN_DUI_BACK, 47, present, scr) == PN_OK && dui.mode == PN_DUI_CLOSED);

    // 书签页切换「高亮摘录」页签并验证列表、跳转与删除。/ Bookmarks UI: switch to Highlights tab, jump & delete.
    pn_bookmark_ui_t bui = {0};
    assert(pn_bookmark_ui_open(&bui, &app, present, scr) == PN_OK);
    scr->capture_name = "ux_bookmark_highlights";
    assert(pn_bookmark_ui_event(&bui, PN_BUI_TAB_HIGHLIGHTS, NULL, 50, present, scr) == PN_OK);
    assert(bui.tab == 1 && bui.count == 1 && bui.highlights[0].text[0] != '\0');
    // 5. 全局「我的摘录」跨书查询与管理（列表、关键词搜索、详情跳转、删除确认）+ 统一居底脚注浮层。
    // 5. Global cross-book Excerpts browser (list, search filter, detail jump, delete) + unified bottom footnote card.
    {
        pn_excerpts_ui_t exui = {0};
        pn_recent_snapshot_t rec = {0};
        rec.count = 1;
        rec.items[0].book = bui.highlights[0].book;
        snprintf(rec.items[0].path, sizeof rec.items[0].path, "%s", book);
        assert(pn_excerpts_ui_open(&exui, &pool, &state_media, state, &rec) == PN_OK);
        assert(exui.mode == PN_EXUI_LIST && pn_excerpts_ui_total_count(&exui) == 1u && pn_excerpts_ui_filtered_count(&exui) == 1u);

        pn_text_source_t ex_src = pn_font_builtin_source();
        pn_font_t ex_font = {0};
        assert(pn_font_open(&ex_font, &pool, &ex_src, 24) == PN_OK);
        uint8_t *ex_px = pn_alloc_tagged(&pool, PN_MEM_FRAME, 342u * 1216u);
        assert(ex_px);
        pn_frame_t ex_frame;
        assert(pn_frame_bind(&ex_frame, ex_px, 342u * 1216u, 684, 1216));
        assert(pn_excerpts_ui_render(&exui, &ex_font, &ex_frame) == PN_OK);
        scr->capture_name = "ux_excerpts_list";
        assert(present(scr, &ex_frame, PN_REFRESH_GL16) == PN_OK);

        // 搜索筛选（匹配书名 "book" 与不匹配关键词 "zzz"）。/ Search filter (matching title & non-matching query).
        assert(pn_excerpts_ui_event(&exui, PN_EXUI_OPEN_SEARCH) == PN_OK && exui.mode == PN_EXUI_SEARCH);
        assert(pn_excerpts_ui_event(&exui, PN_EXUI_KB_ENCODE('z')) == PN_OK);
        assert(pn_excerpts_ui_event(&exui, PN_EXUI_SEARCH_GO) == PN_OK && exui.mode == PN_EXUI_LIST && pn_excerpts_ui_filtered_count(&exui) == 0u);
        assert(pn_excerpts_ui_event(&exui, PN_EXUI_CLEAR_FILTER) == PN_OK && pn_excerpts_ui_filtered_count(&exui) == 1u);

        // 进入详情并验证「设为左上角文案」与跳书请求，以及在设置页「左上角文案 → 从我的摘录中选择」可选用。
        // Open detail, verify "set as top-left motto" and jump request, plus selecting from Settings -> Home Motto -> Excerpts.
        assert(pn_excerpts_ui_event(&exui, PN_EXUI_ROW_BASE + 0) == PN_OK && exui.mode == PN_EXUI_DETAIL);
        assert(pn_excerpts_ui_event(&exui, PN_EXUI_SET_MOTTO) == PN_OK && exui.motto_changed);
        {
            pn_home_prefs_t hp = pn_home_prefs_default();
            assert(pn_home_prefs_load_dir(&state_media, state, &hp) == PN_OK && hp.header[0] != '\0');
            assert(strstr(hp.header, "夜色") != NULL);
        }
        assert(pn_excerpts_ui_event(&exui, PN_EXUI_JUMP) == PN_OK && exui.open_request && strcmp(exui.open_path, book) == 0 && exui.open_highlight_id == bui.highlights[0].id);
        pn_excerpts_ui_close(&exui);

        // 验证设置页「书架与主页 → 左上角文案 → 从我的摘录中选择」可列出并应用摘录。
        // Verify Settings -> Home Motto -> Pick from Excerpts lists and applies saved excerpts.
        {
            pn_settings_ui_t mset = {0};
            assert(pn_settings_ui_open(&mset, &pool, &state_media, state, present, scr) == PN_OK);
            assert(pn_settings_ui_event(&mset, PN_SETUI_APPEARANCE, present, scr) == PN_OK);
            assert(pn_settings_ui_event(&mset, PN_SETUI_HOME_MOTTO, present, scr) == PN_OK);
            assert(pn_settings_ui_hit(&mset, 200, 960) == PN_SETUI_MOTTO_EXCERPTS);
            assert(pn_settings_ui_event(&mset, PN_SETUI_MOTTO_RESET, present, scr) == PN_OK && !mset.home.header[0]);
            scr->capture_name = "ux_motto_excerpts";
            assert(pn_settings_ui_event(&mset, PN_SETUI_MOTTO_EXCERPTS, present, scr) == PN_OK);
            assert(pn_settings_ui_hit(&mset, 200, 240) == PN_SETUI_MOTTO_EXCERPT_ITEM);
            assert(pn_settings_ui_event(&mset, PN_SETUI_MOTTO_EXCERPT_ITEM, present, scr) == PN_OK);
            assert(strstr(mset.home.header, "夜色") != NULL);
            pn_settings_ui_close(&mset);
            pn_w_set_brand("");
        }

        pn_epub_footnote_t fn_meta = {.marker_y = 980, .can_jump = true};
        snprintf(fn_meta.label, sizeof fn_meta.label, "[16]");
        const char *fn_txt = "参考《新唐书》卷二百二十一上·列传第一百四十六上·西域上。无论角标位于页面顶部还是底部，注释浮层统一与离线词典一致固定在屏幕下方弹出。";
        pn_footnote_ui_t fn = {0};
        assert(pn_footnote_ui_open(&fn, &pool, &fn_meta, fn_txt, strlen(fn_txt)) == PN_OK);
        assert(pn_footnote_ui_sheet_y(&fn) == PN_FOOTNOTE_BOTTOM_Y && PN_FOOTNOTE_BOTTOM_Y == PN_DICT_BOTTOM_Y);
        memcpy(ex_px, scr->last_pixels, 342u * 1216u);
        assert(pn_footnote_ui_render(&fn, &ex_font, &ex_frame) == PN_OK);
        scr->capture_name = "ux_footnote_bottom";
        assert(present(scr, &ex_frame, PN_REFRESH_GL16) == PN_OK);
        pn_footnote_ui_close(&fn);
        pn_free(ex_px);
        pn_font_close(&ex_font);
    }

    assert(pn_bookmark_ui_event(&bui, PN_BUI_ROW, NULL, 51, present, scr) == PN_OK && bui.mode == PN_BUI_ACTIONS);
    assert(pn_bookmark_ui_event(&bui, PN_BUI_REMOVE, NULL, 52, present, scr) == PN_OK && bui.mode == PN_BUI_DELETE);
    assert(pn_bookmark_ui_event(&bui, PN_BUI_CONFIRM, NULL, 53, present, scr) == PN_OK && bui.mode == PN_BUI_LIST && bui.count == 0);
    assert(pn_bookmark_ui_event(&bui, PN_BUI_BACK, NULL, 54, present, scr) == PN_OK && bui.mode == PN_BUI_CLOSED);

    /* 6. 无损摘录跳转（不覆盖原阅读进度、支持首帧直达与返回原位置）+ PNHL v2 run 字段往返 + 设置页快速打开与根页整帧缓存。
     * 6. Lossless excerpt jump (preserves reading progress, direct first-frame jump & return) + PNHL v2 run round-trip + fast settings open & root frame cache. */
    {
        // 6a. 在第 1 页重新摘录一句，翻到后方几页确认原进度，再跳摘录并翻页/关闭重开，确认原进度不被覆盖。
        // 6a. Add a highlight on page 1, advance several pages, jump to the highlight, turn context pages, close & reopen, verifying original progress is intact.
        bool hl_added = false;
        assert(pn_reader_app_highlight_toggle(&app, 260, 180, 55, present, scr, &hl_added, excerpt, sizeof excerpt) == PN_OK && hl_added);
        pn_highlight_t list[4] = {0};
        size_t hl_count = 0;
        bool hl_more = false;
        assert(pn_reader_app_highlight_list(&app, 0, list, 4, &hl_count, &hl_more) == PN_OK && hl_count == 1u);
        uint64_t hl_id = list[0].id;
        assert(pn_reader_app_step(&app, PN_APP_NEXT, 56, present, scr) == PN_OK);
        assert(pn_reader_app_step(&app, PN_APP_NEXT, 57, present, scr) == PN_OK);
        assert(pn_reader_app_step(&app, PN_APP_NEXT, 58, present, scr) == PN_OK);
        pn_txt_progress_t orig_prog = {0};
        assert(pn_reader_app_progress(&app, &orig_prog) == PN_OK && orig_prog.source_offset > 0);
        // 强制落盘原进度（关闭重开模拟真实会话）。/ Flush original progress cleanly.
        assert(pn_reader_app_close(&app, 59) == PN_OK);
        assert(pn_reader_app_open(&app, &pool, book, NULL, state, 44, 60) == PN_OK);
        // 6b. 模拟跨书直达：首帧不先 OPEN 原页，直接 highlight_jump！/ Simulate cross-book direct jump before first PN_APP_OPEN.
        assert(pn_reader_app_highlight_jump(&app, hl_id, 61, present, scr) == PN_OK);
        pn_txt_progress_t jumped_prog = {0};
        assert(pn_reader_app_progress(&app, &jumped_prog) == PN_OK && jumped_prog.source_offset <= list[0].start && jumped_prog.source_offset < orig_prog.source_offset);
        // 摘录预览态允许前后翻看上下文，不覆盖进度。/ Context turns in preview never overwrite persisted progress.
        assert(pn_reader_app_step(&app, PN_APP_NEXT, 62, present, scr) == PN_OK);
        // 未点返回直接关闭重开：必须恢复到 orig_prog！/ Close & reopen without tapping return: must still restore orig_prog!
        assert(pn_reader_app_close(&app, 63) == PN_OK);
        assert(pn_reader_app_open(&app, &pool, book, NULL, state, 44, 64) == PN_OK);
        assert(pn_reader_app_step(&app, PN_APP_OPEN, 65, present, scr) == PN_OK);
        pn_txt_progress_t after_reopen = {0};
        assert(pn_reader_app_progress(&app, &after_reopen) == PN_OK && after_reopen.source_offset == orig_prog.source_offset);
        // 书内摘录跳转后翻页上下文，再点「返回跳转前位置」应精准回到 orig_prog 且清空返回点与底部留白。
        // In-book jump + context turn + return goes back to orig_prog and restores bottom inset cleanly.
        assert(!pn_status_bar_return_bar());
        assert(pn_reader_app_highlight_jump(&app, hl_id, 66, present, scr) == PN_OK);
        assert(pn_status_bar_return_bar());
        {
            int rt = 0, rb = 0;
            pn_status_bar_insets(false, &rt, &rb);
            assert(rb == PN_RETURN_BAR_HEIGHT);
        }
        assert(pn_reader_app_step(&app, PN_APP_NEXT, 67, present, scr) == PN_OK);
        assert(pn_status_bar_return_bar());
        assert(pn_reader_app_bookmark_return(&app, 68, present, scr) == PN_OK);
        assert(!pn_status_bar_return_bar());
        assert(pn_reader_app_bookmark_return(&app, 69, present, scr) == PN_EMPTY);
        pn_txt_progress_t returned = {0};
        assert(pn_reader_app_progress(&app, &returned) == PN_OK && returned.source_offset == orig_prog.source_offset);
    }

    assert(pn_reader_app_close(&app, 70) == PN_OK);

    // 6c. PNHL v2 run 字段往返持久化验证。/ PNHL v2 run field persistence round-trip.
    {
        pn_book_id_t fake_book = {0};
        memset(fake_book.sha256, 0x5a, 32);
        pn_highlight_t rec_in = {
            .book = fake_book,
            .is_epub = true,
            .start = pn_highlight_epub_key(12, 17),
            .end = pn_highlight_epub_key(12, 44),
            .start_run = 3,
            .end_run = 5,
            .text_hash = pn_highlight_text_hash("这是带行内样式节点的 EPUB 摘录句子。"),
        };
        snprintf(rec_in.path, sizeof rec_in.path, "OEBPS/ch02.xhtml");
        snprintf(rec_in.text, sizeof rec_in.text, "这是带行内样式节点的 EPUB 摘录句子。");
        pn_media_lease_t wl = {0};
        assert(pn_media_acquire(&state_media, PN_MEDIA_WRITE, &wl) == PN_OK);
        pn_highlights_t hstore = {0};
        assert(pn_highlights_init(&hstore, &state_media, &wl, state, &fake_book) == PN_OK);
        bool added_v2 = false;
        uint64_t id_v2 = 0;
        assert(pn_highlights_toggle(&hstore, &rec_in, &added_v2, &id_v2) == PN_OK && added_v2 && id_v2 > 0);
        pn_highlight_t rec_out = {0};
        assert(pn_highlights_get(&hstore, id_v2, &rec_out) == PN_OK);
        assert(pn_media_release(&state_media, &wl) == PN_OK);
        assert(rec_out.is_epub && pn_highlight_epub_element(rec_out.start) == 12u && pn_highlight_epub_offset(rec_out.start) == 17u);
        assert(pn_highlight_epub_element(rec_out.end) == 12u && pn_highlight_epub_offset(rec_out.end) == 44u);
        assert(rec_out.start_run == 3u && rec_out.end_run == 5u);
        assert(strcmp(rec_out.path, "OEBPS/ch02.xhtml") == 0);
        assert(strcmp(rec_out.text, rec_in.text) == 0);
    }

    // 6d. pn_settings_ui_open_fast 零磁盘重载 + 根页整帧缓存命中验证。/ Zero-reload settings open & root frame cache hit check.
    {
        set_cache_probe_t probe = {0};
        assert(pn_menu_cache_init(&probe.cache, &pool) == PN_OK);
        pn_settings_frame_cache_t fc = {.ctx = &probe, .begin = probe_cache_begin, .commit = probe_cache_commit, .abort = probe_cache_abort};
        pn_settings_snapshot_t snap = {
            .has_flags = true, .flags = PN_INPUT_FLAGS_DEFAULT,
            .has_shelf = true, .shelf = pn_shelf_prefs_default(),
            .has_lan = true, .lan = "TestWiFi",
            .has_index = true, .index_wait = false,
            .has_vertical = true, .vertical_turn = false,
            .has_auto_lock = true, .auto_lock_minutes = 5,
            .has_status_bar = true, .status_bar = pn_status_bar_prefs_default(),
            .has_gestures = true, .gestures = pn_gesture_prefs_default(),
            .has_home = true, .home = pn_home_prefs_default(),
            .has_global_style = true, .global_style = pn_style_default(PN_STYLE_DEFAULT_PIXELS),
        };
        pn_settings_ui_t fset = {0};
        assert(pn_settings_ui_open_fast(&fset, &pool, &state_media, state, NULL, &snap, &fc, present, scr) == PN_OK);
        assert(probe.misses == 1u && probe.hits == 0u);
        assert(pn_settings_ui_event(&fset, PN_SETUI_REFRESH, present, scr) == PN_OK);
        assert(pn_settings_ui_event(&fset, PN_SETUI_BACK, present, scr) == PN_OK);
        assert(probe.hits == 1u);
        pn_settings_ui_close(&fset);

        assert(pn_settings_ui_open_fast(&fset, &pool, &state_media, state, NULL, &snap, &fc, present, scr) == PN_OK);
        assert(probe.hits == 2u);
        // 6e. 全局边距详调 ×1 / ×5 / ×10 步进倍率与边界夹紧验证。/ Global margin detail x1 / x5 / x10 multiplier & clamping.
        assert(pn_settings_ui_event(&fset, PN_SETUI_GLOBAL_STYLE, present, scr) == PN_OK);
        assert(pn_settings_ui_event(&fset, PN_SETUI_GSTYLE_MARGINS, present, scr) == PN_OK);
        assert(pn_settings_ui_hit(&fset, 470, 1020) == PN_SETUI_GSTYLE_MSTEP + 1);
        assert(pn_settings_ui_hit(&fset, 590, 1020) == PN_SETUI_GSTYLE_MSTEP + 2);
        assert(pn_settings_ui_event(&fset, PN_SETUI_GSTYLE_MSTEP + 1, present, scr) == PN_OK); // ×5 = 20px
        assert(pn_settings_ui_event(&fset, PN_SETUI_GSTYLE_MADJ + 1, present, scr) == PN_OK); // top: 0 -> 20
        pn_style_t gs = {0};
        assert(pn_settings_ui_global_style(&fset, &gs) && pn_style_margin_top(&gs) == 20);
        assert(pn_settings_ui_event(&fset, PN_SETUI_GSTYLE_MSTEP + 2, present, scr) == PN_OK); // ×10 = 40px
        assert(pn_settings_ui_event(&fset, PN_SETUI_GSTYLE_MADJ + 1, present, scr) == PN_OK); // top: 20 -> 60
        assert(pn_settings_ui_global_style(&fset, &gs) && pn_style_margin_top(&gs) == 60);
        assert(pn_settings_ui_event(&fset, PN_SETUI_GSTYLE_MADJ + 4, present, scr) == PN_OK); // left: 32 - 40 -> clamp 0
        assert(pn_settings_ui_global_style(&fset, &gs) && pn_style_margin_left(&gs) == 0);
        assert(pn_settings_ui_event(&fset, PN_SETUI_GSTYLE_MSTEP + 2, present, scr) == PN_OK); // 再次点 ×10 切回 ×1
        assert(pn_settings_ui_event(&fset, PN_SETUI_GSTYLE_MADJ + 1, present, scr) == PN_OK); // top: 60 -> 64
        assert(pn_settings_ui_global_style(&fset, &gs) && pn_style_margin_top(&gs) == 64);
        assert(pn_settings_ui_event(&fset, PN_SETUI_GSTYLE_MARGINS_RESET, present, scr) == PN_OK);
        pn_settings_ui_close(&fset);
        pn_menu_cache_close(&probe.cache);
    }

    // 6f. 书内边距详调 ×1 / ×5 / ×10 步进倍率与边界夹紧验证。/ In-book margin detail x1 / x5 / x10 multiplier & clamping.
    {
        pn_reader_app_t mapp = {0};
        assert(pn_reader_app_open(&mapp, &pool, book, NULL, state, 44, 80) == PN_OK);
        assert(pn_reader_app_step(&mapp, PN_APP_OPEN, 81, present, scr) == PN_OK);
        pn_style_ui_t msui = {0};
        assert(pn_style_ui_open(&msui, &mapp, present, scr) == PN_OK);
        assert(pn_style_ui_event(&msui, PN_SUI_MARGINS, 82, present, scr) == PN_OK && msui.margins);
        assert(pn_style_ui_hit(&msui, 470, 1030) == PN_SUI_MARGIN_STEP + 1);
        assert(pn_style_ui_hit(&msui, 590, 1030) == PN_SUI_MARGIN_STEP + 2);
        assert(pn_style_ui_event(&msui, PN_SUI_MARGIN_STEP + 1, 83, present, scr) == PN_OK); // ×5 = 20px
        assert(pn_style_ui_event(&msui, PN_SUI_MARGIN_ADJ + 1, 84, present, scr) == PN_OK); // top: 0 -> 20
        assert(pn_style_margin_top(&msui.draft) == 20);
        assert(pn_style_ui_event(&msui, PN_SUI_MARGIN_STEP + 2, 85, present, scr) == PN_OK); // ×10 = 40px
        assert(pn_style_ui_event(&msui, PN_SUI_MARGIN_ADJ + 1, 86, present, scr) == PN_OK); // top: 20 -> 60
        assert(pn_style_margin_top(&msui.draft) == 60);
        assert(pn_style_ui_event(&msui, PN_SUI_MARGIN_ADJ + 4, 87, present, scr) == PN_OK); // left: 32 - 40 -> clamp 0
        assert(pn_style_margin_left(&msui.draft) == 0);
        assert(pn_style_ui_event(&msui, PN_SUI_MARGIN_STEP + 2, 88, present, scr) == PN_OK); // 再次点 ×10 切回 ×1
        assert(pn_style_ui_event(&msui, PN_SUI_MARGIN_ADJ + 1, 89, present, scr) == PN_OK); // top: 60 -> 64
        assert(pn_style_margin_top(&msui.draft) == 64);
        pn_style_ui_close(&msui);
        assert(pn_reader_app_close(&mapp, 90) == PN_OK);
    }

    // 验证多行控件文本显式 '\n' 正常折行且不画缺字方框。/ Verify explicit '\n' breaks lines cleanly in pn_w_text_lines*.
    pn_text_source_t nl_src = pn_font_builtin_source();
    pn_font_t nl_font = {0};
    assert(pn_font_open(&nl_font, &pool, &nl_src, 24) == PN_OK);
    unsigned nl_used = 0;
    assert(pn_w_text_lines_measure_ex(&nl_font, "第一行\n第二行", 560, 5, false, &nl_used) == PN_OK);
    assert(nl_used == 2u);
    assert(pn_w_text_lines_measure_ex(&nl_font, "第一行\r\n\n第三行", 560, 5, false, &nl_used) == PN_OK);
    assert(nl_used == 3u);
    // 7. 批 1/2/3 回归：触控区/滑动直接生效、背景高亮纹理保留、双选浮条 [摘录|查词]、PNGS v1->v2 迁移与体感翻页算法。
    // 7. Batch 1/2/3 regression: direct tap-zone & swipe authority, texture-preserving bg highlight, long-press dual pill, PNGS v1->v2 migration & shake detector.
    {
        // 7a. use_gestures=true 时，新 7 种触控区和 3 种滑动模式直接生效，不受旧 vertical_turn/no_edge_tap/left_hand 干扰。
        pn_reader_input_t gi = {0};
        pn_gesture_prefs_t gp = pn_gesture_prefs_default();
        gp.tap_zone = PN_TAP_ZONE_TB;
        gp.swipe_turn = PN_SWIPE_TURN_LR;
        gi.config = (pn_reader_input_config_t){.left_hand = true, .no_edge_tap = true, .no_swipe = true, .vertical_turn = true, .use_gestures = true, .gestures = gp};
        pn_reader_action_t ga = PN_APP_OPEN;
        // 上下两分点按：上=上页、下=下页（即使旧 no_edge_tap=true / left_hand=true 仍生效）。
        assert(!pn_reader_input_feed_timed(&gi, 1, 340, 260, true, 100, &ga));
        (void)pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 130, &ga);
        assert(pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 140, &ga) && ga == PN_APP_PREVIOUS);
        assert(!pn_reader_input_feed_timed(&gi, 1, 340, 900, true, 200, &ga));
        (void)pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 230, &ga);
        assert(pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 240, &ga) && ga == PN_APP_NEXT);
        // 左右滑动翻页（即使旧 no_swipe=true / vertical_turn=true 仍生效）。
        assert(!pn_reader_input_feed_timed(&gi, 1, 500, 500, true, 300, &ga));
        assert(!pn_reader_input_feed_timed(&gi, 1, 320, 500, true, 360, &ga));
        (void)pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 380, &ga);
        assert(pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 390, &ga) && ga == PN_APP_NEXT);
        // 单手左手触控区：左下=下页，左上/右1/3=上页。
        gi.config.gestures.tap_zone = PN_TAP_ZONE_ONE_HAND_L;
        assert(!pn_reader_input_feed_timed(&gi, 1, 140, 860, true, 500, &ga));
        (void)pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 530, &ga);
        assert(pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 540, &ga) && ga == PN_APP_NEXT);
        assert(!pn_reader_input_feed_timed(&gi, 1, 140, 260, true, 600, &ga));
        (void)pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 630, &ga);
        assert(pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 640, &ga) && ga == PN_APP_PREVIOUS);
        assert(!pn_reader_input_feed_timed(&gi, 1, 580, 860, true, 700, &ga));
        (void)pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 730, &ga);
        assert(pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 740, &ga) && ga == PN_APP_PREVIOUS);
        // 关闭点按 + 关闭滑动。
        gi.config.gestures.tap_zone = PN_TAP_ZONE_OFF;
        gi.config.gestures.swipe_turn = PN_SWIPE_TURN_OFF;
        assert(!pn_reader_input_feed_timed(&gi, 1, 580, 860, true, 800, &ga));
        (void)pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 830, &ga);
        assert(!pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 840, &ga) && gi.last_tap);
        assert(!pn_reader_input_feed_timed(&gi, 1, 500, 500, true, 900, &ga));
        assert(!pn_reader_input_feed_timed(&gi, 1, 300, 500, true, 960, &ga));
        (void)pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 980, &ga);
        assert(!pn_reader_input_feed_timed(&gi, 0, 0, 0, true, 990, &ga));
        // 默认长按双选浮条（PN_LP_MENU -> PN_APP_LONG_PRESS_MENU）。
        assert(gp.long_press_mode == PN_LP_MENU);
        assert(!pn_reader_input_feed_timed(&gi, 1, 320, 420, true, 1200, &ga));
        assert(pn_reader_input_feed_timed(&gi, 1, 321, 420, true, 1800, &ga) && ga == PN_APP_LONG_PRESS_MENU);

        // 7b. 背景图上的高亮底色：相对压暗像素并保留纸纹对比度。
        uint8_t *hpx = pn_alloc(&pool, 342u * 1216u);
        assert(hpx);
        pn_frame_t hframe;
        assert(pn_frame_bind(&hframe, hpx, 342u * 1216u, 684, 1216));
        for (int y = 0; y < 1216; y++)
            for (int x = 0; x < 684; x++)
                pn_frame_pixel(&hframe, x, y, (uint8_t)(((x + y) & 1) ? 14 : 11));
        pn_reader_highlight_span(&hframe, 100, 260, 300, 44, true);
        // 压暗后保持两种纹理像素可区分且均比原值暗，同时底部有深灰下划线。
        uint8_t p_even = pn_frame_get(&hframe, 120, 280);
        uint8_t p_odd = pn_frame_get(&hframe, 121, 280);
        assert(p_even < 11 && p_odd < 14 && p_even != p_odd);
        assert(pn_frame_get(&hframe, 120, 307) <= 6);
        // 纯白底铺 G13。
        pn_frame_clear(&hframe, 15);
        pn_reader_highlight_span(&hframe, 100, 260, 300, 44, false);
        assert(pn_frame_get(&hframe, 120, 280) == 13);

        // 7c. 长按双选浮条绘制与命中（正常/已摘录/贴顶自动翻至下方）。
        pn_lp_menu_t lpm = {.active = true, .anchor_x = 340, .anchor_y = 400, .highlighted = false};
        assert(pn_lp_menu_render(&lpm, &nl_font, &hframe) == PN_OK);
        assert(pn_lp_menu_hit(&lpm, 260, 340) == PN_LPM_EXCERPT);
        assert(pn_lp_menu_hit(&lpm, 420, 340) == PN_LPM_DICT);
        assert(pn_lp_menu_hit(&lpm, 50, 900) == PN_LPM_DISMISS);
        lpm.highlighted = true;
        lpm.anchor_y = 60; // 贴顶时自动翻到锚点下方 / Flips below anchor near top edge
        assert(pn_lp_menu_render(&lpm, &nl_font, &hframe) == PN_OK);
        assert(pn_lp_menu_hit(&lpm, 260, 120) == PN_LPM_EXCERPT);
        assert(pn_lp_menu_hit(&lpm, 430, 120) == PN_LPM_DICT);
        pn_free(hpx);

        // 7d. PNGS v1 (20B) 向后兼容加载 + v2 (24B) 往返。
        pn_media_lease_t gl = {0};
        assert(pn_media_acquire(&state_media, PN_MEDIA_WRITE, &gl) == PN_OK);
        pn_journal_files_t gf;
        pn_journal_io_t gio;
        assert(pn_gesture_prefs_files(&gf, &state_media, &gl, state, &gio) == PN_OK);
        uint8_t v1[20] = {'P','N','G','S', 1, 0, 0,0,0,0,0,0,0,0,0, PN_TAP_ZONE_THREE_COL, PN_KEY_MAP_SWAP_LR, PN_LP_DICT, 30, 2};
        assert(pn_journal_save(&gio, v1, sizeof v1) == PN_OK);
        pn_gesture_prefs_t g1 = {0};
        assert(pn_gesture_prefs_load(&gio, &g1) == PN_OK);
        assert(g1.tap_zone == PN_TAP_ZONE_THREE_COL && g1.key_map == PN_KEY_MAP_SWAP_LR && g1.key2_short_action == 2 && g1.long_press_mode == PN_LP_DICT && g1.auto_turn_sec == 30);
        assert(g1.swipe_turn == PN_SWIPE_TURN_LR && g1.shake_turn == 0 && g1.shake_sensitivity == PN_SHAKE_SENS_MED);
        g1.tap_zone = PN_TAP_ZONE_ONE_HAND_L;
        g1.swipe_turn = PN_SWIPE_TURN_TB;
        g1.key_map = PN_KEY_MAP_OFF;
        g1.long_press_mode = PN_LP_MENU;
        g1.shake_turn = 1;
        g1.shake_sensitivity = PN_SHAKE_SENS_HIGH;
        assert(pn_gesture_prefs_save(&gio, &g1) == PN_OK);
        pn_gesture_prefs_t g2 = {0};
        assert(pn_gesture_prefs_load(&gio, &g2) == PN_OK);
        assert(g2.tap_zone == PN_TAP_ZONE_ONE_HAND_L && g2.swipe_turn == PN_SWIPE_TURN_TB && g2.key_map == PN_KEY_MAP_OFF && g2.long_press_mode == PN_LP_MENU && g2.shake_turn == 1 && g2.shake_sensitivity == PN_SHAKE_SENS_HIGH);
        assert(pn_media_release(&state_media, &gl) == PN_OK);

        // 7e. 体感翻页检测器：重力姿态免疫、300ms 唤醒稳定期、单脉冲抗干扰、三档灵敏度触发与冷却防连翻。
        pn_shake_turn_t sh = {0};
        pn_shake_turn_reset(&sh, 1000);
        // 300ms 稳定期内剧烈抖动不得触发。
        assert(!pn_shake_turn_feed(&sh, 0, 0, 1000, PN_SHAKE_SENS_HIGH, 1020));
        assert(!pn_shake_turn_feed(&sh, 2500, 0, 1000, PN_SHAKE_SENS_HIGH, 1100));
        assert(!pn_shake_turn_feed(&sh, -2500, 0, 1000, PN_SHAKE_SENS_HIGH, 1140));
        // 任意静态倾角（竖屏握持 Y=+1000mg）连续喂样稳定后不误触。
        for (uint64_t t = 1400; t <= 2000; t += 20)
            assert(!pn_shake_turn_feed(&sh, 0, 1000, 0, PN_SHAKE_SENS_HIGH, t));
        // 单帧孤立尖峰（轻微磕碰）不触发，300ms 窗口过后自动清零。
        assert(!pn_shake_turn_feed(&sh, 1500, 1000, 0, PN_SHAKE_SENS_MED, 2100));
        assert(!pn_shake_turn_feed(&sh, 0, 1000, 0, PN_SHAKE_SENS_MED, 2120));
        assert(!pn_shake_turn_feed(&sh, 0, 1000, 0, PN_SHAKE_SENS_MED, 2500));
        // 中档灵敏度：两次相邻手腕甩动脉冲触发翻页，冷却期内不再连翻，冷却后可再次触发。
        assert(!pn_shake_turn_feed(&sh, 1200, 1000, 0, PN_SHAKE_SENS_MED, 2600));
        assert(pn_shake_turn_feed(&sh, -1200, 1000, 0, PN_SHAKE_SENS_MED, 2640));
        // 950ms 冷却期内禁止连续误翻。
        assert(!pn_shake_turn_feed(&sh, 1500, 1000, 0, PN_SHAKE_SENS_MED, 2700));
        assert(!pn_shake_turn_feed(&sh, -1500, 1000, 0, PN_SHAKE_SENS_MED, 2740));
        // 低档需要更大力度（950mg 不触发，1500mg 两次脉冲触发）。
        for (uint64_t t = 3600; t <= 4000; t += 20)
            assert(!pn_shake_turn_feed(&sh, 0, 1000, 0, PN_SHAKE_SENS_LOW, t));
        assert(!pn_shake_turn_feed(&sh, 900, 1000, 0, PN_SHAKE_SENS_LOW, 4040));
        assert(!pn_shake_turn_feed(&sh, -900, 1000, 0, PN_SHAKE_SENS_LOW, 4080));
        assert(!pn_shake_turn_feed(&sh, 1600, 1000, 0, PN_SHAKE_SENS_LOW, 4200));
        assert(pn_shake_turn_feed(&sh, -1600, 1000, 0, PN_SHAKE_SENS_LOW, 4240));
        // 高档灵敏度：较轻手腕甩动（760mg 双向脉冲）即可在冷却后触发。
        for (uint64_t t = 5500; t <= 5900; t += 20)
            assert(!pn_shake_turn_feed(&sh, 0, 1000, 0, PN_SHAKE_SENS_HIGH, t));
        assert(!pn_shake_turn_feed(&sh, 760, 1000, 0, PN_SHAKE_SENS_HIGH, 5940));
        assert(pn_shake_turn_feed(&sh, -760, 1000, 0, PN_SHAKE_SENS_HIGH, 5980));
    }

    pn_font_close(&nl_font);

    pn_status_bar_prefs_t def_sb = pn_status_bar_prefs_default();
    pn_status_bar_set_active(&def_sb);
    free(scr);
    pn_test_remove_indexes(state);
    rm_tree(root);
    assert(pool.used == 0 && pool.live == 0);
    puts("reading UX suite: status bar, gestures, tap zones, auto-turn, live guide sheet & sentence highlights passed");
    return 0;
}
