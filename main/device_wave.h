/* SPDX-License-Identifier: Apache-2.0
 * 中文：由原表派生的同起点GL16时序；每像素脉冲顺序与次数不变，只缩短晚起步迁移的前导保持。
 * English: start-aligned GL16 timing derived from the original table; per-pixel pulse order and counts are unchanged, only late starters' leading hold is shortened. */
#pragma once
#include <stdbool.h>
#include "epd_waveform.h"

/// 派生表最大相数，等于原GL16相数上限。/ Maximum derived phases, bounded by the original GL16 length.
#define PN_DEVICE_WAVE_MAX_PHASES 64

/// 派生结果摘要，供日志与测试核对。/ Derivation summary for logs and tests.
typedef struct {
    int original_phases; ///< 原GL16相数 / Original GL16 phases
    int phases; ///< 派生后相数（含尾部保持）/ Derived phases including trailing holds
    int start; ///< 基准相：晚于它起步的迁移前移至此 / Base phase: transitions starting later move up to it
} pn_device_wave_info_t;

/// 从已初始化的原波形派生；原表只读。失败返回false，调用方保留原波形。
/// Derive from the initialized original waveform, reading it only; false leaves the caller on the original waveform.
bool pn_device_wave_init(const EpdWaveform *original,int trailing_hold,pn_device_wave_info_t *info);
/// 同起点波形：DU/GC16原样借用，GL16为派生表；未初始化时NULL。
/// Start-aligned waveform: DU/GC16 borrowed unchanged, GL16 derived; NULL before initialization.
const EpdWaveform *pn_device_wave_aligned(void);
/// 只换变化像素的同起点波形：GL16中颜色不变的迁移全部保持，用于书库翻页等局部替换；未初始化时NULL。
/// Changed-pixels-only start-aligned waveform: GL16 holds every unchanged-color transition, for local replacements such as library paging; NULL before initialization.
const EpdWaveform *pn_device_wave_changed_only(void);
/// 菜单用：原GL16时序（旧内容先淡出、新内容后出现）且颜色不变的像素不驱动；同起点表在整页内容替换时会让新旧重叠成一下发黑。
/// For menus: original GL16 timing (old content fades before new content appears) with unchanged pixels undriven; the start-aligned table overlaps old and new into a dark blink on whole-page replacements.
const EpdWaveform *pn_device_wave_menu(void);
