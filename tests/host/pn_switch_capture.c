/* SPDX-License-Identifier: Apache-2.0
 * 中文：同进程A/B轮换，真实保留/释放对象并观察TF续读命中；不模拟面板或TF耗时。
 * English: same-process A/B switching with real park/release and persistent resume hits, never simulated panel/TF timing.
 */
#define main resume_fixture_main
#include "pn_resume_capture.c"
#undef main
#include "pn_hot_session.h"
#include <assert.h>

static unsigned index_opens;
pn_status_t __real_pn_page_index_open(pn_page_index_t *,pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const char *,const pn_book_id_t *,const pn_book_id_t *);
/// 只计真实索引初始化，不改变返回或IO行为。/ Count real index initialization without changing its result or I/O.
pn_status_t __wrap_pn_page_index_open(pn_page_index_t *h,pn_pool_t *p,pn_media_t *m,const pn_media_lease_t *l,const char *root,const pn_book_id_t *b,const pn_book_id_t *k){
    index_opens++;return __real_pn_page_index_open(h,p,m,l,root,b,k);
}

static void settle_sources(bool warm){
    for(unsigned n=0;pn_epub_app_verify_pending(&epub);n++){assert(n<100000);pn_status_t s=pn_epub_app_verify_step(&epub);assert(s==PN_OK || s==PN_BUSY);}
    for(unsigned n=0;warm && pn_epub_app_prefetch_pending(&epub);n++){assert(n<20000);pn_status_t s=pn_epub_app_prefetch(&epub);assert(s==PN_OK || s==PN_BUSY || s==PN_EMPTY);}
}
static void settle_resume(void){
    for(unsigned n=0;n<20000;n++){
        pn_status_t s=pn_epub_app_resume_cache_idle(&epub);assert(s==PN_OK || s==PN_BUSY || s==PN_EMPTY);
        if(s==PN_EMPTY && !pn_epub_app_resume_cache_pending(&epub))return;
    }
    assert(!"resume writer did not settle");
}
/// 写出部分行后打断，覆盖实际临时文件清理及下次重试。/ Interrupt after partial rows, covering real temporary-file cleanup and retry.
static void interrupt_resume(uint64_t now){
    for(unsigned n=0;n<6;n++)assert(pn_epub_app_resume_cache_idle(&epub)==PN_BUSY && pn_epub_app_resume_cache_pending(&epub));
    pn_epub_app_cache_activity(&epub,now);assert(!pn_epub_app_resume_cache_pending(&epub));
}
int main(int argc,char **argv){
    assert(argc==7);is_epub=true;bool interrupt=!strcmp(argv[6],"interrupted"),early=!strcmp(argv[6],"early"),next_interrupt=!strcmp(argv[6],"next-interrupted");
    char cache[1024];snprintf(cache,sizeof cache,"%s/reading",argv[4]);mkdir(argv[4],0700);mkdir(argv[5],0700);mkdir(cache,0700);setenv("PN_READING_CACHE",cache,1);
    pn_w_set_clock("12:34");pn_w_set_battery(85);pn_profile_port(clock_us,observe,NULL);
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_hot_session_t hot={0};pn_epub_progress_t expected[2];uint8_t *pixels[2]={malloc(415872),malloc(415872)};assert(pixels[0] && pixels[1]);
    for(unsigned round=0;round<4;round++){
        unsigned which=round%2;const char *book=argv[2+which];uint64_t now=round*10000u;hits=stored=loads=shown=0;dump_ns=0;
        char directory[1024];snprintf(directory,sizeof directory,"%s/%u",argv[5],round);mkdir(directory,0700);output=directory;
        index_opens=0;uint64_t began=stamp();pn_status_t s=pn_hot_session_resume(&hot,&media,book,PN_BOOK_EPUB,NULL,1,now,&txt,&epub,present,NULL);
        assert(s==PN_EMPTY);assert(pn_hot_session_drop(&hot)==PN_OK);
        assert(pn_epub_app_open_on_media(&epub,&pool,&media,book,argv[1],argv[4],44,now)==PN_OK);
        if(round>=2)assert(pn_epub_app_prepare_resume_unshown(&epub)==PN_OK);
        assert(step(PN_APP_OPEN,now)==PN_OK);uint64_t opened=stamp()-began-dump_ns;unsigned open_hits=hits,open_indexes=index_opens;
        if(round>=2 && getenv("PN_SWITCH_LAZY"))assert(open_indexes==0);
        if(round>=2 && !getenv("PN_SWITCH_BASELINE"))assert(open_hits==1);
        if(round<2){for(unsigned page=0;page<=which;page++)assert(step(PN_APP_NEXT,now+page+1)==PN_OK);assert(pn_epub_app_progress(&epub,&expected[which])==PN_OK);}
        else{pn_epub_progress_t p;assert(pn_epub_app_progress(&epub,&p)==PN_OK);assert(!strcmp(p.location.path,expected[which].location.path) && !memcmp(&p.location.position,&expected[which].location.position,sizeof p.location.position));}
        char frame[1100];snprintf(frame,sizeof frame,"%s/%u.raw",directory,shown-1);FILE *file=fopen(frame,"rb");assert(file);
        uint8_t *actual=malloc(415872);assert(actual && fread(actual,1,415872,file)==415872 && !fclose(file));
        if(round<2)memcpy(pixels[which],actual,415872);else assert(!memcmp(pixels[which],actual,415872));free(actual);
        uint64_t next_ns=0,prev_ns=0;unsigned next_hits=0,prev_hits=0;
        if(round>=2){uint64_t dump_before=dump_ns;began=stamp();assert(step(PN_APP_NEXT,now+1)==PN_OK);next_ns=stamp()-began-(dump_ns-dump_before);next_hits=hits-open_hits;if(next_interrupt)assert(next_hits==1);}
        if(round>=2){
            uint64_t dump_before=dump_ns;unsigned hit_before=hits;began=stamp();assert(step(PN_APP_PREVIOUS,now+2)==PN_OK);prev_ns=stamp()-began-(dump_ns-dump_before);prev_hits=hits-hit_before;
            snprintf(frame,sizeof frame,"%s/%u.raw",directory,shown-1);file=fopen(frame,"rb");assert(file);uint8_t *back=malloc(415872);assert(back && fread(back,1,415872,file)==415872 && !fclose(file) && !memcmp(back,pixels[which],415872));free(back);
            if(next_interrupt)assert(prev_hits==1 && index_opens==0);
        }
        settle_sources(!early);
        if(interrupt && round<2)interrupt_resume(now+5000);
        if(next_interrupt && round<2){
            for(unsigned n=0;!stored;n++){assert(n<20000);pn_status_t v=pn_epub_app_resume_cache_idle(&epub);assert(v==PN_BUSY || v==PN_OK);}
            interrupt_resume(now+5000);
        }
        settle_resume();unsigned saved=stored;
        if(round<2 && !getenv("PN_SWITCH_BASELINE"))assert(saved==(early?1u:2u));
        assert(pn_hot_session_park(&hot,&pool,&media,book,1,now+6000,&txt,&epub)==PN_OK);
        pn_status_t flushed=pn_hot_session_deferred_flush(&hot);assert(flushed==PN_OK || flushed==PN_EMPTY);
        printf("switch round=%u book=%c mode=%s open_hits=%u open_indexes=%u stored=%u open_ns=%llu next_hits=%u next_ns=%llu prev_hits=%u prev_ns=%llu used=%zu peak=%zu\n",round,'A'+which,argv[6],open_hits,open_indexes,saved,(unsigned long long)opened,next_hits,(unsigned long long)next_ns,prev_hits,(unsigned long long)prev_ns,pool.used,pool.peak);fflush(stdout);
    }
    assert(pn_hot_session_drop(&hot)==PN_OK && !pn_media_active(&media) && !pool.used && !pool.live);free(pixels[0]);free(pixels[1]);return 0;
}
