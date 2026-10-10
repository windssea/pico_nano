/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读页的页脚与工具栏绘制（docs/UI_UX.md第4节）；纯绘制与命中，不读写存储或阅读状态。
 * English: reading-page footer and toolbar drawing (docs/UI_UX.md section 4); drawing and hit testing only, never touching storage or reading state.
 * 冻结：正文区 x=32,y=32,w=620,h=1112，页脚 y=1144..1215，工具栏占底部320px、白底细线、无暗幕。
 * Frozen: text area x=32,y=32,w=620,h=1112; footer y=1144..1215; the toolbar covers the bottom 320 px with a white sheet and thin rules, no dimming.
 */
#pragma once
#include "pn_widgets.h"
#define PN_READER_TOP 32 ///< 正文区顶部y / Top y of the text area
#define PN_READER_HEIGHT 1112 ///< 正文区高度 / Text area height
#define PN_READER_FOOTER_Y 1144 ///< 页脚上沿y / Footer top y
#define PN_TOOLBAR_Y 896 ///< 工具栏上沿y，高度320 / Toolbar top y, height 320
/// 工具栏入口，值同时是命中返回值。/ Toolbar entries; the values are also the hit-test results.
typedef enum {
    PN_TOOL_TOC=40, ///< 目录 / Table of contents
    PN_TOOL_BOOKMARKS, ///< 书签 / Bookmarks
    PN_TOOL_SEARCH, ///< 搜索（尚未实现，固定显示为不可用）/ Search (not implemented, always shown as unavailable)
    PN_TOOL_TYPESET, ///< 排版 / Typesetting
    PN_TOOL_REFRESH, ///< 强刷：整屏GC16重绘当前页 / Full-screen GC16 redraw of the current page
    PN_TOOL_SHELF, ///< 回书架 / Back to the shelf
    PN_TOOL_CLOSE ///< 点工具栏以外区域关闭 / Tapping outside the toolbar closes it
} pn_reader_tool_t;
/// 画页脚：y=1144细线，左侧文字（章节/书名）与右侧文字（百分比）；fallback可为NULL，UI字体缺字时用它补。
/// Draw the footer: a thin rule at y=1144, left text (section/title) and right text (percentage); fallback may be NULL and fills glyphs the UI font lacks.
pn_status_t pn_reader_footer_render(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const char *left,const char *right);
/// 同上，另在页脚上沿画进度轨（basis为0–10000，<0则只画细线）。/ Same as above, plus a progress track along the footer top (basis 0–10000; a hairline when negative).
pn_status_t pn_reader_footer_progress(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const char *left,const char *right,int basis);
/// 在frame底部画工具栏（白底覆盖原页面）；unavailable按位标记不可用入口（bit0对应PN_TOOL_TOC，依此类推）。
/// Draw the toolbar over the bottom of the frame (a white sheet over the page); unavailable marks entries bitwise (bit 0 is PN_TOOL_TOC and so on).
pn_status_t pn_reader_toolbar_render(pn_font_t *ui,pn_frame_t *frame,unsigned unavailable);
/// 命中返回PN_TOOL_*；工具栏以上区域返回PN_TOOL_CLOSE；不可用入口返回-1。
/// Hit test returning PN_TOOL_*; the area above the toolbar returns PN_TOOL_CLOSE; unavailable entries return -1.
int pn_reader_toolbar_hit(int x,int y,unsigned unavailable);
