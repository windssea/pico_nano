/* SPDX-License-Identifier: Apache-2.0
 * 中文：用原逐点算法核对批量4bpp矩形和圆角裁剪，包含灰阶混合与未触及字节。
 * English: compare packed rectangle/rounded spans against the original per-pixel oracle, including blending and untouched bytes. */
#include "pn_widgets.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define CAPACITY (684u*1216u/2u+4096u)
static uint8_t expected[CAPACITY],actual[CAPACITY];
static uint32_t random_state=0x14657u;
static unsigned random_value(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static float root(float value){if(value<=0.0f)return 0.0f;uint32_t bits;memcpy(&bits,&value,4);bits=0x1fbd1df5u+(bits>>1);float v;memcpy(&v,&bits,4);v=0.5f*(v+value/v);v=0.5f*(v+value/v);return 0.5f*(v+value/v);}
static float absf(float value){return value<0?-value:value;}
static float clamp(float value){return value<0?0:value>1?1:value;}
static float distance(float px,float py,float x,float y,float w,float h,float r){float qx=absf(px-(x+w*0.5f))-(w*0.5f-r),qy=absf(py-(y+h*0.5f))-(h*0.5f-r),inner=qx>qy?qx:qy;return root((qx>0?qx*qx:0)+(qy>0?qy*qy:0))+(inner>0?0:inner)-r;}
static void blend(pn_frame_t *f,int x,int y,uint8_t shade,float cov){if(cov<=0.004f)return;if(cov>=0.996f){pn_frame_pixel(f,x,y,shade);return;}int dst=pn_frame_get(f,x,y),v=(int)((float)dst+((float)shade-(float)dst)*cov+0.5f);if(v<0)v=0;if(v>15)v=15;pn_frame_pixel(f,x,y,(uint8_t)v);}
static void oracle(pn_frame_t *f,int x,int y,int w,int h,int r,float thickness,uint8_t shade,int kind){
    if(w<=0 || h<=0 || (kind==2 && thickness<=0))return;
    float cap=(float)(w<h?w:h)*0.5f,radius=r>cap?cap:r<0?0:(float)r,half=thickness*0.5f;
    // 仅遍历可见点；原算法对不可见点不写入，裁剪不改变运算次序或覆盖率。
    // Iterate only visible points; invisible points never wrote anything in the original formula.
    for(int py=0;py<f->height;py++)for(int px=0;px<f->width;px++){
        if((int64_t)px<(int64_t)x-1 || (int64_t)px>=(int64_t)x+w+1 || (int64_t)py<(int64_t)y-1 || (int64_t)py>=(int64_t)y+h+1)continue;
        if(!kind){if(px>=x && (int64_t)px<(int64_t)x+w && py>=y && (int64_t)py<(int64_t)y+h)pn_frame_pixel(f,px,py,shade);continue;}
        float d=distance((float)px+0.5f,(float)py+0.5f,(float)x,(float)y,(float)w,(float)h,radius);
        blend(f,px,py,shade,clamp(kind==2?0.5f-(absf(d+half)-half):0.5f-d));
    }
}
static void check(int fw,int fh,int x,int y,int w,int h,int r,float thickness,uint8_t shade,int kind,unsigned seed){
    size_t stride=((size_t)fw+1)/2+2,bytes=stride*(size_t)fh+32;assert(bytes<=CAPACITY);
    for(size_t i=0;i<bytes;i++)expected[i]=actual[i]=(uint8_t)(i*37u+seed*91u);
    pn_frame_t a,b;assert(pn_frame_bind(&a,actual+16,bytes-32,fw,fh));assert(pn_frame_bind(&b,expected+16,bytes-32,fw,fh));a.stride=b.stride=stride;
    oracle(&b,x,y,w,h,r,thickness,shade,kind);
    if(!kind)pn_frame_rect(&a,x,y,w,h,shade);else if(kind==1)pn_w_round_fill(&a,x,y,w,h,r,shade);else pn_w_round_stroke(&a,x,y,w,h,r,thickness,shade);
    if(memcmp(expected,actual,bytes)){for(size_t i=0;i<bytes;i++)if(expected[i]!=actual[i]){fprintf(stderr,"kind=%d frame=%dx%d box=%d,%d,%d,%d r=%d t=%g shade=%u byte=%zu expected=%u actual=%u\n",kind,fw,fh,x,y,w,h,r,(double)thickness,shade,i,expected[i],actual[i]);break;}assert(0);}
}
int main(void){
    static const float widths[]={0.25f,0.5f,1.0f,1.6f,2.0f,3.25f,8.0f,64.0f};
    for(unsigned n=0;n<6000;n++){int fw=1+(int)(random_value()%43),fh=1+(int)(random_value()%37),x=(int)(random_value()%67)-20,y=(int)(random_value()%61)-20,w=(int)(random_value()%53)-2,h=(int)(random_value()%47)-2,r=(int)(random_value()%47)-4;uint8_t shade=(uint8_t)random_value();
        for(int kind=0;kind<3;kind++)check(fw,fh,x,y,w,h,r,widths[n%8],shade,kind,n);
    }
    static const int shapes[][5]={{32,190,620,304,24},{34,195,616,304,24},{30,220,624,520,24},{0,0,684,1216,0},{32,1122,132,76,22},{52,216,48,48,14},{-9,-4,31,17,200},{11,11,1,1,INT_MAX},{-3,6,8,9,-1},{INT_MIN,0,INT_MAX,12,5},{INT_MAX,0,INT_MAX,12,5}};
    for(size_t n=0;n<sizeof shapes/sizeof shapes[0];n++)for(int kind=0;kind<3;kind++)for(unsigned shade=0;shade<=15;shade+=5)check(684,1216,shapes[n][0],shapes[n][1],shapes[n][2],shapes[n][3],shapes[n][4],widths[n%8],(uint8_t)shade,kind,(unsigned)n);
    puts("packed spans match original pixel oracle: clipping, radii, fractional strokes, gray mixing, odd strides and guards");return 0;
}
