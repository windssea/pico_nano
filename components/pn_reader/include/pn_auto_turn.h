/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读自动翻页计时状态机（设备与PC同源，无堆分配、到达书尾或任意交互即自动停止）。
 * English: Shared auto page-turn timer state machine (zero allocation; stops automatically on user input or end-of-book).
 */
#pragma once
#include "pn_input_prefs.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool active; ///< 自动翻页运行中 / Auto page-turn active
    uint8_t interval_sec; ///< 翻页间隔（秒，3..120）/ Page-turn interval in seconds
    uint64_t next_ms; ///< 下次翻页时间戳（毫秒）/ Next turn timestamp in ms
} pn_auto_turn_t;

static inline uint8_t pn_auto_turn_clamp(uint8_t sec){
    if(sec<PN_AUTO_TURN_MIN_SEC)return PN_AUTO_TURN_DEFAULT_SEC;
    if(sec>PN_AUTO_TURN_MAX_SEC)return PN_AUTO_TURN_MAX_SEC;
    return sec;
}
static inline void pn_auto_turn_start(pn_auto_turn_t *at,uint8_t interval_sec,uint64_t now_ms){
    if(!at)return;
    at->interval_sec=pn_auto_turn_clamp(interval_sec);
    at->active=true;
    at->next_ms=now_ms+(uint64_t)at->interval_sec*1000u;
}
static inline void pn_auto_turn_stop(pn_auto_turn_t *at){
    if(!at)return;
    at->active=false;
    at->next_ms=0;
}
static inline bool pn_auto_turn_due(const pn_auto_turn_t *at,uint64_t now_ms){
    return at && at->active && now_ms>=at->next_ms;
}
static inline void pn_auto_turn_receipt(pn_auto_turn_t *at,uint64_t now_ms,bool advanced){
    if(!at || !at->active)return;
    if(!advanced){pn_auto_turn_stop(at);return;}
    at->next_ms=now_ms+(uint64_t)pn_auto_turn_clamp(at->interval_sec)*1000u;
}
