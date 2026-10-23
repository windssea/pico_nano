/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：原创像素熊猫竹林；整数像素，头部、手臂和竹叶配合咀嚼。
 * English: original pixel panda grove; integer pixels with coordinated head, paw and bamboo chewing poses.
 */
#include "pn_widgets.h"

static void pixel(pn_frame_t *f,int x,int y,int px,int py,int w,int h,uint8_t ink){pn_frame_rect(f,x+px*4,y+py*4,w*4,h*4,ink);}
static void bamboo(pn_frame_t *f,int x,int y,int stalk,int top,int bottom,uint8_t ink){
    pixel(f,x,y,stalk,top,1,bottom-top,ink);
    for(int node=top+6;node<bottom;node+=8)pixel(f,x,y,stalk,node,1,1,15);
    pixel(f,x,y,stalk-3,top+3,2,1,ink);pixel(f,x,y,stalk-2,top+4,2,1,ink);
    pixel(f,x,y,stalk+1,top+7,2,1,ink);pixel(f,x,y,stalk+2,top+6,2,1,ink);
    pixel(f,x,y,stalk-3,top+14,2,1,ink);pixel(f,x,y,stalk-2,top+15,2,1,ink);
}
static void shoot(pn_frame_t *f,int x,int y,int left,int top){
    pixel(f,x,y,left+2,top,1,2,5);pixel(f,x,y,left+1,top+2,3,2,5);pixel(f,x,y,left,top+4,5,3,5);
    pixel(f,x,y,left+1,top+3,2,1,12);pixel(f,x,y,left+1,top+5,3,1,12);
}
void pn_w_loading_panda(pn_frame_t *f,int x,int y,unsigned phase){
    if(!f || !f->pixels)return;
    unsigned pose=phase%4;bool bite=pose==1 || pose==2;
    static const int bob[4]={0,1,2,1},lift[4]={0,2,3,1};
    // 竹丛、竹笋和落地脚保持稳定；头和爪有清晰的两到三格动作。/ Grove, shoots and grounded feet stay still; head and paws move by two to three visible pixels.
    bamboo(f,x,y,5,5,32,9);bamboo(f,x,y,11,1,30,7);
    bamboo(f,x,y,52,2,31,7);bamboo(f,x,y,58,7,33,9);
    pixel(f,x,y,2,34,60,1,12);shoot(f,x,y,3,28);shoot(f,x,y,44,27);shoot(f,x,y,55,28);
    x+=12*4;
    // 圆肚皮与外展脚掌组成坐姿，避免方躯干像直立双腿。/ A rounded belly and outward feet make a seated pose, avoiding a square torso with standing legs.
    pixel(f,x,y,11,17,16,15,0);pixel(f,x,y,9,20,20,10,0);pixel(f,x,y,8,23,22,5,0);
    pixel(f,x,y,8,29,7,3,0);pixel(f,x,y,7,30,9,2,0);
    pixel(f,x,y,24,29,7,3,0);pixel(f,x,y,23,30,9,2,0);
    pixel(f,x,y,14,20,10,11,15);pixel(f,x,y,12,22,14,7,15);pixel(f,x,y,13,29,12,1,15);
    pixel(f,x,y,8,19-lift[pose],4,6,0);pixel(f,x,y,7,20-lift[pose],5,4,0);
    pixel(f,x,y,27,19-lift[pose],4,6,0);pixel(f,x,y,27,20-lift[pose],5,4,0);
    int head=y+bob[pose]*4;
    pixel(f,x,head,8,1,5,5,0);pixel(f,x,head,24,1,5,5,0);
    pixel(f,x,head,8,4,21,15,0);pixel(f,x,head,6,6,25,11,0);pixel(f,x,head,10,3,17,17,0);
    pixel(f,x,head,9,5,19,13,15);pixel(f,x,head,7,7,23,9,15);pixel(f,x,head,11,4,15,15,15);
    pixel(f,x,head,11,8,4,6,0);pixel(f,x,head,10,9,6,4,0);
    pixel(f,x,head,22,8,4,6,0);pixel(f,x,head,21,9,6,4,0);
    if(pose!=2){pixel(f,x,head,12+(pose==3),9,1,2,15);pixel(f,x,head,23+(pose==3),9,1,2,15);}
    pixel(f,x,head,17,12,3,2,0);
    pixel(f,x,head,18,14,1,2,0);pixel(f,x,head,17,15,3,bite?3:1,0);
    pixel(f,x,head,9,14,2,1,12);pixel(f,x,head,26,14,2,1,12);
    // 手中竹枝上抬，与低头张嘴交错；无需贴图解码或临时画布。/ The held sprig rises against the nodding, open mouth, without texture decoding or a temporary canvas.
    bamboo(f,x,y,34,5,31,5);
    for(int i=0;i<9;i++)pixel(f,x,y,19+i,21-i/2-lift[pose],2,1,5);
    pixel(f,x,y,28,16-lift[pose],3,1,5);pixel(f,x,y,30,14-lift[pose],2,2,5);
    pixel(f,x,y,24,20-lift[pose],4,3,0);pixel(f,x,y,25,20-lift[pose],1,1,15);
}
void pn_w_loading_pose(pn_frame_t *f,int y,unsigned phase){
    if(!f || !f->pixels)return;
    if(y<0)y=(f->height-300)/2;
    int x=f->width/2-128;
    // 只清除插画实际边界，保留嵌入式卡片上沿；四姿态共用该范围。/ Clear only the shared artwork bounds, retaining the embedded card's top border across all four poses.
    pn_frame_rect(f,x+8,y+4,240,136,15);pn_w_loading_panda(f,x,y,phase);
}
void pn_loading_motion_begin(pn_loading_motion_t *m,uint64_t now){if(m){m->last_ms=now;m->phase=0;m->active=true;m->advanced=false;}}
bool pn_loading_motion_poll(pn_loading_motion_t *m,uint64_t now){
    if(!m || !m->active)return false;
    if(now<m->last_ms){m->last_ms=now;return false;}
    if(now-m->last_ms<(m->advanced?750u:100u))return false;
    // 第一次直接低头咬竹叶，让短开书也有可见变化；之后恢复四姿态循环。/ Start with the deeper bite so short openings have a visible change, then resume the four-pose cycle.
    m->last_ms=now;m->phase=m->advanced?(m->phase+1)%4:2;m->advanced=true;return true;
}
