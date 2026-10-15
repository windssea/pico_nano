/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB应用装配，连接字体/图片/显示确认/语义保存。
 * English: EPUB application wiring for fonts/images/presentation/semantic saving.
 * 冻结：render只绘制，未呈现不保存，关闭失败保留会话，不格式化。
 * Frozen: paint only in rendering; no save before presentation; retain on close failure; never format.
 */
#pragma once
#include "pn_font_preferences.h"
#include "pn_catalog.h"
#include "pn_epub_reader.h"
#include "pn_page_bitmap.h"
#include "pn_epub_save.h"
#include "pn_epub_bookmarks.h"
#include "pn_epub_image.h"
#include "pn_toc.h"
#include "pn_reader_app.h"
typedef struct {void *impl;} pn_epub_app_t; ///< 初始NULL，不复制活动对象 / Initially NULL; never copy a live object
/// 验证已保存挂起会话的原书/字体源，不绘制或推进位置。/ Verify original book/font sources of a saved suspended session without drawing/position commits.
pn_status_t pn_epub_app_retained_ready(pn_epub_app_t *);
/// 验证选择的路径/介质就是当前会话的源，不读文件内容。/ Verify the selected path/media are this session's source, without content reads.
pn_status_t pn_epub_app_retained_source(const pn_epub_app_t *,pn_media_t *,const char *);
/// 仅释放已保存挂起会话，不重写进度/最近记录；失败候选返回BUSY。/ Release only a saved suspended session without rewriting progress/recents; uncommitted candidates return BUSY.
pn_status_t pn_epub_app_release_retained(pn_epub_app_t *);
/// 空闲预排下一页，不呈现或提交位置；候选设置/睡眠跳过。/ Idle next-page preparation without display/position commits; skip draft settings and sleep.
pn_status_t pn_epub_app_prefetch(pn_epub_app_t *app);
/// 正文安静1.5秒后接管已验证包，后续每次推进一个文件步骤；BUSY未提交、OK完成，错误不影响阅读/进度。菜单、按压或扫描期不得调用。
/// Take a verified packet after 1.5 quiet seconds, then advance one file step per call; BUSY is uncommitted, OK complete, errors leave reading/progress intact. Never call during menus, presses or scanning.
pn_status_t pn_epub_app_cache_idle(pn_epub_app_t *,uint64_t);
/// 新输入先归还可选写入包并重新等待1.5秒安静期，不提交缓存或改变阅读位置。/ New input frees optional staging and restarts the 1.5-second quiet delay, without committing or changing position.
void pn_epub_app_cache_activity(pn_epub_app_t *,uint64_t);
/// 只查询未提交包，用于空闲调度；菜单/按压期间仍不可推进。/ Query uncommitted staging for idle scheduling only; menus/presses must still pause it.
bool pn_epub_app_cache_pending(const pn_epub_app_t *);
/// 下一页仍有字形待预热，调用方可缩短空闲间隔。/ Next-page glyphs remain to warm, so callers may shorten idle gaps.
bool pn_epub_app_warm_pending(const pn_epub_app_t *app);
/// 面板扫描（呈现回调阻塞）期间调用的一小步，为待确认页准备下一页；OK表示做了工作，EMPTY表示无事可做或应停止。
/// A small step called during a panel scan (while the present callback blocks) preparing the page after the pending one; OK when work was done, EMPTY when idle or overlap should stop.
pn_status_t pn_epub_app_overlap(pn_epub_app_t *app);
/// 可选整页正文预绘制开关，不改变位置或排版。/ Optional full-body prerender switch without changing location or layout.
pn_status_t pn_epub_app_prerender(pn_epub_app_t *,bool);
/// 管理覆盖页准入前释放可选下一页像素，保留原预绘制启用策略与阅读位置。/ Release optional next-page pixels for management-overlay admission, preserving prerender policy and reading position.
pn_status_t pn_epub_app_trim_bitmap(pn_epub_app_t *);
/// 当前会话预绘制统计，不读硬件。/ Current-session prerender stats without hardware reads.
pn_page_bitmap_stats_t pn_epub_app_prerender_stats(const pn_epub_app_t *);
/// 绑定/清除同源正文叠层，不影响工具页。/ Bind/clear the shared body decoration without affecting tool pages.
pn_status_t pn_epub_app_body_layer(pn_epub_app_t *app,const pn_reader_body_layer_t *layer);
/// state可NULL；本地media或借用已挂载media，不挂载/格式化。/ Optional state; local or borrowed mounted media, never mounted/formatted here.
pn_status_t pn_epub_app_open_on_media(pn_epub_app_t *,pn_pool_t *,pn_media_t *,const char *book,const char *font,const char *state,int pixels,uint64_t);
/// 本地/PC便捷打开，仍同源核心。/ Local/PC convenience open using the shared core.
pn_status_t pn_epub_app_open(pn_epub_app_t *,pn_pool_t *,const char *,const char *,const char *,int,uint64_t);
/// 真实完成字体/图片/显示回调才提交语义位置；保存错误可能在显示后。/ Commit only after fonts/images/presentation; saving may fail after display.
pn_status_t pn_epub_app_step(pn_epub_app_t *,pn_reader_action_t,uint64_t,pn_reader_present_fn,void *);
/// 定期保存检查，非异步deadline服务。/ Periodic saving, not an asynchronous deadline service.
pn_status_t pn_epub_app_tick(pn_epub_app_t *,uint64_t);
/// 保存屏障成功后释放，失败不关闭会话。/ Release only after successful save barriers, retaining the session on failure.
pn_status_t pn_epub_app_close(pn_epub_app_t *,uint64_t);
/// 保存后热挂起会话；预览/失败候选返回BUSY。/ Save then retain session; previews/failed candidates return BUSY.
pn_status_t pn_epub_app_suspend(pn_epub_app_t *,uint64_t);
/// 验证介质后呈现已确认正文，不解析、不推进位置。/ Validate media and redisplay confirmed body without parsing or advancing position.
pn_status_t pn_epub_app_resume(pn_epub_app_t *,uint64_t,pn_reader_present_fn,void *);
/// 返回最后真实显示的位置，拔卡后仍保留。/ Return the last displayed location, retained after media loss.
pn_status_t pn_epub_app_progress(const pn_epub_app_t *,pn_epub_progress_t *);
/// 失效源/pending而不画屏、重挂载或丢已显示位置。/ Expire source/pending without drawing, remounting or discarding displayed position.
pn_status_t pn_epub_app_media_lost(pn_epub_app_t *);
/// 是否本次导航实际确认，保存错误不回滚屏幕。/ Whether navigation actually confirmed; save errors never roll back the screen.
bool pn_epub_app_last_confirmed(const pn_epub_app_t *);
/// 逐书当前配置。/ Current per-book configuration.
pn_status_t pn_epub_app_style_get(const pn_epub_app_t *,pn_style_t *);
/// 实际原锚点重排/显示后保存逐书配置；失败保留会话。/ Reflow/present at the original anchor then persist per-book style; retain on failure.
pn_status_t pn_epub_app_style_apply(pn_epub_app_t *,const pn_style_t *,uint64_t,pn_reader_present_fn,void *);

