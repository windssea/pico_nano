/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：水波纹翻页的同源弧形几何；只计算区域等级，不扫描、不碰波形。
 * English: shared arc geometry for ripple page turns; computes region levels only, never scanning or touching waveforms.
 *
 * 冻结：圆心在翻页侧边中点外320px，按半径分8级；下一页自右向左、上一页镜像。
 * Frozen: the center sits 320 px beyond the turning edge midpoint with eight radius levels; next pages run right to left, previous pages mirror.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "pn_types.h"
#define PN_RIPPLE_BANDS 8 ///< 弧形等级数 / Number of arc levels
#define PN_RIPPLE_SEGMENTS 15 ///< 每物理行最多连续段（V形等级序列上限2×8-1）/ Maximum runs per physical row (V-shaped level sequence, 2×8-1)
#define PN_RIPPLE_ROWS 684 ///< 物理行数＝逻辑宽 / Physical rows, equal to logical width
#define PN_RIPPLE_COLUMNS 1216 ///< 物理列数＝逻辑高 / Physical columns, equal to logical height

/// 一条物理行的连续等级段；end递增且最后一段为1216。/ Consecutive level runs of one physical row; ends increase and the last is 1216.
typedef struct {
    uint16_t end[PN_RIPPLE_SEGMENTS]; ///< 段终点（不含）/ Exclusive run ends
    uint8_t band[PN_RIPPLE_SEGMENTS]; ///< 段等级0..7，0最先 / Run level 0..7, zero first
    uint8_t count; ///< 段数 / Number of runs
} pn_ripple_row_t;

/// 逻辑坐标(x,y)所在等级0..7；越界返回7。/ Level 0..7 of logical point (x,y); out of range returns seven.
unsigned pn_ripple_band_at(bool reverse,int x,int y);
/// 物理行r对应逻辑x=683-r，列c对应逻辑y=c；一次生成全部684行，不分配。
/// Physical row r is logical x=683-r and column c is logical y=c; fill all 684 rows without allocation.
pn_status_t pn_ripple_rows(bool reverse,pn_ripple_row_t *rows,size_t count);
