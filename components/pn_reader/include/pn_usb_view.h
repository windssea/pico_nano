/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "pn_font.h"
#include "pn_frame.h"
#define PN_USB_VIEW_STOP 1
/// USB页面呈现信息，由控制层填写。/ USB page information filled by the controller.
typedef struct {
    bool starting,connected,stopping,failed,released; ///< 当前呈现状态 / Current presentation state
} pn_usb_view_t;
/// 只绘制USB状态及固定资源目录，不访问卡或硬件。/ Paint USB status/fixed resource paths only, without card/hardware access.
pn_status_t pn_usb_view_render(const pn_usb_view_t *view,pn_font_t *font,pn_frame_t *frame);
/// 结束/重试按钮命中。/ Hit test the End/Retry button.
int pn_usb_view_hit(int x,int y);
