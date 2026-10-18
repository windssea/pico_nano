/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：实现可移植的受限基础设施；不接触硬件。
 * English: portable bounded infrastructure without hardware access.
 *
 * 冻结：由调用者串行访问，分配头也计入预算。
 * Frozen: access is serialized by callers; allocation headers count against budgets.
 */
#include "pn_alloc.h"
#include "pn_profile.h"
#include <stdlib.h>

typedef union {
    max_align_t alignment;
    struct { pn_pool_t *pool; size_t bytes; pn_memory_tag_t tag; } record;
} pn_alloc_header_t;

static void *libc_alloc(void *ctx, size_t bytes) { (void)ctx; return malloc(bytes); }
static void libc_free(void *ctx, void *ptr) { (void)ctx; free(ptr); }

int pn_pool_init(pn_pool_t *pool, size_t limit, pn_malloc_fn alloc, pn_free_fn release, void *ctx) {
    if (!pool || ((alloc == NULL) != (release == NULL))) return -1;
    *pool = (pn_pool_t){ .limit=limit, .malloc_fn=alloc ? alloc : libc_alloc,
        .free_fn=release ? release : libc_free, .ctx=ctx,
        // 6MiB产品池在字形/邻页缓存满时仍要容下一整页预渲染位图（约406KiB），否则真机预渲染被拒、翻页每次重光栅约0.35s。
        // A 6 MiB product pool must still fit a whole prerendered page (about 406 KiB) when glyph/neighbor caches are full; otherwise hardware denied every prerender and re-rastered each turn (about 0.35 s).
        .cache_limit=limit>=6u*1024u*1024u?2048u*1024u:limit>=4u*1024u*1024u?1536u*1024u:1024u*1024u,.cache_reserve=limit/8<256u*1024u?limit/8:256u*1024u,
        .cache_caps={512u*1024u,768u*1024u,384u*1024u,384u*1024u,132u*1024u,416u*1024u,limit>=6u*1024u*1024u?832u*1024u:limit>=4u*1024u*1024u?416u*1024u:0u} };
    return 0;
}

void *pn_alloc(pn_pool_t *pool, size_t bytes) {
    return pn_alloc_tagged(pool,PN_MEM_OTHER,bytes);
}
static void *allocate(pn_pool_t *pool,pn_memory_tag_t tag,size_t bytes,bool optional) {
    if((unsigned)tag>=PN_MEM_COUNT)return NULL;
    if (!pool || !pool->malloc_fn || !bytes || bytes > SIZE_MAX-sizeof(pn_alloc_header_t)) return NULL;
    if (pool->attempts == SIZE_MAX) return NULL;
    pool->attempts++;
    if (pool->fail_at && pool->attempts == pool->fail_at) {pool->memory[tag].failures++;return NULL;}
    size_t total=sizeof(pn_alloc_header_t)+bytes;
    if(pool->reclaiming){pool->memory[tag].failures++;return NULL;}
    if(pool->speculative && !pn_cache_allow(pool,pool->speculative_kind,bytes)){pool->memory[tag].failures++;return NULL;}
    if(optional && (pool->used>pool->limit || total>pool->limit-pool->used || pool->limit-pool->used-total<pool->cache_reserve)){pool->memory[tag].failures++;return NULL;}
    if(!optional && !pool->speculative && (pool->used>pool->limit || total>pool->limit-pool->used || pool->limit-pool->used-total<pool->cache_reserve)){
        size_t target=total>SIZE_MAX-pool->cache_reserve?SIZE_MAX:total+pool->cache_reserve;
        pn_cache_reclaim(pool,target);
    }
    if (pool->used > pool->limit || total > pool->limit-pool->used) {pool->memory[tag].failures++;return NULL;}
    pn_alloc_header_t *header=pool->malloc_fn(pool->ctx,total);
    // 底层碎片失败只回收并重试一次，注入故障不会进入此路。/ Reclaim and retry backing-heap failures once; injected faults never reach this path.
    if(!header && !optional && !pool->speculative){pn_cache_reclaim(pool,SIZE_MAX);header=pool->malloc_fn(pool->ctx,total);}
    if (!header) {pool->memory[tag].failures++;return NULL;}
    header->record.pool=pool;
    header->record.bytes=total;
    header->record.tag=tag;
    pool->used+=total;
    if (pool->used > pool->peak) pool->peak=pool->used;
    pool->live++;
    pn_memory_stats_t *stats=&pool->memory[tag];stats->used+=total;stats->live++;stats->allocations++;
    if(stats->used>stats->peak)stats->peak=stats->used;
    pn_profile_allocation(pool);
    return header+1;
}
void *pn_alloc_tagged(pn_pool_t *pool,pn_memory_tag_t tag,size_t bytes){return allocate(pool,tag,bytes,false);}
void *pn_alloc_optional(pn_pool_t *pool,pn_memory_tag_t tag,size_t bytes){return allocate(pool,tag,bytes,true);}
const char *pn_memory_tag_name(pn_memory_tag_t tag){
    static const char *const names[]={"other","frame","font","document","layout","image","index","ui","transfer"};
    return (unsigned)tag<PN_MEM_COUNT?names[tag]:"invalid";
}

