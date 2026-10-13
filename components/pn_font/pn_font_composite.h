/* SPDX-License-Identifier: Apache-2.0
 * 中文：裁切一次后直接混合4bpp半字节，不改变灰阶取整。
 * English: clip once, then blend 4bpp nibbles directly without changing shade rounding. */
#pragma once
#include "pn_frame.h"
static inline void pn_font_composite(pn_frame_t *frame,int64_t left,int64_t top,unsigned width,unsigned rows,const uint8_t *data,size_t pitch,bool binary,unsigned ink){
    if(left>=frame->width || top>=frame->height || left<=-(int64_t)width || top<=-(int64_t)rows)return;
    unsigned x0=left<0?(unsigned)-left:0,y0=top<0?(unsigned)-top:0;
    unsigned x1=left+(int64_t)width>frame->width?(unsigned)(frame->width-left):width;
    unsigned y1=top+(int64_t)rows>frame->height?(unsigned)(frame->height-top):rows;
    for(unsigned y=y0;y<y1;y++){
        uint8_t *row=frame->pixels+(size_t)(top+y)*frame->stride;
        const uint8_t *bitmap=data+(size_t)y*pitch;
        for(unsigned x=x0;x<x1;x++){
            unsigned coverage=bitmap[x];if(binary)coverage=coverage>=128?255:0;
            if(!coverage)continue;
            unsigned px=(unsigned)(left+x),shift=(px&1u)*4u;uint8_t *at=row+px/2;
            unsigned old=(*at>>shift)&15u,shade=(old*(255-coverage)+ink*coverage+127)/255;
            *at=(uint8_t)((*at & (shift?15u:240u)) | (shade<<shift));
        }
    }
}
