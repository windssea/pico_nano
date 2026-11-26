/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读页的页脚与工具栏绘制（docs/UI_UX.md第4节）；纯绘制与命中，不读写存储或阅读状态。
 * English: reading-page footer and toolbar drawing (docs/UI_UX.md section 4); drawing and hit testing only, never touching storage or reading state.
 * 冻结：正文区 x=32,y=32,w=620,h=1112，页脚 y=1144..1215，工具栏占底部320px、白底细线、无暗幕。
 * Frozen: text area x=32,y=32,w=620,h=1112; footer y=1144..1215; the toolbar covers the bottom 320 px with a white sheet and thin rules, no dimming.
 */
#pragma once
#include "pn_cover.h"
#include "pn_widgets.h"
#include "pn_input_prefs.h"
#define PN_READER_TOP 32 ///< 正文区顶部y / Top y of the text area
#define PN_READER_HEIGHT 1112 ///< 正文区高度 / Text area height
#define PN_READER_FOOTER_Y 1144 ///< 页脚上沿y / Footer top y
#define PN_READER_FOOTER_BAND_Y 1152 ///< 覆盖完整正文的32像素对齐刷新边界 / 32-pixel-aligned refresh boundary covering the complete text area
#define PN_TOOLBAR_Y 896 ///< 工具栏上沿y，高度320 / Toolbar top y, height 320
#define PN_TOOLBAR_TOP_H 112 ///< 阅读工具栏顶部信息条高度 / Height of the toolbar top info bar
/// 目录、进度与书签跳转共用的纯绘制等待回调；ctx/metadata不使用。/ Paint-only busy callback shared by TOC, progress and bookmark jumps; ctx/metadata are unused.
pn_status_t pn_reader_jump_loading_render(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame);
/// 工具栏入口，值同时是命中返回值。/ Toolbar entries; the values are also the hit-test results.
typedef enum {
    PN_TOOL_TOC=40, ///< 目录 / Table of contents
    PN_TOOL_BOOKMARKS, ///< 书签 / Bookmarks
    PN_TOOL_JUMP, ///< 按百分比跳转（打开跳转面板）/ Jump by percentage (opens the jump panel)
    PN_TOOL_TYPESET, ///< 排版 / Typesetting
    PN_TOOL_REFRESH, ///< 强刷：整屏GC16重绘当前页 / Full-screen GC16 redraw of the current page
    PN_TOOL_SHELF, ///< 回书架 / Back to the shelf
    PN_TOOL_CLOSE, ///< 点工具栏以外区域关闭 / Tapping outside the toolbar closes it
    PN_TOOL_QUICK_BOOKMARK, ///< 顶栏右侧一键添加书签 / Top-bar one-tap quick bookmark
    PN_TOOL_DICT, ///< 顶栏查词按钮（打开离线词典）/ Top-bar dictionary lookup button
    PN_TOOL_GUIDE, ///< 阅读线半屏实时微调 / Live half-sheet reading-line editor
    PN_TOOL_AUTO_TURN ///< 自动翻页开关与调速 / Auto page-turn toggle and speed
} pn_reader_tool_t;
/// 自定义单条阅读状态栏数据快照（纯绘制只读）。/ Read-only snapshot for custom single-bar reading status rendering.
typedef struct {
    const char *chapter; ///< 当前章节名（可空）/ Current chapter title (nullable)
    const char *book; ///< 当前书名（可空）/ Current book title (nullable)
    const char *section; ///< 节号文案，如 "3/20"（可空）/ Section label such as "3/20" (nullable)
    int book_basis; ///< 全书进度 0..10000（<0 未知）/ Whole-book progress 0..10000 (<0 unknown)
    int chapter_basis; ///< 本章进度 0..10000（<0 未知，回退全书）/ Chapter progress 0..10000 (<0 falls back to book)
    bool show_return; ///< 显示“< 返回跳转前位置” / Show "< 返回跳转前位置" return button
    const char *draft_hint; ///< 预览提示（非空时覆盖左侧显示）/ Preview hint when non-NULL
} pn_reader_bar_info_t;
/// 按当前激活的 pn_status_bar_active() 绘制自定义阅读状态栏（双栏/顶部单栏/底部单栏/关闭）；enabled==0 时返回 PN_EMPTY 由调用方走经典双栏。
/// Render the active custom reading status bar (dual/top/bottom/off); returns PN_EMPTY when enabled==0 so callers use legacy dual bars.
pn_status_t pn_reader_custom_bar_render(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const pn_reader_bar_info_t *info,unsigned background);
/// 在屏幕底部预留的独立返回条区域（y=1160..1216）绘制「< 返回跳转前位置」，避免遮挡正文。
/// Render the dedicated bottom "< 返回跳转前位置" strip in the reserved bottom band (y=1160..1216) without overlapping prose.
pn_status_t pn_reader_return_bar_render(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const pn_reader_bar_info_t *info,unsigned background);
/// 渲染阅读页自定义状态栏刷新（无副作用）。/ Refresh only the custom status bar band without disturbing body prose.
pn_status_t pn_reader_custom_bar_refresh(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const pn_reader_bar_info_t *info,unsigned background);
/// 状态栏“返回跳转前位置”（始终位于底侧）与点击跳转进度条的命中区判断。/ Check whether (x,y) hits the bottom return-to-origin strip or active status bar.
int pn_reader_bar_hit(int x,int y,bool hide_bars,bool can_return);
/// 工具栏顶部阅读信息条数据；title为书名或章节，basis为0..10000（负值表示未知）。/ Reader toolbar top info bar data: title/chapter and progress 0..10000 (negative when unknown).
typedef struct {
    const char *title; ///< 书名 / Book title
    int basis; ///< 阅读进度0..10000，负值未知 / Progress 0..10000, negative when unknown
    bool bookmarked; ///< 当前页已标记书签 / Whether the current page is bookmarked
    bool quick_bookmark; ///< 是否显示右侧一键书签按钮 / Show one-tap bookmark button on the right
} pn_toolbar_top_t;
/// 画页脚：y=1144细线，左侧文字（章节/书名）与右侧文字（百分比）；fallback可为NULL，UI字体缺字时用它补。
/// Draw the footer: a thin rule at y=1144, left text (section/title) and right text (percentage); fallback may be NULL and fills glyphs the UI font lacks.
pn_status_t pn_reader_footer_render(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const char *left,const char *right);
/// 同上，另画进度轨（basis为0–10000，<0为细线）；非零background要求已铺好原生背景。/ Same as above, plus a progress track (basis 0–10000; negative for hairline); a nonzero background requires freshly painted native paper.
pn_status_t pn_reader_footer_progress(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const char *left,const char *right,int basis,unsigned background);
/// 同上，并支持指定页脚字号（<=0沿用默认26px）。/ Same as above with an optional footer font size (<=0 defaults to 26 px).
pn_status_t pn_reader_footer_progress_ex(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const char *left,const char *right,int basis,unsigned background,int font_pixels);
/// 恢复顶部32px原生背景后画时间/电量，不改变正文或分页。/ Restore native paper then draw time/battery in the top 32 px without changing prose or pagination.
pn_status_t pn_reader_status_render(pn_font_t *ui,pn_frame_t *frame,unsigned background);
/// 在frame底部画工具栏（白底覆盖原页面）；unavailable按位标记不可用入口（bit0对应PN_TOOL_TOC，依此类推）。
/// Draw the toolbar over the bottom of the frame (a white sheet over the page); unavailable marks entries bitwise (bit 0 is PN_TOOL_TOC and so on).
pn_status_t pn_reader_toolbar_render(pn_font_t *ui,pn_frame_t *frame,unsigned unavailable);
/// 在frame顶部画工具栏信息条（实时状态带、书名、进度百分比与可选一键书签）。/ Draw the toolbar top info bar (status band, title, progress percentage and optional quick bookmark).
pn_status_t pn_reader_toolbar_top_render(pn_font_t *ui,pn_frame_t *frame,const pn_toolbar_top_t *top);
/// 命中返回PN_TOOL_*；工具栏以上区域返回PN_TOOL_CLOSE；不可用入口返回-1。
/// Hit test returning PN_TOOL_*; the area above the toolbar returns PN_TOOL_CLOSE; unavailable entries return -1.
int pn_reader_toolbar_hit(int x,int y,unsigned unavailable);
/// 静态书籍开场，缺元数据时使用文件名；无动画或假进度。/ Static book opening with filename fallback, without animation or false progress.
pn_status_t pn_reader_loading_render(pn_font_t *ui,pn_frame_t *frame,const char *title);
/// 旧调用兼容，phase不再改变开书画面。/ Legacy compatibility; phase no longer changes the opening screen.
pn_status_t pn_reader_loading_render_phase(pn_font_t *ui,pn_frame_t *frame,const char *title,unsigned phase);
/// 居中绘制立体封面、书名、可选作者与状态；note为真实状态。/ Center a dimensional cover, title, optional author and real status.
pn_status_t pn_reader_opening_render(pn_font_t *,pn_frame_t *,const char *,const pn_book_preview_t *,const char *note);
/// 小TXT直接呈现正文，省掉一次面板刷新；大文件保留开书反馈。 / Present small TXT directly to avoid an extra panel update; keep opening feedback for larger files.
bool pn_reader_loading_needed(bool text_format,uint64_t source_bytes);
/// 在正文视口的一行绘制虚线/点线，shade 0浅/1中/2深/3黑；0样式关闭，越界跳过，不分配或改变排版。 / Draw a dashed/dotted row in a text viewport with shade light/medium/dark/black; style zero disables, out-of-bounds rows are skipped, without allocation or layout changes.
void pn_reader_guide_row(pn_frame_t *viewport,unsigned style,unsigned shade,int y);
/// 扩展阅读线绘制：支持限定横向起止[x0,x1)、粗细1..3px，以及按字号缩放虚线/点线节距。
/// Extended guide-row drawing with explicit horizontal span [x0,x1), thickness 1..3 px, and optional font-scaled dash/dot pitch.
void pn_reader_guide_row_ex(pn_frame_t *viewport,unsigned style,unsigned shade,int x0,int x1,int y,int thickness,int font_pixels,bool follow_font);
/// 全参数阅读线绘制：支持 1虚线/2点线/3实线，以及自定义虚线长度 dash_len（0自动）与间隔 dash_gap（0自动）。
/// Full-parameter guide-row drawing: styles 1 dashed / 2 dotted / 3 solid, plus optional custom dash_len and dash_gap (0=auto).
void pn_reader_guide_row_custom(pn_frame_t *viewport,unsigned style,unsigned shade,int x0,int x1,int y,int thickness,int dash_len,int dash_gap,int font_pixels,bool follow_font);
/// 按行段绘制摘录高亮底色与下划线：纯白底铺柔和浅灰 G13，有阅读背景图时相对压暗现有背景像素以完整保留纹理。
/// Paint a contiguous sentence-highlight band and underline: soft G13 fill on plain paper, or texture-preserving pixel darkening over reading backgrounds.
void pn_reader_highlight_span(pn_frame_t *viewport,int x0,int x1,int baseline,int pixels,bool has_bg);

