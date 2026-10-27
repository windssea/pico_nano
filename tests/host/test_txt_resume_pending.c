/* SPDX-License-Identifier: Apache-2.0
 * TXT来源未确认时回原解析，保持后台完整复核与逐像素结果。/ Parse original text while verification is pending, preserving full background checks and exact pixels. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include "pn_profile.h"
#include "pn_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
static uint8_t screen[415872], expected[415872];
static unsigned hits, pending_misses;
static uint64_t clock_us(void *ctx){(void)ctx;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;}
static void observe(void *ctx,const pn_profile_event_t *event){
    (void)ctx;
    if(!strcmp(event->phase,"resume.hit"))hits++;
    if(!strcmp(event->phase,"resume.miss.source-pending"))pending_misses++;
}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t refresh){
    (void)ctx;(void)refresh;memcpy(screen,frame->pixels,sizeof screen);return PN_OK;
}
int main(int argc,char **argv){
    assert(argc==4 && !setenv("PN_READING_CACHE",argv[3],1));
    pn_w_set_clock("12:34");pn_w_set_battery(85);
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    pn_profile_port(clock_us,observe,NULL);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_reader_app_t app={0};pn_book_id_t book;pn_txt_progress_t position;
    assert(pn_reader_app_open_on_media(&app,&pool,&media,argv[1],NULL,argv[2],32,0)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,1,present,NULL)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_NEXT,2,present,NULL)==PN_OK);
    assert(pn_reader_app_identity(&app,&book)==PN_OK && pn_reader_app_progress(&app,&position)==PN_OK);
    memcpy(expected,screen,sizeof screen);
    unsigned steps=0;
    while(pn_reader_app_resume_checkpoint_pending(&app)){
        pn_status_t s=pn_reader_app_resume_checkpoint_step(&app);assert(s==PN_OK || s==PN_BUSY);assert(++steps<4096);
    }
    assert(steps && pn_reader_app_close(&app,3)==PN_OK && !pool.used);
    // 未改变来源时，完整开书确实命中已认证的包；随后模拟重启的介质代次。/ Full opening must really hit the authenticated packet before simulating a new media generation.
    hits=0;assert(pn_reader_app_open_on_media(&app,&pool,&media,argv[1],NULL,argv[2],32,4)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,5,present,NULL)==PN_OK && hits==1 && !memcmp(screen,expected,sizeof screen));
    assert(pn_reader_app_close(&app,6)==PN_OK && pn_media_detach(&media)==PN_OK && pn_media_attach(&media,2)==PN_OK);
    hits=pending_misses=0;
    assert(pn_reader_app_open_trusted(&app,&pool,&media,argv[1],NULL,argv[2],32,7,&book,position.source_size)==PN_OK);
    assert(pn_reader_app_verify_pending(&app));
    assert(pn_reader_app_step(&app,PN_APP_OPEN,8,present,NULL)==PN_OK);
    assert(!hits && pending_misses==1 && pn_reader_app_verify_pending(&app) && !memcmp(screen,expected,sizeof screen));
    steps=0;
    bool encoding_fallback=false;
    while(pn_reader_app_verify_pending(&app)){
        pn_status_t s=pn_reader_app_verify_step(&app);
        // 现有流式探测对GBK保守拒绝，产品以完整构造重开；不能在本次优化中放宽它。/ The existing streaming GBK probe rejects conservatively and production reopens fully; this optimization must never relax that rejection.
        if(s==PN_CORRUPT && getenv("PN_EXPECT_GBK_FALLBACK")){encoding_fallback=true;break;}
        assert(s==PN_OK || s==PN_BUSY);assert(++steps<100000);
    }
    assert(pn_reader_app_close(&app,9)==PN_OK && !pool.used && !pool.live);
    if(encoding_fallback){
        assert(pn_reader_app_open_on_media(&app,&pool,&media,argv[1],NULL,argv[2],32,10)==PN_OK);
        assert(pn_reader_app_step(&app,PN_APP_OPEN,11,present,NULL)==PN_OK && !memcmp(screen,expected,sizeof screen));
        assert(pn_reader_app_close(&app,12)==PN_OK && !pool.used && !pool.live);
        pn_profile_port(NULL,NULL,NULL);puts("TXT GBK: no foreground drain, conservative background rejection and exact full-parser recovery passed");return 0;
    }
    // 远离首屏的等长篡改仍必须由完整后台SHA发现，缓存不能授予原书信任。/ A same-length mutation far beyond the first page must still fail full background SHA; pixels never grant source trust.
    FILE *f=fopen(argv[1],"r+b");assert(f && !fseek(f,-1,SEEK_END) && fputc('X',f)!=EOF && !fclose(f));
    assert(pn_media_detach(&media)==PN_OK && pn_media_attach(&media,3)==PN_OK);
    assert(pn_reader_app_open_trusted(&app,&pool,&media,argv[1],NULL,argv[2],32,10,&book,position.source_size)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,11,present,NULL)==PN_OK && pn_reader_app_verify_pending(&app));
    pn_status_t verified=PN_BUSY;steps=0;
    while(verified==PN_BUSY){verified=pn_reader_app_verify_step(&app);assert(++steps<100000);}
    assert(verified==PN_STALE_JOB && pn_reader_app_close(&app,12)==PN_OK && !pool.used && !pool.live);
    pn_profile_port(NULL,NULL,NULL);
    puts("TXT resume: no foreground verification drain, exact pixels, verified packet hit, full tail mutation detection passed");return 0;
}
