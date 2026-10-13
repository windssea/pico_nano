/* SPDX-License-Identifier: Apache-2.0
 * 中文：低帧数墨水屏翻页效果，不改波形、不确认位置。/ English: low-frame-count e-paper effects; no waveform changes or position commits. */
#include "pn_turn_transition.h"
#include <string.h>
pn_status_t pn_turn_frame(const pn_frame_t *old,const pn_frame_t *next,pn_frame_t *out,pn_turn_style_t style,unsigned step,unsigned total,bool reverse){
    if(!old || !next || !out || !old->pixels || !next->pixels || !out->pixels || !total || step>total || style>PN_TURN_CURL || old->width!=next->width || old->height!=next->height || out->width!=next->width || out->height!=next->height || out->pixels==old->pixels || out->pixels==next->pixels)return PN_INVALID;
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
    if(t->style>PN_TURN_CURL)return PN_INVALID;
    if(t->style==PN_TURN_NONE || !t->before)return t->present(t->ctx,next,profile);
    if(!t->pool || !t->before->pixels || t->before->stride<(size_t)(next->width+1)/2 || t->before->width!=next->width || t->before->height!=next->height)return PN_INVALID;
    size_t row=(size_t)(next->width+1)/2,bytes=row*(size_t)next->height;
    uint8_t *old=pn_alloc_tagged(t->pool,PN_MEM_FRAME,bytes),*pixels=old?pn_alloc_tagged(t->pool,PN_MEM_FRAME,bytes):NULL;
    if(!old || !pixels){pn_free(old);pn_free(pixels);return t->present(t->ctx,next,profile);}
    pn_frame_t before,frame;bool bound=pn_frame_bind(&before,old,bytes,next->width,next->height) && pn_frame_bind(&frame,pixels,bytes,next->width,next->height);
    if(!bound){pn_free(old);pn_free(pixels);return PN_INVALID;}
    for(int y=0;y<before.height;y++)memcpy(old+(size_t)y*row,t->before->pixels+(size_t)y*t->before->stride,row);
    pn_status_t status=PN_OK;
    for(unsigned step=1;step<3 && status==PN_OK;step++){status=pn_turn_frame(&before,next,&frame,t->style,step,3,t->reverse);if(status==PN_OK)status=t->present(t->ctx,&frame,PN_REFRESH_GL16);}
    if(status==PN_OK)status=t->present(t->ctx,next,profile);
    if(status!=PN_OK)(void)t->present(t->ctx,&before,PN_REFRESH_GC16);
    pn_free(pixels);pn_free(old);return status;
}
