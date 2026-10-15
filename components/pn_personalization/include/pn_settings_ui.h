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
#include "pn_shelf_prefs.h"
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
#define PN_SETUI_APPEARANCE 18 ///< 打开书架外观 / Open shelf appearance
#define PN_SETUI_COLUMNS 19 ///< 加0或1选择3或4列 / Plus zero or one for three or four columns
#define PN_SETUI_HOME 25 ///< 底栏首页请求 / Bottom-bar home request
#define PN_SETUI_STYLE 21 ///< 加0至3选择书架样式 / Plus zero through three for a shelf style
#define PN_SETUI_TURN 26 ///< 加0至2选择默认/扫除/仿真翻页 / Plus zero through two for direct/sweep/fold turns
#define PN_SETUI_TIME 29 ///< 日期与时间子页 / Date and time page
#define PN_SETUI_TIME_SYNC 30 ///< 请求联网对时，由控制层处理 / Request network synchronization from the controller
#define PN_SETUI_TIME_CANCEL 31 ///< 请求结束对时 / Request cancellation of synchronization
typedef struct {
    char datetime[32]; ///< 已确认UTC格式化日期，空串为未知 / Date formatted from confirmed UTC, empty if unknown
    char message[96]; ///< 控制层任务提示 / Operation notice supplied by the controller
    bool running; ///< 对时或清理尚未结束 / Synchronization or cleanup is still running
    bool available; ///< 已保存家庭WiFi，可请求联网 / Saved home WiFi permits a network request
} pn_settings_time_t; ///< 控制层提供的时间与任务快照 / Clock and operation snapshot supplied by the controller
/// “存储与关于”页显示的信息，由调用方按实际读取填写；空串不显示猜测值。/ Information on the "storage and about" page, filled by the caller from actual reads; empty strings never show guesses.
typedef struct {char version[32];char storage[64];char internal[48];char screen[64];} pn_settings_about_t;
typedef pn_status_t (*pn_settings_present_fn)(void *,const pn_frame_t *,pn_refresh_t); ///< 与阅读呈现同签名 / Same signature as reader presentation
typedef struct {
    void *impl; ///< 初始NULL / Initially NULL
    bool active; ///< 返回后false / False after Back
    bool presented; ///< 最近绘制已呈现 / Latest drawing was presented
    int request; ///< 0或导航/对时请求，调用方处理后清零 / Zero or a navigation/time request; callers clear it after handling
    uint8_t flags; ///< 当前已保存翻页标志 / Saved page-turn flags
    pn_shelf_prefs_t shelf; ///< 当前已保存书架外观 / Saved shelf appearance
} pn_settings_ui_t;
/// 打开并读取翻页记录；state_media为NULL时只读默认且不能保存。/ Open and read the page-turn record; NULL state_media shows defaults without saving.
pn_status_t pn_settings_ui_open(pn_settings_ui_t *ui,pn_pool_t *pool,pn_media_t *state_media,const char *state_dir,pn_settings_present_fn present,void *ctx);
/// 保存成功后只重画变化的开关/分段/预览；旧提示、外部快照或显示失败后完整恢复。/ After persistence succeeds repaint changed toggles/segments/previews; restore completely after old notices, external snapshots or display failure.
pn_status_t pn_settings_ui_event(pn_settings_ui_t *ui,int command,pn_settings_present_fn present,void *ctx);
/// 重画。/ Repaint.
pn_status_t pn_settings_ui_present(pn_settings_ui_t *ui,pn_settings_present_fn present,void *ctx);
/// 命中，空白-1。/ Hit test, -1 for blank.
int pn_settings_ui_hit(const pn_settings_ui_t *ui,int x,int y);
/// 释放，幂等。/ Release idempotently.
void pn_settings_ui_close(pn_settings_ui_t *ui);
/// 从内部记录读取翻页标志，无记录得0。/ Read page-turn flags from the internal record, 0 without one.
pn_status_t pn_settings_load_flags(pn_media_t *state_media,const char *state_dir,uint8_t *flags);
/// 读取书架外观，无记录使用默认；错误保留输出。/ Load shelf appearance with defaults without a record; errors preserve output.
pn_status_t pn_settings_load_shelf(pn_media_t *state_media,const char *state_dir,pn_shelf_prefs_t *prefs);

/// 设置“局域网传书”行显示的已保存网络名；NULL或空串隐藏该行。设置后立即重画。/ Set the saved network name shown on the "LAN transfer" row; NULL or an empty string hides the row. Repaints at once.
pn_status_t pn_settings_ui_set_lan(pn_settings_ui_t *ui,const char *ssid,pn_settings_present_fn present,void *ctx);
/// 填写“存储与关于”信息（不重画）。/ Fill the "storage and about" information (no repaint).
void pn_settings_ui_set_about(pn_settings_ui_t *ui,const pn_settings_about_t *about);
/// 设置日期/任务快照，不重画、不读时钟或联网。 / Set the clock/operation snapshot without painting, reading the clock or networking.
void pn_settings_ui_set_time(pn_settings_ui_t *,const pn_settings_time_t *);
/// 当前子页：0首页，1按键，2刷新，3关于，4书架外观，5日期时间。/ Current screen: root, keys, refresh, about, appearance, date/time.
int pn_settings_ui_screen(const pn_settings_ui_t *ui);
/// 刷新策略对应的整屏刷新间隔（页）。/ Pages between full refreshes for the saved refresh policy.
unsigned pn_settings_refresh_pages(uint8_t flags);

/// 将2024..2099的UTC秒格式化为北京时间，不改变任务字段，错误保持输出。 / Format UTC seconds in 2024..2099 as China time without changing operation fields; errors preserve output.
pn_status_t pn_settings_time_format(pn_settings_time_t *,uint32_t utc);
