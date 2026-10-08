/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源TXT应用控制器，连接文件、字体、显示确认与保存。
 * English: shared TXT application controller connecting files, fonts, presentation confirmation and saving.
 * 冻结：一个owner；不格式化，不执行硬件电源操作；关闭失败保留会话。
 * Frozen: one owner; no formatting or hardware power operations; retain session on close failure.
 */
#pragma once
#include "pn_font_preferences.h"
#include "pn_catalog.h"
#include "pn_reader.h"
#include "pn_display.h"
#include "pn_font.h"
#include "pn_text_file.h"
#include "pn_bookmarks.h"
#include "pn_recent.h"
#include "pn_style.h"
typedef struct {void *impl;} pn_reader_app_t; ///< 初始impl=NULL / Initially impl=NULL

typedef enum {
    PN_APP_OPEN=0, ///< 首次呈现/恢复 / First presentation or restore
    PN_APP_NEXT, ///< 下一页 / Next page
    PN_APP_PREVIOUS, ///< 上一页 / Previous page
    PN_APP_SMALLER, ///< 字号减2 / Reduce size by two
    PN_APP_LARGER, ///< 字号加2 / Increase size by two
    PN_APP_BEGINNING ///< 书首 / Beginning
} pn_reader_action_t;
typedef pn_status_t (*pn_reader_present_fn)(void *,const pn_frame_t *,pn_refresh_t);
/// state_dir可空禁用持久化；非空只创建此目录、按内容SHA独立A/B文件。
/// Null state_dir disables persistence; otherwise create only this directory and per-content-SHA A/B files.
pn_status_t pn_reader_app_open(pn_reader_app_t *app,pn_pool_t *pool,const char *book_path,
    const char *font_path,const char *state_dir,int pixels,uint64_t now_ms);
/// 调用真实推屏/窗口呈现回调；只有回调成功和显示确认成功才提交visible/save。
/// Call actual panel/window presentation callback; commit visible/save only after callback and display completion succeed.
pn_status_t pn_reader_app_step(pn_reader_app_t *app,pn_reader_action_t action,uint64_t now_ms,
    pn_reader_present_fn present,void *ctx);
/// 定期检查保存窗口；并非带deadline的异步存储服务。
/// Periodically check save windows; not an asynchronous storage service with deadlines.
pn_status_t pn_reader_app_tick(pn_reader_app_t *app,uint64_t now_ms);
/// 先保存屏障，保存失败不释放；成功后清理源，关闭I/O错误可在释放后报告。
/// Save barrier first, retain on save failure; clean sources on success, reporting close I/O errors after release.
pn_status_t pn_reader_app_close(pn_reader_app_t *app,uint64_t now_ms);
/// 已显示位置或EMPTY；不返回待显示页。
/// Displayed location or EMPTY, never the prepared page.
pn_status_t pn_reader_app_progress(const pn_reader_app_t *app,pn_txt_progress_t *progress);

/// 设备路径借用已确认挂载的共享media，不初始化/重挂载它；源与传输共同仲裁。
/// Device path borrows a confirmed-mounted shared media without initializing/remounting it; arbitrate with transfers.
pn_status_t pn_reader_app_open_on_media(pn_reader_app_t *app,pn_pool_t *pool,pn_media_t *media,
    const char *book_path,const char *font_path,const char *state_dir,int pixels,uint64_t now_ms);
/// owner检测拔卡后立即失效源和pending；不画屏、不挂载、不丢最后已显示位置。
/// Expire source and pending work after owner detects media loss; no paint/mount or loss of last displayed position.
pn_status_t pn_reader_app_media_lost(pn_reader_app_t *app);

/// 使用已显示页首添加持久书签；无state-dir返回UNSUPPORTED。
/// Add a persistent bookmark at the displayed page start; UNSUPPORTED without state-dir.
pn_status_t pn_reader_app_bookmark_add(pn_reader_app_t *app,const char *label,uint64_t *id);
/// 同源分页书签列表，错误count=0。/ Shared bookmark pages; errors set count=0.
pn_status_t pn_reader_app_bookmark_list(pn_reader_app_t *app,uint64_t after,pn_txt_bookmark_t *items,size_t capacity,size_t *count,bool *more);
/// 改名不改变当前位置。/ Rename without changing the reading location.
pn_status_t pn_reader_app_bookmark_rename(pn_reader_app_t *app,uint64_t id,const char *label);
/// caller确认后删除，进度不变。/ Delete after caller confirmation, preserving progress.
pn_status_t pn_reader_app_bookmark_delete(pn_reader_app_t *app,uint64_t id);
/// 跳转呈现成功才改当前位置，并保留返回锚点。
/// Change location only after successful jump presentation, keeping a return anchor.
pn_status_t pn_reader_app_bookmark_jump(pn_reader_app_t *app,uint64_t id,uint64_t now_ms,pn_reader_present_fn present,void *ctx);
/// 返回跳转前锚点，成功呈现才消费返回点。
/// Return to the pre-jump anchor, consuming it only after successful presentation.
pn_status_t pn_reader_app_bookmark_return(pn_reader_app_t *app,uint64_t now_ms,pn_reader_present_fn present,void *ctx);

