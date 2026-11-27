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
#define PN_SUI_FIELDS 13 ///< 0字号…11背景、12书内图片 / 0 size ... 11 paper, 12 book images
#define PN_SUI_TOGGLE_BARS 44 ///< 切换隐藏上下栏（状态栏与页脚）/ Toggle hiding top/bottom bars (status bar and footer)
#define PN_SUI_MARGINS 45 ///< 打开正文与背景四周边距设置页 / Open four-side body and background margins page
#define PN_SUI_MARGINS_BACK 46 ///< 从边距设置页返回 / Return from the margins page
#define PN_SUI_MARGINS_RESET 47 ///< 重置正文与背景四周边距 / Reset body and background margins to defaults
#define PN_SUI_SAVE_GLOBAL 48 ///< 将当前草稿保存为全局默认排版（保留兼容）/ Save current draft as global default typesetting (legacy)
#define PN_SUI_USE_GLOBAL 49 ///< 清除本书单独配置并立即使用全局默认排版 / Clear per-book override and apply global default typesetting
#define PN_SUI_CHAPTER 50 ///< 打开“阅读线与章节”子页 / Open reading-guide and chapter heading sub-page
#define PN_SUI_CHAPTER_BACK 51 ///< 从“阅读线与章节”子页返回 / Return from reading-guide and chapter sub-page
#define PN_SUI_CHAPTER_RESET 52 ///< 重置阅读线扩展与章节/页脚选项 / Reset extended guide and chapter/footer options
#define PN_SUI_STATUS_BAR 53 ///< 打开共享可视化“状态栏设置”子页 / Open shared visual status-bar settings sub-page
#define PN_SUI_GUIDE_GLOBAL 54 ///< 半屏阅读线微调卡恢复全局阅读线参数 / Restore global reading-line fields in live half-sheet
#define PN_SUI_MARGINS_GLOBAL 55 ///< 边距子页恢复全局边距与上下栏参数 / Restore global margins and bar visibility in margins sub-page
#define PN_SUI_CHAPTER_GLOBAL 56 ///< 章节子页恢复全局章节标题与页脚参数 / Restore global chapter heading and footer fields in chapter sub-page
#define PN_SUI_BG_GLOBAL 57 ///< 阅读背景页恢复全局阅读背景 / Restore global reading background in background picker
#define PN_SUI_MARGIN_ADJ 112 ///< 112..127：8项四周边距减/加（0..3正文上下左右，4..7背景上下左右）/ 112..127: dec/inc for 8 margin controls (0..3 body TBLR, 4..7 paper TBLR)
#define PN_SUI_CHAPTER_ADJ 128 ///< 128..143：8项阅读线与章节/页脚减加 / 128..143: dec/inc for 8 guide/chapter/footer controls
#define PN_SUI_GUIDE_STYLE 144 ///< 144..147：半屏阅读线样式（关闭/虚线/点线/实线）/ 144..147: live half-sheet style (off/dash/dot/solid)
#define PN_SUI_GUIDE_ADJ 150 ///< 150..161：半屏阅读线微调（粗细/深浅/长度/间隔/偏移/跟随）/ 150..161: live half-sheet steppers (thick/shade/dash/gap/offset/follow)
#define PN_SUI_MARGIN_STEP 162 ///< 162..164：边距步进倍率（×1=4px / ×5=20px / ×10=40px）/ 162..164: margin step multiplier (x1 / x5 / x10)
#define PN_SUI_SBAR_ACTION 170 ///< 170..200：可视化状态栏设置页动作（170 + PN_SBAR_UI_*）/ 170..200: visual status-bar UI actions (170 + PN_SBAR_UI_*)
typedef struct {
    pn_epub_app_t *epub; ///< 可选EPUB会话，与reader互斥 / Optional EPUB session, exclusive with reader
    pn_reader_app_t *reader; ///< 借用会话 / Borrowed session
    pn_style_t draft; ///< 未保存配置 / Uncommitted configuration
    bool request_fonts,did_preview,application_failed; ///< 字体请求及预览/失败应用状态 / Font request, preview and failed-apply states
    bool active,preview,presented; ///< 活动、正文预览与呈现确认 / Active, body preview and presentation confirmation
    bool backgrounds; ///< 正显示背景选择页，仅修改草稿 / Background picker is showing, modifying the draft only
    bool margins; ///< 正显示正文与背景四周边距页，仅修改草稿 / Body and background margins page is showing, modifying the draft only
    bool margins_from_bg; ///< 从阅读背景页进入边距页，返回时回阅读背景 / Entered margins from the background picker, so Back returns there
    uint8_t margin_step_mult; ///< 边距步进倍率（0/1=×1即4px，5=×5即20px，10=×10即40px）/ Margin step multiplier (1=4px, 5=20px, 10=40px)
    bool chapter_page; ///< 正显示“阅读线与章节”详调页，仅修改草稿 / Reading-guide and chapter sub-page is showing, modifying the draft only
    bool guide_sheet; ///< 正显示半屏实时阅读线微调卡 / Live half-screen reading-line micro-editor is showing
    bool guide_restored_global; ///< 半屏阅读线内刚载入全局参数，保存时若整份草稿与全局一致则清除单书覆盖 / Global reading line loaded in sheet; if full draft matches global on Save, clear per-book override
    bool status_bar_page; ///< 正显示共享可视化状态栏设置页 / Shared visual status-bar settings sub-page is showing
    bool status_bar_changed; ///< 本次打开期间修改过本书状态栏，返回阅读时需重排视口 / Per-book status bar changed during this session; relayout viewport on return
    int sbar_pick_slot; ///< 状态栏正在编辑的插槽 0..9，-1 表示无弹窗 / Active status-bar slot picker (0..9) or -1
    pn_background_gallery_t gallery; ///< 图库所有权仅在选择页 / Gallery ownership lives only in the picker
    bool more; ///< 正显示“更多排版”页 / The more-options page is showing
    unsigned selected; ///< 键盘选中字段0..12 / Keyboard-selected field zero through twelve
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
/// 打开半屏阅读线实时微调卡（上半屏实时渲染当前正文）。/ Open live half-screen reading-line editor over the current reading page.
pn_status_t pn_style_ui_open_guide(pn_style_ui_t *ui,pn_reader_app_t *reader,uint64_t now,pn_reader_present_fn present,void *ctx);
/// EPUB版半屏阅读线实时微调卡。/ EPUB variant of the live half-screen reading-line editor.
pn_status_t pn_style_ui_open_guide_epub(pn_style_ui_t *ui,pn_epub_app_t *epub,uint64_t now,pn_reader_present_fn present,void *ctx);