/// 语义位置跳转，仍要求实际显示成功才确认。/ Semantic jump requiring actual presentation before confirmation.
pn_status_t pn_epub_app_jump(pn_epub_app_t *,const pn_epub_location_t *,uint64_t,pn_reader_present_fn,void *);
/// 懒加载已完整验证的目录，错误输出不变。/ Lazily load fully verified TOC, preserving output on errors.
pn_status_t pn_epub_app_toc_count(pn_epub_app_t *,size_t *);
/// 复制目录条目，不改变位置。/ Copy a TOC entry without changing location.
pn_status_t pn_epub_app_toc_get(pn_epub_app_t *,size_t,pn_toc_entry_t *);
/// 解析目录id/章节，实际推屏确认后跳转；分组EMPTY。/ Resolve TOC ID/chapter and jump after presentation; groups return EMPTY.
pn_status_t pn_epub_app_toc_jump(pn_epub_app_t *,size_t,uint64_t,pn_reader_present_fn,void *);

/// 原字节内容身份；错误输出不变。/ Original-byte content identity, preserving output on errors.
pn_status_t pn_epub_app_identity(const pn_epub_app_t *,pn_book_id_t *);
/// 打开时所选字体缺失/被替换而暂用启动默认字体；记录未改。/ The saved font was missing or replaced at open, so the startup default is in use; the record is unchanged.
bool pn_epub_app_font_unavailable(const pn_epub_app_t *);

/// 纯绘制覆盖页，通过显示owner推屏，不提交原文位置，之后正文强刷。
/// Paint-only overlay presented through display owner without text-position commits; force full next body refresh.
pn_status_t pn_epub_app_overlay(pn_epub_app_t *,pn_reader_overlay_fn,void *,pn_reader_present_fn,void *,pn_refresh_t);

