/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源 EPUB 脚注浮层卡片（与离线词典卡片样式完全一致、统一居底弹出、多页分页与前往脚注跳转）；主机与固件共用，render 只绘制不产生硬件副作用。
 * English: Shared EPUB footnote floating card (matching dictionary popup style, fixed bottom placement, multi-page pagination and go-to-footnote jump); shared across host and firmware, render paints only without hardware side effects.
 */
#pragma once
#include "pn_epub_app.h"
#include "pn_font.h"

#define PN_FOOTNOTE_CARD_X 48     ///< 浮层卡片左上 X（与离线词典浮层一致）/ Floating card left X (matches dictionary card)
#define PN_FOOTNOTE_CARD_W 588    ///< 浮层卡片宽度（像素，与词典一致）/ Floating card width in pixels (matches dictionary card)
#define PN_FOOTNOTE_SHEET_H 540   ///< 浮层卡片高度（像素，与词典一致）/ Floating card height in pixels (matches dictionary card)
#define PN_FOOTNOTE_BOTTOM_Y 620  ///< 统一居底浮层卡片顶边纵坐标（620..1160）/ Fixed bottom-docked floating card top Y (620..1160)
#define PN_FOOTNOTE_TOP_Y 620     ///< 兼容宏（已统一居底）/ Compatibility macro (now always bottom-docked)
#define PN_FOOTNOTE_PAGES_MAX 16u ///< 单条脚注最大弹窗分页数 / Maximum pages inside one footnote popup

/// 脚注弹窗命令编码。/ Footnote popup UI command IDs.
enum {
    PN_FNUI_CLOSE = 1, ///< 关闭弹窗并恢复原阅读页（右上角 ×）/ Close popup and restore the reading page (top-right ×)
    PN_FNUI_JUMP,      ///< 前往脚注锚点位置 / Jump to the footnote anchor
    PN_FNUI_PREV,      ///< 脚注上一页按钮 / Previous footnote page button
    PN_FNUI_NEXT,      ///< 脚注下一页按钮 / Next footnote page button
    PN_FNUI_DISMISS,   ///< 点击弹窗外正文区关闭（不参与焦点框与按下反馈）/ Tap outside popup to dismiss (excluded from focus/press feedback)
    PN_FNUI_BODY_PREV, ///< 点击弹窗正文左侧翻上一页（不参与焦点框）/ Tap left body to page back (excluded from focus)
    PN_FNUI_BODY_NEXT  ///< 点击弹窗正文右侧翻下一页（不参与焦点框）/ Tap right body to page forward (excluded from focus)
};

/// 脚注弹窗状态对象；初始全零，活动状态不可复制，须由 pn_footnote_ui_close 释放。
/// Footnote popup UI handle; zero-initialize, never copy while active, release with pn_footnote_ui_close.
typedef struct {
    void *impl; ///< 池分配的私有状态 / Pool-allocated private state
    bool active; ///< 当前弹窗是否打开 / Whether the footnote popup is currently open
} pn_footnote_ui_t;

/// 打开脚注弹窗并保存提取的注释元数据与文本；不立即推屏（调用 pn_footnote_ui_present 呈现）。
/// Open the footnote popup storing extracted metadata and body text; does not present until pn_footnote_ui_present is called.
pn_status_t pn_footnote_ui_open(pn_footnote_ui_t *ui,pn_pool_t *pool,const pn_epub_footnote_t *meta,const char *text,size_t text_len);

/// 纯绘制：将当前脚注弹窗叠加绘制到 684x1216 帧上（保留弹窗外的阅读正文），无硬件副作用。
/// Pure paint: draw the active footnote popup sheet onto a 684x1216 frame (preserving surrounding reading body), with no hardware side effects.
pn_status_t pn_footnote_ui_render(pn_footnote_ui_t *ui,pn_font_t *font,pn_frame_t *frame);

/// 暂存被遮挡区域、叠加绘制弹窗、经 present(GL16) 局部刷新并还原原帧缓冲。
/// Snapshot the covered band, paint the sheet onto page_frame, present via GL16, and restore page_frame intact.
pn_status_t pn_footnote_ui_present(pn_footnote_ui_t *ui,pn_pool_t *pool,pn_font_t *font,pn_frame_t *page_frame,pn_reader_present_fn present,void *ctx);

/// 屏幕坐标命中测试，返回 PN_FNUI_* 或 -1。/ Hit-test screen coordinate (x, y), returning PN_FNUI_* or -1.
int pn_footnote_ui_hit(const pn_footnote_ui_t *ui,int x,int y);

/// 翻动脚注弹窗页码（delta 为 -1 或 +1）；页码有变化返回 PN_OK，已到首/末页返回 PN_EMPTY。
/// Turn pages inside the footnote popup (delta -1 or +1); returns PN_OK if page changed, PN_EMPTY at ends.
pn_status_t pn_footnote_ui_step(pn_footnote_ui_t *ui,int delta);

/// 读取弹窗绑定的脚注元数据（只读，用于“前往脚注”跳转或测试核对）。/ Borrow popup footnote metadata (read-only) for jump or tests.
const pn_epub_footnote_t *pn_footnote_ui_meta(const pn_footnote_ui_t *ui);

/// 查询弹窗正文、当前页码、总页数与顶边纵坐标（只读诊断/测试辅助）。/ Read-only inspection of body text, page index/count, and top Y.
const char *pn_footnote_ui_text(const pn_footnote_ui_t *ui);
size_t pn_footnote_ui_page_index(const pn_footnote_ui_t *ui);
size_t pn_footnote_ui_page_count(const pn_footnote_ui_t *ui);
int pn_footnote_ui_sheet_y(const pn_footnote_ui_t *ui);

/// 幂等释放池内状态并关闭弹窗。/ Idempotently free pooled state and close the popup.
void pn_footnote_ui_close(pn_footnote_ui_t *ui);
