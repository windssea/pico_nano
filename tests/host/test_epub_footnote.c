/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB 脚注解析、命中扩展、弹窗分页/局部刷新与跳转返回回归测试。
 * English: Regression tests for EPUB footnote parsing, hit-testing, popup pagination/local present, and jump/return.
 */
#include "pn_epub_app.h"
#include "pn_footnote_ui.h"
#include "pn_reader_input.h"
#include "pn_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned calls;
    pn_refresh_t last_profile;
    uint8_t fb[342u * 1216u];
    bool has_fb;
} screen_t;

static pn_status_t present(void *ctx, const pn_frame_t *frame, pn_refresh_t profile) {
    screen_t *s = ctx;
    assert(frame && frame->pixels && frame->width == 684 && frame->height == 1216 && frame->stride >= 342);
    s->calls++;
    s->last_profile = profile;
    for (int y = 0; y < 1216; y++) memcpy(s->fb + (size_t)y * 342u, frame->pixels + (size_t)y * frame->stride, 342u);
    s->has_fb = true;
    return PN_OK;
}

static bool same_progress(const pn_epub_progress_t *a, const pn_epub_progress_t *b) {
    return !memcmp(a->book.sha256, b->book.sha256, 32) &&
           !strcmp(a->location.path, b->location.path) &&
           a->location.position.element == b->location.position.element &&
           a->location.position.offset == b->location.position.offset &&
           a->location.position.run == b->location.position.run &&
           a->location.position.kind == b->location.position.kind;
}

/// 在已渲染页面上按网格扫描第一个匹配指定片段 ID 的脚注坐标。/ Scan the rendered page grid to find a tap point matching fragment.
static bool find_footnote_point(pn_epub_app_t *app, const char *frag, int *out_x, int *out_y,
                                pn_epub_footnote_t *out_meta, char *text, size_t cap, size_t *out_len) {
    for (int y = 40; y < 1140; y += 14) {
        for (int x = 24; x < 660; x += 14) {
            pn_epub_footnote_t meta = {0};
            size_t len = 0;
            if (pn_epub_app_footnote_at(app, x, y, &meta, text, cap, &len) == PN_OK &&
                !strcmp(meta.fragment, frag)) {
                *out_x = x;
                *out_y = y;
                *out_meta = meta;
                *out_len = len;
                return true;
            }
        }
    }
    return false;
}

static void test_input_last_tap(void) {
    pn_reader_input_t in = {0};
    pn_reader_action_t act = PN_APP_OPEN;
    // 默认模式正文轻点：返回翻页动作且 last_tap=true。/ Plain body tap: turns page AND sets last_tap=true.
    assert(!pn_reader_input_feed_timed(&in, 1, 420, 300, true, 100, &act) && !in.last_tap);
    assert(!pn_reader_input_feed_timed(&in, 0, 0, 0, true, 130, &act) && !in.last_tap);
    assert(pn_reader_input_feed_timed(&in, 0, 0, 0, true, 160, &act) && act == PN_APP_NEXT && in.last_tap);
    // 关闭点按翻页时轻点：不返回翻页动作，但 last_tap=true（仍可点开脚注）。/ With no_edge_tap: no turn action, yet last_tap=true.
    in.config.no_edge_tap = true;
    assert(!pn_reader_input_feed_timed(&in, 1, 200, 300, true, 200, &act));
    assert(!pn_reader_input_feed_timed(&in, 0, 0, 0, true, 230, &act));
    assert(!pn_reader_input_feed_timed(&in, 0, 0, 0, true, 260, &act) && in.last_tap);
    // 横滑翻页：last_tap=false。/ Horizontal swipe: last_tap=false.
    in.config.no_edge_tap = false;
    assert(!pn_reader_input_feed_timed(&in, 1, 500, 300, true, 300, &act));
    assert(!pn_reader_input_feed_timed(&in, 1, 350, 302, true, 330, &act));
    assert(!pn_reader_input_feed_timed(&in, 0, 0, 0, true, 360, &act));
    assert(pn_reader_input_feed_timed(&in, 0, 0, 0, true, 390, &act) && act == PN_APP_NEXT && !in.last_tap);
    // 长按查词：last_tap=false，且松手后仍为 false。/ Long-press lookup: last_tap=false on trigger and release.
    assert(!pn_reader_input_feed_timed(&in, 1, 200, 300, true, 500, &act));
    assert(pn_reader_input_feed_timed(&in, 1, 202, 301, true, 1000, &act) && act == PN_APP_LOOKUP && !in.last_tap);
    assert(!pn_reader_input_feed_timed(&in, 0, 0, 0, true, 1040, &act) && !in.last_tap);
    assert(!pn_reader_input_feed_timed(&in, 0, 0, 0, true, 1080, &act) && !in.last_tap);
}

