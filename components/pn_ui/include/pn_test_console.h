/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：诊断测试指令的解析与合成触点时间线，设备诊断镜像和PC共用；不访问硬件、不分配内存。
 * English: diagnostic test-command parsing and synthetic touch timelines shared by the device diagnostic image and PC; no hardware access or allocation.
 * 合成触点从原始采样层进入，下游三键、阅读手势与底栏导航仍走产品原代码。
 * Synthetic contacts enter at the raw-sample layer, so the three keys, reading gestures and bottom navigation still run the product code.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pn_types.h"

#define PN_TC_LINE_MAX 96 ///< 单条指令最大字节数（不含换行） / Maximum command bytes excluding the newline
#define PN_TC_TAP_MS 80u ///< 默认点击按住时长 / Default tap hold duration
#define PN_TC_LONG_MS 900u ///< 默认长按时长，超过三键600ms阈值 / Default long-press duration, above the 600 ms key threshold
#define PN_TC_SWIPE_MS 250u ///< 默认滑动时长 / Default swipe duration
#define PN_TC_RELEASE_MS 40u ///< 松手后连续空采样窗口，覆盖三键所需两次空采样 / Empty-sample window after release, covering the two empty samples keys need
#define PN_TC_HOLD_MIN 20u ///< 最短按住时长 / Minimum hold duration
#define PN_TC_HOLD_MAX 10000u ///< 最长按住时长 / Maximum hold duration
#define PN_TC_X_MAX 683 ///< 横坐标上限（含） / Inclusive x limit
#define PN_TC_Y_MAX 1399 ///< 纵坐标上限（含），包括屏下三键区 / Inclusive y limit including the touch-key strip
#define PN_TC_KEY_Y 1340 ///< 三键合成纵坐标 / Synthetic touch-key y

/// 指令种类 / Command kind
typedef enum {
    PN_TC_PING=1, ///< 连通检查 / Liveness check
    PN_TC_VERSION, ///< 固件版本 / Firmware version
    PN_TC_STATUS, ///< 输入/内存/任务状态快照 / Input, memory and task snapshot
    PN_TC_SHOT, ///< 回传面板已显示参考帧 / Dump the panel's shown reference frame
    PN_TC_TAP, ///< 单点点击 / Single tap
    PN_TC_LONG, ///< 单点长按 / Single long press
    PN_TC_SWIPE, ///< 线性滑动 / Linear swipe
    PN_TC_KEY, ///< 屏下三键 / Touch key below the display
    PN_TC_POWER, ///< 电源短按（锁屏后须实体键唤醒） / Power short press (waking requires the physical key)
} pn_tc_kind_t;

/// 解析后的指令；手势坐标已按种类补全 / Parsed command with gesture coordinates resolved per kind
typedef struct {
    pn_tc_kind_t kind; ///< 指令种类 / Command kind
    uint32_t seq; ///< 主机序号，回执原样带回 / Host sequence echoed in replies
    int x0,y0,x1,y1; ///< 起点与终点；点击/长按两点相同 / Start and end; equal for taps/long presses
    unsigned hold_ms; ///< 按住时长 / Hold duration
} pn_tc_command_t;

/// 合成触点状态机；由单一输入线程推进 / Synthetic contact state machine advanced by one input thread
typedef struct {
    int x0,y0,x1,y1; ///< 起止坐标 / Start and end coordinates
    unsigned hold_ms; ///< 按住时长 / Hold duration
    uint64_t start_ms,end_ms; ///< 首个采样与结束时刻，0未发生 / First-sample and finish times, 0 when not yet reached
    bool started,ending_sent,finished; ///< 已开始/已发松手终点/已结束 / Started, release endpoint sent, finished
} pn_tc_gesture_t;

/// 解析一行"<seq> <verb> [args]"；拒绝未知动词、越界坐标、多余参数和超长行。
/// Parse one "<seq> <verb> [args]" line; reject unknown verbs, out-of-range coordinates, extra arguments and over-long lines.
pn_status_t pn_tc_parse(const char *line,pn_tc_command_t *command);
/// 手势类指令返回true / True for gesture commands
bool pn_tc_is_gesture(const pn_tc_command_t *command);
/// 由手势指令初始化状态机；非手势返回INVALID / Initialize the state machine from a gesture command; INVALID otherwise
pn_status_t pn_tc_gesture_init(pn_tc_gesture_t *gesture,const pn_tc_command_t *command);
/// 给出now时刻的合成采样；返回false表示已结束、应恢复真实读数。首个采样定义起点；松手终点只报告一次。
/// Produce the synthetic sample at now; false means finished and real reads resume. The first sample defines the start; the release endpoint is reported once.
bool pn_tc_gesture_sample(pn_tc_gesture_t *gesture,uint64_t now_ms,unsigned *count,int *x,int *y,bool *ending);
