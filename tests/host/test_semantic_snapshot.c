/* SPDX-License-Identifier: Apache-2.0
 * 中文：跨出版物/随机盐的序列化事件对照与损坏、配额、取消退路。
 * English: serialized-event parity across publication objects/salts, with corruption, budget and cancellation fallbacks. */
#define main original_semantic_main
#include "test_semantic_cache.c"
#undef main
#include "pn_identity.h"
#include <stdlib.h>
#include "pn_xml.h"
static bool watch_crc,loaded_packet,stop_crc;static unsigned crc_chunks,crc_stop_at;
uint32_t __real_pn_crc32_update(uint32_t,const uint8_t *,size_t);
uint32_t __wrap_pn_crc32_update(uint32_t crc,const uint8_t *data,size_t n){uint32_t result=__real_pn_crc32_update(crc,data,n);if(watch_crc && loaded_packet && n>148 && ++crc_chunks==crc_stop_at)stop_crc=true;return result;}
static bool cancelled_crc(void *ctx){(void)ctx;return stop_crc;}
static uint8_t *packet;static size_t packet_size;static unsigned parses,loads,saves;static bool refuse;
pn_status_t __real_pn_xml_parse(pn_pool_t *,const pn_xml_input_t *,const pn_xml_hooks_t *,void *,uint64_t,const uint8_t[16]);
pn_status_t __wrap_pn_xml_parse(pn_pool_t *p,const pn_xml_input_t *i,const pn_xml_hooks_t *h,void *c,uint64_t limit,const uint8_t s[16]){parses++;return __real_pn_xml_parse(p,i,h,c,limit,s);}
static pn_status_t load_packet(void *ctx,pn_pool_t *p,uint8_t **out,size_t *n){(void)ctx;loads++;if(!packet)return PN_EMPTY;uint8_t *copy=pn_alloc_optional(p,PN_MEM_INDEX,packet_size);if(!copy)return PN_EMPTY;memcpy(copy,packet,packet_size);*out=copy;*n=packet_size;loaded_packet=true;return PN_OK;}
static pn_status_t save_packet(void *ctx,const uint8_t *p,size_t n){(void)ctx;if(refuse)return PN_BUSY;saves++;uint8_t *copy=malloc(n);assert(copy);memcpy(copy,p,n);free(packet);packet=copy;packet_size=n;return PN_OK;}
static bool cancel_encode(void *ctx){(void)ctx;return true;}
static bool cancel_parse(void *ctx){unsigned *calls=ctx;return ++*calls>=80;} ///< 输入取消保持到owner处理，不是单检查点脉冲 / Input cancellation persists until owner handling, rather than pulsing for one checkpoint
static pn_status_t step_packet(pn_epub_t *epub,const char *path,pn_xhtml_snapshot_work_t *w){pn_status_t s=pn_xhtml_cache_flush_step(epub,path,w);assert(w->bytes<=4096 && w->records<=16 && w->phase<=PN_XHTML_SNAPSHOT_OFFER);return s;}
static void staged_checks(pn_pool_t *pool,pn_epub_t *epub,const uint8_t *pristine,size_t n,digest_t expected){
    for(unsigned phase=PN_XHTML_SNAPSHOT_COUNT;phase<PN_XHTML_SNAPSHOT_OFFER;phase++){
        pn_xhtml_cache_retry(epub);unsigned before=saves,steps=0;pn_xhtml_snapshot_work_t work;
        do{assert(step_packet(epub,"a.xhtml",&work)==PN_BUSY && pn_xhtml_cache_pending(epub) && ++steps<4096);}while((unsigned)work.phase!=phase);
        assert(saves==before && packet_size==n && !memcmp(packet,pristine,n));
        pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel_encode,NULL);assert(step_packet(epub,"a.xhtml",&work)==PN_CANCELLED);pn_cancel_leave(&scope);
        assert(!pn_xhtml_cache_pending(epub) && saves==before);parses=0;digest_t actual=parse(pool,epub,"a.xhtml");assert(!parses && actual.hash==expected.hash && actual.count==expected.count);
        assert(pn_xhtml_cache_flush(epub,"a.xhtml")==PN_OK && saves==before+1 && packet_size==n && !memcmp(packet,pristine,n));
    }
    pn_xhtml_cache_retry(epub);pn_xhtml_snapshot_work_t work;assert(step_packet(epub,"a.xhtml",&work)==PN_BUSY);
    (void)pn_cache_reclaim(pool,SIZE_MAX);assert(pool->cache_stats[PN_CACHE_CHAPTER].bytes>0 && pn_xhtml_cache_pending(epub));
    parses=0;digest_t actual=parse(pool,epub,"a.xhtml");assert(!pn_xhtml_cache_pending(epub) && !parses && actual.hash==expected.hash && pn_xhtml_cache_flush(epub,"a.xhtml")==PN_OK);
    pn_xhtml_cache_retry(epub);refuse=true;assert(pn_xhtml_cache_flush(epub,"a.xhtml")==PN_BUSY && !pn_xhtml_cache_pending(epub));refuse=false;assert(pn_xhtml_cache_flush(epub,"a.xhtml")==PN_OK);
    pn_xhtml_cache_retry(epub);assert(step_packet(epub,"a.xhtml",&work)==PN_BUSY && step_packet(epub,"b.xhtml",&work)==PN_STALE_JOB && !pn_xhtml_cache_pending(epub));assert(pn_xhtml_cache_flush(epub,"a.xhtml")==PN_OK);
    pn_xhtml_cache_retry(epub);size_t before=pool->used,limit=pool->limit;assert(step_packet(epub,"a.xhtml",&work)==PN_BUSY);
    pool->limit=pool->used+512u*1024u-1u;pn_status_t s;unsigned steps=0;do{s=step_packet(epub,"a.xhtml",&work);assert(++steps<4096);}while(s==PN_BUSY);
    assert(s==PN_EMPTY && !pn_xhtml_cache_pending(epub) && pool->used==before);pool->limit=limit;
    pn_xhtml_cache_retry(epub);assert(pn_xhtml_cache_flush(epub,"a.xhtml")==PN_OK && packet_size==n && !memcmp(packet,pristine,n));
}
static uint32_t crc_packet(const uint8_t *p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void repair(void){put32(packet+144,crc_packet(packet+152,packet_size-152));put32(packet+148,crc_packet(packet,148));}
int main(int argc,char **argv){
    assert(argc==2);pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease={0};assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);pn_text_file_t file={0};pn_text_source_t source;assert(pn_text_file_open(&file,&media,&lease,argv[1],&source)==PN_OK);pn_xhtml_store_t store={.load=load_packet,.save=save_packet};assert(pn_identity_stream(source.ctx,source.size,source.read_at,&store.book)==PN_OK);
    pn_zip_t zip={0};pn_epub_t epub={0};assert(pn_zip_open(&zip,&pool,&source)==PN_OK && pn_epub_open(&epub,&pool,&zip,salt)==PN_OK);epub.semantic_store=&store;digest_t expected=parse(&pool,&epub,"a.xhtml");assert(pn_xhtml_cache_flush(&epub,"a.xhtml")==PN_OK && packet_size>152 && packet_size<=PN_XHTML_SNAPSHOT_MAX && pn_xhtml_cache_flush(&epub,"a.xhtml")==PN_EMPTY);
    // 改为分步前固定的v1载荷摘要；不以同一编码器互相比对代替兼容性。/ V1 payload digest frozen before staging, rather than comparing the encoder with itself.
    const uint8_t baseline_sha[32]={0x54,0x61,0x0c,0x4a,0x66,0x9b,0x42,0xdc,0x08,0x7e,0x74,0x94,0x2c,0x97,0xeb,0x8a,0x59,0x02,0xfa,0xac,0x87,0x28,0x2b,0xa8,0xf6,0x0c,0xba,0xd2,0x13,0x07,0x46,0x7a};
    pn_book_id_t baseline;assert(packet_size==15062 && pn_identity_bytes(packet+152,packet_size-152,&baseline)==PN_OK && !memcmp(baseline.sha256,baseline_sha,32));
    uint8_t *pristine=malloc(packet_size);assert(pristine);memcpy(pristine,packet,packet_size);size_t original_size=packet_size;const uint8_t fresh[16]={9,7,5};
    staged_checks(&pool,&epub,pristine,original_size,expected);
    pn_xhtml_cache_retry(&epub);pn_xhtml_snapshot_work_t phase_work;assert(step_packet(&epub,"a.xhtml",&phase_work)==PN_BUSY);store.book.sha256[0]^=1;assert(step_packet(&epub,"a.xhtml",&phase_work)==PN_STALE_JOB && !pn_xhtml_cache_pending(&epub));store.book.sha256[0]^=1;assert(pn_xhtml_cache_flush(&epub,"a.xhtml")==PN_OK);
    pn_epub_close(&epub);assert(pn_epub_open(&epub,&pool,&zip,fresh)==PN_OK);epub.semantic_store=&store;parses=0;digest_t actual={.hash=UINT64_C(14695981039346656037)};pn_xhtml_stats_t loaded;assert(pn_xhtml_parse(&pool,&epub,"a.xhtml",fresh,event,&actual,&loaded)==PN_OK && actual.hash==expected.hash && actual.count==expected.count && !parses && loads>0);
    // CRC每块后取消：不发布事件/统计、不退回源XML；恢复仍精确命中原包。/ Cancel after each CRC chunk: publish no events/stats or source XML, then resume with an exact cache hit.
    for(crc_stop_at=1;crc_stop_at<=4;crc_stop_at++){
        pn_xhtml_cache_close(&epub);watch_crc=true;loaded_packet=false;stop_crc=false;crc_chunks=0;parses=0;
        pn_cancel_scope_t crc_scope;pn_cancel_enter(&crc_scope,cancelled_crc,NULL);digest_t cancelled={.hash=17};pn_xhtml_stats_t unchanged={.elements=12345};
        assert(pn_xhtml_parse(&pool,&epub,"a.xhtml",fresh,event,&cancelled,&unchanged)==PN_CANCELLED && crc_chunks==crc_stop_at && !parses && !cancelled.count && cancelled.hash==17 && unchanged.elements==12345);
        pn_cancel_leave(&crc_scope);watch_crc=false;actual=parse(&pool,&epub,"a.xhtml");assert(!parses && actual.hash==expected.hash && actual.count==expected.count);
    }
    // CRC之外也检查结构和标量；修复CRC后坏字段仍必须回到完整源。/ Structural/scalar checks also reject bad fields after CRC repair and fall back to complete source parsing.
    // v2沿用同一事件编码，未知版本才拒绝；包头匹配不替代完整载荷验证。/ V2 shares the event encoding, while unknown versions are rejected; matching a header never replaces payload validation.
    pn_xhtml_cache_close(&epub);memcpy(packet,pristine,original_size);packet[4]=2;repair();parses=0;actual=parse(&pool,&epub,"a.xhtml");assert(actual.hash==expected.hash && actual.count==expected.count && !parses);
    for(unsigned mode=0;mode<7;mode++){
        pn_xhtml_cache_close(&epub);memcpy(packet,pristine,original_size);packet_size=original_size;
        if(mode==0)packet[4]=99;else if(mode==1)packet[8]^=1;else if(mode==2)packet[40]^=1;else if(mode==3)packet[152+20]=255;else if(mode==4)packet[packet_size-1]^=1;else if(mode==5)packet_size--;else{packet[118]=33;}
        if(mode!=4 && mode!=5)repair();
        parses=0;actual=parse(&pool,&epub,"a.xhtml");assert(actual.hash==expected.hash && actual.count==expected.count && parses>0);
    }
    pn_xhtml_cache_close(&epub);memcpy(packet,pristine,original_size);packet_size=original_size;pool.cache_caps[PN_CACHE_CHAPTER]=4096;parses=0;actual=parse(&pool,&epub,"a.xhtml");assert(actual.hash==expected.hash && parses>0);pool.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;
    pn_xhtml_cache_close(&epub);digest_t long_alt=parse(&pool,&epub,"c.xhtml");unsigned record_steps=0;pn_status_t encoded;do{encoded=step_packet(&epub,"c.xhtml",&phase_work);if(phase_work.phase==PN_XHTML_SNAPSHOT_RECORDS)record_steps++;}while(encoded==PN_BUSY);assert(encoded==PN_OK && record_steps>1);
    pn_xhtml_cache_close(&epub);parses=0;actual=parse(&pool,&epub,"c.xhtml");assert(!parses && actual.hash==long_alt.hash && actual.count==long_alt.count);
    pn_xhtml_cache_retry(&epub);assert(step_packet(&epub,"c.xhtml",&phase_work)==PN_BUSY);pn_xhtml_cache_close(&epub);assert(!pn_xhtml_cache_pending(&epub));
    unsigned cancelled=0;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel_parse,&cancelled);pn_xhtml_stats_t stats={.elements=12345};pn_status_t stopped=pn_xhtml_parse(&pool,&epub,"a.xhtml",salt,event,&actual,&stats);if(stopped!=PN_CANCELLED || stats.elements!=12345)fprintf(stderr,"cancel parse status=%d checkpoints=%u stats=%llu\n",stopped,cancelled,(unsigned long long)stats.elements);assert(stopped==PN_CANCELLED && stats.elements==12345);pn_cancel_leave(&scope);
    (void)parse(&pool,&epub,"a.xhtml");pn_xhtml_cache_retry(&epub);assert(step_packet(&epub,"a.xhtml",&phase_work)==PN_BUSY && pn_media_detach(&media)==PN_OK && step_packet(&epub,"a.xhtml",&phase_work)==PN_STALE_MEDIA && !pn_xhtml_cache_pending(&epub));
    pn_epub_close(&epub);assert(pn_zip_close(&zip)==PN_OK && pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK && !pool.used && !pool.live);free(pristine);free(packet);puts("semantic snapshot: fixed v1 payload, bounded stages, cancellation/retry, pinned replay, long strings, port/source changes, exact decoded events, corruption and zero cleanup passed");return 0;
}
