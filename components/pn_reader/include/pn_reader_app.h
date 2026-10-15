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
#include "pn_page_bitmap.h"
typedef struct {void *impl;} pn_reader_app_t; ///< 初始impl=NULL / Initially impl=NULL
/// 仅未显示对象，补齐字体身份并保留首屏排版候选；不绘制、显示或写记录。/ For a never-displayed object, finish font identity and retain first-page layout without rasterization, presentation or record writes.
pn_status_t pn_reader_app_prepare_unshown(pn_reader_app_t *);
/// 未显示首屏正文仍可绘制；不读取源或分配。/ Whether the unshown first body can still be painted, without source reads or allocation.
bool pn_reader_app_unshown_paint_pending(const pn_reader_app_t *);
/// 每步至多8节点，借原帧绘正文，保留reserve余量；失败停止可选绘制，前台仍可正常OPEN。/ Paint at most eight body nodes per step into the original frame, preserving reserve headroom; failure stops optional painting while normal OPEN remains available.
pn_status_t pn_reader_app_paint_unshown(pn_reader_app_t *,size_t budget,size_t reserve);
/// 接管前验证未显示对象的原书/字体源，PC额外重查外部变化。/ Verify a never-displayed object's book/font sources before handoff, including external host changes.
pn_status_t pn_reader_app_verify_unshown(pn_reader_app_t *);
/// 丢弃未显示对象及其自有首屏候选；其他设置事务仍拒绝，不保存阅读记录。/ Discard an unshown object and its owned first-page candidate; reject other settings transactions and save no reading records.
pn_status_t pn_reader_app_discard_unshown(pn_reader_app_t *);
/// 只接受已保存挂起会话，校验原书/字体源，不绘制或推进位置。/ Accept only a saved suspended session and verify original book/font sources without drawing or advancing position.
pn_status_t pn_reader_app_retained_ready(pn_reader_app_t *);
/// 验证选择的路径/介质就是当前会话的源，不读文件内容。/ Verify the selected path/media are this session's source, without content reads.
pn_status_t pn_reader_app_retained_source(const pn_reader_app_t *,pn_media_t *,const char *);
/// 仅释放已保存挂起会话，不重写进度/最近记录；失败候选返回BUSY。/ Release only a saved suspended session without rewriting progress/recents; uncommitted candidates return BUSY.
pn_status_t pn_reader_app_release_retained(pn_reader_app_t *);
/// 空闲预排下一页，不呈现/写进度；候选设置和睡眠时跳过。/ Idle next-page preparation without display/progress writes; skip draft settings and sleep.
pn_status_t pn_reader_app_prefetch(pn_reader_app_t *app);
/// 下一页仍有字形待预热，调用方可缩短空闲间隔。/ Next-page glyphs remain to warm, so callers may shorten idle gaps.
bool pn_reader_app_warm_pending(const pn_reader_app_t *app);
/// 面板扫描期间的一小步，为待确认页准备下一页；OK表示做了工作，EMPTY表示无事可做或应停止。
/// A small step during a panel scan preparing the page after the pending one; OK when work was done, EMPTY when idle or overlap should stop.
pn_status_t pn_reader_app_overlap(pn_reader_app_t *app);
/// 可选整页正文预绘制开关，诊断/欠载退路；不改变位置。/ Optional full-body prerender switch for diagnostics/underrun fallback, preserving location.
pn_status_t pn_reader_app_prerender(pn_reader_app_t *,bool);
/// 当前会话的预绘制统计，不读硬件。/ Current-session prerender stats without hardware reads.
pn_page_bitmap_stats_t pn_reader_app_prerender_stats(const pn_reader_app_t *);

