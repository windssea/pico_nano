/* SPDX-License-Identifier: Apache-2.0
 * 中文：独立原图/按下借用、精确命中、故障与低预算，显示端使用像素替身。
 * English: independent original/press borrows, exact hits, failures and low budgets using a pixel-only display double. */
#include "pn_feedback.h"
#include "pn_cancel.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static pn_feedback_t feedback;static pn_frame_t shown;static unsigned reads,shows,calls;static bool fail_read,fail_show;
static pn_feedback_box_t target={33,45,101,99};static bool cancelled,cancel_on_read;
static bool cancel(void *ctx){(void)ctx;return cancelled;}
static void *cached;static unsigned drops;static bool backing_fail;
static size_t cache_bytes(void *ctx){(void)ctx;return pn_alloc_size(cached);}
static void cache_drop(void *ctx){(void)ctx;drops++;pn_free(cached);cached=NULL;}
static void *allocate(void *ctx,size_t bytes){(void)ctx;return backing_fail?NULL:malloc(bytes);}
static void release(void *ctx,void *ptr){(void)ctx;free(ptr);}
static int hit(void *ctx,int x,int y){calls++;bool hole=ctx!=NULL;return x>=target.x && x<target.x+target.width && y>=target.y && y<target.y+target.height && !(hole && x==target.x+7 && y==target.y+7)?42:-1;}
static pn_status_t read(void *ctx,pn_feedback_box_t b,pn_frame_t *out){
    (void)ctx;reads++;assert(pn_feedback_discard(&feedback)==PN_BUSY);
    for(int y=0;y<b.height;y++)for(int x=0;x<b.width;x++)pn_frame_pixel(out,x,y,pn_frame_get(&shown,b.x+x,b.y+y));
    if(cancel_on_read)cancelled=true;
    return fail_read?PN_IO:PN_OK;
}
static pn_status_t show(void *ctx,const pn_feedback_patch_t *p){
    (void)ctx;assert(p->view==17 && p->hit==42);assert(pn_feedback_discard(&feedback)==PN_BUSY);assert(pn_feedback_present(&feedback,true,show,NULL)==PN_BUSY);shows++;
    for(int y=0;y<p->box.height;y++)for(int x=0;x<p->box.width;x++)pn_frame_pixel(&shown,p->box.x+x,p->box.y+y,pn_frame_get(&p->frame,x,y));
    return fail_show?PN_IO:PN_OK;
}
int main(void){
    static uint8_t screen[342*1216],baseline[sizeof screen];assert(pn_frame_bind(&shown,screen,sizeof screen,684,1216));
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++)pn_frame_pixel(&shown,x,y,(uint8_t)((x+y)%16));
    memcpy(baseline,screen,sizeof screen);
    pn_pool_t pool;assert(!pn_pool_init(&pool,128u*1024u,NULL,NULL,NULL));pn_feedback_box_t b={0};int code=-9;
    assert(pn_feedback_region(hit,NULL,70,80,&b,&code)==PN_OK && code==42 && !memcmp(&b,&target,sizeof b));
    calls=0;b=(pn_feedback_box_t){9,9,9,9};code=-9;assert(pn_feedback_region(hit,(void *)1,70,80,&b,&code)==PN_EMPTY && b.x==9 && code==-9 && calls<PN_FEEDBACK_MAX_PIXELS+4000u);
    assert(pn_feedback_region(hit,NULL,0,0,&b,&code)==PN_EMPTY);
    pn_feedback_box_t small=target;target=(pn_feedback_box_t){33,45,318,230};
    assert(pn_feedback_region(hit,NULL,100,100,&b,&code)==PN_OK && b.y==259 && b.height==16 && b.width==318 && code==42);
    target=(pn_feedback_box_t){0,0,684,1216};assert(pn_feedback_region(hit,NULL,100,100,&b,&code)==PN_EMPTY);target=small;
    assert(pn_feedback_prepare(&feedback,&pool,target,17,42,read,NULL)==PN_OK && pool.used<12000);
    assert(pn_feedback_prepare(&feedback,&pool,target,17,42,read,NULL)==PN_BUSY);
    assert(!memcmp(screen,baseline,sizeof screen));assert(pn_feedback_present(&feedback,true,show,NULL)==PN_OK && memcmp(screen,baseline,sizeof screen));
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++)if(hit(NULL,x,y)<0){pn_frame_t base;assert(pn_frame_bind(&base,baseline,sizeof baseline,684,1216));assert(pn_frame_get(&shown,x,y)==pn_frame_get(&base,x,y));}
    fail_show=true;assert(pn_feedback_present(&feedback,false,show,NULL)==PN_IO && feedback.active);fail_show=false;
    assert(pn_feedback_present(&feedback,false,show,NULL)==PN_OK && !memcmp(screen,baseline,sizeof screen));assert(pn_feedback_discard(&feedback)==PN_OK && pn_feedback_discard(&feedback)==PN_OK && !pool.used && !pool.live);
    fail_read=true;assert(pn_feedback_prepare(&feedback,&pool,target,17,42,read,NULL)==PN_IO && !feedback.active && !pool.used);fail_read=false;
    for(size_t fault=1;fault<=2;fault++){pool.fail_at=pool.attempts+fault;assert(pn_feedback_prepare(&feedback,&pool,target,17,42,read,NULL)==PN_EMPTY && !pool.used && !feedback.active);pool.fail_at=0;}
    pn_feedback_box_t giant={0,0,684,1216},odd={0,0,55,1191};unsigned before=reads;
    assert(pn_feedback_prepare(&feedback,&pool,giant,17,42,read,NULL)==PN_EMPTY && pn_feedback_prepare(&feedback,&pool,odd,17,42,read,NULL)==PN_EMPTY && reads==before);
    assert(pn_feedback_prepare(&feedback,&pool,(pn_feedback_box_t){INT_MAX,0,20,20},17,42,read,NULL)==PN_INVALID);
    pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,NULL);cancelled=true;size_t attempts=pool.attempts;
    assert(pn_feedback_prepare(&feedback,&pool,target,17,42,read,NULL)==PN_CANCELLED && pool.attempts==attempts);
    cancelled=false;cancel_on_read=true;assert(pn_feedback_prepare(&feedback,&pool,target,17,42,read,NULL)==PN_CANCELLED && !pool.used);cancel_on_read=false;cancelled=false;
    assert(pn_feedback_prepare(&feedback,&pool,target,17,42,read,NULL)==PN_OK);cancelled=true;before=shows;assert(pn_feedback_present(&feedback,true,show,NULL)==PN_CANCELLED && shows==before);
    assert(pn_feedback_discard(&feedback)==PN_OK && !pool.used);pn_cancel_leave(&scope);cancelled=false;
    assert(!pn_pool_init(&pool,1024,NULL,NULL,NULL));assert(pn_feedback_prepare(&feedback,&pool,target,17,42,read,NULL)==PN_EMPTY && !pool.used && !pool.live);
    // 可选按下图不能为了准入或底层失败抢掉前台热缓存。/ Optional press pixels never steal foreground hot caches for admission or backing-heap failures.
    assert(!pn_pool_init(&pool,8192,allocate,release,NULL));cached=pn_alloc(&pool,6000);assert(cached);pn_cache_owner_t owner={0};pn_cache_attach(&pool,&owner,PN_CACHE_GLYPH,NULL,cache_bytes,cache_drop);
    size_t used=pool.used;assert(pn_feedback_prepare(&feedback,&pool,target,17,42,read,NULL)==PN_EMPTY && cached && !drops && pool.used==used);
    pool.limit=128u*1024u;backing_fail=true;assert(pn_feedback_prepare(&feedback,&pool,target,17,42,read,NULL)==PN_EMPTY && cached && !drops && pool.used==used);backing_fail=false;
    pn_cache_detach(&owner);pn_free(cached);cached=NULL;assert(!pool.used && !pool.live);
    printf("Feedback: owned odd-width pixels, rectangle/byte caps, immutable borrows, failure recovery, optional allocation fallback and exact original/outer pixels passed; shows=%u\n",shows);return 0;
}
