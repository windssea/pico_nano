/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设备入口的文字提示绘制，不访问设置或硬件。
 * English: device-entry text message drawing without settings or hardware access.
 */
#pragma once
#include "pn_font.h"
pn_status_t pn_device_text(pn_font_t *font,pn_frame_t *frame,const char *value,int x,int baseline);
