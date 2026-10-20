/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源锁屏壁纸设置页：模式与原图列表、预览调整、应用/取消。
 * English: shared lock-wallpaper settings page: mode and source list, preview adjustment, apply/cancel.
 * 冻结：只有“应用”写内部记录；预览不改当前锁屏；原图只读；存储失败保留旧选择。
 * Frozen: only Apply writes the internal record; previews never change the current lock screen; sources are read-only; storage failures keep the old selection.
 */
#pragma once
#include "pn_catalog.h"
#include "pn_display.h"
#include "pn_wallpaper.h"
#include "pn_bookmark_lock.h"
#define PN_WUI_BACK 1
#define PN_WUI_PREVIOUS 2
#define PN_WUI_NEXT 3
#define PN_WUI_DEFAULT 4
#define PN_WUI_SIMPLE 5
#define PN_WUI_FIT 6
#define PN_WUI_ROTATE 7
#define PN_WUI_LEFT 8
#define PN_WUI_RIGHT 9
#define PN_WUI_WALLPAPER 10
#define PN_WUI_APPLY 11
#define PN_WUI_CANCEL 12
#define PN_WUI_BOOK 13
#define PN_WUI_MASK 14
#define PN_WUI_BOOKMARK 15
#define PN_WUI_ROW 16
typedef pn_status_t (*pn_wallpaper_present_fn)(void *,const pn_frame_t *,pn_refresh_t); ///< 与阅读呈现回调同签名 / Same signature as reader presentation callbacks
/// 页面：列表或预览。/ Screen: list or preview.
typedef enum {PN_WUI_LIST=0,PN_WUI_PREVIEW=1} pn_wallpaper_screen_t;
typedef struct {
    void *impl; ///< 内部状态，初始NULL，不可复制 / Internal state, initially NULL, never copy
    bool active; ///< 仍在页面内；返回后false由调用方关闭 / Still on the page; false after Back, then caller closes
    bool presented; ///< 最近一次绘制已呈现 / Latest drawing was presented
    pn_wallpaper_screen_t screen; ///< 当前页面 / Current screen
    pn_status_t last; ///< 最近一次应用结果，未应用为EMPTY / Latest apply result, EMPTY before any apply
} pn_wallpaper_ui_t;

/// 打开页面：images_media/directory为原图目录（可不存在），store可NULL表示内部分区不可用（仍可预览，应用会失败）。
/// Open the page: images_media/directory locate sources (may be absent); NULL store means the internal partition is unavailable (previews still work, apply fails).
/// 借用pool，内部约占两个整屏帧加四分之一预览帧，预处理另按pool余量限额。
/// Borrows pool, holding about two full frames plus a quarter-size preview; preprocessing is capped by the remaining pool.
pn_status_t pn_wallpaper_ui_open(pn_wallpaper_ui_t *ui,pn_pool_t *pool,pn_media_t *images_media,const char *directory,
    const pn_wallpaper_store_t *store,pn_wallpaper_present_fn present,void *ctx);
/// 同上，owner提供可重建图片缓存目录；已有缩略图首帧复用，完整预览仍核对源SHA。/ As above with an owner-owned rebuildable cache; reuse thumbnails in the first frame, and still verify source SHA for full previews.
pn_status_t pn_wallpaper_ui_open_cached(pn_wallpaper_ui_t *,pn_pool_t *,pn_media_t *,const char *,const pn_wallpaper_store_t *,pn_wallpaper_present_fn,void *,const char *cache_dir);
/// 处理一个命令（PN_WUI_*或ROW+i）并重绘。/ Handle one command (PN_WUI_* or ROW+i) and repaint.
pn_status_t pn_wallpaper_ui_event(pn_wallpaper_ui_t *ui,int command,pn_wallpaper_present_fn present,void *ctx);
/// 重画当前模型。/ Repaint the current model.
pn_status_t pn_wallpaper_ui_present(pn_wallpaper_ui_t *ui,pn_wallpaper_present_fn present,void *ctx);
/// 逻辑坐标命中，空白-1。/ Logical hit test, -1 for blank space.
int pn_wallpaper_ui_hit(const pn_wallpaper_ui_t *ui,int x,int y);
/// 当前已保存选择的副本，失败false。/ Copy of the saved selection, false on failure.
bool pn_wallpaper_ui_current(const pn_wallpaper_ui_t *ui,pn_lock_selection_t *out);
/// 释放全部页面资源，幂等。/ Release all page resources idempotently.
void pn_wallpaper_ui_close(pn_wallpaper_ui_t *ui);

/// 设置书签预览快照的副本；不绘制、不读文件。/ Copy the bookmark preview snapshot without drawing or file reads.
void pn_wallpaper_ui_set_bookmark(pn_wallpaper_ui_t *ui,const pn_bookmark_lock_t *snapshot);
/// 空闲推进缩略图；首张就绪及整页完成时合并呈现，BUSY表示仍待续，退出/换页取消旧任务。/ Advance thumbnails while idle; present the first ready tile and the finished page, BUSY means more work, and navigation cancels old jobs.
pn_status_t pn_wallpaper_ui_tick(pn_wallpaper_ui_t *ui,pn_wallpaper_present_fn present,void *ctx);
/// 纯查询缩略图任务是否待续，供owner选择短等待。/ Query pending thumbnail work without IO so the owner can choose a short wait.
bool pn_wallpaper_ui_pending(const pn_wallpaper_ui_t *ui);