/// 原锚点草稿预览，不写PNTS；只有实际显示后进入预览。/ Preview drafts at original anchor without PNTS writes, entering preview only after actual display.
pn_status_t pn_epub_app_style_preview(pn_epub_app_t *,const pn_style_t *,uint64_t,pn_reader_present_fn,void *);
/// 恢复保存配置和原锚点，失败不关闭草稿。/ Restore saved style and original anchor, retaining draft on failure.
pn_status_t pn_epub_app_style_cancel(pn_epub_app_t *,uint64_t,pn_reader_present_fn,void *);

/// 添加最后已确认原文位置，未显示返回EMPTY。/ Add last confirmed original location; EMPTY before display.
pn_status_t pn_epub_app_bookmark_add(pn_epub_app_t *,const char *,uint64_t *);
/// 六条或其他有界容量的ID分页，不改变位置。/ Bounded ID pages without changing location.
pn_status_t pn_epub_app_bookmark_list(pn_epub_app_t *,uint64_t,pn_epub_bookmark_t *,size_t,size_t *,bool *);
/// 修改名称，原位置不变。/ Rename without moving.
pn_status_t pn_epub_app_bookmark_rename(pn_epub_app_t *,uint64_t,const char *);
/// 删除留墓碑，不修改阅读位置。/ Delete with tombstone without changing reading position.
pn_status_t pn_epub_app_bookmark_delete(pn_epub_app_t *,uint64_t);
/// 实际显示成功后建立返回点，保存错误仍按实际显示判断。/ Establish return point after actual display, including post-display save errors.
pn_status_t pn_epub_app_bookmark_jump(pn_epub_app_t *,uint64_t,uint64_t,pn_reader_present_fn,void *);
/// 返回失败保留返回点供重试。/ Retain return point when returning fails, for retry.
pn_status_t pn_epub_app_bookmark_return(pn_epub_app_t *,uint64_t,pn_reader_present_fn,void *);
/// 是否有本会话跳转前位置。/ Whether this session has a pre-jump location.
bool pn_epub_app_bookmark_can_return(const pn_epub_app_t *);

/// 正文顶部命中：8返架、10书签、11返回、12排版、13目录；空白-1。/ Body header hits: 8 shelf, 10 bookmarks, 11 return, 12 styles, 13 TOC; -1 blank.
int pn_epub_app_header_hit(const pn_epub_app_t *,int,int);

/// 首次显示前绑定可选备用TTF，先校验；已有显示或备用返回BUSY。/ Bind optional fallback TTF before first display, validating first; BUSY after display or existing fallback.
pn_status_t pn_epub_app_fallback_font(pn_epub_app_t *,const char *);

/// 原文锚点预览完整字体选择，不写记录。/ Preview concrete font preferences at original anchor without writing records.
pn_status_t pn_epub_app_font_preview(pn_epub_app_t *,const pn_font_preferences_t *,uint64_t,pn_reader_present_fn,void *);
/// 原文确认显示后逐书保存，失败保留待提交意图。/ Save per-book after actual anchored presentation, retaining pending intent on failure.
pn_status_t pn_epub_app_font_apply(pn_epub_app_t *,const pn_font_preferences_t *,uint64_t,pn_reader_present_fn,void *);
/// 恢复旧字体及原锚点；失败不丢候选或待提交请求。/ Restore accepted fonts/original anchor, retaining candidate/pending request on failure.
pn_status_t pn_epub_app_font_cancel(pn_epub_app_t *,uint64_t,pn_reader_present_fn,void *);
/// 候选字体直接呈现在目标设置页，确认后保存；不呈现中间正文帧。/ Present candidate fonts directly in the destination settings page and save after confirmation, without an intermediate body frame.
pn_status_t pn_epub_app_font_apply_view(pn_epub_app_t *,const pn_font_preferences_t *,pn_reader_overlay_fn,void *,uint64_t,pn_reader_present_fn,void *);
/// 恢复字体后直接呈现目标设置页，确认/失败屏障保持。/ Restore fonts directly into the destination settings page, retaining confirmation/failure barriers.
pn_status_t pn_epub_app_font_cancel_view(pn_epub_app_t *,pn_reader_overlay_fn,void *,uint64_t,pn_reader_present_fn,void *);


