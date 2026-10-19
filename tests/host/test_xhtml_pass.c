/* SPDX-License-Identifier: Apache-2.0
 * 中文：旧双遍与可重置单遍的完整事件/统计对照，观察XML供数、尾部校验与回退。
 * English: compare complete events/stats against the two-pass path, observing XML input, tail validation and fallbacks. */
#define main original_semantic_main
#include "test_semantic_cache.c"
#undef main
#include "pn_resource.h"
#include "pn_profile.h"
#include <stdlib.h>
static unsigned xml_calls,chapter_opens;static uint64_t chapter_bytes;static bool fail_xml_once,fail_tail;
static unsigned known_passes,fresh_passes,known_jobs,job_retries;
static uint64_t compat_clock(void *ctx){(void)ctx;return 0;}
static void compat_emit(void *ctx,const pn_profile_event_t *e){(void)ctx;if(!strcmp(e->phase,"epub.compat-known"))known_passes++;if(!strcmp(e->phase,"epub.compat"))fresh_passes++;if(!strcmp(e->phase,"epub.prefetch.compat-known"))known_jobs++;if(!strcmp(e->phase,"epub.prefetch.xml") && e->status==PN_CORRUPT)job_retries++;}
pn_status_t __real_pn_xml_parse(pn_pool_t *,const pn_xml_input_t *,const pn_xml_hooks_t *,void *,uint64_t,const uint8_t[16]);
pn_status_t __wrap_pn_xml_parse(pn_pool_t *p,const pn_xml_input_t *i,const pn_xml_hooks_t *h,void *c,uint64_t limit,const uint8_t s[16]){xml_calls++;if(fail_xml_once){fail_xml_once=false;p->fail_at=p->attempts+1;}return __real_pn_xml_parse(p,i,h,c,limit,s);}
pn_status_t __real_pn_epub_chapter_open(pn_epub_t *,const char *,pn_epub_chapter_input_t *);
pn_status_t __wrap_pn_epub_chapter_open(pn_epub_t *e,const char *path,pn_epub_chapter_input_t *i){chapter_opens++;return __real_pn_epub_chapter_open(e,path,i);}
pn_status_t __real_pn_epub_chapter_read(void *,uint8_t *,size_t,size_t *);
pn_status_t __wrap_pn_epub_chapter_read(void *ctx,uint8_t *p,size_t n,size_t *size){pn_status_t s=__real_pn_epub_chapter_read(ctx,p,n,size);chapter_bytes+=*size;return fail_tail && s==PN_EMPTY?PN_CORRUPT:s;}
typedef struct {digest_t digest;unsigned resets;bool short_page,detach,has_anchor,fail_once;pn_xhtml_position_t anchor;uint64_t anchor_order;char anchor_id[PN_RESOURCE_FRAGMENT_MAX];pn_media_t *media;} consumer_t;
static void consumer_init(consumer_t *c){c->digest=(digest_t){.hash=UINT64_C(14695981039346656037)};c->has_anchor=false;c->anchor=(pn_xhtml_position_t){0};c->anchor_order=0;*c->anchor_id=0;}
static pn_status_t consume(void *ctx,const pn_xhtml_event_t *e){consumer_t *c=ctx;pn_status_t s=event(&c->digest,e);if(e->kind==PN_XHTML_ANCHOR && !c->has_anchor){c->has_anchor=true;c->anchor=e->position;c->anchor_order=c->digest.count;assert(strlen(e->value)<sizeof c->anchor_id);strcpy(c->anchor_id,e->value);}if(c->detach && c->digest.count==32)assert(pn_media_detach(c->media)==PN_OK);if(c->fail_once && c->digest.count==32){c->fail_once=false;return PN_NO_MEMORY;}return s;}
static bool complete(void *ctx){consumer_t *c=ctx;return c->short_page && c->digest.count>=32;}
static void rewind_consumer(void *ctx,uint64_t order,const pn_xhtml_event_t *e){consumer_t *c=ctx;assert(!order && !e);consumer_init(c);c->resets++;}
static bool stop_after_page(void *ctx){return complete(ctx);}
static void counters(void){xml_calls=chapter_opens=0;chapter_bytes=0;}
static pn_status_t run(pn_pool_t *p,pn_epub_t *e,const char *path,consumer_t *c,bool fast,pn_xhtml_stats_t *stats){return pn_xhtml_parse_window(p,e,path,salt,NULL,consume,complete,fast?rewind_consumer:NULL,c,stats);}
static void equal(consumer_t a,consumer_t b,pn_xhtml_stats_t sa,pn_xhtml_stats_t sb){assert(a.digest.hash==b.digest.hash && a.digest.count==b.digest.count && !memcmp(&sa,&sb,sizeof sa));}
static bool same_position(pn_xhtml_position_t a,pn_xhtml_position_t b){return a.element==b.element && a.run==b.run && a.offset==b.offset && a.kind==b.kind;}
static pn_status_t staged_run(pn_pool_t *p,pn_epub_t *e,const char *path,consumer_t *c,pn_xhtml_stats_t *stats){
    pn_xhtml_job_t job={0};pn_status_t s=pn_xhtml_job_begin(&job,p,e,path,salt,NULL,consume,complete,rewind_consumer,c);unsigned steps=0;
    if(s==PN_OK)do{s=pn_xhtml_job_step(&job,stats);assert(++steps<100000);}while(s==PN_BUSY);
    pn_xhtml_job_close(&job);return s;
}
int main(int argc,char **argv){
    assert(argc==2);pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease={0};assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);pn_text_file_t f={0};pn_text_source_t source;assert(pn_text_file_open(&f,&media,&lease,argv[1],&source)==PN_OK);pn_zip_t zip={0};pn_epub_t epub={0};assert(pn_zip_open(&zip,&pool,&source)==PN_OK && pn_epub_open(&epub,&pool,&zip,salt)==PN_OK);
    const char *paths[]={"normal.xhtml","large.xhtml","late.xhtml","multi.xhtml","bare.xhtml"};
    for(unsigned i=0;i<5;i++){
        uint32_t index;pn_zip_info_t info;assert(pn_zip_find(&zip,paths[i],&index)==PN_OK && pn_zip_info(&zip,index,&info)==PN_OK);if(i==1)assert(info.unpacked>128u*1024u);
        pn_xhtml_cache_close(&epub);pool.cache_caps[PN_CACHE_CHAPTER]=0;consumer_t baseline={0};consumer_init(&baseline);pn_xhtml_stats_t expected; counters();assert(run(&pool,&epub,paths[i],&baseline,false,&expected)==PN_OK && xml_calls==2 && chapter_opens==2 && chapter_bytes==2ull*info.unpacked);
        uint64_t old_bytes=chapter_bytes;pool.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;consumer_t fast={0};consumer_init(&fast);pn_xhtml_stats_t actual;counters();assert(run(&pool,&epub,paths[i],&fast,true,&actual)==PN_OK);equal(baseline,fast,expected,actual);unsigned passes=i==2?2u:1u;assert(xml_calls==passes && chapter_opens==passes && chapter_bytes==(uint64_t)passes*info.unpacked && fast.resets==(i==2?1u:0u));printf("%s complete events=%zu xml 2->%u input %llu->%llu\n",paths[i],fast.digest.count,passes,(unsigned long long)old_bytes,(unsigned long long)chapter_bytes);
        // 固定小页消费者仍读完整源；热复放可在足够后停止。/ A small-page consumer still validates the full source; hot replay may stop when sufficient.
        pn_xhtml_cache_close(&epub);pool.cache_caps[PN_CACHE_CHAPTER]=0;baseline=(consumer_t){.short_page=true};consumer_init(&baseline);assert(run(&pool,&epub,paths[i],&baseline,false,&expected)==PN_OK);
        pool.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;fast=(consumer_t){.short_page=true};consumer_init(&fast);counters();assert(run(&pool,&epub,paths[i],&fast,true,&actual)==PN_OK && chapter_bytes==(uint64_t)passes*info.unpacked);equal(baseline,fast,expected,actual);
        fast=(consumer_t){.short_page=true};consumer_init(&fast);counters();assert(run(&pool,&epub,paths[i],&fast,true,&actual)==PN_OK && !xml_calls && !chapter_bytes);equal(baseline,fast,expected,actual);
        // 分步冷源与热复放保持完整语义/统计；暂停不推进，候选关闭不留pin。/ Staged cold parsing and hot replay preserve semantics/stats; pauses do not advance and closing leaves no pins.
        pn_xhtml_cache_close(&epub);
        for(unsigned pass=0;pass<2;pass++){
            consumer_t staged={.short_page=true};consumer_init(&staged);pn_xhtml_job_t job={0};pn_xhtml_stats_t staged_stats={.elements=UINT64_MAX};
            assert(pn_xhtml_job_begin(&job,&pool,&epub,paths[i],salt,NULL,consume,complete,rewind_consumer,&staged)==PN_OK);
            pn_status_t step;unsigned steps=0;
            do{step=pn_xhtml_job_step(&job,&staged_stats);assert(++steps<100000);if(step==PN_BUSY)assert(staged_stats.elements==UINT64_MAX);}while(step==PN_BUSY);
            assert(step==PN_OK);equal(baseline,staged,expected,staged_stats);pn_xhtml_job_close(&job);assert(!job.impl);
        }
        pn_xhtml_cache_close(&epub);pool.cache_caps[PN_CACHE_CHAPTER]=0;uint64_t order,start_order;pn_xhtml_position_t point;counters();assert(pn_xhtml_window_start(&pool,&epub,paths[i],NULL,salt,&order,&start_order,&point)==PN_OK && xml_calls==passes);
        const uint64_t old_totals[]={2268,78752,2252,2268,2268};assert(order==old_totals[i]);if(i==1)assert(start_order==70146 && point.element==6 && point.run==0 && point.offset==79495 && point.kind==PN_XHTML_TEXT_POSITION);else assert(!start_order && !point.element);
        counters();assert(baseline.has_anchor && pn_xhtml_anchor(&pool,&epub,paths[i],baseline.anchor_id,salt,&point)==PN_OK && same_position(point,baseline.anchor) && xml_calls==passes);
        counters();assert(pn_xhtml_window_start(&pool,&epub,paths[i],&baseline.anchor,salt,&order,&start_order,&point)==PN_OK && xml_calls==passes && order==baseline.anchor_order && !start_order && !point.element);
    }
    // 相邻章提升只交换已验证槽：盐/路径/忙与取消均不能提升候选，来回复放不重读。/ Neighbor promotion swaps verified slots only: salt/path/busy/cancel cannot promote candidates, and round trips replay without rereading.
    pn_xhtml_cache_close(&epub);pool.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;consumer_t first={.short_page=true};consumer_init(&first);pn_xhtml_stats_t first_stats;
    assert(run(&pool,&epub,"normal.xhtml",&first,true,&first_stats)==PN_OK);void *original=epub.semantic_cache;
    consumer_t next={.short_page=true};consumer_init(&next);pn_xhtml_stats_t next_stats;
    assert(staged_run(&pool,&epub,"large.xhtml",&next,&next_stats)==PN_OK);void *neighbor=epub.semantic_cache_alt;
    uint8_t wrong[16];memcpy(wrong,salt,16);wrong[0]^=1;
    assert(pn_xhtml_cache_select(&epub,"large.xhtml",wrong)==PN_EMPTY && epub.semantic_cache==original);
    pn_xhtml_job_t borrowed={0};consumer_t replay={.short_page=true};consumer_init(&replay);
    assert(pn_xhtml_job_begin(&borrowed,&pool,&epub,"large.xhtml",salt,NULL,consume,complete,rewind_consumer,&replay)==PN_OK);
    assert(pn_xhtml_cache_select(&epub,"large.xhtml",salt)==PN_BUSY && epub.semantic_cache==original);pn_xhtml_job_close(&borrowed);
    assert(pn_xhtml_cache_select(&epub,"large.xhtml",salt)==PN_OK && epub.semantic_cache==neighbor && epub.semantic_cache_alt==original);
    replay=(consumer_t){.short_page=true};consumer_init(&replay);pn_xhtml_stats_t replay_stats;counters();
    assert(run(&pool,&epub,"normal.xhtml",&replay,true,&replay_stats)==PN_OK && !chapter_bytes);equal(first,replay,first_stats,replay_stats);
    assert(epub.semantic_cache==original && epub.semantic_cache_alt==neighbor);
    replay=(consumer_t){.short_page=true};consumer_init(&replay);counters();assert(staged_run(&pool,&epub,"large.xhtml",&replay,&replay_stats)==PN_OK && !chapter_bytes);equal(next,replay,next_stats,replay_stats);
    // 注入合并XML初始化失败，释放额外峰值后只回退一次，消费者不重复累计。/ Inject combined XML-init failure; release the extra peak and retry once without duplicate consumer output.
    pn_xhtml_cache_close(&epub);pool.cache_caps[PN_CACHE_CHAPTER]=0;consumer_t baseline={0};consumer_init(&baseline);pn_xhtml_stats_t expected;assert(run(&pool,&epub,"normal.xhtml",&baseline,false,&expected)==PN_OK);consumer_t recovered={0};consumer_init(&recovered);pn_xhtml_stats_t actual;counters();fail_xml_once=true;assert(run(&pool,&epub,"normal.xhtml",&recovered,true,&actual)==PN_OK && xml_calls==3 && recovered.resets==1);pool.fail_at=0;equal(baseline,recovered,expected,actual);
    recovered=(consumer_t){.fail_once=true};consumer_init(&recovered);counters();assert(run(&pool,&epub,"normal.xhtml",&recovered,true,&actual)==PN_OK && xml_calls==3 && recovered.resets==1);equal(baseline,recovered,expected,actual);
    // 已验证兼容模式在记录回收后仍可复用，但仍完整读取；输出事件/位置/统计逐项相同。
    // Reuse a verified compatible mode after record eviction, still reading fully and matching every output event/location/statistic.
    pn_xhtml_cache_close(&epub);pn_profile_port(compat_clock,compat_emit,NULL);pool.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;
    consumer_t compat_before={0};consumer_init(&compat_before);pn_xhtml_stats_t compat_stats;
    assert(run(&pool,&epub,"compatible.xhtml",&compat_before,true,&compat_stats)==PN_OK && fresh_passes==1 && !known_passes);
    pn_cache_reclaim(&pool,SIZE_MAX);consumer_t compat_after={0};consumer_init(&compat_after);pn_xhtml_stats_t compat_again;counters();
    assert(run(&pool,&epub,"compatible.xhtml",&compat_after,true,&compat_again)==PN_OK && fresh_passes==1 && known_passes==1 && chapter_bytes>0);
    equal(compat_before,compat_after,compat_stats,compat_again);
    pn_cache_reclaim(&pool,SIZE_MAX);compat_after=(consumer_t){0};consumer_init(&compat_after);compat_again=(pn_xhtml_stats_t){.elements=12345};fail_tail=true;
    assert(run(&pool,&epub,"compatible.xhtml",&compat_after,true,&compat_again)==PN_CORRUPT && compat_again.elements==12345 && known_passes==2 && !pn_xhtml_cache_pending(&epub));
    fail_tail=false;pn_profile_port(NULL,NULL,NULL);
    pn_xhtml_cache_close(&epub);pool.cache_caps[PN_CACHE_CHAPTER]=0;consumer_t compat_short={.short_page=true};consumer_init(&compat_short);pn_xhtml_stats_t compat_short_stats;
    assert(run(&pool,&epub,"compatible.xhtml",&compat_short,true,&compat_short_stats)==PN_OK);pool.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;
    compat_after=(consumer_t){.short_page=true};consumer_init(&compat_after);
    assert(staged_run(&pool,&epub,"compatible.xhtml",&compat_after,&compat_again)==PN_OK);equal(compat_short,compat_after,compat_short_stats,compat_again);
    // 当前章的已验证兼容模式也供独立邻页槽使用，记录回收后不再重复必败的严格遍；新salt仍走原检查。/ The independent neighbor slot also inherits the current chapter's verified compatibility after record eviction; a new salt still takes the original checks.
    pn_xhtml_cache_close(&epub);compat_after=(consumer_t){.short_page=true};consumer_init(&compat_after);
    assert(run(&pool,&epub,"compatible.xhtml",&compat_after,true,&compat_again)==PN_OK);pn_cache_reclaim(&pool,SIZE_MAX);
    pn_profile_port(compat_clock,compat_emit,NULL);known_jobs=job_retries=0;compat_after=(consumer_t){.short_page=true};consumer_init(&compat_after);
    assert(staged_run(&pool,&epub,"compatible.xhtml",&compat_after,&compat_again)==PN_OK && known_jobs==1 && !job_retries);equal(compat_short,compat_after,compat_short_stats,compat_again);
    uint8_t other_salt[16];memcpy(other_salt,salt,16);other_salt[0]^=1;compat_after=(consumer_t){.short_page=true};consumer_init(&compat_after);pn_xhtml_job_t changed={0};
    assert(pn_xhtml_job_begin(&changed,&pool,&epub,"compatible.xhtml",other_salt,NULL,consume,complete,rewind_consumer,&compat_after)==PN_OK);
    pn_status_t changed_status;do{changed_status=pn_xhtml_job_step(&changed,&compat_again);}while(changed_status==PN_BUSY);pn_xhtml_job_close(&changed);
    assert(changed_status==PN_OK && known_jobs==1 && job_retries==1);pn_profile_port(NULL,NULL,NULL);
    // 换目标后立即取消，旧兼容章不能污染普通新章的首次严格校验。/ Retargeting and immediate cancellation must not poison the new regular chapter with an old compatibility mode.
    compat_after=(consumer_t){.short_page=true};consumer_init(&compat_after);pn_xhtml_job_t aborted={0};
    assert(pn_xhtml_job_begin(&aborted,&pool,&epub,"normal.xhtml",salt,NULL,consume,complete,rewind_consumer,&compat_after)==PN_OK);pn_xhtml_job_close(&aborted);
    known_jobs=job_retries=0;pn_profile_port(compat_clock,compat_emit,NULL);compat_after=(consumer_t){.short_page=true};consumer_init(&compat_after);
    assert(staged_run(&pool,&epub,"normal.xhtml",&compat_after,&compat_again)==PN_OK && !known_jobs && !job_retries);pn_profile_port(NULL,NULL,NULL);
    const char *bad[]={"bad.xhtml","crc.xhtml"};for(unsigned i=0;i<2;i++){
        // 词法错误允许一次有界重试，但坏根仍拒绝；CRC错误不重试，均不得形成有效热缓存。/ Lexical errors get one bounded retry but a bad root still fails; CRC errors never retry, and neither path may form a valid hot cache.
        unsigned passes=i==0?2u:1u,resets=i==0?1u:0u;
        pn_xhtml_cache_close(&epub);pool.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;consumer_t c={.short_page=true};consumer_init(&c);pn_xhtml_stats_t untouched={.elements=12345};counters();
        pn_status_t failed=run(&pool,&epub,bad[i],&c,true,&untouched);
        if(failed!=PN_CORRUPT || c.digest.count!=32 || untouched.elements!=12345 || xml_calls!=passes || c.resets!=resets)fprintf(stderr,"bad pass path=%s status=%d events=%zu stats=%llu xml=%u resets=%u\n",bad[i],failed,c.digest.count,(unsigned long long)untouched.elements,xml_calls,c.resets);
        assert(failed==PN_CORRUPT && c.digest.count==32 && untouched.elements==12345 && xml_calls==passes && c.resets==resets && !pn_xhtml_cache_pending(&epub));
        c=(consumer_t){.short_page=true};consumer_init(&c);counters();assert(run(&pool,&epub,bad[i],&c,true,&untouched)==PN_CORRUPT && xml_calls==passes && c.resets==resets && c.digest.count==32 && untouched.elements==12345);
        pn_xhtml_position_t out={.element=12345};uint64_t order=12345,start_order=12345;assert(pn_xhtml_anchor(&pool,&epub,bad[i],"large",salt,&out)==PN_CORRUPT && out.element==12345 && pn_xhtml_window_start(&pool,&epub,bad[i],NULL,salt,&order,&start_order,&out)==PN_CORRUPT && order==12345 && start_order==12345 && out.element==12345);
        pn_xhtml_cache_close(&epub);c=(consumer_t){.short_page=true};consumer_init(&c);
        assert(staged_run(&pool,&epub,bad[i],&c,&untouched)==PN_CORRUPT && !c.digest.count && untouched.elements==12345 && !pn_xhtml_cache_pending(&epub));
    }
    pn_xhtml_position_t unavailable={.element=12345};assert(pn_xhtml_anchor(&pool,&epub,"hidden.xhtml","secret",salt,&unavailable)==PN_UNSUPPORTED && unavailable.element==12345 && pn_xhtml_anchor(&pool,&epub,"duplicate.xhtml","dup",salt,&unavailable)==PN_CORRUPT && unavailable.element==12345);
    pn_xhtml_cache_close(&epub);consumer_t stopped={.short_page=true};consumer_init(&stopped);pn_cancel_scope_t scope;pn_cancel_enter(&scope,stop_after_page,&stopped);pn_xhtml_stats_t untouched={.elements=12345};assert(run(&pool,&epub,"large.xhtml",&stopped,true,&untouched)==PN_CANCELLED && stopped.digest.count==32 && untouched.elements==12345);pn_cancel_leave(&scope);
    pn_xhtml_cache_close(&epub);stopped=(consumer_t){.short_page=true};consumer_init(&stopped);pn_cancel_enter(&scope,stop_after_page,&stopped);
    assert(staged_run(&pool,&epub,"large.xhtml",&stopped,&untouched)==PN_CANCELLED && stopped.digest.count==32 && untouched.elements==12345);pn_cancel_leave(&scope);
    pn_xhtml_cache_close(&epub);pool.cache_caps[PN_CACHE_CHAPTER]=0;consumer_t short_reference={.short_page=true};consumer_init(&short_reference);pn_xhtml_stats_t short_stats;
    assert(run(&pool,&epub,"normal.xhtml",&short_reference,false,&short_stats)==PN_OK);
    const size_t caps[]={0,4096,16384};
    for(unsigned i=0;i<3;i++){pool.cache_caps[PN_CACHE_CHAPTER]=caps[i];stopped=(consumer_t){.short_page=true};consumer_init(&stopped);assert(staged_run(&pool,&epub,"normal.xhtml",&stopped,&actual)==PN_OK);equal(short_reference,stopped,short_stats,actual);pn_xhtml_cache_close(&epub);}
    pool.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;
    // 任意分步分配失败均释放候选，之后能重新完整读取。/ Every staged allocation failure releases its candidate and permits a complete retry.
    for(unsigned fail=1;fail<=30;fail++){
        pn_xhtml_cache_close(&epub);stopped=(consumer_t){.short_page=true};consumer_init(&stopped);untouched=(pn_xhtml_stats_t){.elements=12345};pool.fail_at=pool.attempts+fail;
        pn_status_t status=staged_run(&pool,&epub,"normal.xhtml",&stopped,&untouched);pool.fail_at=0;assert(status==PN_OK || status==PN_NO_MEMORY);if(status==PN_OK)equal(short_reference,stopped,short_stats,untouched);else assert(untouched.elements==12345);
        pn_xhtml_cache_close(&epub);
    }
    stopped=(consumer_t){.short_page=true};consumer_init(&stopped);assert(staged_run(&pool,&epub,"normal.xhtml",&stopped,&actual)==PN_OK);
    pn_xhtml_cache_close(&epub);untouched=(pn_xhtml_stats_t){.elements=12345};
    stopped=(consumer_t){.short_page=true,.detach=true,.media=&media};consumer_init(&stopped);assert(staged_run(&pool,&epub,"large.xhtml",&stopped,&untouched)==PN_STALE_MEDIA && untouched.elements==12345);
    stopped=(consumer_t){.short_page=true,.detach=true,.media=&media};consumer_init(&stopped);assert(run(&pool,&epub,"large.xhtml",&stopped,true,&untouched)==PN_STALE_MEDIA && untouched.elements==12345);
    pn_epub_close(&epub);assert(pn_zip_close(&zip)==PN_OK && pn_text_file_close(&f)==PN_OK && pn_media_release(&media,&lease)==PN_OK && !pool.used && !pool.live);puts("single-pass XHTML: complete legacy event/style/position parity, full tails/CRC, late-head and OOM replay, hot windows, cancellation and stale media passed");return 0;
}
