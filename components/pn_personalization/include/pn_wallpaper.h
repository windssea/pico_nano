/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：锁屏壁纸预处理、内部A/B缓存与锁屏绘制。
 * English: lock-screen wallpaper preprocessing, internal A/B cache and lock-screen drawing.
 * 冻结：原图只读；缓存记录同时携带模式与位图，提交即切换；任何失败都可回退系统默认锁屏。
 * Frozen: source images are read-only; cache records carry mode and bitmap together so committing switches atomically; every failure can fall back to the system default lock screen.
 */
#pragma once
#include "pn_font.h"
#include "pn_frame.h"
#include "pn_storage.h"
#include "pn_identity.h"
#define PN_WALLPAPER_WIDTH 684
#define PN_WALLPAPER_HEIGHT 1216
#define PN_WALLPAPER_BYTES (PN_WALLPAPER_WIDTH/2*PN_WALLPAPER_HEIGHT)
#define PN_WALLPAPER_PATH_MAX 1024
#define PN_WALLPAPER_SHIFT_MAX 4

/// 锁屏模式。/ Lock-screen mode.
typedef enum {
    PN_LOCK_DEFAULT=0, ///< 固件内置黑白锁屏 / Built-in black-and-white lock screen
    PN_LOCK_CUSTOM=1, ///< 用户图片预处理位图 / Preprocessed user image bitmap
    PN_LOCK_SIMPLE=2, ///< 白底产品名与静态提示 / White background with product name and static hint
    PN_LOCK_BOOK=3, ///< 旧版书封记录 / Legacy book-cover record
    PN_LOCK_MASK=4, ///< 阅读及锁屏透明PNG遮罩，源缺失回退 / Transparent PNG mask for reading and locking, falling back when absent
    PN_LOCK_BOOKMARK=5 ///< 本地阅读书签与月历 / Local reading bookmark and month calendar
} pn_lock_mode_t;
/// 适配方式。/ Fit mode.
typedef enum {
    PN_WALLPAPER_CONTAIN=0, ///< 完整显示，白色留边 / Show whole image with white margins
    PN_WALLPAPER_COVER=1 ///< 铺满裁切 / Fill the screen and crop
} pn_wallpaper_fit_t;
/// 预处理参数。/ Preprocessing parameters.
typedef struct {
    pn_wallpaper_fit_t fit; ///< 适配方式 / Fit mode
    uint8_t rotation; ///< 顺时针四分之一圈0–3 / Clockwise quarter turns 0–3
    int8_t shift; ///< 铺满时溢出方向裁切位置-4..4，0居中 / Cover crop position along the overflow axis, -4..4, 0 centered
} pn_wallpaper_transform_t;
/// 当前锁屏选择；与位图同记录提交。/ Current lock selection, committed in the same record as the bitmap.
typedef struct {
    pn_lock_mode_t mode; ///< 模式 / Mode
    pn_wallpaper_transform_t transform; ///< 自定义时的参数 / Parameters for custom mode
    bool hint; ///< 底部白色安全带提示 / Bottom white safety-band hint
    uint64_t sequence; ///< 记录序号，零为未保存 / Record sequence, zero when never saved
    uint64_t source_size; ///< 原图大小，仅供编辑页提示 / Source size, for edit-page hints only
    int64_t source_mtime; ///< 原图mtime / Source mtime
    char source[PN_WALLPAPER_PATH_MAX]; ///< 原图路径，原图可已不存在 / Source path; the source may no longer exist
    bool cached_book; ///< BOOK记录已含有效书封位图 / BOOK record contains a validated cover bitmap
    pn_book_id_t book; ///< 已缓存书封的内容身份 / Content identity of the cached book cover
} pn_lock_selection_t;
/// 缓存文件A/B两槽，由可信owner给出不同路径。/ Two cache slot paths supplied by a trusted owner.
typedef struct {
    pn_media_t *media; ///< 内部壁纸分区介质 / Internal wallpaper-partition media
    char slots[2][PN_WALLPAPER_PATH_MAX]; ///< 两槽路径 / Slot paths
} pn_wallpaper_store_t;

/// 读取JPEG/PNG原图并生成684×1216的4bpp位图；budget限定全部临时分配。成功才写out，out须为绑定好的整屏帧。
/// Decode a JPEG/PNG source into a 684×1216 4bpp bitmap; budget caps all temporary allocations. out, a bound full-screen frame, is written only on success.
/// 超宽/超高图在铺满时缩放后单边超过4096返回LIMIT。/ Cover scaling past 4096 on one side returns LIMIT.
pn_status_t pn_wallpaper_prepare(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,
    pn_wallpaper_transform_t transform,size_t budget,pn_frame_t *out);
/// 保留PNG灰度与透明度，错误不修改两个输出；不支持JPEG遮罩。
/// Preserve PNG gray and alpha, leaving both outputs unchanged on errors; JPEG masks are unsupported.
pn_status_t pn_wallpaper_mask_prepare(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,
    pn_wallpaper_transform_t transform,size_t budget,pn_frame_t *gray,pn_frame_t *alpha);
