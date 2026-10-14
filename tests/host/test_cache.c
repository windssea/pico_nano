/* SPDX-License-Identifier: Apache-2.0
 * 中文：预算、回收顺序、借用和前台重试的故障回归。/ English: failure regressions for budgets, eviction order, borrowing and foreground retry. */
#include "pn_alloc.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct {pn_cache_owner_t owner;void *data;unsigned id;unsigned *order,*count;} cache_t;
static size_t bytes(void *ctx){return pn_alloc_size(((cache_t *)ctx)->data);}
static void drop(void *ctx){cache_t *c=ctx;c->order[(*c->count)++]=c->id;pn_free(c->data);c->data=NULL;}
typedef struct {unsigned calls,fail;} heap_t;
static void *allocate(void *ctx,size_t n){heap_t *h=ctx;h->calls++;return h->calls==h->fail?NULL:malloc(n);}
static void release(void *ctx,void *ptr){(void)ctx;free(ptr);}
int main(void){
    pn_pool_t p;assert(!pn_pool_init(&p,8192,NULL,NULL,NULL));p.cache_reserve=512;
    unsigned order[12]={0},count=0;cache_t c[3]={0};
    // 逆序注册也必须按类别回收，不碰前台帧或保存块。/ Reverse registration must still reclaim by class, never foreground frames or save blocks.
    void *current=pn_alloc_tagged(&p,PN_MEM_FRAME,1000),*save=pn_alloc(&p,200);assert(current && save);memset(current,0x5a,1000);memset(save,0xa5,200);
    for(int i=2;i>=0;i--){c[i].id=(unsigned)i;c[i].order=order;c[i].count=&count;c[i].data=pn_alloc(&p,500);assert(c[i].data);pn_cache_attach(&p,&c[i].owner,(pn_cache_class_t)i,&c[i],bytes,drop);}
    pn_cache_snapshot(&p);assert(p.cache_stats[0].bytes==pn_alloc_size(c[0].data));
    size_t original=p.used;p.fail_at=p.attempts+1;assert(!pn_alloc(&p,7000) && p.used==original && !count);p.fail_at=0;
    // 可回收字节按类别掩码统计，借用中的owner不计。/ Reclaimable bytes follow the kind mask and exclude pinned owners.
    {size_t all=pn_cache_reclaimable(&p,7u),one=pn_cache_reclaimable(&p,1u<<1);assert(all>one && one>0 && !pn_cache_reclaimable(&p,0));
     pn_cache_pin(&c[1].owner);assert(!pn_cache_reclaimable(&p,1u<<1) && pn_cache_reclaimable(&p,7u)==all-one);pn_cache_unpin(&c[1].owner);}
    pn_cache_pin(&c[0].owner);assert(pn_cache_reclaim(&p,SIZE_MAX)>0 && count==2 && order[0]==1 && order[1]==2 && c[0].data);
    pn_cache_unpin(&c[0].owner);assert(pn_cache_reclaim(&p,SIZE_MAX)>0 && count==3 && order[2]==0);
    for(unsigned i=0;i<1000;i++)assert(((unsigned char *)current)[i]==0x5a);
    for(unsigned i=0;i<200;i++)assert(((unsigned char *)save)[i]==0xa5);
    c[0].data=pn_cache_alloc(&p,PN_CACHE_NEIGHBOR,PN_MEM_LAYOUT,600);assert(c[0].data);
    size_t freed=pn_alloc_size(c[0].data);p.limit=p.used+32;
    void *foreground=pn_alloc(&p,400);assert(foreground && !c[0].data && count==4 && p.cache_stats[0].reclaimed>=freed);
    pn_free(foreground);p.limit=8192;p.cache_caps[0]=64;assert(!pn_cache_alloc(&p,PN_CACHE_NEIGHBOR,PN_MEM_LAYOUT,100));
    p.cache_caps[0]=1024;p.cache_limit=64+pn_alloc_overhead();
    c[0].data=pn_cache_alloc(&p,PN_CACHE_NEIGHBOR,PN_MEM_LAYOUT,64);assert(c[0].data);
    assert(!pn_cache_alloc(&p,PN_CACHE_GLYPH,PN_MEM_FONT,1) && c[0].data && count==4);
    pn_free(c[0].data);c[0].data=NULL;p.cache_limit=1024u*1024u;
    pn_cache_speculate(&p,true);assert(!pn_alloc(&p,2000) && !p.reclaiming);pn_cache_speculate(&p,false);assert(!p.speculative);
    for(unsigned i=0;i<3;i++)pn_cache_detach(&c[i].owner);
    pn_free(current);pn_free(save);assert(!p.used && !p.live && !p.caches);
    // 真堆失败最多一次重试；可选分配和注入失败不重试。/ Real heap failure retries once; optional and injected failures never retry.
    heap_t h={0};assert(!pn_pool_init(&p,8192,allocate,release,&h));h.fail=1;
    void *a=pn_alloc(&p,10);assert(a && h.calls==2);pn_free(a);
    h.fail=h.calls+1;unsigned before=h.calls;assert(!pn_cache_alloc(&p,PN_CACHE_GLYPH,PN_MEM_FONT,10) && h.calls==before+1);
    p.fail_at=p.attempts+1;before=h.calls;assert(!pn_alloc(&p,10) && h.calls==before && !p.live);
    puts("cache: bounded admission, priority, pinning, protected data, injected failure and single heap retry passed");return 0;
}
