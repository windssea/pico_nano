/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源设置页：锁屏壁纸、字体管理入口与翻页开关。
 * English: shared settings page: lock-wallpaper and font-management entries plus page-turn switches.
 * 冻结：开关立即保存，失败保持旧值；子页面由调用方打开。/ Frozen: switches save immediately and keep old values on failure; sub-pages are opened by the caller.
 */
#pragma once
#include "pn_display.h"
#include "pn_font.h"
#include "pn_input_prefs.h"
#define PN_SETUI_BACK 1
#define PN_SETUI_WALLPAPER 2
#define PN_SETUI_FONTS 3
#define PN_SETUI_TOGGLE 8 ///< 加0–3对应左手/滑动/边缘点按/三键 / Plus 0–3 for hand/swipe/edge taps/keys
typedef pn_status_t (*pn_settings_present_fn)(void *,const pn_frame_t *,pn_refresh_t); ///< 与阅读呈现同签名 / Same signature as reader presentation
typedef struct {
    void *impl; ///< 初始NULL / Initially NULL
    bool active; ///< 返回后false / False after Back
    bool presented; ///< 最近绘制已呈现 / Latest drawing was presented
    int request; ///< 0或PN_SETUI_WALLPAPER/FONTS，调用方处理后清零 / 0 or PN_SETUI_WALLPAPER/FONTS; callers clear it after handling
    uint8_t flags; ///< 当前已保存翻页标志 / Saved page-turn flags
} pn_settings_ui_t;
/// 打开并读取翻页记录；state_media为NULL时只读默认且不能保存。/ Open and read the page-turn record; NULL state_media shows defaults without saving.
pn_status_t pn_settings_ui_open(pn_settings_ui_t *ui,pn_pool_t *pool,pn_media_t *state_media,const char *state_dir,pn_settings_present_fn present,void *ctx);
/// 处理命令并重绘。/ Handle a command and repaint.
pn_status_t pn_settings_ui_event(pn_settings_ui_t *ui,int command,pn_settings_present_fn present,void *ctx);
/// 重画。/ Repaint.
pn_status_t pn_settings_ui_present(pn_settings_ui_t *ui,pn_settings_present_fn present,void *ctx);
/// 命中，空白-1。/ Hit test, -1 for blank.
int pn_settings_ui_hit(const pn_settings_ui_t *ui,int x,int y);
/// 释放，幂等。/ Release idempotently.
void pn_settings_ui_close(pn_settings_ui_t *ui);
/// 从内部记录读取翻页标志，无记录得0。/ Read page-turn flags from the internal record, 0 without one.
pn_status_t pn_settings_load_flags(pn_media_t *state_media,const char *state_dir,uint8_t *flags);
