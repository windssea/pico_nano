/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：三轴加速度计体感翻页（晃动/甩腕）纯逻辑检测器，设备和主机测试共用。
 * English: platform-independent 3-axis accelerometer shake/flick page-turn detector shared by device and host tests.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pn_input_prefs.h"

/// 体感翻页检测器状态（零堆分配，全整数运算）。/ Shake-to-turn detector state (zero heap allocation, integer math).
typedef struct {
    bool primed; ///< 已收到首帧重力基准 / Gravity baseline initialized
    int32_t gx,gy,gz; ///< 指数滑动平均重力向量（mg × 8 定点）/ EMA gravity baseline (mg * 8 fixed-point)
    int16_t prev_x,prev_y,prev_z; ///< 上一次原始样本（mg）/ Previous raw sample (mg)
    uint64_t armed_at; ///< 启动/唤醒稳定基准时间戳（ms）/ Wake/arm timestamp (ms)
    uint64_t cooldown_until; ///< 冷却截止时间（ms），防连续误翻 / Cooldown deadline (ms) to prevent double turns
    uint64_t burst_at; ///< 本轮第一次超阈值脉冲时间戳（ms）/ First supra-threshold sample timestamp (ms)
    uint8_t burst_hits; ///< 窗口内连续/相邻超阈值计数 / Hits within confirmation window
} pn_shake_turn_t;

/// 重置并从 now_ms 开始进入 300ms 唤醒稳定期。/ Reset detector and enter a 300 ms wake-settling window from now_ms.
void pn_shake_turn_reset(pn_shake_turn_t *s,uint64_t now_ms);
/// 投喂一帧设备坐标系加速度样本（mg）；触发翻页返回 true 并自动进入冷却期。
/// Feed one device-frame accelerometer sample (mg); returns true on a validated shake/flick and enters cooldown.
bool pn_shake_turn_feed(pn_shake_turn_t *s,int16_t x_mg,int16_t y_mg,int16_t z_mg,uint8_t sensitivity,uint64_t now_ms);
