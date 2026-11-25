/* SPDX-License-Identifier: Apache-2.0
 * 中文：低帧数墨水屏翻页效果，不改波形、不确认位置。/ English: low-frame-count e-paper effects; no waveform changes or position commits. */
#include "pn_turn_transition.h"
#include <string.h>
pn_status_t pn_turn_frame(const pn_frame_t *old,const pn_frame_t *next,pn_frame_t *out,pn_turn_style_t style,unsigned step,unsigned total,bool reverse){
    if(!old || !next || !out || !old->pixels || !next->pixels || !out->pixels || !total || step>total || style>PN_TURN_RIPPLE || old->width!=next->width || old->height!=next->height || out->width!=next->width || out->height!=next->height || out->pixels==old->pixels || out->pixels==next->pixels)return PN_INVALID;
    if(next->width<1 || next->width>4096 || next->height<1 || next->height>4096 || old->stride<(size_t)(next->width+1)/2 || next->stride<(size_t)(next->width+1)/2 || out->stride<(size_t)(next->width+1)/2)return PN_INVALID;
    int edge=(int)((uint64_t)next->width*(total-step)/total),fold=next->width/8;
    if(fold<1)fold=1;
    for(int y=0;y<out->height;y++)for(int x=0;x<out->width;x++){
        int at=reverse?out->width-1-x:x;uint8_t ink;
        if(!step)ink=pn_frame_get(old,x,y);
        else if(step==total || style==PN_TURN_NONE || at>=edge)ink=pn_frame_get(next,x,y);
        else if(style==PN_TURN_CURL && at>=edge-fold){
            int source=edge+(edge-at);if(source>=out->width)source=out->width-1;
            int sx=reverse?out->width-1-source:source;ink=pn_frame_get(old,sx,y);ink=(uint8_t)((ink+15u)/2u);
            if(at==edge-1)ink=5;
        }else ink=pn_frame_get(old,x,y);
        pn_frame_pixel(out,x,y,ink);
    }
    return PN_OK;
}
pn_status_t pn_turn_present(void *ctx,const pn_frame_t *next,pn_refresh_t profile){
    pn_turn_t *t=ctx;if(!t || !t->present || !next)return PN_INVALID;
    if(!next->pixels || next->width<1 || next->width>4096 || next->height<1 || next->height>4096 || next->stride<(size_t)(next->width+1)/2)return PN_INVALID;
    if(t->style>PN_TURN_RIPPLE)return PN_INVALID;
    // 历史样式都直接显示一次；不分配动画帧。/ All legacy styles present once, without animation frame allocation.
    if(t->before && (!t->before->pixels || t->before->stride<(size_t)(next->width+1)/2 || t->before->width!=next->width || t->before->height!=next->height))return PN_INVALID;
    // 普通翻页按模式路由：水波纹走弧形错峰、高清走完整48相GL16、极速走20相DU；周期清理页除水波纹外均走完整GC16。
    // Route ordinary turns by style: ripple arc-staggered, crisp full 48-phase GL16, fast 20-phase DU; periodic cleans use full GC16 (or GC16 ripple).
    if(t->style==PN_TURN_RIPPLE && t->before && profile==PN_REFRESH_GL16)profile=t->reverse?PN_REFRESH_RIPPLE_REVERSE:PN_REFRESH_RIPPLE;
    else if(t->style==PN_TURN_RIPPLE && t->before && profile==PN_REFRESH_GC16)profile=t->reverse?PN_REFRESH_RIPPLE_CLEAN_REVERSE:PN_REFRESH_RIPPLE_CLEAN;
    else if(t->style==PN_TURN_CRISP && t->before && profile==PN_REFRESH_GL16)profile=PN_REFRESH_CRISP;
    else if(t->style==PN_TURN_FAST && t->before && profile==PN_REFRESH_GL16)profile=PN_REFRESH_FAST_TURN;
    pn_status_t status=t->present(t->ctx,next,profile);
    if(status!=PN_OK && status!=PN_CANCELLED && status!=PN_BUSY && t->before)(void)t->present(t->ctx,t->before,PN_REFRESH_GC16);
    return status;
}
