/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：统一的线条图标集（圆角端点、等线宽、抗锯齿），见pn_widgets.h；坐标按图标边长归一化。
 * English: one line-icon set (round caps, even stroke, anti-aliased), see pn_widgets.h; coordinates are normalised to the icon's side length.
 */
#include "pn_widgets.h"

typedef struct {pn_frame_t *frame;float x,y,size,stroke;uint8_t shade;} ctx_t;
/* 归一化坐标换算。/ Convert normalised coordinates. */
static float gx(const ctx_t *c,float u){return c->x+c->size*u;}
static float gy(const ctx_t *c,float v){return c->y+c->size*v;}
static void line(const ctx_t *c,float u0,float v0,float u1,float v1){pn_w_line(c->frame,gx(c,u0),gy(c,v0),gx(c,u1),gy(c,v1),c->stroke,c->shade);}
static void ring(const ctx_t *c,float u,float v,float r){pn_w_ring(c->frame,gx(c,u),gy(c,v),c->size*r,c->stroke,c->shade);}
static void dot(const ctx_t *c,float u,float v,float r){pn_w_dot(c->frame,gx(c,u),gy(c,v),c->size*r,c->shade);}
static void box(const ctx_t *c,float u,float v,float w,float h,float r){
    pn_w_round_stroke(c->frame,(int)(gx(c,u)+0.5f),(int)(gy(c,v)+0.5f),(int)(c->size*w+0.5f),(int)(c->size*h+0.5f),(int)(c->size*r+0.5f),c->stroke,c->shade);
}
/* 以固定15°步长旋转，画圆弧，免用三角函数。/ Draw an arc by rotating in fixed 15 degree steps, so no trigonometry is needed. */
static void arc(const ctx_t *c,float u,float v,float r,float start_u,float start_v,int steps,bool clockwise){
    const float cs=0.9659258f,sn=clockwise?0.2588190f:-0.2588190f;
    float dx=start_u,dy=start_v,px=u+dx*r,py=v+dy*r;
    for(int i=0;i<steps;i++){
        float nx=dx*cs-dy*sn,ny=dx*sn+dy*cs;dx=nx;dy=ny;
        float qx=u+dx*r,qy=v+dy*r;line(c,px,py,qx,qy);px=qx;py=qy;
    }
}
void pn_w_icon(pn_frame_t *frame,pn_icon_t icon,int x,int y,int size,uint8_t shade){
    if(!frame || !frame->pixels || size<16)return;
    float stroke=(float)size/14.0f;if(stroke<2.0f)stroke=2.0f;
    ctx_t c={frame,(float)x,(float)y,(float)size,stroke,shade};
    switch(icon){
    case PN_ICON_SEARCH: ring(&c,0.43f,0.43f,0.30f);line(&c,0.66f,0.66f,0.90f,0.90f);break;
    case PN_ICON_GRID:
        box(&c,0.10f,0.10f,0.34f,0.34f,0.07f);box(&c,0.56f,0.10f,0.34f,0.34f,0.07f);
        box(&c,0.10f,0.56f,0.34f,0.34f,0.07f);box(&c,0.56f,0.56f,0.34f,0.34f,0.07f);break;
    case PN_ICON_LIST:
        dot(&c,0.13f,0.24f,0.05f);dot(&c,0.13f,0.50f,0.05f);dot(&c,0.13f,0.76f,0.05f);
        line(&c,0.32f,0.24f,0.90f,0.24f);line(&c,0.32f,0.50f,0.90f,0.50f);line(&c,0.32f,0.76f,0.90f,0.76f);break;
    case PN_ICON_BACK: line(&c,0.64f,0.16f,0.30f,0.50f);line(&c,0.30f,0.50f,0.64f,0.84f);break;
    case PN_ICON_CHEVRON: line(&c,0.36f,0.16f,0.70f,0.50f);line(&c,0.70f,0.50f,0.36f,0.84f);break;
    case PN_ICON_SHELF:
        box(&c,0.10f,0.16f,0.24f,0.70f,0.05f);box(&c,0.38f,0.16f,0.24f,0.70f,0.05f);
        line(&c,0.70f,0.26f,0.88f,0.20f);line(&c,0.88f,0.20f,0.98f,0.78f);line(&c,0.98f,0.78f,0.80f,0.86f);line(&c,0.80f,0.86f,0.70f,0.26f);break;
    case PN_ICON_TRANSFER:
        line(&c,0.30f,0.86f,0.30f,0.16f);line(&c,0.12f,0.34f,0.30f,0.16f);line(&c,0.30f,0.16f,0.48f,0.34f);
        line(&c,0.70f,0.14f,0.70f,0.84f);line(&c,0.52f,0.66f,0.70f,0.84f);line(&c,0.70f,0.84f,0.88f,0.66f);break;
    case PN_ICON_SETTINGS:{
        static const float rows[3]={0.22f,0.50f,0.78f},knobs[3]={0.66f,0.30f,0.72f};
        for(int i=0;i<3;i++){
            line(&c,0.08f,rows[i],0.92f,rows[i]);
            pn_w_dot(frame,gx(&c,knobs[i]),gy(&c,rows[i]),c.size*0.15f,PN_UI_PAPER);
            ring(&c,knobs[i],rows[i],0.11f);
        }
        break;}
    case PN_ICON_TOC:
        line(&c,0.10f,0.20f,0.90f,0.20f);line(&c,0.30f,0.50f,0.90f,0.50f);line(&c,0.30f,0.80f,0.90f,0.80f);
        dot(&c,0.12f,0.50f,0.045f);dot(&c,0.12f,0.80f,0.045f);break;
    case PN_ICON_BOOKMARK:
        line(&c,0.26f,0.10f,0.74f,0.10f);line(&c,0.74f,0.10f,0.74f,0.90f);line(&c,0.74f,0.90f,0.50f,0.68f);
        line(&c,0.50f,0.68f,0.26f,0.90f);line(&c,0.26f,0.90f,0.26f,0.10f);break;
    case PN_ICON_TYPESET:
        line(&c,0.06f,0.84f,0.32f,0.16f);line(&c,0.32f,0.16f,0.58f,0.84f);line(&c,0.15f,0.62f,0.49f,0.62f);
        ring(&c,0.77f,0.68f,0.13f);line(&c,0.90f,0.55f,0.90f,0.84f);break;
    case PN_ICON_REFRESH:
        arc(&c,0.50f,0.55f,0.30f,0.766f,-0.643f,20,true);
        line(&c,0.38f,0.105f,0.52f,0.245f);line(&c,0.52f,0.245f,0.38f,0.385f);break;
    case PN_ICON_CLOSE: line(&c,0.20f,0.20f,0.80f,0.80f);line(&c,0.80f,0.20f,0.20f,0.80f);break;
    case PN_ICON_PLUS: line(&c,0.16f,0.50f,0.84f,0.50f);line(&c,0.50f,0.16f,0.50f,0.84f);break;
    case PN_ICON_MINUS: line(&c,0.16f,0.50f,0.84f,0.50f);break;
    case PN_ICON_ARROW: line(&c,0.10f,0.50f,0.90f,0.50f);line(&c,0.62f,0.20f,0.90f,0.50f);line(&c,0.90f,0.50f,0.62f,0.80f);break;
    case PN_ICON_FONT:
        line(&c,0.16f,0.20f,0.84f,0.20f);line(&c,0.50f,0.20f,0.50f,0.84f);line(&c,0.34f,0.84f,0.66f,0.84f);
        line(&c,0.16f,0.20f,0.16f,0.34f);line(&c,0.84f,0.20f,0.84f,0.34f);break;
    case PN_ICON_IMAGE:
        box(&c,0.08f,0.16f,0.84f,0.68f,0.08f);dot(&c,0.30f,0.38f,0.07f);
        line(&c,0.10f,0.78f,0.38f,0.52f);line(&c,0.38f,0.52f,0.56f,0.70f);line(&c,0.56f,0.70f,0.70f,0.56f);line(&c,0.70f,0.56f,0.90f,0.76f);break;
    case PN_ICON_LOCK:
        box(&c,0.20f,0.46f,0.60f,0.44f,0.08f);
        line(&c,0.32f,0.46f,0.32f,0.32f);line(&c,0.68f,0.46f,0.68f,0.32f);arc(&c,0.50f,0.32f,0.18f,-1.0f,0.0f,12,true);
        dot(&c,0.50f,0.68f,0.05f);break;
    case PN_ICON_TRASH:
        line(&c,0.12f,0.24f,0.88f,0.24f);line(&c,0.38f,0.24f,0.38f,0.12f);line(&c,0.38f,0.12f,0.62f,0.12f);line(&c,0.62f,0.12f,0.62f,0.24f);
        line(&c,0.22f,0.24f,0.28f,0.90f);line(&c,0.28f,0.90f,0.72f,0.90f);line(&c,0.72f,0.90f,0.78f,0.24f);
        line(&c,0.42f,0.42f,0.42f,0.74f);line(&c,0.58f,0.42f,0.58f,0.74f);break;
    case PN_ICON_CHECK: line(&c,0.14f,0.54f,0.40f,0.80f);line(&c,0.40f,0.80f,0.88f,0.24f);break;
    case PN_ICON_WIFI:
        arc(&c,0.50f,0.82f,0.70f,-0.707f,-0.707f,6,true);arc(&c,0.50f,0.82f,0.46f,-0.707f,-0.707f,6,true);arc(&c,0.50f,0.82f,0.22f,-0.707f,-0.707f,6,true);
        dot(&c,0.50f,0.84f,0.07f);break;
    case PN_ICON_MORE: dot(&c,0.50f,0.20f,0.07f);dot(&c,0.50f,0.50f,0.07f);dot(&c,0.50f,0.80f,0.07f);break;
    case PN_ICON_IMPORT:
        line(&c,0.50f,0.10f,0.50f,0.62f);line(&c,0.30f,0.44f,0.50f,0.64f);line(&c,0.50f,0.64f,0.70f,0.44f);
        line(&c,0.14f,0.66f,0.14f,0.88f);line(&c,0.14f,0.88f,0.86f,0.88f);line(&c,0.86f,0.88f,0.86f,0.66f);break;
    case PN_ICON_PROGRESS: line(&c,0.08f,0.62f,0.92f,0.62f);dot(&c,0.56f,0.62f,0.12f);line(&c,0.56f,0.12f,0.56f,0.36f);line(&c,0.44f,0.26f,0.56f,0.38f);line(&c,0.56f,0.38f,0.68f,0.26f);break;
    default: break;
    }
}
void pn_w_battery(pn_frame_t *frame,int x,int y,int width,int height,int percent,uint8_t shade){
    if(!frame || !frame->pixels || width<16 || height<8)return;
    if(percent<0)percent=0;
    if(percent>100)percent=100;
    int body=width-4;
    pn_w_round_stroke(frame,x,y,body,height,height/4,2.0f,shade);
    pn_w_round_fill(frame,x+body,y+height/4,3,height/2,1,shade);
    int inner=body-8;int fill=inner*percent/100;
    if(fill>0)pn_w_round_fill(frame,x+4,y+4,fill,height-8,1,shade);
}
