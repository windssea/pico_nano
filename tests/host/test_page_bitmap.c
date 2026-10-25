/* SPDX-License-Identifier: Apache-2.0
 * 中文：部分像素不可呈现、当前帧不变、借用回收保护、取消和预算退路。
 * English: no partial presentation, immutable current frames, borrow protection, cancellation and budget fallback. */
#include "pn_page_bitmap.h"
#include "pn_cancel.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct {pn_pool_t *pool;bool fail;size_t largest;bool *cancel;} paint_t;
static bool stop(void *ctx){return *(bool *)ctx;}
static pn_status_t paint(void *ctx,pn_frame_t *frame,size_t first,size_t count,unsigned *missing){
    paint_t *p=ctx;if(count>p->largest)p->largest=count;
    assert(!pn_cache_reclaim(p->pool,SIZE_MAX));
    for(size_t i=first;i<first+count;i++)pn_frame_rect(frame,(int)i*2,40,2,2,(uint8_t)(i%16));
    (*missing)++;if(p->cancel)*p->cancel=true;return p->fail?PN_IO:PN_OK;
}
int main(void){
    pn_pool_t pool;assert(!pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL));pn_page_bitmap_t cache={0};pn_page_bitmap_init(&cache,&pool);
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,684u*1216u/2u);assert(pixels);pn_frame_t shown;assert(pn_frame_bind(&shown,pixels,684u*1216u/2u,684,1216));pn_frame_clear(&shown,11);
    unsigned missing=99;assert(pn_page_bitmap_copy(&cache,&shown,&missing)==PN_EMPTY && missing==99);
    assert(pn_page_bitmap_begin(&cache,17)==PN_OK);paint_t ctx={.pool=&pool};
    for(unsigned i=0;i<3;i++){assert(pn_page_bitmap_step(&cache,paint,&ctx,7)==PN_OK);for(size_t j=0;j<684u*1216u/2u;j++)assert(pixels[j]==0xbb);if(i<2)assert(pn_page_bitmap_copy(&cache,&shown,&missing)==PN_EMPTY && missing==99);}
    assert(ctx.largest==7 && cache.ready && cache.cursor==17 && pn_page_bitmap_copy(&cache,&shown,&missing)==PN_OK && missing==3);
    for(int i=0;i<17;i++)assert(pn_frame_get(&shown,i*2,40)==i%16);
    /* 带填充行不可覆盖尾部；取消拷贝不改当前帧或缺字计数。/ Padded rows keep their tails; cancelled copies preserve the current frame and missing count. */
    uint8_t *padded=pn_alloc_tagged(&pool,PN_MEM_FRAME,350u*1216u);assert(padded);memset(padded,0xa5,350u*1216u);
    pn_frame_t padded_frame=shown;padded_frame.pixels=padded;padded_frame.stride=350;
    assert(pn_page_bitmap_copy(&cache,&padded_frame,&missing)==PN_OK && missing==3);
    for(size_t y=0;y<1216;y++){assert(!memcmp(padded+y*350,pixels+y*342,342));for(size_t x=342;x<350;x++)assert(padded[y*350+x]==0xa5);}
    pn_free(padded);bool copy_cancelled=true;pn_cancel_scope_t copy_scope;pn_cancel_enter(&copy_scope,stop,&copy_cancelled);missing=99;memset(pixels,0x66,415872);
    assert(pn_page_bitmap_copy(&cache,&shown,&missing)==PN_CANCELLED && missing==99);for(size_t i=0;i<415872;i++)assert(pixels[i]==0x66);pn_cancel_leave(&copy_scope);
    assert(pn_page_bitmap_begin(&cache,17)==PN_OK);ctx.fail=true;assert(pn_page_bitmap_step(&cache,paint,&ctx,7)==PN_IO && !cache.ready && !cache.working);ctx.fail=false;
    assert(pn_page_bitmap_begin(&cache,17)==PN_OK);bool cancelled=true;pn_cancel_scope_t scope;pn_cancel_enter(&scope,stop,&cancelled);assert(pn_page_bitmap_step(&cache,paint,&ctx,7)==PN_CANCELLED && !cache.ready);pn_cancel_leave(&scope);
    assert(pn_page_bitmap_begin(&cache,17)==PN_OK);cancelled=false;ctx.cancel=&cancelled;pn_cancel_enter(&scope,stop,&cancelled);assert(pn_page_bitmap_step(&cache,paint,&ctx,7)==PN_CANCELLED && !cache.ready && pn_page_bitmap_copy(&cache,&shown,&missing)==PN_EMPTY);pn_cancel_leave(&scope);ctx.cancel=NULL;
    pn_cache_reclaim(&pool,SIZE_MAX);assert(!cache.frame.pixels && !cache.ready);
    pool.cache_caps[PN_CACHE_BITMAP]=0;assert(pn_page_bitmap_begin(&cache,17)==PN_EMPTY);pool.cache_caps[PN_CACHE_BITMAP]=416u*1024u;
    pool.fail_at=pool.attempts+1;assert(pn_page_bitmap_begin(&cache,17)==PN_EMPTY);pool.fail_at=0;assert(pn_page_bitmap_begin(&cache,17)==PN_OK);
    pn_page_bitmap_enable(&cache,false);assert(!cache.frame.pixels && pn_page_bitmap_begin(&cache,17)==PN_EMPTY);pn_page_bitmap_enable(&cache,true);
    pn_page_bitmap_close(&cache);pn_page_bitmap_close(&cache);pn_free(pixels);assert(!pool.used && !pool.live && !pool.caches);
    puts("Page bitmap: bounded steps, unchanged scanned frame, full-only copy, reclaim pins, cancel/failure invalidation and low-budget fallback passed");return 0;
}
