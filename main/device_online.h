/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设备端在线素材库控制层（Wi-Fi STA、自动 SNTP 校时、多镜像 HTTPS、缩略图解码缓存与共享安装事务）。
 * English: device controller for online assets (Wi-Fi STA, auto SNTP sync, multi-mirror HTTPS, thumbnail decode cache, and shared install transaction).
 */
#pragma once
#include "device_wifi.h"
#include "pn_online_ui.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void *impl; ///< 活动在线素材会话（PSRAM 分配，不可复制）/ Live PSRAM-backed session, never copy
} pn_device_online_t;

/// 状态轮询快照 / Poll snapshot for Core 1 UI loop.
typedef struct {
    bool dirty;          ///< 界面状态或缩略图有新进展，需要刷新屏幕 / UI or thumbnails updated, needs repaint
    bool full_refresh;   ///< 切页或大图切换建议 GC16 刷新 / Prefer GC16 refresh on screen transition
    bool exited;         ///< 会话已完全结束且无线已关闭，可调用 close 返回传书页 / Worker and Wi-Fi fully stopped; safe to close
    bool time_synced;    ///< 本次自动 SNTP 校时成功 / Automatic SNTP time sync succeeded
    uint32_t synced_utc; ///< 同步得到的 UTC 秒数 / Synced UTC epoch seconds
    pn_online_screen_t screen;
} pn_device_online_poll_t;

/// 打开在线素材下载会话（后台连接家庭 Wi-Fi 并拉取清单）；不阻塞 UI。
/// Open an online asset session (connects saved Wi-Fi and fetches manifest in background); never blocks UI.
pn_status_t pn_device_online_open(pn_device_online_t *out, pn_media_t *sd_media,
                                  const pn_device_wifi_config_t *wifi_cfg, const char *sd_root);

/// 向在线素材会话发送交互命令（PN_ONLINE_CMD_*）。
/// Send a UI command (PN_ONLINE_CMD_*) to the online asset session.
pn_status_t pn_device_online_command(pn_device_online_t *online, int cmd);

/// 命中测试当前在线素材页面，返回 PN_ONLINE_CMD_* 或 -1。
/// Hit test the active online asset screen, returning PN_ONLINE_CMD_* or -1.
int pn_device_online_hit(pn_device_online_t *online, int x, int y);

/// 纯绘制当前在线素材页面到帧缓冲。
/// Render the active online asset screen into frame.
pn_status_t pn_device_online_render(pn_device_online_t *online, pn_font_t *font, pn_frame_t *frame);

/// 非阻塞轮询会话更新；消费 dirty 标记。
/// Non-blocking poll for state updates; clears dirty flags once reported.
pn_status_t pn_device_online_poll(pn_device_online_t *online, pn_device_online_poll_t *out);

/// 请求停止在线会话（取消下载并关闭 Wi-Fi）。
/// Request session stop (cancel active transfers and tear down Wi-Fi).
pn_status_t pn_device_online_request_stop(pn_device_online_t *online);

/// 在 exited 为真后释放全部 PSRAM 与内核对象；未退出返回 PN_BUSY。
/// Release all PSRAM and kernel objects after exited is true; returns PN_BUSY while still stopping.
pn_status_t pn_device_online_close(pn_device_online_t *online);