typedef pn_status_t (*pn_reader_overlay_fn)(void *,pn_font_t *,pn_font_t *,pn_frame_t *);
/// paint借用常驻UI字体、可空36px正文回退字体和帧；纯绘制，不提交阅读位置。
/// paint borrows resident UI font, nullable 36px body fallback and frame; paint only, without location commits.
/// 仅GC16/GL16；之后正文强刷。/ GC16/GL16 only; force full reader refresh afterwards.
pn_status_t pn_reader_app_overlay(pn_reader_app_t *app,pn_reader_overlay_fn paint,void *paint_ctx,pn_reader_present_fn present,void *ctx,pn_refresh_t profile);
/// 上一次导航是否确认（保存错误可能在确认之后），用于界面切换。
/// Whether the last navigation was confirmed (saving may fail afterwards), for UI transitions.
bool pn_reader_app_last_confirmed(const pn_reader_app_t *app);
/// 当前会话是否有书签跳转返回点。/ Whether this session has a bookmark return anchor.
bool pn_reader_app_bookmark_can_return(const pn_reader_app_t *app);
/// 未显示时也可核对内容身份；错误不改输出。
/// Check content identity before presentation too; errors preserve output.
pn_status_t pn_reader_app_identity(const pn_reader_app_t *app,pn_book_id_t *book);
/// 打开时所选字体缺失/被替换而暂用启动默认字体；记录未改。/ The saved font was missing or replaced at open, so the startup default is in use; the record is unchanged.
bool pn_reader_app_font_unavailable(const pn_reader_app_t *app);
/// 最近历史记录错误，不替代进度保存状态。
/// Recent-history recording error, distinct from progress-save status.
pn_status_t pn_reader_app_recent_status(const pn_reader_app_t *app);
/// 当前可见排版值；不写存储。/ Current visual style without storage writes.
pn_status_t pn_reader_app_style_get(const pn_reader_app_t *app,pn_style_t *style);
/// 草稿重排并呈现，保留进入时的源锚点，不保存配置。
/// Reflow and present a draft at the entry source anchor without saving configuration.
pn_status_t pn_reader_app_style_preview(pn_reader_app_t *app,const pn_style_t *style,uint64_t now,pn_reader_present_fn present,void *ctx);
/// 成功呈现后同步保存；失败保留应用意图供重试。
/// Save synchronously after successful presentation; retain apply intent on failure for retry.
pn_status_t pn_reader_app_style_apply(pn_reader_app_t *app,const pn_style_t *style,uint64_t now,pn_reader_present_fn present,void *ctx);
/// 取消未提交草稿，恢复原值和源锚点；不擦损坏配置。
/// Cancel uncommitted drafts and restore style/source anchor without erasing corrupt configuration.
pn_status_t pn_reader_app_style_cancel(pn_reader_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx);

/// 首次显示前绑定可选备用TTF，先校验；已有显示或备用返回BUSY。/ Bind optional fallback TTF before first display, validating first; BUSY after display or existing fallback.
pn_status_t pn_reader_app_fallback_font(pn_reader_app_t *,const char *);

/// 原文锚点预览完整字体选择，不写记录。/ Preview concrete font preferences at original anchor without writing records.
pn_status_t pn_reader_app_font_preview(pn_reader_app_t *,const pn_font_preferences_t *,uint64_t,pn_reader_present_fn,void *);
/// 原文确认显示后逐书保存，失败保留待提交意图。/ Save per-book after actual anchored presentation, retaining pending intent on failure.
pn_status_t pn_reader_app_font_apply(pn_reader_app_t *,const pn_font_preferences_t *,uint64_t,pn_reader_present_fn,void *);
/// 恢复旧字体及原锚点；失败不丢候选或待提交请求。/ Restore accepted fonts/original anchor, retaining candidate/pending request on failure.
pn_status_t pn_reader_app_font_cancel(pn_reader_app_t *,uint64_t,pn_reader_present_fn,void *);

/// 当前字体组及默认目录，错误不改输出。/ Current font set and default directory, preserving outputs on error.
pn_status_t pn_reader_app_fonts_get(pn_reader_app_t *,pn_font_preferences_t *,char *directory,size_t capacity);
/// 借用书源租约读取字体专用分页。/ Read font-only pages under the borrowed book-source lease.
pn_status_t pn_reader_app_fonts_page(pn_reader_app_t *,const char *,const char *,bool reverse,pn_catalog_page_t *);
/// 校验候选字体，返回实际引用、名称与默认样例缺字统计。/ Verify a candidate and return actual reference, names and default-sample missing counts.
pn_status_t pn_reader_app_fonts_probe(pn_reader_app_t *,const pn_catalog_item_t *,pn_font_reference_t *,pn_font_info_t *,unsigned *,unsigned *);

/// 设置全局默认，不改活动字体或逐书覆盖；写失败由重试/关闭屏障处理。
/// Set global defaults without changing active fonts or per-book overrides; retry/close handles write failures.
pn_status_t pn_reader_app_font_default(pn_reader_app_t *,const pn_font_preferences_t *);
/// 在原文锚点应用已有全局默认，显示成功后保存继承标记；未设全局返回EMPTY。
/// Apply existing global defaults at the original anchor and save inheritance after presentation; absent global returns EMPTY.
pn_status_t pn_reader_app_font_inherit(pn_reader_app_t *,uint64_t,pn_reader_present_fn,void *);
