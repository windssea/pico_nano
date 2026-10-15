/* SPDX-License-Identifier: Apache-2.0
 * 中文：字体列表首帧与小样合并呈现的真实文件探针，兼容冻结旧入口。
 * English: real-file probe for font-list first paint and batched samples, compatible with the frozen old entry. */
#include "pn_font_manage.h"
#include "pn_text_file.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/// 主机探针可链接旧版；正常产品不用弱符号。/ Host-only compatibility with old builds; products do not use weak symbols.
extern pn_status_t pn_font_manage_samples_idle(pn_font_manage_t *,uint64_t,pn_font_manage_present_fn,void *) __attribute__((weak));
static unsigned opens,presents;
static bool fail_paint,cancelled;
static uint8_t image[684*1216/2];
pn_status_t __real_pn_text_file_open(pn_text_file_t *,pn_media_t *,const pn_media_lease_t *,const char *,pn_text_source_t *);
pn_status_t __wrap_pn_text_file_open(pn_text_file_t *f,pn_media_t *m,const pn_media_lease_t *l,const char *path,pn_text_source_t *s){opens++;return __real_pn_text_file_open(f,m,l,path,s);}
static pn_status_t present(void *ctx,const pn_frame_t *f,pn_refresh_t mode){(void)ctx;assert(mode==PN_REFRESH_GL16);if(fail_paint){fail_paint=false;return PN_IO;}assert(f->stride==342);memcpy(image,f->pixels,sizeof image);presents++;return PN_OK;}
static bool cancel_check(void *ctx){(void)ctx;return cancelled;}
static void failures(pn_pool_t *pool,pn_media_t *media,const char *directory){
    pn_font_manage_t ui={0};assert(pn_font_manage_open(&ui,pool,media,directory,NULL,NULL,present,NULL)==PN_OK);
    unsigned before=opens;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel_check,NULL);cancelled=true;
    assert(pn_font_manage_samples_idle(&ui,1000,present,NULL)==PN_CANCELLED && opens==before && !pn_media_active(media));cancelled=false;pn_cancel_leave(&scope);
    fail_paint=true;unsigned i=0;
    for(;i<16;i++){pn_status_t s=pn_font_manage_samples_idle(&ui,1000+i,present,NULL);assert(!pn_media_active(media));if(s==PN_IO)break;assert(s==PN_BUSY);}
    assert(i<16 && !ui.presented);before=opens;
    assert(pn_font_manage_samples_idle(&ui,1500,present,NULL)==PN_EMPTY && opens==before);
    assert(pn_font_manage_samples_idle(&ui,10000,present,NULL)==PN_OK && ui.presented && opens==before && !pn_media_active(media));
    pn_font_manage_close(&ui);assert(!pool->used && !pool->live);puts("font samples cancellation, failed batch retention, retry delay and zero face reopen on retry passed");
}
int main(int argc,char **argv){
    assert(argc==5);pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_font_manage_t ui={0};
    assert(pn_font_manage_open(&ui,&pool,&media,argv[1],NULL,NULL,present,NULL)==PN_OK && ui.presented && presents==1 && !pn_media_active(&media));
    unsigned first_opens=opens,max_step=0,steps=0;
    if(pn_font_manage_samples_idle){
        assert(first_opens==0);
        for(;steps<128;steps++){
            unsigned before=opens;pn_status_t status=pn_font_manage_samples_idle(&ui,1000+steps,present,NULL);
            unsigned delta=opens-before;if(delta>max_step)max_step=delta;assert(delta<=1 && !pn_media_active(&media));
            assert(status==PN_BUSY || status==PN_OK || status==PN_EMPTY);
            if(status==PN_EMPTY)break;
            if(status==PN_BUSY)assert(presents==1);
        }
        assert(steps<128 && presents<=2);
    }
    unsigned prepared=opens;uint8_t *saved=malloc(sizeof image);assert(saved);memcpy(saved,image,sizeof image);
    assert(pn_font_manage_present(&ui,present,NULL)==PN_OK && !memcmp(saved,image,sizeof image) && opens==prepared);free(saved);
    FILE *file=fopen(argv[4],"wb");assert(file);fprintf(file,"P5\n684 1216\n15\n");
    for(unsigned y=0;y<1216;y++)for(unsigned x=0;x<684;x++){uint8_t v=image[y*342+x/2];assert(fputc(x&1?v&15:v>>4,file)!=EOF);}
    assert(!fclose(file));
    pn_font_manage_close(&ui);assert(!pool.used && !pool.live && !pn_media_active(&media));
    printf("font samples first_opens=%u total_opens=%u max_step_opens=%u steps=%u presents=%u peak=%zu used=%zu live=%zu\n",first_opens,opens,max_step,steps,presents,pool.peak,pool.used,pool.live);
    if(getenv("PN_FONT_SAMPLES_FAILURES") && pn_font_manage_samples_idle)failures(&pool,&media,argv[1]);
    return 0;
}
