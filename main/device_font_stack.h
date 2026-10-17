/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：CFF字形深栈worker：PSRAM栈上同步执行FreeType字形载入，调用方阻塞等待。
 * English: deep-stack worker for CFF glyphs: runs FreeType glyph loads synchronously on a PSRAM stack while the caller blocks.
 */
#pragma once
#include <stdbool.h>

/// 深栈字节数：CFF解释器单帧约16KiB，加载入/光栅/读卡/池分配余量。/ Deep stack bytes: about 16 KiB for the CFF interpreter frame plus load/raster/card-read/pool headroom.
#define PN_DEVICE_FONT_STACK_BYTES 40960u

/// 创建worker并向pn_font注册runner；失败时不注册，CFF字形报告内存不足而不是溢栈。启动早期调用一次。
/// Create the worker and register the runner with pn_font; on failure nothing is registered and CFF glyphs report out-of-memory instead of overflowing. Call once early at boot.
bool pn_device_font_stack_start(void);
