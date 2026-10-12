/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "pn_frame.h"
#include "pn_types.h"
/// 转换684×1216低半字节在先的灰度帧为板级1216×684反向竖屏布局，不分配内存。
/// Convert a low-nibble-first 684×1216 grayscale frame to the board's inverted-portrait 1216×684 layout without allocations.
pn_status_t pn_device_frame_copy(const pn_frame_t *source,uint8_t *destination,size_t capacity);