typedef enum {
    PN_APP_OPEN=0, ///< 首次呈现/恢复 / First presentation or restore
    PN_APP_NEXT, ///< 下一页 / Next page
    PN_APP_PREVIOUS, ///< 上一页 / Previous page
    PN_APP_SMALLER, ///< 字号减2 / Reduce size by two
    PN_APP_LARGER, ///< 字号加2 / Increase size by two
    PN_APP_BEGINNING, ///< 书首 / Beginning
    PN_APP_TOOLS ///< 点正文中央打开工具栏；只由输入识别器产生，应用不处理 / Center tap opens the toolbar; produced only by the input recognizer and never handled by the apps
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
/// 保存后热挂起会话；预览/失败候选返回BUSY。/ Save then retain session; previews/failed candidates return BUSY.
pn_status_t pn_reader_app_suspend(pn_reader_app_t *,uint64_t);
/// 验证介质后呈现已确认正文，不解析、不推进位置。/ Validate media and redisplay confirmed body without parsing or advancing position.
pn_status_t pn_reader_app_resume(pn_reader_app_t *,uint64_t,pn_reader_present_fn,void *);
/// 已显示位置或EMPTY；不返回待显示页。
/// Displayed location or EMPTY, never the prepared page.
pn_status_t pn_reader_app_progress(const pn_reader_app_t *app,pn_txt_progress_t *progress);
/// 返回已确认页的展示进度（0..10000），末页为10000；不改变用于续读的页首位置。
/// Get display progress for the confirmed page (0..10000), with 10000 on the last page; preserve the page-start resume location.
pn_status_t pn_reader_app_percent(const pn_reader_app_t *app,unsigned *basis_points);

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
/// 常驻正文叠层；paint只绘制，complete仅在正文推屏完成后通知，资源由控制层拥有。
/// Persistent body decoration; paint only draws, complete reports body presentation, and the controller owns resources.
typedef struct {
    void *ctx; ///< 叠层状态 / Decoration state
    pn_status_t (*paint)(void *,pn_frame_t *); ///< 正文绘制后调用 / Called after body drawing
    void (*complete)(void *,const pn_frame_t *,bool); ///< 成功呈现才保存锁屏底图 / Save the lock underlay only after successful presentation
} pn_reader_body_layer_t;
/// 绑定/清除正文叠层；不影响工具页，不取得资源所有权。/ Bind/clear a body decoration without affecting tool pages or taking ownership.
pn_status_t pn_reader_app_body_layer(pn_reader_app_t *app,const pn_reader_body_layer_t *layer);
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
/// 候选字体直接呈现在目标设置页，确认后保存；不呈现中间正文帧。/ Present candidate fonts directly in the destination settings page and save after confirmation, without an intermediate body frame.
pn_status_t pn_reader_app_font_apply_view(pn_reader_app_t *,const pn_font_preferences_t *,pn_reader_overlay_fn,void *,uint64_t,pn_reader_present_fn,void *);
/// 恢复字体后直接呈现目标设置页，确认/失败屏障保持。/ Restore fonts directly into the destination settings page, retaining confirmation/failure barriers.
pn_status_t pn_reader_app_font_cancel_view(pn_reader_app_t *,pn_reader_overlay_fn,void *,uint64_t,pn_reader_present_fn,void *);


/// 当前字体组及默认目录，错误不改输出。/ Current font set and default directory, preserving outputs on error.
pn_status_t pn_reader_app_fonts_get(pn_reader_app_t *,pn_font_preferences_t *,char *directory,size_t capacity);
/// 借用书源租约读取字体专用分页。/ Read font-only pages under the borrowed book-source lease.
pn_status_t pn_reader_app_fonts_page(pn_reader_app_t *,const char *,const char *,bool reverse,pn_catalog_page_t *);
/// 校验候选字体，返回实际引用、名称与默认样例缺字统计。/ Verify a candidate and return actual reference, names and default-sample missing counts.
pn_status_t pn_reader_app_fonts_probe(pn_reader_app_t *,const pn_catalog_item_t *,pn_font_reference_t *,pn_font_info_t *,unsigned *,unsigned *);

/// 设置全局默认，不改活动字体或逐书覆盖；写失败由重试/关闭屏障处理。
/// Set global defaults without changing active fonts or per-book overrides; retry/close handles write failures.
pn_status_t pn_reader_app_font_default(pn_reader_app_t *,const pn_font_preferences_t *);
/// 在原文锚点应用已有全局默认，显示成功后保存继承标记；未设全局使用内置字体。
/// Apply existing global defaults at the original anchor and save inheritance after presentation; absent global uses the resident font.
pn_status_t pn_reader_app_font_inherit(pn_reader_app_t *,uint64_t,pn_reader_present_fn,void *);

/// 当前正文字体（借用，不可关闭；字体已挂起时engine为空）。排版预览用它按草稿字号试排。
/// The current body font (borrowed, never close it; its engine is empty while fonts are suspended). The typesetting preview uses it to sample the draft size.
pn_font_t *pn_reader_app_body_font(const pn_reader_app_t *app);

/// 跳到全书basis_points/10000处（按原文件字节比例，落点由阅读层对齐到字符边界）；成功确认后可用“返回跳转前位置”。
/// Jump to basis_points/10000 of the book (by original-file byte share; the reading layer aligns to a character boundary); "return to the position before the jump" works after a confirmed jump.
pn_status_t pn_reader_app_jump_percent(pn_reader_app_t *app,unsigned basis_points,uint64_t now_ms,pn_reader_present_fn present,void *ctx);


/// 接入全局刷新周期，保留已计页数；不写逐书排版。/ Apply global refresh period, retaining counted turns without writing per-book style.
void pn_reader_app_refresh_period(pn_reader_app_t *app,unsigned pages);
/// 显示owner确认整屏清理后，由产品owner重置计数。/ Product owner resets the counter after display owner confirms full cleaning.
void pn_reader_app_refresh_reset(pn_reader_app_t *app);

/// 跟随全局并呈现目标设置页；无全局记录使用内置字体。/ Inherit global defaults and present the destination; absent defaults use the resident face.
pn_status_t pn_reader_app_font_inherit_view(pn_reader_app_t *,pn_reader_overlay_fn,void *,uint64_t,pn_reader_present_fn,void *);
/// 当前接受的字体是否跟随全局；预览不改变它。/ Whether the accepted font follows global defaults; previews do not change it.
bool pn_reader_app_font_is_inherited(const pn_reader_app_t *);
