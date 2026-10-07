/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：受限资源与绘制的公共契约，不依赖设备或操作系统。
 * English: bounded resource and drawing contracts, independent of hardware and OS.
 *
 * 冻结：调用者串行访问；不修改硬件电源或设置。
 * Frozen: callers serialize access; never modify hardware power or settings.
 */
#include "pn_frame.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)
int main(void) {
    uint8_t guarded[10]; memset(guarded,0xA5,sizeof guarded);
    pn_frame_t f;
    CHECK(pn_frame_bind(&f,guarded+1,8,5,2)==true);
    CHECK(f.stride==3);
    pn_frame_clear(&f,15);
    pn_frame_pixel(&f,0,0,0); pn_frame_pixel(&f,1,0,7);
    CHECK(guarded[1]==0x70);
    CHECK(pn_frame_get(&f,4,0)==15 && pn_frame_get(&f,0,1)==15);
    pn_frame_rect(&f,-2,-1,4,3,4);
    CHECK(pn_frame_get(&f,0,0)==4 && pn_frame_get(&f,1,1)==4);
    CHECK(pn_frame_get(&f,2,1)==15);
    pn_frame_rect(&f,INT_MIN,INT_MIN,INT_MAX,INT_MAX,0);
    pn_frame_rect(&f,INT_MAX,0,INT_MAX,2,0);
    pn_frame_pixel(&f,-1,0,0); pn_frame_pixel(&f,5,0,0);
    CHECK(guarded[0]==0xA5 && guarded[7]==0xA5 && guarded[9]==0xA5);
    CHECK(pn_frame_get(&f,-1,0)==15);
    pn_frame_pixel(&f,4,1,255);
    CHECK(pn_frame_get(&f,4,1)==15);
    CHECK(!pn_frame_bind(&f,guarded,5,5,2));
    CHECK(f.pixels==NULL);
    CHECK(!pn_frame_bind(&f,guarded,sizeof guarded,0,2));
    pn_frame_rect(&f,0,0,1,1,0);
    pn_frame_clear(&f,0);
    CHECK(!pn_frame_bind(NULL,guarded,sizeof guarded,5,2));
    puts("4bpp packing, odd rows, clipping, overflow and storage guards passed");
    return 0;
}
