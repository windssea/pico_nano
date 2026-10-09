/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：七项排版草稿，应用/取消/正文预览共用控制器。
 * English: seven-field typesetting drafts with shared apply/cancel/body-preview control.
 * 冻结：调整值不直接写存储；未呈现确认的界面禁止应用。
 * Frozen: field changes never write storage directly; unpresented UI cannot apply.
 */
#pragma once
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#define PN_SUI_PREVIEW 1
#define PN_SUI_APPLY 2
#define PN_SUI_CANCEL 3
#define PN_SUI_FORM 4
#define PN_SUI_RETRY 5
#define PN_SUI_FONTS 6
#define PN_SUI_MORE 7 ///< 打开“边距与更多选项” / Open the margins and more options page
#define PN_SUI_BACK_MAIN 8 ///< 从更多页回到排版主页 / Back from the more page to the main typesetting page
#define PN_SUI_RESET 9 ///< 恢复默认草稿 / Restore the default draft
#define PN_SUI_PRESET 10 ///< 10..12为舒适/紧凑/大字 / 10..12 are comfortable/compact/large
#define PN_SUI_FIELD 16
#define PN_SUI_FIELDS 7
typedef struct {
    pn_epub_app_t *epub; ///< 可选EPUB会话，与reader互斥 / Optional EPUB session, exclusive with reader
    pn_reader_app_t *reader; ///< 借用会话 / Borrowed session
    pn_style_t draft; ///< 未保存配置 / Uncommitted configuration
    bool request_fonts,did_preview,application_failed; ///< 字体请求及预览/失败应用状态 / Font request, preview and failed-apply states
    bool active,preview,presented; ///< 活动、正文预览与呈现确认 / Active, body preview and presentation confirmation
    bool more; ///< 正显示“更多排版”页 / The more-options page is showing
    unsigned selected; ///< 键盘选中字段0..6 / Keyboard-selected field zero through six
    const char *notice; ///< 静态反馈 / Static feedback
} pn_style_ui_t;
/// 打开排版草稿，不修改设置。/ Open a typesetting draft without changing settings.
pn_status_t pn_style_ui_open(pn_style_ui_t *ui,pn_reader_app_t *reader,pn_reader_present_fn present,void *ctx);
/// FIELD+i*2减、FIELD+i*2+1加；其他为预览/应用/取消。
/// FIELD+i*2 decreases, FIELD+i*2+1 increases; other commands preview/apply/cancel.
pn_status_t pn_style_ui_event(pn_style_ui_t *ui,int command,uint64_t now,pn_reader_present_fn present,void *ctx);
/// 仅重画当前草稿，不自动应用。/ Repaint the draft only without automatic application.
pn_status_t pn_style_ui_present(pn_style_ui_t *ui,pn_reader_present_fn present,void *ctx);
/// 同源坐标命中，空白-1。/ Shared coordinate hits, or -1 for blanks.
int pn_style_ui_hit(const pn_style_ui_t *ui,int x,int y);
/// 离书/锁屏清暂存界面，配置提交仍由reader负责。
/// Clear temporary UI when leaving/locking; reader still owns configuration commits.
void pn_style_ui_close(pn_style_ui_t *ui);

/// EPUB使用同一中文字段、命中和草稿行为。/ EPUB uses the same Chinese fields, hits and draft behavior.
pn_status_t pn_style_ui_open_epub(pn_style_ui_t *,pn_epub_app_t *,pn_reader_present_fn,void *);
