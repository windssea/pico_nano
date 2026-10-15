/* SPDX-License-Identifier: Apache-2.0
 * 中文：空闲定稿合并、真实回执、旧候选/新输入、失败限频与内容覆盖。
 * English: idle-settle merging, real receipts, stale candidates/new input, rate-limited failures and content coverage. */
#include "pn_feedback.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
int main(void){
    pn_feedback_idle_t s={0};pn_feedback_idle_plan_t p={0},old;
    pn_feedback_idle_activity(&s,50);assert(!s.pending && pn_feedback_idle_plan(&s,1000,false,&p)==PN_EMPTY);
    pn_feedback_idle_note(&s,(pn_feedback_box_t){100,130,90,20},100);assert(s.pending && s.due==420);
    assert(pn_feedback_idle_plan(&s,419,false,&p)==PN_EMPTY && pn_feedback_idle_plan(&s,1000,true,&p)==PN_EMPTY);
    pn_feedback_idle_note(&s,(pn_feedback_box_t){400,260,20,35},200);
    assert(pn_feedback_idle_plan(&s,520,false,&p)==PN_OK && p.area.x==0 && p.area.y==128 && p.area.width==684 && p.area.height==192);old=p;
    pn_feedback_idle_activity(&s,500);pn_feedback_idle_receipt(&s,old,PN_OK,520);assert(s.pending && !s.completed && s.due==820);
    assert(pn_feedback_idle_plan(&s,820,false,&p)==PN_OK);pn_feedback_idle_receipt(&s,p,PN_IO,825);assert(s.pending && s.failures==1 && s.due==1825);
    pn_feedback_idle_receipt(&s,p,PN_OK,826);assert(s.pending && !s.completed);
    assert(pn_feedback_idle_plan(&s,1824,false,&p)==PN_EMPTY && pn_feedback_idle_plan(&s,1825,false,&p)==PN_OK);
    pn_feedback_idle_note(&s,(pn_feedback_box_t){10,64,40,20},1825);pn_feedback_idle_receipt(&s,p,PN_OK,1826);assert(s.pending && !s.completed);
    pn_feedback_idle_cover(&s,(pn_feedback_box_t){0,64,684,64});assert(s.pending);
    assert(pn_feedback_idle_plan(&s,2145,false,&p)==PN_OK && p.area.y==64 && p.area.height==256);
    pn_feedback_idle_receipt(&s,p,PN_OK,2146);assert(!s.pending && s.completed==1);pn_feedback_idle_receipt(&s,p,PN_OK,2147);assert(s.completed==1);
    pn_feedback_idle_note(&s,(pn_feedback_box_t){30,1180,620,36},3000);assert(pn_feedback_idle_plan(&s,3320,false,&p)==PN_OK && p.area.y==1152 && p.area.height==64);
    pn_feedback_idle_cover(&s,(pn_feedback_box_t){0,0,684,1152});assert(s.pending);pn_feedback_idle_cover(&s,(pn_feedback_box_t){0,0,684,1216});assert(!s.pending && s.completed==1);
    pn_feedback_idle_note(&s,(pn_feedback_box_t){INT_MAX,0,20,20},4000);assert(!s.pending);
    pn_feedback_idle_note(&s,(pn_feedback_box_t){0,0,100,100},UINT64_MAX-1);assert(s.due==UINT64_MAX && pn_feedback_idle_plan(&s,UINT64_MAX-1,false,&p)==PN_EMPTY);
    assert(pn_feedback_idle_plan(&s,UINT64_MAX,false,&p)==PN_OK);old=p;p.area.width=20;pn_feedback_idle_receipt(&s,p,PN_OK,UINT64_MAX);assert(s.pending && s.completed==1);
    pn_feedback_idle_receipt(&s,old,PN_CANCELLED,UINT64_MAX);assert(s.pending && s.due==UINT64_MAX);
    puts("Feedback idle: 320 ms quiet merge, held gating, aligned one-scan bands, stale/new-input receipts, backoff, whole/partial coverage and overflow passed");return 0;
}
