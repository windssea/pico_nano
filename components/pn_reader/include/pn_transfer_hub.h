/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：传书一级页（T01）：按设备真实能力列出热点、局域网与USB通道，显示存储与格式说明，底部固定三栏；只绘制与命中。
 * English: the Transfer root tab (T01): hotspot, LAN and USB channels listed by real device capability, storage and format notes, and the fixed three-tab bar; drawing and hit testing only.
 * 冻结：不可用的通道只显示原因，不给入口、不显示虚假地址。/ Frozen: unavailable channels show only the reason, never an entry or a fake address.
 */
#pragma once
#include "pn_font.h"
#include "pn_frame.h"
#define PN_HUB_HOTSPOT 1 ///< 开热点传书 / Start hotspot transfer
#define PN_HUB_LAN 2 ///< 局域网传书 / LAN transfer
#define PN_HUB_SHELF 3 ///< 底栏书架 / Bottom-bar Shelf
#define PN_HUB_SETTINGS 4 ///< 底栏设置 / Bottom-bar Settings
/// 页面上显示的能力与信息，由调用方按实际状态填写。/ Capabilities and information shown on the page, filled by the caller from actual state.
typedef struct {
    bool hotspot; ///< 热点传书可用 / Hotspot transfer available
    bool lan; ///< 已保存家庭网络，局域网传书可用 / A home network is saved so LAN transfer is available
    const char *lan_name; ///< 已保存网络名，可NULL / Saved network name, may be NULL
    const char *storage; ///< 存储卡容量说明，NULL则显示未读取到 / Card capacity note; NULL shows "not read"
    const char *unavailable; ///< 无线不可用时的原因，NULL用默认文案 / Reason wireless is unavailable; NULL uses the default copy
} pn_transfer_hub_info_t;
/// 绘制整页。/ Draw the whole page.
pn_status_t pn_transfer_hub_render(pn_font_t *font,pn_frame_t *frame,const pn_transfer_hub_info_t *info);
/// 命中：PN_HUB_*，不可用通道与空白返回-1。/ Hit test: PN_HUB_*; unavailable channels and blank space return -1.
int pn_transfer_hub_hit(const pn_transfer_hub_info_t *info,int x,int y);
