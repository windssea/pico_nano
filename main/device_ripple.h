/* SPDX-License-Identifier: Apache-2.0
 * 中文：水波纹翻页的板级端口：GL16或GC16按弧形等级错峰，每个驱动像素完整执行原相位序列；GL16波纹不驱动颜色未变的像素；复用驱动保持原样。
 * English: board port for ripple turns: GL16 or GC16 staggered by arc level, every driven pixel running its complete phase sequence; GL16 ripples skip unchanged pixels; reused drivers stay unchanged. */
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

/// 扫描开始前冻结方向、差分缓冲与所选模式（MODE_GL16=5柔和、MODE_GC16=2清残影）的原相位表；返回专用波形。失败不改变任何驱动状态。
/// Freeze direction, difference buffer and the phases of the chosen mode (MODE_GL16=5 gentle, MODE_GC16=2 cleaning) before scanning, returning the dedicated waveform; failure changes no driver state.
bool pn_device_ripple_begin(pn_device_ripple_memory_t *memory,const EpdWaveform *original,int mode,bool reverse,const uint8_t *difference,const EpdWaveform **waveform);
/// 同上并可选：flat为真时不错峰（整屏同一相位，菜单维护用）；white_push为真（仅GL16且账本完整）时账上保持纯白的像素改走原GL16的0→15纯白相迁移，无黑闪地推掉浅灰残影。
/// As above, optionally: flat removes the stagger (one phase across the screen, for menu maintenance); white_push (GL16 with a complete ledger only) sends ledger pixels staying pure white through the original GL16 0->15 all-white-phase transition, removing light-gray ghosts without a dark flash.
bool pn_device_ripple_begin_ex(pn_device_ripple_memory_t *memory,const EpdWaveform *original,int mode,bool reverse,bool flat,bool white_push,const uint8_t *difference,const EpdWaveform **waveform);
/// 非水波纹显示前调用：把目标帧与已显示参考（同为板级4bpp布局）不同的像素记入残影账本。账本不完整时不做事。
/// Call before non-ripple draws: record pixels where the target differs from the shown reference (both board 4bpp layout) into the ghost ledger. No-op while the ledger is incomplete.
void pn_device_ripple_ghost_note(const uint8_t *target,const uint8_t *shown);
/// 驱动选表入口：活动且为同模式1ppB差分时换成分区函数，记住原函数对。/ Driver selection hook: while active for the same-mode 1ppB difference, substitute regional functions and remember the original pair.
bool pn_device_ripple_select(int mode,pn_ripple_build_fn *build,pn_ripple_lookup_fn *lookup);
/// 扫描及两生产者停止后结束；仅在确实选表且无错误行指针时true。/ End after the scan and both producers stop; true only if selection happened with no invalid row pointer.
bool pn_device_ripple_end(void);
/// 专用波形总拍数，供日志/测试。/ Total ticks of the dedicated waveform for logs and tests.
int pn_device_ripple_ticks(void);
/// 残影账本：柔和波纹记下每个变过色的像素；下一次清残影波纹只驱动本次变化及账上像素，其余（从未变过的留白）保持不动。
/// 非水波纹的局部显示无法逐像素记账，调用forget让下一次清理退回整页；整屏GC16之后调用clear清空账本。
/// Ghost ledger: gentle ripples record every pixel that changed color; the next cleaning ripple drives only this turn's changes and ledger pixels, holding the rest (never-touched margins).
/// Non-ripple partial draws cannot be recorded per pixel, so call forget to make the next clean a full page; call clear after a full-screen GC16.
void pn_device_ripple_ghost_forget(void);
/// 整屏GC16已清除全部残影，清空账本。/ A full-screen GC16 removed every ghost; empty the ledger.
void pn_device_ripple_ghost_clear(void);
/// 下一次清残影是否只驱动账上像素（测试/日志）。/ Whether the next clean drives only ledger pixels (tests/logs).
bool pn_device_ripple_ghost_selective(void);
