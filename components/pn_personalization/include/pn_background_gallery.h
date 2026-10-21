/* SPDX-License-Identifier: Apache-2.0
 * 中文：阅读背景图库的有界、可取消缩略图控制器。 / English: bounded, cancellable reading-paper thumbnail controller. */
#pragma once
#include "pn_paper.h"
typedef struct {void *impl;} pn_background_gallery_t; ///< 唯一owner，不复制 / Single owner, never copy live state
/// 只列固定目录；不存在时为空图库。/ List the fixed directory only; a missing directory yields an empty gallery.
pn_status_t pn_background_gallery_open(pn_background_gallery_t *,pn_pool_t *,pn_media_t *);
/// 可信owner复用缩略任务预热锁屏目录；crop=false保持完整比例。/ A trusted owner reuses thumbnail work to warm lock-screen sources; crop=false preserves the whole image.
pn_status_t pn_background_gallery_open_at(pn_background_gallery_t *,pn_pool_t *,pn_media_t *,const char *directory,const char *cache,bool crop);
/// 分页先关闭旧解码任务。/ Paging closes old decode work first.
pn_status_t pn_background_gallery_page(pn_background_gallery_t *,bool previous);
/// 安装通知溢出时仅重建缩略图，各后续分页同样失效。/ Rebuild thumbnails only after install-note overflow, including subsequent pages.
void pn_background_gallery_invalidate(pn_background_gallery_t *);
/// 每次推进一小步；OK表示需要重画，BUSY待续，EMPTY无任务。/ Advance a small step; OK requests repaint, BUSY continues, EMPTY means no work.
pn_status_t pn_background_gallery_tick(pn_background_gallery_t *);
/// 只绘制当前六缩略图，不读盘或写设置。/ Paint six current thumbnails without IO or settings writes.
void pn_background_gallery_paint(const pn_background_gallery_t *,const pn_style_t *,pn_frame_t *);
/// 查询分页/条目，不读盘。/ Query paging/items without IO.
bool pn_background_gallery_can_page(const pn_background_gallery_t *,bool previous);
size_t pn_background_gallery_count(const pn_background_gallery_t *);
/// 选中时计算完整源摘要，只改草稿；应用阶段仍验证源和完整图。/ Hash the full selected source into the draft; apply still verifies source and full image.
pn_status_t pn_background_gallery_choose(pn_background_gallery_t *,unsigned,pn_style_t *);
/// 退出/锁屏取消源任务并释放租约和缩略图。/ Exit/lock cancels source work and releases leases/thumbnails.
void pn_background_gallery_close(pn_background_gallery_t *);
