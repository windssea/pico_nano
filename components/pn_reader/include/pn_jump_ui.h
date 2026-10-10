/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读进度跳转面板（docs/UI_UX.md第4节“点击进度条打开跳转面板”）：数值步进＋预览＋确认，避免一次随手触摸直接跨越大量内容。
 * English: reading progress jump panel (docs/UI_UX.md section 4, "tap the progress to open the jump panel"): numeric stepping, preview and confirmation so one stray touch never leaps across a lot of content.
 * 冻结：步进只改草稿；只有“跳转”才移动阅读位置；取消重画当前页。TXT与EPUB都按百分比，EPUB落到该百分比所在章节的开头。
 * Frozen: stepping only edits the draft; only "Jump" moves the reading position; cancel redraws the current page. TXT and EPUB both jump by percentage; EPUB lands at the start of the containing section.
 */
#pragma once
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#define PN_JUI_CANCEL 1 ///< 取消并重画当前页 / Cancel and redraw the current page
#define PN_JUI_CONFIRM 2 ///< 跳转到草稿位置 / Jump to the draft position
#define PN_JUI_RETRY 3 ///< 重画面板 / Repaint the panel
#define PN_JUI_STEP 10 ///< 10..13：-大步/-1/+1/+大步 / 10..13: -large/-1/+1/+large
typedef struct {
    pn_reader_app_t *reader; ///< TXT会话（与epub互斥）/ TXT session (exclusive with epub)
    pn_epub_app_t *epub; ///< EPUB会话 / EPUB session
    bool active,presented; ///< 面板活动与呈现确认 / Panel active and presentation confirmed
    unsigned current; ///< 打开时的位置：百分比0..100 / Position at opening: percent 0..100
    unsigned draft; ///< 草稿目标，范围同current / Draft target, same range as current
    unsigned maximum; ///< 草稿最大值100 / Largest draft, 100
    unsigned large; ///< 大步长 / Large step
    unsigned sections; ///< EPUB章节数，用于预览说明 / EPUB section count for the preview line
    const char *notice; ///< 静态反馈 / Static feedback
} pn_jump_ui_t;
/// 打开面板并呈现；失败时不改变阅读位置。/ Open and present the panel; a failure leaves the reading position unchanged.
pn_status_t pn_jump_ui_open(pn_jump_ui_t *ui,pn_reader_app_t *reader,pn_reader_present_fn present,void *ctx);
pn_status_t pn_jump_ui_open_epub(pn_jump_ui_t *ui,pn_epub_app_t *epub,pn_reader_present_fn present,void *ctx);
/// 命令：CANCEL/CONFIRM/RETRY或STEP+0..3。CONFIRM成功或CANCEL后面板关闭（active=false）。
/// Commands: CANCEL/CONFIRM/RETRY or STEP+0..3. The panel closes (active=false) after a successful CONFIRM or a CANCEL.
pn_status_t pn_jump_ui_event(pn_jump_ui_t *ui,int command,uint64_t now,pn_reader_present_fn present,void *ctx);
/// 重画面板。/ Repaint the panel.
pn_status_t pn_jump_ui_present(pn_jump_ui_t *ui,pn_reader_present_fn present,void *ctx);
/// 命中返回命令，空白-1。/ Hit test returning a command, or -1 for blank space.
int pn_jump_ui_hit(const pn_jump_ui_t *ui,int x,int y);
/// 离书/锁屏时清状态，不改阅读位置。/ Clear state when leaving the book or locking, without moving the reading position.
void pn_jump_ui_close(pn_jump_ui_t *ui);
