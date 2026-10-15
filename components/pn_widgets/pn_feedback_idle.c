/* SPDX-License-Identifier: Apache-2.0
 * 中文：反馈空闲定稿调度，单产品owner，不借用页面或调用硬件。
 * English: feedback idle-settle scheduling, one product owner without page borrows or hardware calls. */
#include "pn_feedback.h"
static bool valid(pn_feedback_box_t b){return b.x>=0 && b.y>=0 && b.width>0 && b.height>0 && b.width<=PN_UI_WIDTH-b.x && b.height<=PN_UI_HEIGHT-b.y;}
static uint64_t after(uint64_t now,unsigned delay){return now>UINT64_MAX-delay?UINT64_MAX:now+delay;}
static void advance(pn_feedback_idle_t *s){if(++s->generation==0)s->generation=1;}
void pn_feedback_idle_activity(pn_feedback_idle_t *s,uint64_t now){if(s && s->pending){s->due=after(now,PN_FEEDBACK_IDLE_MS);advance(s);}}
void pn_feedback_idle_note(pn_feedback_idle_t *s,pn_feedback_box_t b,uint64_t now){
    if(!s || !valid(b))return;
    if(s->pending){int right=s->area.x+s->area.width,bottom=s->area.y+s->area.height;
        if(b.x+b.width>right)right=b.x+b.width;
        if(b.y+b.height>bottom)bottom=b.y+b.height;
        if(b.x<s->area.x)s->area.x=b.x;
        if(b.y<s->area.y)s->area.y=b.y;
        s->area.width=right-s->area.x;s->area.height=bottom-s->area.y;
    }else s->area=b;
    s->pending=true;pn_feedback_idle_activity(s,now);
}
void pn_feedback_idle_cover(pn_feedback_idle_t *s,pn_feedback_box_t b){
    if(!s || !s->pending || !valid(b))return;
    if(b.x<=s->area.x && b.y<=s->area.y && b.x+b.width>=s->area.x+s->area.width && b.y+b.height>=s->area.y+s->area.height){s->pending=false;s->area=(pn_feedback_box_t){0};advance(s);}
}
pn_status_t pn_feedback_idle_plan(const pn_feedback_idle_t *s,uint64_t now,bool busy,pn_feedback_idle_plan_t *out){
    if(!s || !out)return PN_INVALID;
    if(!s->pending || busy || now<s->due)return PN_EMPTY;
    if(!valid(s->area))return PN_INVALID;
    int top=s->area.y/64*64,bottom=(s->area.y+s->area.height+63)/64*64;if(bottom>PN_UI_HEIGHT)bottom=PN_UI_HEIGHT;
    *out=(pn_feedback_idle_plan_t){.area={0,top,PN_UI_WIDTH,bottom-top},.generation=s->generation};return PN_OK;
}
void pn_feedback_idle_receipt(pn_feedback_idle_t *s,pn_feedback_idle_plan_t p,pn_status_t status,uint64_t now){
    if(!s || !s->pending || s->generation!=p.generation || !valid(p.area) || p.area.x!=0 || p.area.width!=PN_UI_WIDTH || p.area.y%64 || p.area.height%64 || p.area.y>s->area.y || p.area.y+p.area.height<s->area.y+s->area.height)return;
    if(status==PN_OK){s->completed++;pn_feedback_idle_cover(s,p.area);}
    else{s->failures++;s->due=after(now,PN_FEEDBACK_RETRY_MS);advance(s);}
}