int main(int argc, char **argv) {
    test_input_last_tap();
    if (argc < 4) return 0;

    pn_pool_t pool;
    assert(pn_pool_init(&pool, 4 * 1024 * 1024, NULL, NULL, NULL) == 0);
    pn_epub_app_t app = {0};
    screen_t screen = {0};
    uint64_t now = 100;

    assert(pn_epub_app_open(&app, &pool, argv[1], argv[3], argv[2], 32, now++) == PN_OK);
    assert(pn_epub_app_step(&app, PN_APP_OPEN, now++, present, &screen) == PN_OK && screen.has_fb);
    pn_epub_progress_t origin = {0};
    assert(pn_epub_app_progress(&app, &origin) == PN_OK);

    char text[PN_FOOTNOTE_TEXT_MAX];
    pn_epub_footnote_t meta = {0};
    size_t len = 0;
    int fx = 0, fy = 0;

    // 1. EPUB2/Kindle 上标 [103]：提取长注释、去掉返回箭头 ↩、居底弹窗、多页分页、局部呈现不污染底层帧。
    assert(find_footnote_point(&app, "fn1", &fx, &fy, &meta, text, sizeof text, &len));
    assert(!strcmp(meta.label, "[103]"));
    assert(meta.can_jump);
    assert(meta.marker_y < 680);
    assert(strstr(text, "水精盘中之珠") != NULL);
    assert(strstr(text, "河图") != NULL);
    assert(strstr(text, "↩") == NULL);

    // 验证弹窗渲染、分页、命中、以及 pn_footnote_ui_present 呈现后原页帧完全还原。
    pn_footnote_ui_t ui = {0};
    assert(pn_footnote_ui_open(&ui, &pool, &meta, text, len) == PN_OK && ui.active);
    assert(pn_footnote_ui_sheet_y(&ui) == PN_FOOTNOTE_BOTTOM_Y);

    uint8_t page_copy[342u * 1216u], orig_copy[342u * 1216u];
    memcpy(page_copy, screen.fb, sizeof page_copy);
    memcpy(orig_copy, screen.fb, sizeof orig_copy);
    pn_frame_t page_frame;
    assert(pn_frame_bind(&page_frame, page_copy, sizeof page_copy, 684, 1216));

    pn_font_t ui_font = {0};
    pn_text_source_t builtin = pn_font_builtin_source();
    assert(pn_font_open(&ui_font, &pool, &builtin, 30) == PN_OK);
    pn_w_font_scope_t scope = {0};
    assert(pn_w_font_scope_begin(&scope, &pool, NULL) == PN_OK);

    unsigned calls_before = screen.calls;
    assert(pn_footnote_ui_present(&ui, &pool, &ui_font, &page_frame, present, &screen) == PN_OK);
    assert(screen.calls == calls_before + 1 && screen.last_profile == PN_REFRESH_GL16);
    // 弹窗呈现后，传入的 page_copy 必须被完全复原。/ page_copy must be restored byte-for-byte after present.
    assert(memcmp(page_copy, orig_copy, sizeof page_copy) == 0);
    // 屏幕上弹窗外（顶部阅读正文区）像素与原页逐字节一致，弹窗内像素已发生变化。
    assert(memcmp(screen.fb, orig_copy, 342u * (unsigned)(PN_FOOTNOTE_BOTTOM_Y - 6)) == 0);
    assert(memcmp(screen.fb + 342u * (unsigned)PN_FOOTNOTE_BOTTOM_Y,
                  orig_copy + 342u * (unsigned)PN_FOOTNOTE_BOTTOM_Y,
                  342u * (unsigned)PN_FOOTNOTE_SHEET_H) != 0);

    // 长注释应分为 >=2 页，支持翻页与命中编码区分（与离线词典卡片几何一致）。
    assert(pn_footnote_ui_page_count(&ui) >= 2u);
    assert(pn_footnote_ui_page_index(&ui) == 0u);
    assert(pn_footnote_ui_hit(&ui, 100, 100) == PN_FNUI_DISMISS);
    assert(pn_footnote_ui_hit(&ui, 10, PN_FOOTNOTE_BOTTOM_Y + 200) == PN_FNUI_DISMISS);
    assert(pn_footnote_ui_hit(&ui, 670, PN_FOOTNOTE_BOTTOM_Y + 200) == PN_FNUI_DISMISS);
    assert(pn_footnote_ui_hit(&ui, PN_FOOTNOTE_CARD_X + 550, PN_FOOTNOTE_BOTTOM_Y + 34) == PN_FNUI_CLOSE);
    assert(pn_footnote_ui_hit(&ui, PN_FOOTNOTE_CARD_X + 100, PN_FOOTNOTE_BOTTOM_Y + 506) == PN_FNUI_JUMP);
    assert(pn_footnote_ui_hit(&ui, PN_FOOTNOTE_CARD_X + PN_FOOTNOTE_CARD_W - 180, PN_FOOTNOTE_BOTTOM_Y + 506) == -1); // 首页上一页禁用 / Prev disabled on page 0
    assert(pn_footnote_ui_hit(&ui, PN_FOOTNOTE_CARD_X + PN_FOOTNOTE_CARD_W - 45, PN_FOOTNOTE_BOTTOM_Y + 506) == PN_FNUI_NEXT);
    assert(pn_footnote_ui_hit(&ui, PN_FOOTNOTE_CARD_X + 420, PN_FOOTNOTE_BOTTOM_Y + 200) == PN_FNUI_BODY_NEXT);
    assert(pn_footnote_ui_step(&ui, -1) == PN_EMPTY);
    assert(pn_footnote_ui_step(&ui, 1) == PN_OK && pn_footnote_ui_page_index(&ui) == 1u);
    assert(pn_footnote_ui_hit(&ui, PN_FOOTNOTE_CARD_X + PN_FOOTNOTE_CARD_W - 180, PN_FOOTNOTE_BOTTOM_Y + 506) == PN_FNUI_PREV);
    assert(pn_footnote_ui_hit(&ui, PN_FOOTNOTE_CARD_X + 80, PN_FOOTNOTE_BOTTOM_Y + 200) == PN_FNUI_BODY_PREV);
    assert(pn_footnote_ui_present(&ui, &pool, &ui_font, &page_frame, present, &screen) == PN_OK);
    assert(pn_footnote_ui_step(&ui, -1) == PN_OK && pn_footnote_ui_page_index(&ui) == 0u);
    pn_footnote_ui_close(&ui);
    assert(!ui.active && !ui.impl);

    // 2. EPUB3 隐藏 <aside epub:type="footnote">：能提取注释正文，且 can_jump=false（隐藏节点不可跳转）。
    assert(find_footnote_point(&app, "fn_aside", &fx, &fy, &meta, text, sizeof text, &len));
    assert(strstr(text, "隐藏在 EPUB3 aside") != NULL);
    assert(!meta.can_jump);
    assert(pn_footnote_ui_open(&ui, &pool, &meta, text, len) == PN_OK);
    assert(pn_footnote_ui_hit(&ui, PN_FOOTNOTE_CARD_X + 100, PN_FOOTNOTE_BOTTOM_Y + 506) == -1); // can_jump=false 时无前往脚注按钮
    pn_footnote_ui_close(&ui);

    // 3. 跨文件 notes.xhtml#n3（空锚点前置写法）：捕获紧随段落，不混入下一段，支持跳转与返回原页。
    assert(find_footnote_point(&app, "n3", &fx, &fy, &meta, text, sizeof text, &len));
    assert(strstr(text, "第三条注释") != NULL);
    assert(strstr(text, "不应混入") == NULL);
    assert(meta.can_jump);

    // 4. 普通占满整行的目录超链接（指向 n3_next）整页扫描都不应触发脚注弹窗。
    assert(!find_footnote_point(&app, "n3_next", &fx, &fy, &meta, text, sizeof text, &len));
    // 检查页底 [4]：无论角标在页顶还是页底，注释卡片统一居底弹出（与离线词典一致），且内联锚点去掉“返回”字样。
    assert(find_footnote_point(&app, "fn4", &fx, &fy, &meta, text, sizeof text, &len));
    assert(strstr(text, "第四条内联锚点注释正文") != NULL);
    assert(strstr(text, "返回") == NULL);
    assert(meta.marker_y >= 680);
    assert(pn_footnote_ui_open(&ui, &pool, &meta, text, len) == PN_OK);
    assert(pn_footnote_ui_sheet_y(&ui) == PN_FOOTNOTE_BOTTOM_Y);
    assert(pn_footnote_ui_hit(&ui, 300, 300) == PN_FNUI_DISMISS);
    assert(pn_footnote_ui_present(&ui, &pool, &ui_font, &page_frame, present, &screen) == PN_OK);
    assert(memcmp(page_copy, orig_copy, sizeof page_copy) == 0);
    pn_footnote_ui_close(&ui);

    // 5. 策略 A：多次前往脚注及在脚注区内翻页不覆盖最初阅读位置，返回原页始终准确回到最初正文页。
    // Strategy A: repeated footnote jumps and paging within footnotes preserve the earliest reading origin.
    assert(find_footnote_point(&app, "fn1", &fx, &fy, &meta, text, sizeof text, &len));
    pn_epub_footnote_t second_meta = meta;
    // 同时验证冗余角标前缀剥离（如正文以 "[103] " 开头时不在“注 [103]”下重复）。
    {
        const char *dup = "[103]：重复前缀测试正文";
        assert(pn_footnote_ui_open(&ui, &pool, &second_meta, dup, strlen(dup)) == PN_OK);
        assert(!strcmp(pn_footnote_ui_text(&ui), "重复前缀测试正文"));
        pn_footnote_ui_close(&ui);
    }
    assert(find_footnote_point(&app, "n3", &fx, &fy, &meta, text, sizeof text, &len));
    assert(pn_epub_app_footnote_jump(&app, &meta, now++, present, &screen) == PN_OK);
    pn_epub_progress_t jumped = {0};
    assert(pn_epub_app_progress(&app, &jumped) == PN_OK);
    assert(!strcmp(jumped.location.path, "OPS/notes.xhtml"));
    assert(pn_epub_app_bookmark_can_return(&app));
    // 在脚注区翻页后仍保留返回点；二次“前往脚注”到另一位置后仍锁死最初正文页 origin。
    (void)pn_epub_app_step(&app, PN_APP_PREVIOUS, now++, present, &screen);
    assert(pn_epub_app_bookmark_can_return(&app));
    assert(pn_epub_app_footnote_jump(&app, &second_meta, now++, present, &screen) == PN_OK);
    assert(pn_epub_app_bookmark_can_return(&app));
    assert(pn_epub_app_bookmark_return(&app, now++, present, &screen) == PN_OK);
    pn_epub_progress_t back = {0};
    assert(pn_epub_app_progress(&app, &back) == PN_OK);
    assert(same_progress(&origin, &back));
    assert(!pn_epub_app_bookmark_can_return(&app));

    pn_w_font_scope_end(&scope);
    pn_font_close(&ui_font);
    assert(pn_epub_app_close(&app, now++) == PN_OK);
    assert(pool.used == 0 && pool.live == 0);
    printf("test_epub_footnote OK\n");
    return 0;
}