/// 正文长按双选浮条（摘录 | 查词）命中码，零单击翻页延迟。/ Body long-press dual action pill (Excerpt | Dictionary) hit codes, zero tap-turn delay.
typedef enum {
    PN_LPM_EXCERPT=1, ///< 摘录 / 取消摘录 / Highlight or remove highlight
    PN_LPM_DICT=2, ///< 离线查词 / Open dictionary lookup
    PN_LPM_DISMISS=3 ///< 点击浮条外侧关闭 / Tap outside pill to dismiss
} pn_lp_menu_cmd_t;
/// 正文长按双选浮条状态。/ State of the body long-press dual action pill.
typedef struct {
    bool active; ///< 浮条是否显示 / Whether the floating pill is open
    int anchor_x,anchor_y; ///< 长按触发坐标 / Long-press anchor coordinates
    bool highlighted; ///< 该句是否已摘录（控制文案「摘录」或「取消摘录」）/ Whether the anchored sentence is already highlighted
} pn_lp_menu_t;
/// 在已显示阅读页副本上叠画轻量圆角胶囊「摘录 | 查词」。/ Paint the lightweight rounded floating pill "Excerpt | Dict" over the reading page copy.
pn_status_t pn_lp_menu_render(const pn_lp_menu_t *menu,pn_font_t *ui,pn_frame_t *frame);
/// 命中检测：返回 PN_LPM_EXCERPT / PN_LPM_DICT / PN_LPM_DISMISS，未激活返回 -1。/ Hit test returning PN_LPM_* or -1 when inactive.
int pn_lp_menu_hit(const pn_lp_menu_t *menu,int x,int y);

/// 只画状态文字，底图由控制层准备。/ Draw status text only over a controller-prepared underlay.
pn_status_t pn_reader_status_text(pn_font_t *,pn_frame_t *);
