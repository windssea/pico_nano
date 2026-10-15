/* SPDX-License-Identifier: Apache-2.0
 * 中文：内容滑动/长按的所有权、取消与释放检查。
 * English: ownership, cancellation and release checks for content swipes and holds. */
#include "pn_shelf_view.h"
#include <assert.h>
#include <stdio.h>
static bool feed(pn_shelf_gesture_t *g,unsigned n,int x,int y,bool valid,int book,uint64_t t,int *out){return pn_shelf_gesture_feed(g,true,true,n,x,y,valid,book,t,out);}
int main(void){
    pn_shelf_gesture_t g={0};int out=-1;
    assert(!feed(&g,1,500,500,true,2,0,&out));assert(feed(&g,1,180,510,true,0,150,&out) && out==-1);
    assert(feed(&g,0,180,510,true,-1,200,&out) && out==-1);assert(feed(&g,0,180,510,true,-1,210,&out) && out==PN_SHELF_NEXT && !g.down);
    out=-1;assert(!feed(&g,1,150,880,true,8,300,&out));assert(feed(&g,1,480,890,true,11,440,&out));assert(feed(&g,0,0,0,true,-1,450,&out));assert(feed(&g,0,0,0,true,-1,460,&out) && out==PN_SHELF_PREVIOUS);
    out=-1;assert(!feed(&g,1,200,500,true,4,1000,&out));assert(!feed(&g,1,200,500,true,4,1599,&out));assert(feed(&g,1,205,502,true,4,1600,&out) && out==PN_SHELF_MORE+4);
    out=-1;assert(feed(&g,1,205,502,true,4,3000,&out) && out==-1);assert(feed(&g,0,0,0,true,-1,3010,&out));assert(feed(&g,0,0,0,true,-1,3020,&out) && out==-1);
    for(unsigned mode=0;mode<6;mode++){
        g=(pn_shelf_gesture_t){0};out=-1;assert(!feed(&g,1,400,500,true,1,0,&out));
        if(mode==0)assert(feed(&g,1,390,700,true,4,100,&out));
        if(mode==1)assert(feed(&g,1,350,505,true,1,100,&out));
        if(mode==2)(void)feed(&g,2,400,500,true,1,100,&out);
        if(mode==3)(void)feed(&g,1,400,500,false,1,100,&out);
        if(mode==4)(void)pn_shelf_gesture_feed(&g,false,true,1,400,500,true,1,100,&out);
        if(mode==5){g=(pn_shelf_gesture_t){0};assert(!feed(&g,1,400,150,true,-1,0,&out));}
        (void)feed(&g,1,mode==1?350:100,500,true,0,1000,&out);(void)feed(&g,0,0,0,true,-1,1010,&out);(void)feed(&g,0,0,0,true,-1,1020,&out);assert(out==-1 && !g.down);
    }
    for(int book=0;book<PN_CATALOG_PAGE_MAX;book++){g=(pn_shelf_gesture_t){0};out=-1;assert(!feed(&g,1,200,500,true,book,0,&out));assert(feed(&g,1,200,500,true,book,600,&out) && out==PN_SHELF_MORE+book);}
    g=(pn_shelf_gesture_t){0};out=-1;(void)feed(&g,1,500,500,true,0,0,&out);assert(feed(&g,1,100,500,true,0,100,&out));assert(pn_shelf_gesture_feed(&g,false,true,0,0,0,true,-1,200,&out));assert(pn_shelf_gesture_feed(&g,false,true,0,0,0,true,-1,210,&out) && out==-1);
    assert(PN_SHELF_TOGGLE>=PN_CATALOG_PAGE_MAX && PN_SHELF_CONTINUE>=PN_CATALOG_PAGE_MAX);
    g=(pn_shelf_gesture_t){0};out=-1;assert(!feed(&g,1,550,500,true,2,0,&out));
    assert(feed(&g,1,150,610,true,0,100,&out));assert(feed(&g,0,0,0,true,-1,110,&out));
    assert(feed(&g,0,0,0,true,-1,120,&out) && out==PN_SHELF_NEXT);
    puts("shelf gestures: both directions, 12 book identities, hold once, movement, vertical/short drag, invalid/multitouch/modal/header cancellation and release passed");return 0;
}
