/* SPDX-License-Identifier: Apache-2.0
 * 中文：水波纹翻页的板级端口：原GC16按弧形等级错峰，每像素完整执行原相位序列；复用驱动保持原样。
 * English: board port for ripple turns: the original GC16 staggered by arc level, every pixel running the complete original phase sequence; reused drivers stay unchanged. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epd_waveform.h"
#include "pn_ripple.h"

/// 相邻等级起步间隔（相）。/ Start offset between adjacent levels, in phases.
#define PN_DEVICE_RIPPLE_STEP 1
/// 原表最大相数。/ Maximum original phases.
#define PN_DEVICE_RIPPLE_MAX_PHASES 64

/// 与epdiy构表/查表函数同签名。/ Same signatures as the epdiy LUT build/lookup functions.
typedef void (*pn_ripple_build_fn)(uint8_t *lut,const EpdWaveformPhases *phases,int frame);
typedef void (*pn_ripple_lookup_fn)(const uint32_t *line,uint8_t *out,const uint8_t *lut,uint32_t width);

/// 调用方提供的常驻内存：8张1KiB片内LUT及两方向行段表。/ Caller-provided resident memory: eight 1 KiB internal LUTs and per-direction row runs.
typedef struct {
    uint32_t (*tables)[256]; ///< PN_RIPPLE_BANDS张填充LUT，须片内 / PN_RIPPLE_BANDS padded LUTs, internal memory
    pn_ripple_row_t *rows[2]; ///< [0]下一页、[1]上一页，各684行 / [0] next, [1] previous, 684 rows each
    bool rows_ready[2]; ///< 行段表已生成 / Row runs generated
} pn_device_ripple_memory_t;

/// 扫描开始前冻结方向、差分缓冲与原GC16；返回专用波形。失败不改变任何驱动状态。
/// Freeze direction, difference buffer and the original GC16 before scanning, returning the dedicated waveform; failure changes no driver state.
bool pn_device_ripple_begin(pn_device_ripple_memory_t *memory,const EpdWaveform *original,bool reverse,const uint8_t *difference,const EpdWaveform **waveform);
/// 驱动选表入口：活动且为1ppB差分GC16时换成分区函数，记住原函数对。/ Driver selection hook: while active for 1ppB difference GC16, substitute regional functions and remember the original pair.
bool pn_device_ripple_select(int mode,pn_ripple_build_fn *build,pn_ripple_lookup_fn *lookup);
/// 扫描及两生产者停止后结束；仅在确实选表且无错误行指针时true。/ End after the scan and both producers stop; true only if selection happened with no invalid row pointer.
bool pn_device_ripple_end(void);
/// 专用波形总拍数，供日志/测试。/ Total ticks of the dedicated waveform for logs and tests.
int pn_device_ripple_ticks(void);
