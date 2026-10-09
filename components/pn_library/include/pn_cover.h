/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书架封面提取、灰阶缩略图与可重建TF缓存。
 * English: shelf cover extraction, grayscale thumbnails and a rebuildable TF cache.
 * 冻结：原书/侧车只读；只写缓存目录；不改阅读位置；失败不改输出缩略图。
 * Frozen: books/sidecars are read-only; only the cache directory is written; reading positions untouched; failures never change the output thumbnail.
 */
#pragma once
#include "pn_catalog.h"
#include "pn_frame.h"
#define PN_COVER_WIDTH 184 ///< 网格封面宽，见UI_UX书架布局 / Grid cover width, see the shelf layout in UI_UX
#define PN_COVER_HEIGHT 256 ///< 网格封面高 / Grid cover height
#define PN_COVER_BYTES (PN_COVER_WIDTH*PN_COVER_HEIGHT/2)
#define PN_COVER_CACHE_VERSION 2 ///< 尺寸变化后缓存重建 / Caches rebuild after the size change
#define PN_COVER_SLOT_LAST PN_CATALOG_PAGE_MAX ///< 最近阅读卡使用的额外槽 / Extra slot used by the continue-reading card

/// 单书封面状态。/ Per-book cover state.
typedef enum {
    PN_COVER_NONE=0, ///< 不提取或无封面，显示排版卡 / Not extracted or no cover; draw the typographic card
    PN_COVER_PENDING, ///< 等待后台逐条提取 / Waiting for incremental extraction
    PN_COVER_READY, ///< 缩略图已完整校验可绘制 / Thumbnail fully verified and drawable
    PN_COVER_FAILED ///< 封面存在但损坏/超限，书仍可打开 / Cover present but corrupt/over budget; the book still opens
} pn_cover_state_t;

/// 当前书目页六条封面槽外加一个“继续阅读”槽，调用方拥有，可放pool或静态区。
/// Six cover slots for the current catalog page plus one continue-reading slot, caller-owned in pool or static storage.
typedef struct {
    uint8_t pixels[PN_CATALOG_PAGE_MAX+1][PN_COVER_BYTES]; ///< 4bpp行内低半字节在左 / 4bpp rows, left pixel in low nibble
    pn_cover_state_t state[PN_CATALOG_PAGE_MAX+1]; ///< 每槽状态 / Per-slot state
    pn_status_t reason[PN_CATALOG_PAGE_MAX+1]; ///< 最后一次提取结果 / Last extraction status
    bool has_last; ///< last有效 / last is valid
    pn_catalog_item_t last; ///< 继续阅读的书（路径、名称、进度） / The continue-reading book (path, name, progress)
} pn_shelf_covers_t;

/// 提取单本封面并contain缩放到thumb整帧（白底居中）；EPUB用OPF声明或cover文件名候选，TXT用同名侧车。
/// Extract one cover and contain-fit it into the whole thumb frame (centered on white); EPUB uses OPF declarations or cover filename candidates, TXT uses same-basename sidecars.
/// salt为XML哈希盐，须来自可信随机源；budget限制本次全部临时分配并同时计入pool。
/// salt is the XML hash salt from a trusted random source; budget caps all temporary allocations, which are also charged to pool.
/// 无封面EMPTY；图片/ZIP校验失败CORRUPT；任何非OK都不改thumb，结束时本次分配全部释放。
/// EMPTY without a cover; CORRUPT for image/ZIP verification failures; any non-OK result leaves thumb unchanged and every allocation is released.
pn_status_t pn_cover_render(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const pn_catalog_item_t *item,
    const uint8_t salt[16],size_t budget,pn_frame_t *thumb);

/// 按页面重置：TXT/EPUB为PENDING，其他格式NONE。/ Reset for a page: TXT/EPUB become PENDING, other formats NONE.
void pn_shelf_covers_reset(pn_shelf_covers_t *covers,const pn_catalog_page_t *page);
/// 设置或清除继续阅读的书（item为NULL清除）；下次reset/step/cached起为其提取封面。
/// Set or clear the continue-reading book (NULL clears it); its cover is extracted by later reset/step/cached calls.
void pn_shelf_covers_set_last(pn_shelf_covers_t *covers,const pn_catalog_item_t *item);
/// 只从缓存填充PENDING槽，供首次绘制前调用以免墨水屏二次刷新；未命中仍PENDING，介质失效STALE_MEDIA。
/// Fill PENDING slots from the cache only, called before the first paint to avoid a second e-ink refresh; misses stay PENDING, STALE_MEDIA on invalidation.
pn_status_t pn_shelf_covers_cached(pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,const char *cache_dir,bool *changed);
/// 处理下一条PENDING，供空闲时逐条调用以免阻塞输入；changed表示需要重绘。
/// Process the next PENDING slot, called once per idle tick so input is not blocked; changed means a redraw is needed.
/// 内部短暂取得READ读书/缓存，需写缓存时另取WRITE；cache_dir为NULL不用缓存，由可信owner给出。
/// Briefly acquires READ for book/cache reads and WRITE for cache stores; NULL cache_dir disables caching; the directory comes from a trusted owner.
/// budget为单书解码上限（大渐进式JPEG需数MiB）；无剩余EMPTY；介质失效/占用返回STALE_MEDIA/BUSY并保留PENDING；单书失败记FAILED并返回OK。
/// budget caps one book's decode (large progressive JPEGs need several MiB); EMPTY when none remain; STALE_MEDIA/BUSY keep the slot PENDING; per-book failures become FAILED and return OK.
pn_status_t pn_shelf_covers_step(pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,
    const char *cache_dir,const uint8_t salt[16],size_t budget,bool *changed);
/// 槽位已就绪时绑定只读帧视图，否则false。/ Bind a read-only frame view for a ready slot, otherwise false.
bool pn_shelf_cover_frame(const pn_shelf_covers_t *covers,size_t index,pn_frame_t *frame);
