/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：主机测试图入口，使用固件共用绘制代码。
 * English: host test-pattern entry using the firmware's shared drawing code.
 *
 * 冻结：不模拟真实面板光学效果，不宣称阅读功能已实现。
 * Frozen: no simulation of panel optics and no claim of implemented reading features.
 */
#include "pn_alloc.h"
#include "pn_frame.h"
#include "pn_display.h"
#include "reader_capture.h"
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#ifdef PN_SIM_SDL
int pn_sim_epub_window(pn_epub_app_t *,pn_pool_t *);
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef PN_SIM_SDL
int pn_sim_window(const pn_frame_t *frame);
int pn_sim_reader_window(pn_reader_app_t *app,pn_pool_t *pool);
int pn_sim_library_window(pn_pool_t *pool,const char *directory,const char *font,const char *fallback,const char *state);
#endif

static int save_pgm(const char *path, const pn_frame_t *frame) {
    FILE *file=fopen(path,"wb");
    if (!file) { perror(path); return 1; }
    int failed=fprintf(file,"P5\n%d %d\n255\n",frame->width,frame->height)<0;
    uint8_t row[684];
    for (int y=0;y<frame->height && !failed;y++) {
        for (int x=0;x<frame->width;x++) row[x]=(uint8_t)(pn_frame_get(frame,x,y)*17);
        failed=fwrite(row,1,(size_t)frame->width,file)!=(size_t)frame->width;
    }
    if (fclose(file)!=0) failed=1;
    return failed;
}

static pn_status_t prepare(pn_display_t *display, pn_job_token_t token, uint64_t page, bool pattern) {
    pn_draw_lease_t lease;
    pn_frame_t *frame=NULL;
    pn_status_t status=pn_display_begin_draw(display,token,&lease,&frame);
    if (status!=PN_OK) return status;
    if (pattern) pn_frame_test_pattern(frame);
    else pn_frame_clear(frame,0);
    status=pn_display_publish(display,&lease,page,PN_REFRESH_GC16);
    if (status!=PN_OK) (void)pn_display_discard(display,&lease);
    return status;
}

static pn_status_t ownership_scenario(pn_display_t *display, pn_display_job_t *visible) {
    pn_job_token_t old_token=display->current, new_token={old_token.session,old_token.generation+1};
    pn_status_t status=prepare(display,old_token,1,false);
    if (status!=PN_OK) return status;
    pn_display_job_t old_job;
    status=pn_display_start(display,&old_job);
    if (status!=PN_OK) return status;
    status=pn_display_set_token(display,new_token);
    if (status==PN_OK) status=prepare(display,new_token,2,true);
    if (status!=PN_OK) { (void)pn_display_complete(display,&old_job,false); return status; }
    status=pn_display_complete(display,&old_job,true);
    if (status!=PN_STALE_JOB) return PN_CORRUPT;
    printf("scenario=ownership stale_completion=discarded old_page=1 new_page=2\n");
    return pn_display_start(display,visible);
}

