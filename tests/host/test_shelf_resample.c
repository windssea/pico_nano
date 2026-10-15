/* SPDX-License-Identifier: Apache-2.0
 * 中文：缩放快速路径与原逐像素盒滤波完整帧对照。
 * English: full-frame comparison of the scaling fast path with the original per-pixel box filter. */
#include "../../components/pn_library/pn_shelf_view.c"
#include <assert.h>
static void oracle(pn_frame_t *frame,const pn_frame_t *cover,int x,int y,int w,int h){
    for(int dy=0;dy<h;dy++){int y0=dy*cover->height/h,y1=(dy+1)*cover->height/h;if(y1<=y0)y1=y0+1;
        for(int dx=0;dx<w;dx++){int x0=dx*cover->width/w,x1=(dx+1)*cover->width/w;if(x1<=x0)x1=x0+1;unsigned sum=0,n=0;
            for(int sy=y0;sy<y1;sy++)for(int sx=x0;sx<x1;sx++){sum+=pn_frame_get(cover,sx,sy);n++;}
            pn_frame_pixel(frame,x+dx,y+dy,(uint8_t)((sum+n/2)/n));}}
}
int main(void){
    uint8_t *a=malloc(415872),*b=malloc(415872),*pixels=malloc(PN_COVER_BYTES);assert(a && b && pixels);pn_frame_t fast,old,cover;assert(pn_frame_bind(&fast,a,415872,684,1216) && pn_frame_bind(&old,b,415872,684,1216) && pn_frame_bind(&cover,pixels,PN_COVER_BYTES,PN_COVER_WIDTH,PN_COVER_HEIGHT));
    int sizes[][2]={{152,210},{134,186},{80,112},{176,244},{184,256},{198,272},{257,251},{151,209}};
    for(unsigned pattern=0;pattern<3;pattern++){for(size_t i=0;i<PN_COVER_BYTES;i++)pixels[i]=(uint8_t)(pattern==0?i*37:pattern==1?(i%17)*17:0xf0);
        for(unsigned s=0;s<sizeof sizes/sizeof sizes[0];s++)for(unsigned place=0;place<4;place++){memset(a,0x5a,415872);memcpy(b,a,415872);int x=place==0?44:place==1?33:place==2?-2:600,y=place==3?1130:266;
            blit_scaled(&fast,&cover,x,y,sizes[s][0],sizes[s][1]);oracle(&old,&cover,x,y,sizes[s][0],sizes[s][1]);assert(!memcmp(a,b,415872));}}
    free(a);free(b);free(pixels);puts("shelf resample: original box filter and rounding across densities, list/home, all grays, upscaling, clipping and nibble boundaries match every frame byte");return 0;
}
