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
/// 先用64位边界裁剪，避免不可见区域的循环和整数溢出。/ Clip with 64-bit bounds before iterating invisible pixels or overflowing coordinates.
static bool round_bounds(const pn_frame_t *frame,int x,int y,int width,int height,int *left,int *top,int *right,int *bottom){
    int64_t l=(int64_t)x-1,t=(int64_t)y-1,r=(int64_t)x+width+1,b=(int64_t)y+height+1;
    if(l>=frame->width || t>=frame->height || r<=0 || b<=0)return false;
    *left=l<0?0:(int)l;*top=t<0?0:(int)t;*right=r>frame->width?frame->width:(int)r;*bottom=b>frame->height?frame->height:(int)b;return true;
}
/// UI整数范围内半像素可精确表示；极端坐标仍用原距离式。/ Half pixels are exact within the UI integer range; extreme coordinates retain the original distance formula.
static bool round_spans(int x,int y,int width,int height){return x>=-16384 && x<=16384 && y>=-16384 && y<=16384 && width<=16384 && height<=16384;}
static int clipped(int value,int low,int high){return value<low?low:value>high?high:value;}
void pn_w_round_fill(pn_frame_t *frame,int x,int y,int width,int height,int radius,uint8_t shade){
    if(!frame || !frame->pixels || width<=0 || height<=0)return;
    int left,top,right,bottom;if(!round_bounds(frame,x,y,width,height,&left,&top,&right,&bottom))return;
    float r=limit_radius((float)width,(float)height,(float)radius);bool spans=round_spans(x,y,width,height);int inset=(int)r;if((float)inset<r)inset++;
    for(int py=top;py<bottom;py++){
        // 只有确定覆盖率为1的内区批量填充；所有圆角仍走原抗锯齿公式。
        // Bulk-fill only interiors with certain full coverage; every corner keeps the original antialiasing formula.
        int first=right,last=right;
        if(spans){
            if(py>=y+inset && py<y+height-inset){pn_frame_rect(frame,x,py,width,1,shade);continue;}
            first=clipped(x+inset,left,right);last=clipped(x+width-inset,first,right);
            if(py>=y && py<y+height && last>first)pn_frame_rect(frame,first,py,last-first,1,shade);
        }
        for(int px=left;px<first;px++)put(frame,px,py,shade,clamp01(0.5f-round_distance((float)px+0.5f,(float)py+0.5f,(float)x,(float)y,(float)width,(float)height,r)));
        for(int px=last;px<right;px++)put(frame,px,py,shade,clamp01(0.5f-round_distance((float)px+0.5f,(float)py+0.5f,(float)x,(float)y,(float)width,(float)height,r)));
    }
}
void pn_w_round_stroke(pn_frame_t *frame,int x,int y,int width,int height,int radius,float thickness,uint8_t shade){
    if(!frame || !frame->pixels || width<=0 || height<=0 || thickness<=0.0f)return;
    int left,top,right,bottom;if(!round_bounds(frame,x,y,width,height,&left,&top,&right,&bottom))return;
    float r=limit_radius((float)width,(float)height,(float)radius),half=thickness*0.5f;
    int inset=0;bool spans=round_spans(x,y,width,height) && thickness<16384.0f;
    if(spans){float padding=r+thickness+1.0f;inset=(int)padding;if((float)inset<padding)inset++;}
    for(int py=top;py<bottom;py++){
        // 深处覆盖率确定为0，不计算透明描边内部；可见边缘的运算保持不变。
        // Skip the certainly transparent deep interior; preserve all arithmetic along visible edges.
        int first=right,last=right;
        if(spans && py>=y+inset && py<y+height-inset){first=clipped(x+inset,left,right);last=clipped(x+width-inset,first,right);}
        else if(spans){
            // 上下圆角带的中段距直边距离只随行变化：每行算一次覆盖率，逐像素混合同值，数值与逐点公式相同。
            // In the top/bottom corner bands the middle span's distance depends only on the row: compute coverage once per row and blend the same value per pixel, numerically identical to the per-point formula.
            int mid_first=clipped(x+inset,left,right),mid_last=clipped(x+width-inset,mid_first,right);
            // 仅当行向分量不小于中段两端列向分量时，距离与列无关；小矩形中部行退回逐点。/ Only when the row component is at least the column component at both span ends is the distance column-independent; middle rows of small rectangles fall back to per-point.
            float cx=(float)x+(float)width*0.5f,cy=(float)y+(float)height*0.5f,half_w=(float)width*0.5f-r,qy=absf((float)py+0.5f-cy)-((float)height*0.5f-r);
            float qxl=absf((float)mid_first+0.5f-cx)-half_w,qxr=absf((float)(mid_last-1)+0.5f-cx)-half_w;
            if(mid_last>mid_first && qy>=qxl && qy>=qxr){
                float d=round_distance((float)mid_first+0.5f,(float)py+0.5f,(float)x,(float)y,(float)width,(float)height,r)+half;
                float cov=clamp01(0.5f-(absf(d)-half));
                if(cov>0.004f)for(int px=mid_first;px<mid_last;px++)put(frame,px,py,shade,cov);
                first=mid_first;last=mid_last;}
        }
        for(int section=0;section<2;section++)for(int px=section?last:left;px<(section?right:first);px++){
            float d=round_distance((float)px+0.5f,(float)py+0.5f,(float)x,(float)y,(float)width,(float)height,r)+half;
            put(frame,px,py,shade,clamp01(0.5f-(absf(d)-half)));
        }
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