int main(int argc, char **argv) {
    bool headless=false, ownership=false, budget_explicit=false;
    const char *capture=NULL,*book=NULL,*font_path=NULL,*fallback_path=NULL,*state_dir=NULL,*library=NULL;
    unsigned book_page=1;int font_pixels=44;
    size_t budget=1024*1024;
    for (int i=1;i<argc;i++) {
        if (strcmp(argv[i],"--headless")==0) headless=true;
        else if (strcmp(argv[i],"--capture")==0 && i+1<argc) capture=argv[++i];
        else if (strcmp(argv[i],"--book")==0 && i+1<argc) book=argv[++i];
        else if (strcmp(argv[i],"--library")==0 && i+1<argc) library=argv[++i];
        else if (strcmp(argv[i],"--state-dir")==0 && i+1<argc) state_dir=argv[++i];
        else if (strcmp(argv[i],"--fallback-font")==0 && i+1<argc) fallback_path=argv[++i];
        else if (strcmp(argv[i],"--font")==0 && i+1<argc) font_path=argv[++i];
        else if ((strcmp(argv[i],"--page")==0 || strcmp(argv[i],"--size")==0) && i+1<argc) {
            bool is_page=strcmp(argv[i],"--page")==0;const char *value=argv[++i];char *end=NULL;errno=0;
            unsigned long parsed=strtoul(value,&end,10);
            if(*value<'0' || *value>'9' || !end || *end || errno || parsed<1 || parsed>10000)return 2;
            if(is_page)book_page=(unsigned)parsed;else {if(parsed<28 || parsed>72)return 2;font_pixels=(int)parsed;}
        }
        else if (strcmp(argv[i],"--scenario")==0 && i+1<argc && strcmp(argv[i+1],"ownership")==0) {
            ownership=true; i++;
        } else if (strcmp(argv[i],"--budget")==0 && i+1<argc) {
            char *end=NULL;
            const char *value=argv[++i];
            errno=0;
            unsigned long long parsed=strtoull(value,&end,10);
            if (*value < '0' || *value > '9' || !end || *end || errno || parsed > SIZE_MAX) return 2;
            budget=(size_t)parsed;
            budget_explicit=true;
        } else {
            fprintf(stderr,"Usage: pn_sim [--headless] [--capture FILE.pgm] [--budget BYTES] [--scenario ownership] [--book TXT --font TTF --page N --size PX]\n");
            return 2;
        }
    }
    if (headless && !capture) { fprintf(stderr,"Headless mode requires --capture\n"); return 2; }
    if ((ownership && (book || library)) || (book && library) || ((font_path || fallback_path) && !book && !library)) return 2;
    if(fallback_path && (headless || (!book && !library)))return 2;
    if(library && (headless || capture || book_page!=1))return 2;
    if(library && !budget_explicit)budget=6*1024*1024;
    if (ownership && !budget_explicit) budget=2*1024*1024;
    bool epub_book=false;
    if(book){FILE *file=fopen(book,"rb");if(file){uint8_t prefix[4];size_t n=fread(prefix,1,4,file);fclose(file);epub_book=n==4 && prefix[0]=='P' && prefix[1]=='K' && prefix[2]==3 && prefix[3]==4;}}
    if(epub_book && !budget_explicit)budget=6u*1024u*1024u;
    pn_pool_t pool;
    if (pn_pool_init(&pool,budget,NULL,NULL,NULL)!=0) return 1;
    if(library){
#ifdef PN_SIM_SDL
        int result=pn_sim_library_window(&pool,library,font_path,fallback_path,state_dir?state_dir:"sim-data");printf("library_window pool_peak=%zu used=%zu live=%zu\n",pool.peak,pool.used,pool.live);return result;
#else
        fprintf(stderr,"Library window requires PN_SIM_SDL=ON\n");return 2;
#endif
    }
    if(book && !headless) {
#ifdef PN_SIM_SDL
        if(book_page!=1 || capture)return 2;
        if(epub_book){pn_epub_app_t app={0};pn_status_t opened=pn_epub_app_open(&app,&pool,book,font_path,state_dir?state_dir:"sim-data",font_pixels,0);
            if(opened==PN_OK && fallback_path)opened=pn_epub_app_fallback_font(&app,fallback_path);
            int result=opened==PN_OK?pn_sim_epub_window(&app,&pool):1;if(opened!=PN_OK)fprintf(stderr,"EPUB open status=%d\n",(int)opened);
            if(app.impl && pn_epub_app_close(&app,UINT64_MAX)!=PN_OK)result=1;
            printf("epub_window pool_peak=%zu used=%zu live=%zu\n",pool.peak,pool.used,pool.live);return result;
        }
        pn_reader_app_t app={0};pn_status_t opened=pn_reader_app_open(&app,&pool,book,font_path,state_dir?state_dir:"sim-data",font_pixels,0);
        if(opened==PN_OK && fallback_path)opened=pn_reader_app_fallback_font(&app,fallback_path);
        int result=opened==PN_OK?pn_sim_reader_window(&app,&pool):1;
        if(opened!=PN_OK)fprintf(stderr,"Reader open failed: %d\n",(int)opened);
        if(app.impl){pn_status_t closed=pn_reader_app_close(&app,UINT64_MAX);if(closed!=PN_OK)result=1;}
        printf("reader_window pool_peak=%zu used=%zu live=%zu\n",pool.peak,pool.used,pool.live);return result;
#else
        fprintf(stderr,"Interactive reader requires PN_SIM_SDL=ON\n");return 2;
#endif
    }
    if(state_dir)return 2;
    size_t bytes=(size_t)684*1216/2, count=ownership ? 3 : 1;
    uint8_t *pixels[PN_DISPLAY_MAX_BUFFERS]={0};
    pn_frame_t frames[PN_DISPLAY_MAX_BUFFERS]={0};
    int status=0;
    for (size_t i=0;i<count;i++) {
        pixels[i]=pn_alloc(&pool,bytes);
        if (!pn_frame_bind(&frames[i],pixels[i],bytes,684,1216)) {
            fprintf(stderr,"Framebuffer exceeds budget or backing allocation failed\n");
            status=1;
            break;
        }
    }
    if (!status) {
        pn_display_t display;
        pn_job_token_t token={1,1};
        pn_display_job_t job;
        pn_status_t scheduled=pn_display_init(&display,frames,count,token);
        if (scheduled==PN_OK) {
            if (ownership) scheduled=ownership_scenario(&display,&job);
            else {
                scheduled=book?pn_sim_reader_prepare(&display,token,&pool,book,font_path,book_page,font_pixels):prepare(&display,token,1,true);
                if (scheduled==PN_OK) scheduled=pn_display_start(&display,&job);
            }
        }
        if (scheduled!=PN_OK) {
            fprintf(stderr,"Display preparation failed: %d\n",(int)scheduled);
            status=1;
        } else {
            if (capture) status=save_pgm(capture,job.frame);
            if (!headless && !status) {
#ifdef PN_SIM_SDL
                status=pn_sim_window(job.frame);
#else
                fprintf(stderr,"Window backend disabled; rebuild with PN_SIM_SDL=ON or use --headless\n");
                status=2;
#endif
            }
            pn_status_t completed=pn_display_complete(&display,&job,status==0);
            if (completed!=PN_OK && !status) status=1;
        }
    }
    for (size_t i=0;i<count;i++) pn_free(pixels[i]);
    printf("684x1216 4bpp bytes=%zu buffers=%zu pool_peak=%zu used=%zu live=%zu\n",
        bytes,count,pool.peak,pool.used,pool.live);
    return status;
}
