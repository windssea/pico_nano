/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：抗锯齿图元（线、圆、圆角矩形）与反相，见pn_widgets.h；只改帧缓冲，4bpp灰阶混合。
 * English: anti-aliased primitives (lines, circles, rounded rectangles) and inversion, see pn_widgets.h; they only touch the frame buffer and blend in 4 bpp grays.
 */
#include "pn_widgets.h"
#include <stdint.h>
#include <string.h>

/* 平方根：位技巧起步加三次牛顿迭代，不依赖libm，精度远高于4bpp所需。
 * Square root: a bit-trick estimate plus three Newton steps, no libm, far more precise than 4 bpp needs. */
static float root(float value){
    if(value<=0.0f)return 0.0f;
    uint32_t bits;memcpy(&bits,&value,sizeof bits);bits=0x1fbd1df5u+(bits>>1);
    float x;memcpy(&x,&bits,sizeof x);
    x=0.5f*(x+value/x);x=0.5f*(x+value/x);x=0.5f*(x+value/x);return x;
}
static float absf(float v){return v<0.0f?-v:v;}
static float clamp01(float v){return v<0.0f?0.0f:v>1.0f?1.0f:v;}
/* 以覆盖率cov把shade混进像素。/ Blend shade into a pixel by coverage cov. */
static void put(pn_frame_t *frame,int x,int y,uint8_t shade,float cov){
    if(cov<=0.004f || x<0 || y<0 || x>=frame->width || y>=frame->height)return;
    if(cov>=0.996f){pn_frame_pixel(frame,x,y,shade);return;}
    int dst=pn_frame_get(frame,x,y);float mixed=(float)dst+((float)shade-(float)dst)*cov;
    int value=(int)(mixed+0.5f);if(value<0)value=0;if(value>15)value=15;pn_frame_pixel(frame,x,y,(uint8_t)value);
}
/* 点到线段的距离。/ Distance from a point to a segment. */
static float segment(float px,float py,float x0,float y0,float x1,float y1){
    float dx=x1-x0,dy=y1-y0,len2=dx*dx+dy*dy,t=len2>0.0f?((px-x0)*dx+(py-y0)*dy)/len2:0.0f;
    if(t<0.0f)t=0.0f;else if(t>1.0f)t=1.0f;
    float ex=px-(x0+dx*t),ey=py-(y0+dy*t);return root(ex*ex+ey*ey);
}
void pn_w_line(pn_frame_t *frame,float x0,float y0,float x1,float y1,float width,uint8_t shade){
    if(!frame || !frame->pixels || width<=0.0f)return;
    float half=width*0.5f;
    int left=(int)(x0<x1?x0:x1)-(int)half-2,right=(int)(x0>x1?x0:x1)+(int)half+3,top=(int)(y0<y1?y0:y1)-(int)half-2,bottom=(int)(y0>y1?y0:y1)+(int)half+3;
    for(int y=top;y<bottom;y++)for(int x=left;x<right;x++)put(frame,x,y,shade,clamp01(0.5f-(segment((float)x+0.5f,(float)y+0.5f,x0,y0,x1,y1)-half)));
}
void pn_w_dot(pn_frame_t *frame,float cx,float cy,float radius,uint8_t shade){
    if(!frame || !frame->pixels || radius<=0.0f)return;
    for(int y=(int)(cy-radius)-2;y<(int)(cy+radius)+3;y++)for(int x=(int)(cx-radius)-2;x<(int)(cx+radius)+3;x++){
        float dx=(float)x+0.5f-cx,dy=(float)y+0.5f-cy;put(frame,x,y,shade,clamp01(0.5f-(root(dx*dx+dy*dy)-radius)));
    }
}
void pn_w_ring(pn_frame_t *frame,float cx,float cy,float radius,float width,uint8_t shade){
    if(!frame || !frame->pixels || radius<=0.0f || width<=0.0f)return;
    float half=width*0.5f,reach=radius+half;
    for(int y=(int)(cy-reach)-2;y<(int)(cy+reach)+3;y++)for(int x=(int)(cx-reach)-2;x<(int)(cx+reach)+3;x++){
        float dx=(float)x+0.5f-cx,dy=(float)y+0.5f-cy;put(frame,x,y,shade,clamp01(0.5f-(absf(root(dx*dx+dy*dy)-radius)-half)));
    }
}
/* 圆角矩形的有符号距离（内部为负）。/ Signed distance to a rounded rectangle (negative inside). */
static float round_distance(float px,float py,float x,float y,float w,float h,float radius){
    float cx=x+w*0.5f,cy=y+h*0.5f,qx=absf(px-cx)-(w*0.5f-radius),qy=absf(py-cy)-(h*0.5f-radius);
    float ox=qx>0.0f?qx:0.0f,oy=qy>0.0f?qy:0.0f,inner=qx>qy?qx:qy;if(inner>0.0f)inner=0.0f;
    return root(ox*ox+oy*oy)+inner-radius;
}
static float limit_radius(float w,float h,float radius){float cap=(w<h?w:h)*0.5f;return radius>cap?cap:radius<0.0f?0.0f:radius;}
void pn_w_round_fill(pn_frame_t *frame,int x,int y,int width,int height,int radius,uint8_t shade){
    if(!frame || !frame->pixels || width<=0 || height<=0)return;
    float r=limit_radius((float)width,(float)height,(float)radius);
    for(int py=y-1;py<y+height+1;py++)for(int px=x-1;px<x+width+1;px++)put(frame,px,py,shade,clamp01(0.5f-round_distance((float)px+0.5f,(float)py+0.5f,(float)x,(float)y,(float)width,(float)height,r)));
}
void pn_w_round_stroke(pn_frame_t *frame,int x,int y,int width,int height,int radius,float thickness,uint8_t shade){
    if(!frame || !frame->pixels || width<=0 || height<=0 || thickness<=0.0f)return;
    float r=limit_radius((float)width,(float)height,(float)radius),half=thickness*0.5f;
    for(int py=y-1;py<y+height+1;py++)for(int px=x-1;px<x+width+1;px++){
        float d=round_distance((float)px+0.5f,(float)py+0.5f,(float)x,(float)y,(float)width,(float)height,r)+half;
        put(frame,px,py,shade,clamp01(0.5f-(absf(d)-half)));
    }
}
void pn_w_invert_round(pn_frame_t *frame,int x,int y,int width,int height,int radius){
    if(!frame || !frame->pixels || width<=0 || height<=0)return;
    float r=limit_radius((float)width,(float)height,(float)radius);
    for(int py=y;py<y+height;py++)for(int px=x;px<x+width;px++){
        if(px<0 || py<0 || px>=frame->width || py>=frame->height)continue;
        float cov=clamp01(0.5f-round_distance((float)px+0.5f,(float)py+0.5f,(float)x,(float)y,(float)width,(float)height,r));
        if(cov<=0.004f)continue;
        int v=pn_frame_get(frame,px,py);put(frame,px,py,(uint8_t)(15-v),cov);
    }
}
void pn_w_mask_corners(pn_frame_t *frame,int x,int y,int width,int height,int radius,uint8_t paper){
    if(!frame || !frame->pixels || width<=0 || height<=0 || radius<=0)return;
    float r=limit_radius((float)width,(float)height,(float)radius);int span=(int)r+1;
    for(int py=0;py<height;py++){
        if(py>=span && py<height-span)continue;
        for(int px=0;px<width;px++){
            if(px>=span && px<width-span)continue;
            float cov=clamp01(0.5f+round_distance((float)(x+px)+0.5f,(float)(y+py)+0.5f,(float)x,(float)y,(float)width,(float)height,r));
            put(frame,x+px,y+py,paper,cov);
        }
    }
}
