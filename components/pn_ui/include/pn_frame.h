/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：受限资源与绘制的公共契约，不依赖设备或操作系统。
 * English: bounded resource and drawing contracts, independent of hardware and OS.
 *
 * 冻结：调用者串行访问；不修改硬件电源或设置。
 * Frozen: callers serialize access; never modify hardware power or settings.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *pixels; ///< 每行独立、左像素低半字节 / Row-local storage, left pixel in low nibble
    int width; ///< 逻辑宽度 / Logical width
    int height; ///< 逻辑高度 / Logical height
    size_t stride; ///< 行字节数 / Row bytes
} pn_frame_t;

/// 校验空间并绑定缓冲，不清空内容；单像素0黑15白。
/// Validate storage and bind without clearing; each pixel uses zero black and fifteen white.
bool pn_frame_bind(pn_frame_t *frame, uint8_t *data, size_t capacity, int width, int height);
/// 清空全帧；灰度限制为0到15。
/// Clear the frame; clamp shade to zero through fifteen.
void pn_frame_clear(pn_frame_t *frame, uint8_t shade);
/// 有界写入，越界忽略；不写邻接像素。
/// Bounded write, ignoring out-of-range coordinates without changing adjacent pixels.
void pn_frame_pixel(pn_frame_t *frame, int x, int y, uint8_t shade);
/// 有界读取，越界或无效帧返回白色。
/// Bounded read, returning white for out-of-range coordinates or invalid frames.
uint8_t pn_frame_get(const pn_frame_t *frame, int x, int y);
/// 裁切矩形，宽高非正时不画；端点运算不溢出。
/// Clip rectangles; nonpositive extents draw nothing and endpoint arithmetic never overflows.
void pn_frame_rect(pn_frame_t *frame, int x, int y, int width, int height, uint8_t shade);
/// 共享灰阶与几何测试图，不代表阅读器产品页面。
/// Shared grayscale and geometry test pattern, not a reader product page.
void pn_frame_test_pattern(pn_frame_t *frame);
