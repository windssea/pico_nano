/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：三轴加速度计体感翻页检测器（高通去重力 + 脉冲确认 + 冷却防误触）。
 * English: 3-axis shake/flick page-turn detector (high-pass gravity removal + impulse confirmation + cooldown).
 */
#include "pn_shake_turn.h"
#include <string.h>

#define SHAKE_SETTLE_MS 300u ///< 唤醒后 300ms 静默稳定期，屏蔽上电瞬态 / 300 ms wake settling window

void pn_shake_turn_reset(pn_shake_turn_t *s,uint64_t now_ms){
    if(!s)return;
    memset(s,0,sizeof *s);
    s->armed_at=now_ms;
}

bool pn_shake_turn_feed(pn_shake_turn_t *s,int16_t x_mg,int16_t y_mg,int16_t z_mg,uint8_t sensitivity,uint64_t now_ms){
    if(!s)return false;
    if(sensitivity>=PN_SHAKE_SENS_COUNT)sensitivity=PN_SHAKE_SENS_MED;
    if(!s->primed){
        s->primed=true;
        s->gx=(int32_t)x_mg*8;
        s->gy=(int32_t)y_mg*8;
        s->gz=(int32_t)z_mg*8;
        s->prev_x=x_mg;s->prev_y=y_mg;s->prev_z=z_mg;
        if(!s->armed_at)s->armed_at=now_ms;
        return false;
    }
    // 计算相对当前重力基准的动态线性加速度与相邻帧瞬时跃变（jerk）。
    // Compute dynamic linear acceleration relative to gravity baseline and frame-to-frame jerk.
    int32_t bx=s->gx/8,by=s->gy/8,bz=s->gz/8;
    int32_t dx=(int32_t)x_mg-bx,dy=(int32_t)y_mg-by,dz=(int32_t)z_mg-bz;
    int32_t jx=(int32_t)x_mg-s->prev_x,jy=(int32_t)y_mg-s->prev_y,jz=(int32_t)z_mg-s->prev_z;
    s->prev_x=x_mg;s->prev_y=y_mg;s->prev_z=z_mg;
    // 1/8 指数滑动平均跟踪缓慢姿态变化，自动过滤任意手持角度的 1g 重力。
    // 1/8 EMA tracks slow posture changes so 1g gravity is rejected at any holding angle.
    s->gx=s->gx-bx+x_mg;
    s->gy=s->gy-by+y_mg;
    s->gz=s->gz-bz+z_mg;
    if(now_ms<s->armed_at+SHAKE_SETTLE_MS || now_ms<s->cooldown_until){
        s->burst_hits=0;
        return false;
    }
    int64_t dyn2=(int64_t)dx*dx+(int64_t)dy*dy+(int64_t)dz*dz;
    int64_t jerk2=(int64_t)jx*jx+(int64_t)jy*jy+(int64_t)jz*jz;
    // 三档灵敏度参数（动态加速度阈值、相邻跃变 jerk 阈值、确认窗口 ms、脉冲数、冷却 ms）。
    // Three sensitivity tiers (dynamic accel threshold, frame-to-frame jerk threshold, window ms, hits, cooldown ms).
    // 三档均要求 dyn 与 jerk 双达标且至少两次脉冲，避免单次磕碰或静置姿态切换误翻。
    // All tiers require both dyn and jerk thresholds across 2 impulses so posture shifts or single bumps never false-trigger.
    static const int64_t dyn_ths[3]={1350LL*1350LL,950LL*950LL,700LL*700LL};
    static const int64_t jerk_ths[3]={650LL*650LL,500LL*500LL,420LL*420LL};
    static const uint32_t win_ms[3]={280u,240u,240u};
    static const uint8_t need_hits[3]={2u,2u,2u};
    static const uint32_t cool_ms[3]={1200u,950u,800u};
    if(dyn2>=dyn_ths[sensitivity] && jerk2>=jerk_ths[sensitivity]){
        if(!s->burst_hits || now_ms>s->burst_at+win_ms[sensitivity]){
            s->burst_at=now_ms;
            s->burst_hits=1u;
        }else{
            s->burst_hits++;
        }
        if(s->burst_hits>=need_hits[sensitivity]){
            s->burst_hits=0;
            s->cooldown_until=now_ms+cool_ms[sensitivity];
            return true;
        }
    }else if(s->burst_hits && now_ms>s->burst_at+win_ms[sensitivity]){
        s->burst_hits=0;
    }
    return false;
}
