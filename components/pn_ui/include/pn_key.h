/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：屏幕下方触摸三键的命中与短按/长按识别，设备和PC共用。
 * English: hit testing and short/long-press recognition for the three touch keys below the display, shared by device and PC.
 * 冻结：三键是触摸面板显示区外的感应区，不是GPIO；坐标沿用参考固件实测中心，真机须复核。
 * Frozen: the keys are touch-panel zones outside the display, not GPIOs; coordinates follow the reference firmware's measured centers and need board verification.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define PN_KEY_AREA_TOP 1300 ///< 感应区上沿，显示区最大1216 / Strip top; the display ends at 1216
#define PN_KEY_PITCH 160 ///< 每键宽度，中心约80/240/400 / Key width, centers near 80/240/400
#define PN_KEY_1 0 ///< 左键 / Left key
#define PN_KEY_2 1 ///< 中键 / Middle key
#define PN_KEY_3 2 ///< 右键 / Right key
#define PN_KEY_LONG_MS 600 ///< 长按阈值 / Long-press threshold
typedef enum {PN_KEY_NONE=0,PN_KEY_SHORT,PN_KEY_LONG} pn_key_event_t; ///< 识别结果 / Recognized event
typedef struct {
    int key; ///< 当前按住的键，-1无 / Held key, -1 for none
    uint64_t since; ///< 按下时刻毫秒 / Press time in ms
    unsigned empty; ///< 连续空采样 / Consecutive empty samples
    bool down,fired,cancelled,foreign; ///< 按住/已发长按/取消/起点不在键区 / Held/long fired/cancelled/started outside the strip
} pn_key_t;
/// 坐标命中键，键区外-1。/ Key under a coordinate, -1 outside the strip.
int pn_key_hit(int x,int y);
/// 每次触摸采样调用一次：长按按住满600ms时发一次；未发长按的单点按下在两次空采样后发短按。
/// Feed every touch sample: a long press fires once after 600 ms held; otherwise a single-contact press fires a short press after two empty samples.
/// 起点在键区外、多点、读错或滑出该键都取消本次。/ Starting outside the strip, multitouch, read errors or sliding off the key cancel the press.
pn_key_event_t pn_key_feed(pn_key_t *keys,unsigned count,int x,int y,bool valid,uint64_t now_ms,int *key);
/// 取消当前按压直到完全释放。/ Cancel the current press until fully released.
void pn_key_cancel(pn_key_t *keys);