void pn_free(void *ptr) {
    if (!ptr) return;
    pn_alloc_header_t *header=(pn_alloc_header_t *)ptr-1;
    pn_pool_t *pool=header->record.pool;
    pn_memory_stats_t *stats=&pool->memory[header->record.tag];stats->used-=header->record.bytes;stats->live--;
    pool->used-=header->record.bytes;
    pool->live--;
    pool->free_fn(pool->ctx,header);
}

size_t pn_alloc_size(const void *ptr){return ptr?((const pn_alloc_header_t *)ptr-1)->record.bytes:0;}
size_t pn_alloc_overhead(void){return sizeof(pn_alloc_header_t);}
void pn_cache_attach(pn_pool_t *p,pn_cache_owner_t *o,pn_cache_class_t kind,void *ctx,pn_cache_bytes_fn bytes,pn_cache_drop_fn drop){
    if(!p || !o || o->pool || (unsigned)kind>=PN_CACHE_COUNT || !bytes || !drop)return;
    *o=(pn_cache_owner_t){.pool=p,.ctx=ctx,.bytes=bytes,.drop=drop,.kind=kind};
    pn_cache_owner_t **at=&p->caches;while(*at)at=&(*at)->next;*at=o;
}
void pn_cache_detach(pn_cache_owner_t *o){
    if(!o || !o->pool)return;
    pn_cache_owner_t **at=&o->pool->caches;while(*at && *at!=o)at=&(*at)->next;
    if(*at)*at=o->next;
    *o=(pn_cache_owner_t){0};
}
void pn_cache_pin(pn_cache_owner_t *o){if(o && o->pool)o->pins++;}
void pn_cache_unpin(pn_cache_owner_t *o){if(o && o->pool && o->pins)o->pins--;}
void pn_cache_snapshot(pn_pool_t *p){
    if(!p)return;
    for(unsigned k=0;k<PN_CACHE_COUNT;k++)p->cache_stats[k].bytes=0;
    for(pn_cache_owner_t *o=p->caches;o;o=o->next)p->cache_stats[o->kind].bytes+=o->bytes(o->ctx);
    for(unsigned k=0;k<PN_CACHE_COUNT;k++)if(p->cache_stats[k].bytes>p->cache_stats[k].peak)p->cache_stats[k].peak=p->cache_stats[k].bytes;
}
bool pn_cache_allow(pn_pool_t *p,pn_cache_class_t kind,size_t bytes){
    if(!p || (unsigned)kind>=PN_CACHE_COUNT || bytes>SIZE_MAX-sizeof(pn_alloc_header_t))return false;
    pn_cache_snapshot(p);size_t total=bytes+sizeof(pn_alloc_header_t),sum=0;
    for(unsigned k=0;k<PN_CACHE_COUNT;k++)sum+=p->cache_stats[k].bytes;
    size_t extra=p->speculative && p->used>p->speculative_begin?p->used-p->speculative_begin:0;
    bool allowed=p->used<=p->limit && total<=p->limit-p->used && p->limit-p->used-total>=p->cache_reserve &&
        sum<=p->cache_limit && extra<=p->cache_limit-sum && total<=p->cache_limit-sum-extra &&
        p->cache_stats[kind].bytes<=p->cache_caps[kind] && extra<=p->cache_caps[kind]-p->cache_stats[kind].bytes && total<=p->cache_caps[kind]-p->cache_stats[kind].bytes-extra;
    if(!allowed)p->cache_stats[kind].denied++;
    return allowed;
}
void *pn_cache_alloc(pn_pool_t *p,pn_cache_class_t kind,pn_memory_tag_t tag,size_t bytes){
    if(!pn_cache_allow(p,kind,bytes))return NULL;
    // 可选缓存使用同一个分配器，但不允许抢占或重试。/ Optional caches use the same allocator without eviction or retry.
    unsigned saved=p->speculative;p->speculative=saved+1;
    pn_cache_class_t old_kind=p->speculative_kind;if(!saved)p->speculative_kind=kind;
    size_t begin=p->speculative_begin;if(!saved)p->speculative_begin=p->used;
    void *result=pn_alloc_tagged(p,tag,bytes);p->speculative=saved;p->speculative_begin=begin;p->speculative_kind=old_kind;return result;
}
void pn_cache_observe(pn_pool_t *p,pn_cache_class_t kind,bool hit){
    if(!p || (unsigned)kind>=PN_CACHE_COUNT)return;
    pn_cache_snapshot(p);if(hit)p->cache_stats[kind].hits++;else p->cache_stats[kind].misses++;
}
size_t pn_cache_reclaimable(pn_pool_t *p,unsigned kinds){
    size_t sum=0;if(!p)return 0;
    for(pn_cache_owner_t *o=p->caches;o;o=o->next)if(!o->pins && (unsigned)o->kind<PN_CACHE_COUNT && (kinds&(1u<<o->kind)))sum+=o->bytes(o->ctx);
    return sum;
}
size_t pn_cache_reclaim(pn_pool_t *p,size_t headroom){
    if(!p || p->reclaiming || p->speculative)return 0;
    size_t before=p->used;p->reclaiming=true;pn_cache_snapshot(p);
    // 菜单整页帧最先让位：阅读时它不再需要，丢掉后仍可重绘。/ Menu frames yield first: they are not needed while reading and can be repainted.
    static const pn_cache_class_t order[]={PN_CACHE_MENU,PN_CACHE_SOURCE,PN_CACHE_BITMAP,PN_CACHE_NEIGHBOR,PN_CACHE_GLYPH,PN_CACHE_COVER,PN_CACHE_CHAPTER};
    for(unsigned rank=0;rank<PN_CACHE_COUNT;rank++)for(pn_cache_owner_t *o=p->caches;o;o=o->next){
        pn_cache_class_t kind=order[rank];
        if(p->used<=p->limit && headroom<=p->limit-p->used)goto done;
        if(o->kind!=kind || o->pins || !o->bytes(o->ctx))continue;
        size_t old=p->used;o->drop(o->ctx);
        if(p->used<old){p->cache_stats[kind].evictions++;p->cache_stats[kind].reclaimed+=old-p->used;}
    }
done:
    p->reclaiming=false;pn_cache_snapshot(p);return before-p->used;
}
void pn_cache_speculate(pn_pool_t *p,bool enter){
    if(!p)return;
    if(enter){if(!p->speculative){p->speculative_begin=p->used;p->speculative_kind=PN_CACHE_NEIGHBOR;}p->speculative++;}
    else if(p->speculative)p->speculative--;
}
