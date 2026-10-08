/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界WiFi二维码载荷及黑白整数模块绘制，不持有秘密或网络状态。
 * English: bounded WiFi QR payloads and black/white integer-module painting without retaining secrets or network state.
 */
#pragma once
#include "pn_frame.h"
#include "pn_types.h"
/// UTF8 SSID最多32字节、口令8–63可打印ASCII字节，失败不输出半个载荷。
/// UTF8 SSID at most 32 bytes, password 8–63 printable ASCII bytes; errors never output partial payloads.
pn_status_t pn_qr_wifi_payload(const char *,const char *,char *,size_t);
/// 最多240 UTF8字节，版本<=10，ECI UTF8、至少M纠错、四模块静区，纯绘制。
/// At most 240 UTF8 bytes, version <=10, UTF8 ECI, at least M correction, four-module quiet zone; paint only.
pn_status_t pn_qr_draw(const char *,pn_frame_t *,int,int,int);
