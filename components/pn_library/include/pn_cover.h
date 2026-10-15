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
#define PN_COVER_CACHE_VERSION 6 ///< ZIP兼容及提取修正后重建失败记录 / Rebuild failure records after ZIP compatibility and extraction fixes
#define PN_COVER_SLOT_LAST PN_CATALOG_PAGE_MAX ///< 最近阅读卡使用的额外槽 / Extra slot used by the continue-reading card

/// 单书封面状态。/ Per-book cover state.
typedef enum {
    PN_COVER_NONE=0, ///< 不提取或无封面，显示排版卡 / Not extracted or no cover; draw the typographic card
    PN_COVER_PENDING, ///< 等待后台逐条提取 / Waiting for incremental extraction
    PN_COVER_READY, ///< 缩略图已完整校验可绘制 / Thumbnail fully verified and drawable
    PN_COVER_FAILED ///< 封面存在但损坏/超限，书仍可打开 / Cover present but corrupt/over budget; the book still opens
} pn_cover_state_t;

/// 已验证作品元数据；空串使用文件名回退。/ Verified publication metadata; empty strings use filename fallback.
typedef struct {
    char title[512]; ///< OPF作品名 / OPF publication title
    char author[512]; ///< OPF作者，不从文件名猜测 / OPF author, never guessed from filenames
} pn_book_display_t;

/// 当前书目页最多十二条封面槽外加一个“继续阅读”槽，调用方拥有，可放pool或静态区。
/// Up to twelve cover slots for the current catalog page plus one continue-reading slot, caller-owned in pool or static storage.
typedef struct {
    uint8_t pixels[PN_CATALOG_PAGE_MAX+1][PN_COVER_BYTES]; ///< 4bpp行内低半字节在左 / 4bpp rows, left pixel in low nibble
    pn_cover_state_t state[PN_CATALOG_PAGE_MAX+1]; ///< 每槽状态 / Per-slot state
    pn_status_t reason[PN_CATALOG_PAGE_MAX+1]; ///< 最后一次提取结果 / Last extraction status
    pn_book_display_t display[PN_CATALOG_PAGE_MAX+1]; ///< 当前槽展示快照 / Current slot display snapshots
    bool has_last; ///< last有效 / last is valid
    pn_catalog_item_t last; ///< 继续阅读的书（路径、名称、进度） / The continue-reading book (path, name, progress)
    uint16_t cache_dirty; ///< 已验证但尚未写入的槽，换页清除 / Verified slots awaiting persistence, cleared on page reset
    uint8_t cache_key[PN_CATALOG_PAGE_MAX+1][33]; ///< 仅重试用的来源快照，不是书籍身份 / Source snapshots for retries, never book identity
    size_t cache_budget[PN_CATALOG_PAGE_MAX+1]; ///< 失败结论的原解码预算 / Original decode budget for failure outcomes
} pn_shelf_covers_t;

/// 产品owner持有的可暂停缓存写入；零初始化，释放池/介质前abort。/ Resumable cache writer owned by the product owner; zero-initialize and abort before releasing pool/media.
typedef struct {
    void *impl; ///< 自有暂存与租约，不借用封面槽 / Owned staging and lease without borrowing cover slots
} pn_cover_write_t;
/// 是否还有缓存步骤；不表示缓存已经持久化。/ Whether cache steps remain, without claiming persistence.
bool pn_cover_write_pending(const pn_cover_write_t *write);
/// 放弃未提交缓存并释放文件/租约；不提交剩余数据。/ Discard uncommitted cache and release files/lease without finishing the write.
void pn_cover_write_abort(pn_cover_write_t *write);
/// 一次CRC最多4KiB或一个文件阶段；BUSY需继续，取消仅暂停；OK仅在替换完成后返回。
/// At most 4 KiB of CRC or one file stage; BUSY needs continuation, cancellation pauses, and OK requires completed replacement.
pn_status_t pn_cover_write_step(pn_cover_write_t *write);

