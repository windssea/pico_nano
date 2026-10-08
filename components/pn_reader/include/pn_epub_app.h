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
#include "pn_epub_save.h"
#include "pn_epub_bookmarks.h"
#include "pn_epub_image.h"
#include "pn_toc.h"
#include "pn_reader_app.h"
typedef struct {void *impl;} pn_epub_app_t; ///< 初始NULL，不复制活动对象 / Initially NULL; never copy a live object
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

/// 当前字体组及默认目录，错误不改输出。/ Current font set and default directory, preserving outputs on error.
pn_status_t pn_epub_app_fonts_get(pn_epub_app_t *,pn_font_preferences_t *,char *directory,size_t capacity);
/// 借用书源租约读取字体专用分页。/ Read font-only pages under the borrowed book-source lease.
pn_status_t pn_epub_app_fonts_page(pn_epub_app_t *,const char *,const char *,bool reverse,pn_catalog_page_t *);
/// 校验候选字体，返回实际引用、名称与默认样例缺字统计。/ Verify a candidate and return actual reference, names and default-sample missing counts.
pn_status_t pn_epub_app_fonts_probe(pn_epub_app_t *,const pn_catalog_item_t *,pn_font_reference_t *,pn_font_info_t *,unsigned *,unsigned *);

/// 设置全局默认，不改活动字体或逐书覆盖；写失败由重试/关闭屏障处理。
/// Set global defaults without changing active fonts or per-book overrides; retry/close handles write failures.
pn_status_t pn_epub_app_font_default(pn_epub_app_t *,const pn_font_preferences_t *);
/// 在原文锚点应用已有全局默认，显示成功后保存继承标记；未设全局返回EMPTY。
/// Apply existing global defaults at the original anchor and save inheritance after presentation; absent global returns EMPTY.
pn_status_t pn_epub_app_font_inherit(pn_epub_app_t *,uint64_t,pn_reader_present_fn,void *);
