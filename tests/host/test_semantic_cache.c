/* SPDX-License-Identifier: Apache-2.0
 * 中文：流式与已验证语义复放逐事件比较，覆盖回收、取消和失效。/ English: compare streamed and verified replay events, including reclamation, cancellation and invalidation. */
#include "pn_xhtml.h"
#include "pn_text_file.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint64_t hash;size_t count;pn_pool_t *pool;pn_media_t *media;bool reclaim,detach,building_pressure,optional_failure;} digest_t;
static void bytes(digest_t *d,const void *data,size_t n){const uint8_t *p=data;for(size_t i=0;i<n;i++)d->hash=(d->hash^p[i])*UINT64_C(1099511628211);}
static pn_status_t event(void *ctx,const pn_xhtml_event_t *e){
    digest_t *d=ctx;d->count++;
    bytes(d,&e->kind,sizeof e->kind);bytes(d,&e->position.element,sizeof e->position.element);bytes(d,&e->position.offset,sizeof e->position.offset);bytes(d,&e->position.run,sizeof e->position.run);bytes(d,&e->position.kind,sizeof e->position.kind);
    // 结构填充不是语义或画质；逐字段比较，分配故障后也不依赖未规定的padding。/ Struct padding is neither semantics nor image quality; compare fields without allocator-dependent padding after faults.
    const pn_xhtml_length_t *lengths[]={&e->style.width,&e->style.height,&e->style.max_width,&e->style.max_height};
    for(unsigned i=0;i<4;i++){bytes(d,&lengths[i]->value,sizeof lengths[i]->value);bytes(d,&lengths[i]->percent,sizeof lengths[i]->percent);}
    bytes(d,&e->style.align,sizeof e->style.align);bytes(d,&e->style.bold,sizeof e->style.bold);bytes(d,&e->style.italic,sizeof e->style.italic);bytes(d,&e->style.pre,sizeof e->style.pre);bytes(d,&e->style.hidden,sizeof e->style.hidden);bytes(d,&e->style.heading,sizeof e->style.heading);
    bytes(d,&e->block,sizeof e->block);bytes(d,&e->codepoint,sizeof e->codepoint);
    if(e->value)bytes(d,e->value,strlen(e->value)+1);
    if(e->alt)bytes(d,e->alt,strlen(e->alt)+1);
    if(d->reclaim && d->count==20){pn_cache_reclaim(d->pool,SIZE_MAX);assert(d->pool->cache_stats[PN_CACHE_CHAPTER].bytes>0);}
    if(d->building_pressure && d->count==20){pn_cache_reclaim(d->pool,SIZE_MAX);assert(!d->pool->cache_stats[PN_CACHE_CHAPTER].bytes);}
    if(d->optional_failure && e->kind==PN_XHTML_ANCHOR && d->count==1)d->pool->fail_at=d->pool->attempts+1;
    if(d->detach && d->count==20)assert(pn_media_detach(d->media)==PN_OK);
    return PN_OK;
}
static const uint8_t salt[16]={1,2,3};
static digest_t parse(pn_pool_t *p,pn_epub_t *e,const char *path){digest_t d={.hash=UINT64_C(14695981039346656037)};pn_xhtml_stats_t stats;assert(pn_xhtml_parse(p,e,path,salt,event,&d,&stats)==PN_OK);pn_cache_snapshot(p);return d;}
static bool cancel(void *ctx){unsigned *n=ctx;return ++*n==80;}
int main(int argc,char **argv){
    assert(argc==2);pn_pool_t p;assert(!pn_pool_init(&p,3u*1024u*1024u,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t lease={0};assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);pn_text_file_t f={0};pn_text_source_t source;assert(pn_text_file_open(&f,&media,&lease,argv[1],&source)==PN_OK);
    pn_zip_t z={0};pn_epub_t e={0};assert(pn_zip_open(&z,&p,&source)==PN_OK && pn_epub_open(&e,&p,&z,salt)==PN_OK);
    p.cache_caps[PN_CACHE_CHAPTER]=0;digest_t baseline=parse(&p,&e,"a.xhtml");p.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;
    digest_t built=parse(&p,&e,"a.xhtml");assert(built.hash==baseline.hash && built.count==baseline.count);size_t attempts=p.attempts;
    digest_t replay=parse(&p,&e,"a.xhtml");assert(replay.hash==baseline.hash && replay.count==baseline.count && p.attempts==attempts && p.cache_stats[PN_CACHE_CHAPTER].hits==1);
    // 可选预排另一章写入独立槽，不替换已验证的当前章；随后当前章仍命中。/ Speculative work on another chapter uses a separate slot without replacing the verified current model; the current chapter still hits.
    uint64_t spec_hits=p.cache_stats[PN_CACHE_CHAPTER].hits;digest_t spec={.hash=UINT64_C(14695981039346656037)};pn_xhtml_stats_t spec_stats;spec_stats.elements=0;
    pn_cache_speculate(&p,true);assert(pn_xhtml_parse(&p,&e,"b.xhtml",salt,event,&spec,&spec_stats)==PN_OK);pn_cache_speculate(&p,false);
    assert(spec.count>0 && spec.hash!=baseline.hash && spec_stats.elements>0);
    digest_t after_spec=parse(&p,&e,"a.xhtml");assert(after_spec.hash==baseline.hash && after_spec.count==baseline.count && p.cache_stats[PN_CACHE_CHAPTER].hits>spec_hits);
    // 前台切到预排章直接接管独立槽，不再重建。/ A foreground switch to the prefetched chapter takes over the separate slot without rebuilding.
    size_t transfer_attempts=p.attempts;digest_t b_transfer=parse(&p,&e,"b.xhtml");assert(b_transfer.hash==spec.hash && p.attempts==transfer_attempts);
    digest_t restore=parse(&p,&e,"a.xhtml");assert(restore.hash==baseline.hash);
    digest_t borrowed={.hash=UINT64_C(14695981039346656037),.pool=&p,.reclaim=true};pn_xhtml_stats_t stats;
    assert(pn_xhtml_parse(&p,&e,"a.xhtml",salt,event,&borrowed,&stats)==PN_OK && borrowed.hash==baseline.hash);
    assert(pn_cache_reclaim(&p,SIZE_MAX)>0 && p.cache_stats[PN_CACHE_CHAPTER].bytes==0);built=parse(&p,&e,"a.xhtml");assert(built.hash==baseline.hash);
    unsigned calls=0;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,&calls);stats.elements=12345;
    assert(pn_xhtml_parse(&p,&e,"a.xhtml",salt,event,&replay,&stats)==PN_CANCELLED && stats.elements==12345);pn_cancel_leave(&scope);replay=parse(&p,&e,"a.xhtml");assert(replay.hash==baseline.hash);
    pn_xhtml_cache_close(&e);p.fail_at=p.attempts+1;built=parse(&p,&e,"a.xhtml");p.fail_at=0;assert(built.hash==baseline.hash && !e.semantic_cache);
    digest_t pressure={.hash=UINT64_C(14695981039346656037),.pool=&p,.building_pressure=true};
    assert(pn_xhtml_parse(&p,&e,"a.xhtml",salt,event,&pressure,&stats)==PN_OK && pressure.hash==baseline.hash);pn_cache_snapshot(&p);assert(!p.cache_stats[PN_CACHE_CHAPTER].bytes);
    pn_xhtml_cache_close(&e);digest_t optional={.hash=UINT64_C(14695981039346656037),.pool=&p,.optional_failure=true};
    assert(pn_xhtml_parse(&p,&e,"a.xhtml",salt,event,&optional,&stats)==PN_OK && optional.hash==baseline.hash);p.fail_at=0;pn_cache_snapshot(&p);assert(!p.cache_stats[PN_CACHE_CHAPTER].bytes);
    pn_xhtml_cache_close(&e);
    p.cache_caps[PN_CACHE_CHAPTER]=4096;built=parse(&p,&e,"a.xhtml");assert(built.hash==baseline.hash && p.cache_stats[PN_CACHE_CHAPTER].bytes==0);
    pn_xhtml_cache_close(&e);p.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;
    for(unsigned i=0;i<2;i++){stats.elements=12345;assert(pn_xhtml_parse(&p,&e,"bad.xhtml",salt,NULL,NULL,&stats)==PN_CORRUPT && stats.elements==12345);pn_cache_snapshot(&p);assert(!p.cache_stats[PN_CACHE_CHAPTER].bytes);}
    digest_t b=parse(&p,&e,"b.xhtml");assert(b.hash!=baseline.hash);built=parse(&p,&e,"a.xhtml");assert(built.hash==baseline.hash);
    // 回调中换卡必须中止复放，不能发布缓存统计或部分页。/ A card change inside the sink must abort replay without publishing stats or partial pages.
    digest_t stale={.hash=1,.media=&media,.detach=true};stats.elements=12345;assert(pn_xhtml_parse(&p,&e,"a.xhtml",salt,event,&stale,&stats)==PN_STALE_MEDIA && stats.elements==12345);
    pn_epub_close(&e);assert(pn_zip_close(&z)==PN_OK && pn_text_file_close(&f)==PN_OK && pn_media_release(&media,&lease)==PN_OK);assert(!p.used && !p.live && !p.caches);
    puts("semantic cache: exact events, zero-allocation replay, bounded fallback, pin/reclaim, cancellation, malformed XML and stale media passed");return 0;
}
