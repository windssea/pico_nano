/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB快开：跳过首屏前整书摘要后首屏与完整打开逐字节一致（含插图），后台复核通过；身份不符报告内容已变，大小不符走完整路径。
 * English: EPUB fast open: skipping the pre-screen whole-book hash yields a first screen identical to a full open (including images) and background verification passes; a wrong identity reports changed content and a size mismatch takes the complete path.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t full[342u*1216u],fast[342u*1216u];
static pn_status_t capture(void *ctx,const pn_frame_t *f,pn_refresh_t profile){
    (void)profile;uint8_t *out=ctx;for(int y=0;y<f->height;y++)memcpy(out+(size_t)y*342,f->pixels+(size_t)y*f->stride,342);return PN_OK;
}
static pn_status_t finish(pn_epub_open_job_t *job,pn_epub_app_t *app){pn_status_t s;while((s=pn_epub_app_open_job_step(job,app))==PN_BUSY){}return s;}
int main(int argc,char **argv){
    assert(argc==3);const char *book=argv[1],*state=argv[2];
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==0);
    pn_epub_app_t app={0};pn_epub_open_job_t job={0};pn_book_id_t id;uint64_t now=1;
    FILE *f=fopen(book,"rb");assert(f && !fseek(f,0,SEEK_END));long size=ftell(f);fclose(f);
    assert(pn_epub_app_open_job_begin(&job,&pool,NULL,book,NULL,state,44,now++)==PN_OK && finish(&job,&app)==PN_OK && !pn_epub_app_verify_pending(&app));
    assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,capture,full)==PN_OK && pn_epub_app_identity(&app,&id)==PN_OK && pn_epub_app_close(&app,now++)==PN_OK);
    assert(pn_epub_app_open_job_begin_trusted(&job,&pool,NULL,book,NULL,state,44,now++,&id,(uint64_t)size)==PN_OK && finish(&job,&app)==PN_OK && pn_epub_app_verify_pending(&app));
    assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,capture,fast)==PN_OK && !memcmp(full,fast,sizeof full));
    pn_status_t s;unsigned steps=0;while((s=pn_epub_app_verify_step(&app))==PN_BUSY)steps++;
    assert(s==PN_OK && !pn_epub_app_verify_pending(&app) && pn_epub_app_verify_step(&app)==PN_EMPTY && pn_epub_app_close(&app,now++)==PN_OK);
    pn_book_id_t wrong=id;wrong.sha256[31]^=0x80;
    assert(pn_epub_app_open_job_begin_trusted(&job,&pool,NULL,book,NULL,state,44,now++,&wrong,(uint64_t)size)==PN_OK && finish(&job,&app)==PN_OK);
    while((s=pn_epub_app_verify_step(&app))==PN_BUSY){}
    assert(s==PN_STALE_JOB && pn_epub_app_close(&app,now++)==PN_OK);
    assert(pn_epub_app_open_job_begin_trusted(&job,&pool,NULL,book,NULL,state,44,now++,&id,(uint64_t)size+1)==PN_OK && finish(&job,&app)==PN_OK && !pn_epub_app_verify_pending(&app));
    assert(pn_epub_app_close(&app,now++)==PN_OK && pool.used==0 && pool.live==0);
    printf("epub_fast_open verify_steps=%u\n",steps);return 0;
}