typedef struct {
    pn_cover_write_t writer; ///< 独立持久化状态 / Independent persistence state
    void *extract; ///< 自有解析/解码上下文，不借用UI槽 / Owned parsing/decoding context without borrowed UI slots
} pn_cover_work_t; ///< 产品owner零初始化且不可移动的后台任务 / Product-owner background task, zero-initialized and never moved while active
/// 是否有解码或持久化待续；不是完成回执。/ Whether decoding or persistence remains, never a completion receipt.
bool pn_cover_work_pending(const pn_cover_work_t *);
/// 放弃全部暂定工作，关源/解码器/租约；不提交像素。/ Discard provisional work, closing sources/decoders/leases without presenting pixels.
void pn_cover_work_abort(pn_cover_work_t *);

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
/// 当前页/继续阅读槽是否仍有封面结论待提取；不含缓存写盘。/ Whether page/continue-reading slots still await extraction, excluding cache persistence.
bool pn_shelf_covers_pending(const pn_shelf_covers_t *,const pn_catalog_page_t *);
/// 只从缓存填充PENDING槽，供首次绘制前调用以免墨水屏二次刷新；未命中仍PENDING，介质失效STALE_MEDIA。
/// Fill PENDING slots from the cache only, called before the first paint to avoid a second e-ink refresh; misses stay PENDING, STALE_MEDIA on invalidation.
pn_status_t pn_shelf_covers_cached(pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,const char *cache_dir,bool *changed);
/// 空闲时处理下一条PENDING；单次解码仍同步，changed表示需要重绘。
/// Process the next PENDING slot while idle; individual decodes remain synchronous, and changed requests a redraw.
/// 内部短暂取得READ读书/缓存，需写缓存时另取WRITE；cache_dir为NULL不用缓存，由可信owner给出。
/// Briefly acquires READ for book/cache reads and WRITE for cache stores; NULL cache_dir disables caching; the directory comes from a trusted owner.
/// budget为单书解码上限（大渐进式JPEG需数MiB）；无剩余EMPTY；介质失效/占用返回STALE_MEDIA/BUSY并保留PENDING；单书失败记FAILED并返回OK。
/// budget caps one book's decode (large progressive JPEGs need several MiB); EMPTY when none remain; STALE_MEDIA/BUSY keep the slot PENDING; per-book failures become FAILED and return OK.
pn_status_t pn_shelf_covers_step(pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,
    const char *cache_dir,const uint8_t salt[16],size_t budget,bool *changed);
/// 产品空闲入口：先推进缓存写入，再提取下一本；BUSY表示本轮有进展，单书解析/解码仍同步。
/// Product idle entry: advance cache writes before extracting another book; BUSY means work remains, while parsing/decoding one book is still synchronous.
/// write必须零初始化，任何前台操作/导航/介质交接前abort；缓存失败不使已验证缩略图失效。
/// Zero-initialize write and abort before foreground actions/navigation/media handoff; cache failures do not invalidate verified thumbnails.
pn_status_t pn_shelf_covers_idle(pn_cover_write_t *write,pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,
    const char *cache_dir,const uint8_t salt[16],size_t budget,bool *changed);
/// 产品分步入口：一个出版物阶段/4KiB元信息XML、八个解码子步骤、四行缩小或一个持久化阶段；BUSY待续。
/// Product staged entry: one publication stage/4 KiB metadata XML, eight decoder substeps, four downsampled rows, or one persistence stage; continue on BUSY.
/// 完整校验及来源复核前槽不变；ZIP目录、模型排序关联、图像头和单个系统调用仍可能阻塞。
/// Slots stay unchanged until complete checks and source revalidation; ZIP directories, model sorting/linking, image headers and individual system calls may still block.
/// 可见槽先补齐，再从已验证槽快照写盘；显示可在pending为false后进行，无须等待缓存提交。
/// Fill visible slots before snapshotting them to disk; display once pending is false, without waiting for cache commits.
pn_status_t pn_shelf_covers_work(pn_cover_work_t *,pn_shelf_covers_t *,const pn_catalog_page_t *,pn_pool_t *,pn_media_t *,const char *,const uint8_t[16],size_t,bool *);
/// 槽位已就绪时绑定只读帧视图，否则false。/ Bind a read-only frame view for a ready slot, otherwise false.
bool pn_shelf_cover_frame(const pn_shelf_covers_t *covers,size_t index,pn_frame_t *frame);
