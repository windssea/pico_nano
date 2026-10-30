/* SPDX-License-Identifier: Apache-2.0
 * 中文：单张图片区域或独页正文的无损可回收缓存。/ English: one lossless reclaimable image-region or standalone-body cache. */
#pragma once
#include "pn_epub_image.h"
#include "pn_text_file.h"
#include "pn_style.h"
typedef struct {
    pn_cache_owner_t owner; ///< 计入原封面分项及总缓存 / Charged to the existing cover class and aggregate cap
    void *entry; ///< 完整解码后才发布的私有压缩记录 / Private compressed record published only after complete decoding
} pn_epub_image_cache_t;
/// 注册，不分配像素；对象地址保持稳定。/ Register without allocating pixels; retain a stable object address.
void pn_epub_image_cache_init(pn_epub_image_cache_t *,pn_pool_t *);
/// 丢弃并注销，幂等，不关闭书源。/ Drop and detach idempotently without closing the book source.
void pn_epub_image_cache_close(pn_epub_image_cache_t *);
/// 完整键匹配且原已验证源仍有效才复用；EMPTY沿原解码，其他错误传播。/ Reuse only a full key match with a still-valid verified source; decode normally on EMPTY and propagate other errors.
pn_status_t pn_epub_image_cache_restore(pn_epub_image_cache_t *,pn_text_file_t *,const char *,const pn_style_t *,pn_frame_t *);
/// 分块版本由调用方验证完整图片资源；不伪造原书完整摘要资格。/ A block-version caller verifies the complete image resource without fabricating whole-book digest authority.
pn_status_t pn_epub_image_cache_restore_guarded(pn_epub_image_cache_t *,pn_text_file_t *,const char *,const pn_style_t *,pn_frame_t *,pn_status_t (*)(void *,const char *),void *);
/// 仅完整资源EOF/CRC成功的独页正文可存；可选失败不驱逐资源或影响原帧。/ Store only a standalone body after complete resource EOF/CRC; optional failure never evicts resources or changes the original frame.
void pn_epub_image_cache_store(pn_epub_image_cache_t *,pn_text_file_t *,const char *,const pn_style_t *,const pn_frame_t *);
/// 混排区域键包含原矩形、完整视口和绘制前底图SHA；源复核、取消和原96KiB额度与独页共用。/ Mixed-page keys include the original rectangle, full viewport and prepaint underlay SHA; share source checks, cancellation and the original 96 KiB cap with standalone pages.
pn_status_t pn_epub_image_cache_restore_region_guarded(pn_epub_image_cache_t *,pn_text_file_t *,const char *,const pn_style_t *,pn_frame_t *,pn_image_rect_t,const pn_book_id_t *,pn_status_t (*)(void *,const char *),void *);
/// 只存完整EOF/CRC解码后的区域，混排传入绘图前underlay，不读写框外、不新增画布。/ Store a region only after complete EOF/CRC decoding, supplying its prepaint underlay for mixed pages, without touching outside pixels or adding a canvas.
void pn_epub_image_cache_store_region(pn_epub_image_cache_t *,pn_text_file_t *,const char *,const pn_style_t *,const pn_frame_t *,pn_image_rect_t,const pn_book_id_t *);
/// 绘图前按实际区域像素计算底图SHA，透明合成只复用相同底图；逐行响应取消，无堆分配。/ Hash actual underlay pixels before drawing; transparent composites require the same underlay, with row cancellation and no heap allocation.
pn_status_t pn_epub_image_cache_underlay(const pn_frame_t *,pn_image_rect_t,pn_book_id_t *);
