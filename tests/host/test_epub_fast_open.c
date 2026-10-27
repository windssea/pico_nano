/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：无可信块表时开书完成整源校验，首屏与完整打开逐字节一致；旧身份不符须完整导入，字体后台检查保持。
 * English: without an authenticated block table, opening completes full-source verification with identical pixels; stale identities require complete import and font background checks remain.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_app.h"
#include "pn_font_asset.h"
#include "pn_font_preferences.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t full[342u*1216u],fast[342u*1216u];
static pn_status_t capture(void *ctx,const pn_frame_t *f,pn_refresh_t profile){
    (void)profile;uint8_t *out=ctx;for(int y=0;y<f->height;y++)memcpy(out+(size_t)y*342,f->pixels+(size_t)y*f->stride,342);return PN_OK;
}
static pn_status_t finish(pn_epub_open_job_t *job,pn_epub_app_t *app){pn_status_t s;while((s=pn_epub_app_open_job_step(job,app))==PN_BUSY){}return s;}
static bool stop(void *ctx){(void)ctx;return true;}
static void opening_boundaries(pn_pool_t *pool,const char *book,const char *state){
    pn_epub_open_job_t job={0};pn_epub_app_t app={0};unsigned steps=0;pn_status_t s;
    assert(!pn_epub_app_open_job_begin(&job,pool,NULL,book,NULL,state,44,0));
    do{s=pn_epub_app_open_job_step(&job,&app);assert(++steps<4096);}while(s==PN_BUSY);
    assert(s==PN_OK && !job.impl && !pn_epub_app_discard_unshown(&app) && !pool->used);
    for(unsigned pause=0;pause<steps;pause++){
        assert(!pn_epub_app_open_job_begin(&job,pool,NULL,book,NULL,state,44,0));
        for(unsigned k=0;k<pause;k++)assert(pn_epub_app_open_job_step(&job,&app)==PN_BUSY && !app.impl);
        pn_cancel_scope_t scope;pn_cancel_enter(&scope,stop,NULL);
        assert(pn_epub_app_open_job_step(&job,&app)==PN_CANCELLED && !app.impl && pn_epub_app_open_job_pending(&job));pn_cancel_leave(&scope);
        if(pause&1){assert(finish(&job,&app)==PN_OK && !pn_epub_app_discard_unshown(&app));}
        else assert(!pn_epub_app_open_job_close(&job));
        assert(!job.impl && !pool->used && !pool->live);
    }
    printf("opening cancellation boundaries=%u used=0 live=0\n",steps);
}
int main(int argc,char **argv){
    assert(argc==4);const char *book=argv[1],*state=argv[2],*font=argv[3];
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==0);
    pn_epub_app_t app={0};pn_epub_open_job_t job={0};pn_book_id_t id;uint64_t now=1;
    opening_boundaries(&pool,book,state);
    FILE *f=fopen(book,"rb");assert(f && !fseek(f,0,SEEK_END));long size=ftell(f);fclose(f);
    assert(pn_epub_app_open_job_begin(&job,&pool,NULL,book,NULL,state,44,now++)==PN_OK && finish(&job,&app)==PN_OK && !pn_epub_app_verify_pending(&app));
    assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,capture,full)==PN_OK && pn_epub_app_identity(&app,&id)==PN_OK && pn_epub_app_close(&app,now++)==PN_OK);
    assert(pn_epub_app_open_job_begin_trusted(&job,&pool,NULL,book,NULL,state,44,now++,&id,(uint64_t)size)==PN_OK && finish(&job,&app)==PN_OK && !pn_epub_app_verify_pending(&app));
    assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,capture,fast)==PN_OK && !memcmp(full,fast,sizeof full));
    pn_status_t s;unsigned steps=0;while((s=pn_epub_app_verify_step(&app))==PN_BUSY)steps++;
    assert(s==PN_EMPTY && !pn_epub_app_verify_pending(&app) && pn_epub_app_close(&app,now++)==PN_OK);
    pn_book_id_t wrong=id;wrong.sha256[31]^=0x80;
    assert(pn_epub_app_open_job_begin_trusted(&job,&pool,NULL,book,NULL,state,44,now++,&wrong,(uint64_t)size)==PN_OK && finish(&job,&app)==PN_OK);
    while((s=pn_epub_app_verify_step(&app))==PN_BUSY){}
    pn_book_id_t actual;assert(s==PN_EMPTY && pn_epub_app_source_reimported(&app) && pn_epub_app_identity(&app,&actual)==PN_OK && !memcmp(actual.sha256,id.sha256,32) && pn_epub_app_close(&app,now++)==PN_OK);
    assert(pn_epub_app_open_job_begin_trusted(&job,&pool,NULL,book,NULL,state,44,now++,&id,(uint64_t)size+1)==PN_OK && finish(&job,&app)==PN_OK && !pn_epub_app_verify_pending(&app));
    assert(pn_epub_app_close(&app,now++)==PN_OK && pool.used==0 && pool.live==0);
    // 全局外置阅读字体：快开不在首屏前整读字体（故意错的字体摘要也能打开），后台先复核字体并报告不符；完整打开仍校验并拒用该字体。
    // Global external reading font: a fast open never reads the font whole before the first screen (even a deliberately wrong font digest opens), background verification checks the font first and reports the mismatch; a full open still verifies and refuses that font.
    {pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
     pn_font_asset_t asset={0};pn_font_preferences_t prefs={0};assert(pn_font_asset_open(&asset,&pool,&media,&lease,font,0,44)==PN_OK && pn_font_asset_details(&asset,&prefs.primary,NULL)==PN_OK && pn_font_asset_close(&asset)==PN_OK);
     assert(pn_media_release(&media,&lease)==PN_OK && pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);prefs.fallback.kind=PN_FONT_RESIDENT;pn_journal_files_t files;pn_journal_io_t io;
     assert(pn_font_preferences_files(&files,&media,&lease,state,NULL,&io)==PN_OK && pn_font_preferences_save(&io,&pool,NULL,&prefs)==PN_OK);
     assert(pn_epub_app_open_job_begin_trusted(&job,&pool,NULL,book,NULL,state,44,now++,&id,(uint64_t)size)==PN_OK && finish(&job,&app)==PN_OK && !pn_epub_app_font_unavailable(&app) && pn_epub_app_verify_pending(&app));
     unsigned with_font=0;while((s=pn_epub_app_verify_step(&app))==PN_BUSY)with_font++;assert(s==PN_OK && with_font>steps && pn_epub_app_close(&app,now++)==PN_OK);
     prefs.primary.identity.sha256[0]^=1;assert(pn_font_preferences_save(&io,&pool,NULL,&prefs)==PN_OK);
     assert(pn_epub_app_open_job_begin_trusted(&job,&pool,NULL,book,NULL,state,44,now++,&id,(uint64_t)size)==PN_OK && finish(&job,&app)==PN_OK && !pn_epub_app_font_unavailable(&app));
     while((s=pn_epub_app_verify_step(&app))==PN_BUSY){}assert(s==PN_STALE_JOB && pn_epub_app_close(&app,now++)==PN_OK);
     assert(pn_epub_app_open_job_begin(&job,&pool,NULL,book,NULL,state,44,now++)==PN_OK && finish(&job,&app)==PN_OK);
     while((s=pn_epub_app_verify_step(&app))==PN_BUSY){}
     assert(s==PN_STALE_JOB && pn_epub_app_font_unavailable(&app) && pn_epub_app_close(&app,now++)==PN_OK);
     assert(pn_media_release(&media,&lease)==PN_OK);}
    assert(pool.used==0 && pool.live==0);
    printf("epub_fast_open verify_steps=%u\n",steps);return 0;
}
