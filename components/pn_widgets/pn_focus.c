/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：三键焦点导航，见pn_focus.h。
 * English: three-key focus navigation, see pn_focus.h.
 */
#include "pn_focus.h"
#include <string.h>
#define STEP 6 /* 扫描步长，兼顾精度与命中函数调用次数 / Scan step balancing precision against hit-function calls */
#define ROW_TOLERANCE 24 /* 上沿相差不超过它视为同一行 / Tops closer than this count as one row */
static bool skipped(const int *skip,size_t count,int code){for(size_t i=0;skip && i<count;i++)if(skip[i]==code)return true;return false;}
/* a排在b之前：行靠上优先，同行靠左优先。/ Whether a comes before b: higher rows first, then further left within a row. */
static bool before(const pn_focus_item_t *a,const pn_focus_item_t *b){
    int dy=a->y-b->y;if(dy<0)dy=-dy;
    if(dy<ROW_TOLERANCE)return a->x<b->x;
    return a->y<b->y;
}
void pn_focus_scan(pn_focus_t *focus,pn_focus_hit_fn hit,void *ctx,const int *skip,size_t skip_count){
    if(!focus)return;
    memset(focus,0,sizeof *focus);focus->index=-1;
    if(!hit)return;
    for(int y=0;y<PN_UI_HEIGHT;y+=STEP){
        for(int x=0;x<PN_UI_WIDTH;x+=STEP){
            int code=hit(ctx,x,y);
            if(code<0 || skipped(skip,skip_count,code))continue;
            size_t i=0;while(i<focus->count && focus->items[i].code!=code)i++;
            if(i==focus->count){
                if(focus->count>=PN_FOCUS_MAX)continue;
                focus->items[i]=(pn_focus_item_t){code,x,y,STEP,STEP};focus->count++;
            }else{
                pn_focus_item_t *item=&focus->items[i];
                int right=item->x+item->width,bottom=item->y+item->height;
                if(x<item->x)item->x=x;
                if(y<item->y)item->y=y;
                if(x+STEP>right)right=x+STEP;
                if(y+STEP>bottom)bottom=y+STEP;
                item->width=right-item->x;item->height=bottom-item->y;
            }
        }
    }
    // 插入排序，项数很少。/ Insertion sort, the count is tiny.
    for(size_t i=1;i<focus->count;i++){
        pn_focus_item_t item=focus->items[i];size_t j=i;
        while(j>0 && before(&item,&focus->items[j-1])){focus->items[j]=focus->items[j-1];j--;}
        focus->items[j]=item;
    }
}
int pn_focus_move(pn_focus_t *focus,int delta){
    if(!focus || !focus->count)return -1;
    int count=(int)focus->count;
    if(focus->index<0)focus->index=delta>0?0:count-1;
    else focus->index=(focus->index+(delta>0?1:count-1))%count;
    return focus->index;
}
const pn_focus_item_t *pn_focus_current(const pn_focus_t *focus){
    if(!focus || focus->index<0 || (size_t)focus->index>=focus->count)return NULL;
    return &focus->items[focus->index];
}
void pn_focus_draw(pn_frame_t *frame,const pn_focus_item_t *item){
    if(!frame || !item)return;
    pn_w_round_stroke(frame,item->x-4,item->y-4,item->width+8,item->height+8,16,4.0f,PN_UI_INK);
}
