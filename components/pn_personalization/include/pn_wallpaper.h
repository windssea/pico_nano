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
#define PN_WALLPAPER_WIDTH 684
#define PN_WALLPAPER_HEIGHT 1216
#define PN_WALLPAPER_BYTES (PN_WALLPAPER_WIDTH/2*PN_WALLPAPER_HEIGHT)
#define PN_WALLPAPER_PATH_MAX 1024
#define PN_WALLPAPER_SHIFT_MAX 4

/// 锁屏模式。/ Lock-screen mode.
typedef enum {
    PN_LOCK_DEFAULT=0, ///< 固件内置黑白锁屏 / Built-in black-and-white lock screen
    PN_LOCK_CUSTOM=1, ///< 用户图片预处理位图 / Preprocessed user image bitmap
    PN_LOCK_SIMPLE=2 ///< 白底产品名与静态提示 / White background with product name and static hint
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

/// 绑定两槽，路径必须不同。/ Bind two distinct slot paths.
pn_status_t pn_wallpaper_store_init(pn_wallpaper_store_t *store,pn_media_t *media,const char *a,const char *b);
/// 选最高有效序号；自定义模式把位图读入frame（整屏、stride 342）。EMPTY无记录；CORRUPT两槽皆坏；错误时frame可能变化不可呈现，选择不变。
/// Pick the highest valid sequence; custom mode reads the bitmap into frame (full screen, stride 342). EMPTY without records; CORRUPT when both slots are bad; on error frame may change and must not be shown while selection stays unchanged.
pn_status_t pn_wallpaper_load(const pn_wallpaper_store_t *store,const pn_media_lease_t *lease,pn_lock_selection_t *selection,pn_frame_t *frame);
/// 写入非活动槽、同步并读回CRC；sequence由现有最高有效代加一并回填selection。custom须给bitmap。
/// Write the inactive slot, sync and read back the CRC; the sequence is the highest valid one plus one and is written back into selection. Custom mode requires bitmap.
/// 失败可能已经提交，调用方应重新load确认。/ Failures may still have committed; callers should reload to confirm.
pn_status_t pn_wallpaper_save(const pn_wallpaper_store_t *store,const pn_media_lease_t *lease,pn_lock_selection_t *selection,const pn_frame_t *bitmap);

/// 按选择绘制锁屏；custom时frame须已含位图，只叠加提示带。font为常驻UI字体，hint为一行提示文本。
/// Draw the lock screen for a selection; in custom mode frame already holds the bitmap and only the hint band is overlaid. font is the resident UI font; hint is one line of text.
pn_status_t pn_lock_render(const pn_lock_selection_t *selection,pn_font_t *font,const char *hint,pn_frame_t *frame);
