/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：排版与阅读线草稿，应用/取消/正文预览共用控制器。
 * English: typesetting and reading-guide drafts with shared apply/cancel/body-preview control.
 * 冻结：调整值不直接写存储；未呈现确认的界面禁止应用。
 * Frozen: field changes never write storage directly; unpresented UI cannot apply.
 */
#pragma once
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include "pn_background_gallery.h"
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
#define PN_SUI_BACKGROUND 13 ///< 打开阅读背景图库 / Open the reading background gallery
#define PN_SUI_BACKGROUND_BACK 15 ///< 返回排版草稿 / Return to the typesetting draft
#define PN_SUI_BACKGROUND_ITEM 64 ///< 命令保留持久ID：64无、68竹影、71纯色中 / Commands retain persistent IDs: None 64, bamboo 68, paper 71
#define PN_SUI_BACKGROUND_PREVIOUS 76 ///< 外部背景上一页 / Previous external paper page
#define PN_SUI_BACKGROUND_NEXT 77 ///< 外部背景下一页 / Next external paper page
#define PN_SUI_BACKGROUND_EXTERNAL 96 ///< 96..101为当前六张外部背景 / 96..101 select the six current external papers
#define PN_SUI_INDENT 14 ///< 切换首行缩进（0或2字）/ Toggle the first-line indent (0 or 2 characters)
#define PN_SUI_FIELD 16
#define PN_SUI_FIELDS 12 ///< 0字号…10清晰度、11背景 / 0 size ... 10 sharpness, 11 paper
typedef struct {
    pn_epub_app_t *epub; ///< 可选EPUB会话，与reader互斥 / Optional EPUB session, exclusive with reader
    pn_reader_app_t *reader; ///< 借用会话 / Borrowed session
    pn_style_t draft; ///< 未保存配置 / Uncommitted configuration
    bool request_fonts,did_preview,application_failed; ///< 字体请求及预览/失败应用状态 / Font request, preview and failed-apply states
    bool active,preview,presented; ///< 活动、正文预览与呈现确认 / Active, body preview and presentation confirmation
    bool backgrounds; ///< 正显示背景选择页，仅修改草稿 / Background picker is showing, modifying the draft only
    pn_background_gallery_t gallery; ///< 图库所有权仅在选择页 / Gallery ownership lives only in the picker
    bool more; ///< 正显示“更多排版”页 / The more-options page is showing
    unsigned selected; ///< 键盘选中字段0..11 / Keyboard-selected field zero through eleven
    const char *notice; ///< 静态反馈 / Static feedback
} pn_style_ui_t;
/// 打开排版草稿，不修改设置。/ Open a typesetting draft without changing settings.
pn_status_t pn_style_ui_open(pn_style_ui_t *ui,pn_reader_app_t *reader,pn_reader_present_fn present,void *ctx);
/// FIELD+i*2减、FIELD+i*2+1加；其他为预览/应用/取消。
/// FIELD+i*2 decreases, FIELD+i*2+1 increases; other commands preview/apply/cancel.
pn_status_t pn_style_ui_event(pn_style_ui_t *ui,int command,uint64_t now,pn_reader_present_fn present,void *ctx);
/// 仅重画当前草稿，不自动应用。/ Repaint the draft only without automatic application.
pn_status_t pn_style_ui_present(pn_style_ui_t *ui,pn_reader_present_fn present,void *ctx);
/// 只绘制当前草稿，供字体事务合成最终目标页；不推屏、不写配置。/ Paint the current draft for a font transaction destination, without presenting or writing settings.
pn_status_t pn_style_ui_render(void *,pn_font_t *,pn_font_t *,pn_frame_t *);
/// 同源坐标命中，空白-1。/ Shared coordinate hits, or -1 for blanks.
int pn_style_ui_hit(const pn_style_ui_t *ui,int x,int y);
/// 离书/锁屏清暂存界面，配置提交仍由reader负责。
/// Clear temporary UI when leaving/locking; reader still owns configuration commits.
void pn_style_ui_close(pn_style_ui_t *ui);
/// owner无输入空闲时推进缩略图，绘制不读文件。/ Advance thumbnails during owner input idle; rendering never reads files.
pn_status_t pn_style_ui_tick(pn_style_ui_t *,pn_reader_present_fn,void *);

/// EPUB使用同一中文字段、命中和草稿行为。/ EPUB uses the same Chinese fields, hits and draft behavior.
pn_status_t pn_style_ui_open_epub(pn_style_ui_t *,pn_epub_app_t *,pn_reader_present_fn,void *);
