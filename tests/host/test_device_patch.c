/* SPDX-License-Identifier: Apache-2.0
 * 中文：局部反馈的旋转/奇偶边界与完整帧转换逐字节对照。
 * English: compare feedback rotation and odd boundaries against full-frame conversion byte for byte. */
#include "device_frame.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
int main(void){
    enum {N=342*1216};static uint8_t logical[N],board[N],expected[N],working[N],patch[32768];pn_frame_t full,part;
    assert(pn_frame_bind(&full,logical,N,684,1216));
    const pn_feedback_box_t boxes[]={{0,0,1,1},{683,1215,1,1},{33,45,101,99},{1,1,683,31},{0,1185,684,31},{572,672,64,36},{0,0,19,1216}};
    for(size_t i=0;i<sizeof boxes/sizeof *boxes;i++){
        pn_feedback_box_t b=boxes[i];pn_frame_clear(&full,15);
        for(int y=0;y<1216;y++)for(int x=0;x<684;x++)pn_frame_pixel(&full,x,y,(uint8_t)((x*7+y*3)%16));
        assert(pn_device_frame_copy(&full,board,N)==PN_OK);memcpy(working,board,N);
        assert(pn_frame_bind(&part,patch,sizeof patch,b.width,b.height));pn_frame_clear(&part,5);
        assert(pn_device_frame_extract(board,N,b,&part)==PN_OK);
        for(int y=0;y<b.height;y++)for(int x=0;x<b.width;x++){assert(pn_frame_get(&part,x,y)==pn_frame_get(&full,b.x+x,b.y+y));uint8_t shade=(uint8_t)((x+y+i)%16);pn_frame_pixel(&part,x,y,shade);pn_frame_pixel(&full,b.x+x,b.y+y,shade);}
        assert(pn_device_frame_patch(&part,b,working,N)==PN_OK && pn_device_frame_copy(&full,expected,N)==PN_OK && !memcmp(working,expected,N));
    }
    assert(pn_device_frame_patch(&part,(pn_feedback_box_t){INT_MAX,0,19,1216},working,N)==PN_INVALID);
    assert(pn_device_frame_extract(board,N-1,boxes[6],&part)==PN_INVALID);
    pn_frame_t overlap=part;overlap.pixels=board+N-10;assert(pn_device_frame_extract(board,N,boxes[6],&overlap)==PN_INVALID);
    overlap.pixels=board;assert(pn_device_frame_patch(&overlap,boxes[6],board,N)==PN_INVALID);
    puts("Board patches: full-frame byte oracle, odd origins/extents, all corners, tall/narrow regions and overlap rejection passed");return 0;
}