/// 将已校验遮罩叠加到文字底图；只绘制，无文件或硬件操作。
/// Composite a validated mask over a text underlay; drawing only, without file or hardware operations.
pn_status_t pn_wallpaper_mask_render(const pn_frame_t *gray,const pn_frame_t *alpha,pn_frame_t *underlay);
/// 解码一次的遮罩与最后成功呈现的文字底图缓存。/ A once-decoded mask and the last successfully presented text underlay.
typedef struct {void *impl;} pn_wallpaper_mask_t; ///< 初始NULL，控制层拥有 / Initially NULL, owned by the controller
/// 取得READ租约预处理，完成即释放；失效图片不留下活动缓存。
/// Prepare under a READ lease, releasing it immediately; unusable images leave no live cache.
pn_status_t pn_wallpaper_mask_open(pn_wallpaper_mask_t *mask,pn_pool_t *pool,pn_media_t *media,const pn_lock_selection_t *selection);
/// 同上，并在cache_dir读写可重建的灰度/透明平面缓存（路径/大小/mtime/变换为键，CRC校验）；写失败不影响结果。
/// As above, reading/writing a rebuildable gray/alpha plane cache in cache_dir (keyed by path/size/mtime/transform, CRC-checked); write failures do not affect the result.
pn_status_t pn_wallpaper_mask_open_cached(pn_wallpaper_mask_t *mask,pn_pool_t *pool,pn_media_t *media,const pn_lock_selection_t *selection,const char *cache_dir);
#define PN_WALLPAPER_MASK_CACHE "mask.pnm" ///< 缓存目录内唯一遮罩缓存文件名 / Sole mask-cache file name inside the cache directory
/// 同上，但缓存读写使用cache_media（如内部壁纸分区），原图仍只在media上读取；PackBits压缩并整文件CRC。
/// As above, but cache I/O uses cache_media (such as the internal wallpaper partition) while the source is read only from media; PackBits-compressed with a whole-file CRC.
pn_status_t pn_wallpaper_mask_open_stored(pn_wallpaper_mask_t *mask,pn_pool_t *pool,pn_media_t *media,const pn_lock_selection_t *selection,pn_media_t *cache_media,const char *cache_dir);
/// 锁屏遮罩纯绘制回调，不解码或读取文件。/ Lock-mask drawing callback without decoding or reading files.
pn_status_t pn_wallpaper_mask_paint(void *mask,pn_frame_t *frame);
/// 正文推屏完成通知；失败保留上一次成功底图。/ Body presentation notification; failures retain the previous successful underlay.
void pn_wallpaper_mask_complete(void *mask,const pn_frame_t *frame,bool shown);
/// 拷贝最后成功的阅读画面供锁屏使用，不重复合成半透明像素。
/// Copy the last successful reading image for locking without compositing semitransparent pixels twice.
bool pn_wallpaper_mask_snapshot(const pn_wallpaper_mask_t *mask,pn_frame_t *frame);
/// 幂等释放遮罩与底图。/ Idempotently release the mask and underlay.
void pn_wallpaper_mask_close(pn_wallpaper_mask_t *mask);

/// 绑定两槽，路径必须不同。/ Bind two distinct slot paths.
pn_status_t pn_wallpaper_store_init(pn_wallpaper_store_t *store,pn_media_t *media,const char *a,const char *b);
/// 选最高有效序号；自定义/已缓存书封把位图读入frame（整屏、stride 342）。EMPTY无记录；CORRUPT两槽皆坏；错误时frame可能变化不可呈现，选择不变。
/// Pick the highest valid sequence; custom/cached-book modes read the bitmap into frame (full screen, stride 342). EMPTY without records; CORRUPT when both slots are bad; on error frame may change and must not be shown while selection stays unchanged.
pn_status_t pn_wallpaper_load(const pn_wallpaper_store_t *store,const pn_media_lease_t *lease,pn_lock_selection_t *selection,pn_frame_t *frame);
/// 写入非活动槽、同步并读回CRC；sequence由现有最高有效代加一并回填selection。自定义/已缓存书封须给bitmap。
/// Write the inactive slot, sync and read back the CRC; the sequence is the highest valid one plus one and is written back into selection. Custom/cached-book modes require bitmap.
/// 失败可能已经提交，调用方应重新load确认。/ Failures may still have committed; callers should reload to confirm.
pn_status_t pn_wallpaper_save(const pn_wallpaper_store_t *store,const pn_media_lease_t *lease,pn_lock_selection_t *selection,const pn_frame_t *bitmap);

/// 按选择绘制锁屏；custom/book时frame须已含位图，只叠加提示带。font为常驻UI字体，hint为一行提示文本。
/// book模式由调用方先把封面画入frame；取不到封面时调用方应改用默认选择，本函数不检查frame内容。
/// Draw the lock screen for a selection; in custom/book mode frame already holds the bitmap and only the hint band is overlaid. font is the resident UI font; hint is one line of text.
/// In book mode the caller first draws the cover into frame; when no cover is available the caller must switch to the default selection, as this function does not inspect frame contents.
pn_status_t pn_lock_render(const pn_lock_selection_t *selection,pn_font_t *font,const char *hint,pn_frame_t *frame);
