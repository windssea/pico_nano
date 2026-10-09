/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：焦点扫描、排序、循环移动与焦点环。
 * English: focus scanning, ordering, wrap-around movement and the focus ring.
 */
#include <assert.h>
#include "pn_focus.h"
/* 四个区域：左上返回、同一行的两个步进键、整行宽的按钮，以及整屏的“关闭”区（被跳过）。/ Four areas: a top-left back, two steppers on one row, a full-width button and a full-screen close area (skipped). */
static int hit(void *ctx,int x,int y){
    (void)ctx;
    if(x>=32 && x<232 && y>=24 && y<104)return 1;
    if(y>=500 && y<580 && x>=300 && x<380)return 3;
    if(y>=504 && y<584 && x>=100 && x<180)return 2;
    if(y>=800 && y<900 && x>=32 && x<652)return 4;
    return y<1100?9:-1;
}
int main(void){
    pn_focus_t focus;const int skip[]={9};
    pn_focus_scan(&focus,hit,NULL,skip,1);
    assert(focus.count==4 && focus.index==-1 && pn_focus_current(&focus)==NULL);
    /* 同一行的两个步进键按x排序（2在3前），整体自上而下。/ Two steppers on a row order by x (2 before 3), overall top to bottom. */
    assert(focus.items[0].code==1 && focus.items[1].code==2 && focus.items[2].code==3 && focus.items[3].code==4);
    assert(focus.items[0].x<=38 && focus.items[0].x+focus.items[0].width>=226 && focus.items[3].width>=600);
    /* 向前与向后移动，首尾循环。/ Forward and backward moves wrap at both ends. */
    assert(pn_focus_move(&focus,1)==0 && pn_focus_current(&focus)->code==1);
    assert(pn_focus_move(&focus,-1)==3 && pn_focus_current(&focus)->code==4);
    assert(pn_focus_move(&focus,1)==0);
    pn_focus_scan(&focus,hit,NULL,NULL,0);assert(focus.count==5);
    pn_focus_scan(&focus,hit,NULL,skip,1);assert(pn_focus_move(&focus,-1)==3);
    /* 没有可聚焦项。/ Nothing focusable. */
    pn_focus_t empty;pn_focus_scan(&empty,NULL,NULL,NULL,0);assert(empty.count==0 && pn_focus_move(&empty,1)==-1);
    /* 焦点环画在区域外一圈，区域内部不动。/ The ring goes just outside the area and leaves the inside untouched. */
    pn_pool_t pool;assert(pn_pool_init(&pool,1024*1024,NULL,NULL,NULL)==0);
    uint8_t *bytes=pn_alloc(&pool,684*1216/2);pn_frame_t frame;assert(pn_frame_bind(&frame,bytes,684*1216/2,684,1216));pn_frame_clear(&frame,15);
    pn_focus_draw(&frame,&focus.items[1]);
    int mid_y=focus.items[1].y+focus.items[1].height/2;
    assert(pn_frame_get(&frame,focus.items[1].x-2,mid_y)<15 && pn_frame_get(&frame,focus.items[1].x+focus.items[1].width/2,mid_y)==15);
    pn_free(bytes);assert(!pool.live && !pool.used);return 0;
}
