/* 中文：大短段章的缓存与有界复放，冷源仍拒绝损坏尾部。 / English: cache large short-run chapters and bound replay while still rejecting cold corrupt tails. */
#define main semantic_fixture_main
#include "test_semantic_cache.c"
#undef main
static unsigned parses;
pn_status_t __real_pn_xml_parse(pn_pool_t *,const pn_xml_input_t *,const pn_xml_hooks_t *,void *,uint64_t,const uint8_t[16]);
pn_status_t __wrap_pn_xml_parse(pn_pool_t *p,const pn_xml_input_t *i,const pn_xml_hooks_t *h,void *c,uint64_t limit,const uint8_t s[16]){parses++;return __real_pn_xml_parse(p,i,h,c,limit,s);}
static bool done(void *ctx){return ((digest_t *)ctx)->count>=32;}
int main(int argc,char **argv){
 assert(argc==2);pn_pool_t p;assert(!pn_pool_init(&p,3u*1024u*1024u,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
 pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);pn_text_file_t f={0};pn_text_source_t source;assert(pn_text_file_open(&f,&media,&lease,argv[1],&source)==PN_OK);
 pn_zip_t z={0};pn_epub_t e={0};assert(pn_zip_open(&z,&p,&source)==PN_OK && pn_epub_open(&e,&p,&z,salt)==PN_OK);
 p.cache_caps[PN_CACHE_CHAPTER]=0;digest_t full={.hash=14695981039346656037ull};pn_xhtml_stats_t expected,stats;
 assert(pn_xhtml_parse(&p,&e,"a.xhtml",salt,event,&full,&expected)==PN_OK && full.count>16000);
 digest_t cold={.hash=14695981039346656037ull};assert(pn_xhtml_parse_window(&p,&e,"a.xhtml",salt,NULL,event,done,NULL,&cold,&stats)==PN_OK && cold.count==32 && !memcmp(&stats,&expected,sizeof stats));
 p.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;digest_t built=parse(&p,&e,"a.xhtml");assert(built.hash==full.hash && built.count==full.count && p.cache_stats[PN_CACHE_CHAPTER].bytes>0);
 parses=0;size_t attempts=p.attempts;digest_t window={.hash=14695981039346656037ull};assert(pn_xhtml_parse_window(&p,&e,"a.xhtml",salt,NULL,event,done,NULL,&window,&stats)==PN_OK && window.count==32 && window.hash==cold.hash && !memcmp(&stats,&expected,sizeof stats) && !parses && p.attempts==attempts);
 printf("Large short-run chapter: events=%zu charged=%zu replay_events=%zu xml_parses=%u\n",full.count,p.cache_stats[PN_CACHE_CHAPTER].bytes,window.count,parses);fflush(stdout);
 digest_t replay=parse(&p,&e,"a.xhtml");assert(replay.hash==full.hash && replay.count==full.count);
 // head样式收集先完整解析，损坏尾部在发出任何正文前拒绝。 / Head stylesheet collection parses fully first, rejecting the corrupt tail before any body event.
 pn_xhtml_cache_close(&e);stats.elements=12345;cold=(digest_t){0};assert(pn_xhtml_parse_window(&p,&e,"bad.xhtml",salt,NULL,event,done,NULL,&cold,&stats)==PN_CORRUPT && !cold.count && stats.elements==12345);
 pn_xhtml_cache_close(&e);p.cache_caps[PN_CACHE_CHAPTER]=4096;window=(digest_t){0};assert(pn_xhtml_parse_window(&p,&e,"a.xhtml",salt,NULL,event,done,NULL,&window,&stats)==PN_OK && window.count==32);pn_cache_snapshot(&p);assert(!p.cache_stats[PN_CACHE_CHAPTER].bytes);
 p.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;pn_xhtml_cache_close(&e);built=parse(&p,&e,"a.xhtml");digest_t stale={.media=&media,.detach=true};stats.elements=12345;
 assert(pn_xhtml_parse_window(&p,&e,"a.xhtml",salt,NULL,event,done,NULL,&stale,&stats)==PN_STALE_MEDIA && stats.elements==12345);
 pn_epub_close(&e);assert(pn_zip_close(&z)==PN_OK && pn_text_file_close(&f)==PN_OK && pn_media_release(&media,&lease)==PN_OK && !p.used && !p.live);
 puts("EPUB windows: exact full events/stats, packed cache, bounded hot replay, cold corrupt tail, low-budget fallback and stale media passed");return 0;
}