/// 当前字体组及默认目录，错误不改输出。/ Current font set and default directory, preserving outputs on error.
pn_status_t pn_epub_app_fonts_get(pn_epub_app_t *,pn_font_preferences_t *,char *directory,size_t capacity);
/// 借用书源租约读取字体专用分页。/ Read font-only pages under the borrowed book-source lease.
pn_status_t pn_epub_app_fonts_page(pn_epub_app_t *,const char *,const char *,bool reverse,pn_catalog_page_t *);
/// 校验候选字体，返回实际引用、名称与默认样例缺字统计。/ Verify a candidate and return actual reference, names and default-sample missing counts.
pn_status_t pn_epub_app_fonts_probe(pn_epub_app_t *,const pn_catalog_item_t *,pn_font_reference_t *,pn_font_info_t *,unsigned *,unsigned *);

/// 设置全局默认，不改活动字体或逐书覆盖；写失败由重试/关闭屏障处理。
/// Set global defaults without changing active fonts or per-book overrides; retry/close handles write failures.
pn_status_t pn_epub_app_font_default(pn_epub_app_t *,const pn_font_preferences_t *);
/// 在原文锚点应用已有全局默认，显示成功后保存继承标记；未设全局使用内置字体。
/// Apply existing global defaults at the original anchor and save inheritance after presentation; absent global uses the resident font.
pn_status_t pn_epub_app_font_inherit(pn_epub_app_t *,uint64_t,pn_reader_present_fn,void *);

/// 当前正文字体（借用，不可关闭；字体已挂起时engine为空）。排版预览用它按草稿字号试排。
/// The current body font (borrowed, never close it; its engine is empty while fonts are suspended). The typesetting preview uses it to sample the draft size.
pn_font_t *pn_epub_app_body_font(const pn_epub_app_t *app);

/// 章节（spine）数量与当前显示所在章节序号（从0起）；尚无显示位置返回PN_EMPTY。
/// Section (spine) count and the zero-based section of the displayed page; PN_EMPTY before anything is displayed.
pn_status_t pn_epub_app_section_info(pn_epub_app_t *,size_t *count,size_t *current);
/// 跳到第index章开头（从0起）；成功确认后可用“返回跳转前位置”。/ Jump to the start of section index (zero-based); "return to the position before the jump" works after a confirmed jump.
pn_status_t pn_epub_app_section_jump(pn_epub_app_t *,size_t index,uint64_t,pn_reader_present_fn,void *);
/// 资源路径所在章节（从0起）与章节总数；不是章节资源返回PN_EMPTY。/ Zero-based section of a resource path and the section count; PN_EMPTY when the path is not a section.
pn_status_t pn_epub_app_section_of(pn_epub_app_t *,const char *path,size_t *index,size_t *count);
/// 当前位置的章节进度（0–10000，按章节起点估算）、所在章节与章节数。/ Section-based progress of the current position (0–10000, estimated at chapter starts), its section and the section count.
pn_status_t pn_epub_app_percent(pn_epub_app_t *,unsigned *basis,size_t *section,size_t *count);
/// 百分比落在哪一章（从0起）。/ The section (zero-based) a percentage falls into.
pn_status_t pn_epub_app_percent_section(pn_epub_app_t *,unsigned basis,size_t *section);
/// 按百分比跳转：跳到该百分比所在章节的开头；成功确认后可返回跳转前位置。/ Jump by percentage to the start of the section it falls into; the pre-jump position stays reachable after a confirmed jump.
pn_status_t pn_epub_app_jump_percent(pn_epub_app_t *,unsigned basis,uint64_t,pn_reader_present_fn,void *);

/// 接入全局刷新周期，保留已计页数；不写逐书排版。/ Apply global refresh period, retaining counted turns without writing per-book style.
void pn_epub_app_refresh_period(pn_epub_app_t *app,unsigned pages);
/// 显示owner确认整屏清理后，由产品owner重置计数。/ Product owner resets the counter after display owner confirms full cleaning.
void pn_epub_app_refresh_reset(pn_epub_app_t *app);

/// 跟随全局并呈现目标设置页；无全局记录使用内置字体。/ Inherit global defaults and present the destination; absent defaults use the resident face.
pn_status_t pn_epub_app_font_inherit_view(pn_epub_app_t *,pn_reader_overlay_fn,void *,uint64_t,pn_reader_present_fn,void *);
/// 当前接受的字体是否跟随全局；预览不改变它。/ Whether the accepted font follows global defaults; previews do not change it.
bool pn_epub_app_font_is_inherited(const pn_epub_app_t *);
