/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源设置（一级页）：字体、壁纸、按键与手势、刷新与屏幕、存储与关于。
 * English: shared Settings root tab: fonts, wallpaper, keys and gestures, refresh and screen, storage and about.
 * 冻结：开关立即保存，失败保持旧值；子页面由调用方打开。/ Frozen: switches save immediately and keep old values on failure; sub-pages are opened by the caller.
 */
#pragma once
#include "pn_display.h"
#include "pn_font.h"
#include "pn_input_prefs.h"
#define PN_SETUI_BACK 1
#define PN_SETUI_WALLPAPER 2
#define PN_SETUI_FONTS 3
#define PN_SETUI_LAN 4 ///< 局域网传书（仅已保存家庭网络时出现）/ LAN transfer (shown only when a home network is saved)
#define PN_SETUI_KEYS 5 ///< 打开“按键与手势” / Open "keys and gestures"
#define PN_SETUI_REFRESH 6 ///< 打开“刷新与屏幕” / Open "refresh and screen"
#define PN_SETUI_ABOUT 7 ///< 打开“存储与关于” / Open "storage and about"
#define PN_SETUI_TOGGLE 8 ///< 加0–3对应左手/滑动/边缘点按/三键（在“按键与手势”页）/ Plus 0–3 for hand/swipe/edge taps/keys (on the keys page)
#define PN_SETUI_LEVEL 12 ///< 加0–2对应均衡/清晰/省电刷新策略 / Plus 0–2 for the balanced/crisp/saver refresh policy
#define PN_SETUI_FULL_REFRESH 15 ///< 立即整屏刷新本页 / Fully refresh this page now
#define PN_SETUI_SHELF 16 ///< 底栏“书架”（request）/ Bottom-bar Shelf (request)
#define PN_SETUI_TRANSFER 17 ///< 底栏“传书”（request）/ Bottom-bar Transfer (request)
/// “存储与关于”页显示的信息，由调用方按实际读取填写；空串不显示猜测值。/ Information on the "storage and about" page, filled by the caller from actual reads; empty strings never show guesses.
typedef struct {char version[32];char storage[64];char internal[48];char screen[64];} pn_settings_about_t;
typedef pn_status_t (*pn_settings_present_fn)(void *,const pn_frame_t *,pn_refresh_t); ///< 与阅读呈现同签名 / Same signature as reader presentation
typedef struct {
    void *impl; ///< 初始NULL / Initially NULL
    bool active; ///< 返回后false / False after Back
    bool presented; ///< 最近绘制已呈现 / Latest drawing was presented
    int request; ///< 0或PN_SETUI_WALLPAPER/FONTS/LAN/SHELF/TRANSFER，调用方处理后清零 / 0 or PN_SETUI_WALLPAPER/FONTS/LAN/SHELF/TRANSFER; callers clear it after handling
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

/// 设置“局域网传书”行显示的已保存网络名；NULL或空串隐藏该行。设置后立即重画。/ Set the saved network name shown on the "LAN transfer" row; NULL or an empty string hides the row. Repaints at once.
pn_status_t pn_settings_ui_set_lan(pn_settings_ui_t *ui,const char *ssid,pn_settings_present_fn present,void *ctx);
/// 填写“存储与关于”信息（不重画）。/ Fill the "storage and about" information (no repaint).
void pn_settings_ui_set_about(pn_settings_ui_t *ui,const pn_settings_about_t *about);
/// 当前子页：0设置首页，1按键与手势，2刷新与屏幕，3存储与关于。/ Current screen: 0 root, 1 keys and gestures, 2 refresh and screen, 3 storage and about.
int pn_settings_ui_screen(const pn_settings_ui_t *ui);
/// 刷新策略对应的整屏刷新间隔（页）。/ Pages between full refreshes for the saved refresh policy.
unsigned pn_settings_refresh_pages(uint8_t flags);
