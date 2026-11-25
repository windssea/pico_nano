/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读手势与按钮的一次提交识别器，设备和PC共用。
 * English: single-commit reader gesture/button recognizer shared by device and PC.
 * 冻结：多点/读错取消，滑出按钮不再触发；两次空采样确认释放。
 * Frozen: multitouch/read errors cancel; leaving a button prevents activation; two empty samples confirm release.
 */
#pragma once
#include "pn_reader_app.h"
/// 翻页偏好；全零为默认（右手、左右翻页、滑动与边缘点按开启、含上下栏）。/ Page-turn preferences; all zero is the default (right hand, horizontal turns, swipe and edge taps on, bars shown).
typedef struct {
    bool left_hand; ///< 左手：左边缘下一页、右边缘上一页（左右翻页模式）/ Left hand: left edge next, right edge previous (horizontal mode)
    bool no_swipe; ///< 关闭滑动翻页 / Disable swipe turns
    bool no_edge_tap; ///< 关闭点按翻页 / Disable tap turns
    bool no_keys; ///< 关闭三键翻页（调用方使用）/ Disable key turns (used by callers)
    bool vertical_turn; ///< 上下翻页模式：上滑/点下半屏下一页，下滑/点上半屏上一页 / Vertical turn mode: swipe up or tap bottom half for next, swipe down or tap top half for previous
    bool full_screen; ///< 去上下栏全屏模式：整屏0..1215均响应翻页 / Bar-less full-screen mode: entire 0..1215 height accepts page turns
} pn_reader_input_config_t;
#define PN_READER_LONG_PRESS_MS 450u ///< 正文长按查词触发时长（毫秒）/ Long-press duration (ms) for dictionary lookup
typedef struct {
    pn_reader_input_config_t config; ///< 当前偏好，可随时替换 / Current preferences, replaceable at any time
    int start_x,start_y,last_x,last_y; ///< 手势起止坐标 / Gesture start and last coordinates
    int region; ///< 按钮或正文区域 / Button or content region
    unsigned empty_samples; ///< 稳定空采样 / Stable empty samples
    bool down,cancelled,blocked; ///< 手势与取消状态 / Gesture and cancellation state
    uint64_t down_ms; ///< 按下时间戳（毫秒）/ Press start timestamp in ms
    bool long_fired; ///< 本次按压已触发长按查词 / Long-press lookup already fired for this press
} pn_reader_input_t;
/// 初始零状态；count为活动触点，invalid或多点必须等全部释放。
/// Initially zero; count is active contacts; invalid/multitouch requires full release.
bool pn_reader_input_feed(pn_reader_input_t *input,unsigned count,int x,int y,bool valid,pn_reader_action_t *action);
/// 带毫秒时间戳的输入投喂：在正文区静止按压满 PN_READER_LONG_PRESS_MS 时立即触发 PN_APP_LOOKUP，并屏蔽随后抬起翻页。
/// Timestamped input feed: fires PN_APP_LOOKUP after holding still in the body for PN_READER_LONG_PRESS_MS and suppresses the subsequent release turn.
bool pn_reader_input_feed_timed(pn_reader_input_t *input,unsigned count,int x,int y,bool valid,uint64_t now_ms,pn_reader_action_t *action);
/// 切页/睡眠/字体重载时取消，不允许同一手指释放误提交。
/// Cancel on page/sleep/font reload, preventing the same finger's release from committing.
void pn_reader_input_cancel(pn_reader_input_t *input);
