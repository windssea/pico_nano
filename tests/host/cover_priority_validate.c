/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实书页先补封面后写缓存，同步像素oracle、重复槽和写入中断重试。
 * English: fill actual book-page covers before persistence, with synchronous pixel oracles, duplicate slots and interrupted-write retries. */
#define _POSIX_C_SOURCE 200809L
#include "pn_cover.h"
#include "pn_profile.h"
#include "pn_text_file.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static const uint8_t salt[16]={1};
static pn_shelf_covers_t *active;static pn_catalog_page_t *page;static unsigned writes,syncs,extracts,picks;static bool watching;
static size_t source_reads,source_bytes,source_cache_peak,bound_sources;
static size_t optional_source_requests,optional_source_denied;
typedef struct {void *data;bool evicted;} foreground_guard_t;
static size_t foreground_bytes(void *ctx){foreground_guard_t *g=ctx;return g->data?pn_alloc_size(g->data):0;}
static void foreground_drop(void *ctx){foreground_guard_t *g=ctx;g->evicted=true;pn_free(g->data);g->data=NULL;}
void *__real_pn_alloc_optional(pn_pool_t *,pn_memory_tag_t,size_t);
void *__wrap_pn_alloc_optional(pn_pool_t *pool,pn_memory_tag_t tag,size_t bytes){
    if(watching && pn_shelf_covers_pending(active,page)){
        optional_source_requests++;
        if(getenv("PN_COVER_FAIL_SOURCE_OPTIONAL")){
            // 真正压低父池余量，并用独立可回收对象检查前台未被驱逐。/ Constrain actual parent headroom and guard an independent reclaimable foreground object against eviction.
            foreground_guard_t g={.data=pn_alloc_tagged(pool,PN_MEM_FONT,128)};assert(g.data);pn_cache_owner_t guard={0};
            pn_cache_attach(pool,&guard,PN_CACHE_GLYPH,&g,foreground_bytes,foreground_drop);
            size_t limit=pool->limit;assert(bytes+pn_alloc_overhead()<limit-pool->used);
            pool->limit=pool->used+bytes+pn_alloc_overhead()-1;void *result=__real_pn_alloc_optional(pool,tag,bytes);pool->limit=limit;
            assert(!result && !g.evicted && g.data);pn_cache_detach(&guard);pn_free(g.data);optional_source_denied++;return result;
        }
    }
    return __real_pn_alloc_optional(pool,tag,bytes);
}
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *p,size_t size,size_t count,FILE *file){size_t n=__real_fread(p,size,count,file);source_reads++;source_bytes+=n*size;return n;}
pn_status_t __real_pn_text_file_close(pn_text_file_t *);
pn_status_t __wrap_pn_text_file_close(pn_text_file_t *file){
    if(file && file->cache_owner.pool){
        pn_pool_t *pool=file->cache_owner.pool;pn_cache_snapshot(pool);size_t bytes=pool->cache_stats[PN_CACHE_SOURCE].bytes;
        assert(pool->cache_caps[PN_CACHE_SOURCE]==36u*1024u && bytes<=pool->cache_caps[PN_CACHE_SOURCE]);
        if(bytes>source_cache_peak)source_cache_peak=bytes;
        bound_sources++;
    }
    return __real_pn_text_file_close(file);
}
size_t __real_fwrite(const void *,size_t,size_t,FILE *);
size_t __wrap_fwrite(const void *p,size_t size,size_t count,FILE *file){if(watching){assert(!pn_shelf_covers_pending(active,page));writes++;}return __real_fwrite(p,size,count,file);}
int __real_fsync(int);
int __wrap_fsync(int fd){if(watching){assert(!pn_shelf_covers_pending(active,page));syncs++;}return __real_fsync(fd);}
static uint64_t clock_us(void *ctx){(void)ctx;return 1;}
static void emit(void *ctx,const pn_profile_event_t *e){(void)ctx;if(!strcmp(e->phase,"cover.meta-file"))extracts++;if(!strcmp(e->phase,"cover.pick"))picks++;}
int main(int argc,char **argv){
    if(argc<4 || argc>3+PN_CATALOG_PAGE_MAX)return 2;
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    page=calloc(1,sizeof *page);pn_shelf_covers_t *oracle=calloc(1,sizeof *oracle);active=calloc(1,sizeof *active);assert(page && oracle && active);
    page->count=page->total=(size_t)argc-3;unsigned epubs=0;
    for(size_t i=0;i<page->count;i++){assert(strlen(argv[i+3])<sizeof page->items[i].path);strcpy(page->items[i].path,argv[i+3]);const char *ext=strrchr(argv[i+3],'.');assert(ext);
        page->items[i].format=!strcmp(ext,".epub")?PN_BOOK_EPUB:PN_BOOK_TXT;epubs+=page->items[i].format==PN_BOOK_EPUB;}
    pn_shelf_covers_set_last(oracle,&page->items[0]);pn_shelf_covers_reset(oracle,page);pn_status_t s;bool changed;size_t steps=0;
    do{s=pn_shelf_covers_step(oracle,page,&pool,&media,argv[1],salt,pool.limit,&changed);assert(++steps<128);}while(s==PN_OK);
    assert(s==PN_EMPTY && !pool.used && !pool.live && !pn_media_active(&media));
    pn_shelf_covers_set_last(active,&page->items[0]);pn_shelf_covers_reset(active,page);pn_cover_work_t work={0};steps=0;watching=true;pn_profile_port(clock_us,emit,NULL);
    do{
        s=pn_shelf_covers_work(&work,active,page,&pool,&media,argv[2],salt,pool.limit,&changed);assert(++steps<65536 && (s==PN_OK || s==PN_BUSY));
        // 第二轮必须推进首个任务，及时发现身份检查导致的反复重建。/ The second round must advance the first job, detecting identity-check rebuild loops promptly.
        if(steps==2)assert(picks==1);
        assert(!writes && !syncs && !pn_cover_write_pending(&work.writer));
    }while(pn_shelf_covers_pending(active,page));
    size_t visible_steps=steps;assert(!work.extract && !pool.used && !pool.live && !pn_media_active(&media) && active->cache_dirty);
    for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++)if(i<page->count || i==PN_COVER_SLOT_LAST){
        assert(active->state[i]==oracle->state[i] && active->reason[i]==oracle->reason[i] && !memcmp(&active->display[i],&oracle->display[i],sizeof active->display[i]));
        if(active->state[i]==PN_COVER_READY)assert(!memcmp(active->pixels[i],oracle->pixels[i],PN_COVER_BYTES));
    }
    assert(extracts==epubs);unsigned before=extracts;
    if(getenv("PN_COVER_REBIND_BEFORE_WRITE")){
        // 原像素尚未落盘时换序；来源键、脏位和唯一写入资格必须随槽移动。
        // Reorder before persistence: source keys, dirty bits and unique-write ownership must move with their slots.
        pn_cover_work_abort(&work);pn_catalog_page_t *reordered=calloc(1,sizeof *reordered);assert(reordered);*reordered=*page;
        for(size_t i=0;i<page->count;i++)reordered->items[i]=page->items[page->count-1-i];
        pn_catalog_item_t last=active->last;assert(pn_shelf_covers_bind(active,reordered,&last,&pool,&media,argv[2],&changed)==PN_OK);
        pn_catalog_page_t *home=calloc(1,sizeof *home);assert(home);*home=*reordered;home->count=home->total=home->per_page=1;
        assert(pn_shelf_covers_bind(active,home,&last,&pool,&media,argv[2],&changed)==PN_OK);
        assert(pn_shelf_covers_bind(active,reordered,&last,&pool,&media,argv[2],&changed)==PN_OK);free(home);
        assert(!writes && !syncs && active->cache_dirty && !pn_shelf_covers_pending(active,reordered));
        for(size_t i=0;i<page->count;i++){size_t old=page->count-1-i;
            assert(active->state[i]==oracle->state[old] && active->reason[i]==oracle->reason[old] && !memcmp(&active->display[i],&oracle->display[old],sizeof active->display[i]));
            if(active->state[i]==PN_COVER_READY)assert(!memcmp(active->pixels[i],oracle->pixels[old],PN_COVER_BYTES));
        }
        *page=*reordered;free(reordered);printf("priority resident_rebind_before_write=1 offpage_resume=1\n");
    }
    // 暂存取消不丢失已验证像素或强迫重复解码。/ Cancelling staging neither loses verified pixels nor forces decoding again.
    assert(pn_shelf_covers_work(&work,active,page,&pool,&media,argv[2],salt,pool.limit,&changed)==PN_BUSY && pn_cover_write_pending(&work.writer));
    pn_cover_work_abort(&work);assert(active->cache_dirty && !pool.used && !pool.live && !pn_media_active(&media));
    do{s=pn_shelf_covers_work(&work,active,page,&pool,&media,argv[2],salt,pool.limit,&changed);assert(++steps<65536);}while(s==PN_OK || s==PN_BUSY);
    pn_cover_work_abort(&work);watching=false;pn_profile_port(NULL,NULL,NULL);
    assert(s==PN_EMPTY && !active->cache_dirty && !pool.used && !pool.live && !pn_media_active(&media) && extracts==before && syncs==page->count);
    printf("priority books=%zu visible_steps=%zu all_steps=%zu writes=%u syncs=%u extracts=%u peak=%zu used=%zu live=%zu\n",page->count,visible_steps,steps,writes,syncs,extracts,pool.peak,pool.used,pool.live);
    printf("source reads=%zu bytes=%zu cache_peak=%zu bound_sources=%zu\n",source_reads,source_bytes,source_cache_peak,bound_sources);
    if(epubs)assert(bound_sources>=2*epubs && source_cache_peak);
    printf("source optional_requests=%zu optional_denied=%zu\n",optional_source_requests,optional_source_denied);
    if(epubs)assert(optional_source_requests && (getenv("PN_COVER_FAIL_SOURCE_OPTIONAL")?optional_source_denied==optional_source_requests:!optional_source_denied));
    free(active);free(oracle);free(page);return 0;
}
