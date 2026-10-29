/* SPDX-License-Identifier: Apache-2.0
 * 中文：同一EPUB的图片原绘制/完整预绘像素、关闭图片的零探测/解码及持久重开。
 * English: identical EPUB image pixels with/without full prerender, zero probes/decodes when hidden, and persistent reopen. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_app.h"
#include "pn_epub_image.h"
#include "pn_cancel.h"
#include "test_remove_indexes.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static unsigned probes,draws;
static bool inject_cancel;
static unsigned checks;
static bool cancel_image(void *ctx){(void)ctx;return ++checks>=6;}
pn_status_t __real_pn_epub_image_probe(pn_pool_t *,pn_epub_t *,const char *,pn_image_info_t *);
pn_status_t __wrap_pn_epub_image_probe(pn_pool_t *p,pn_epub_t *e,const char *path,pn_image_info_t *i){probes++;return __real_pn_epub_image_probe(p,e,path,i);}
pn_status_t __real_pn_epub_image_draw(pn_pool_t *,pn_epub_t *,const char *,pn_frame_t *,pn_image_rect_t,pn_image_info_t *);
pn_status_t __wrap_pn_epub_image_draw(pn_pool_t *p,pn_epub_t *e,const char *path,pn_frame_t *f,pn_image_rect_t r,pn_image_info_t *i){
    draws++;if(!inject_cancel)return __real_pn_epub_image_draw(p,e,path,f,r,i);
    inject_cancel=false;checks=0;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel_image,NULL);pn_status_t s=__real_pn_epub_image_draw(p,e,path,f,r,i);pn_cancel_leave(&scope);assert(s==PN_CANCELLED);return s;
}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t mode){(void)mode;uint8_t *out=ctx;assert(frame->width==684 && frame->height==1216 && frame->stride==342);memcpy(out,frame->pixels,415872);return PN_OK;}
static void warm(pn_epub_app_t *app){for(unsigned i=0;i<8192;i++){pn_status_t s=pn_epub_app_prefetch(app);assert(s==PN_OK || s==PN_EMPTY || s==PN_CANCELLED);if(pn_epub_app_prerender_stats(app).completed)return;}assert(!"image prerender never completed");}
int main(int argc,char **argv){
    assert(argc==3);uint8_t *oracle=malloc(415872),*frame=malloc(415872);assert(oracle && frame);
    for(unsigned mode=0;mode<4;mode++){
        pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));pn_epub_app_t app={0};
        assert(pn_epub_app_open(&app,&pool,argv[1],argv[2],NULL,44,0)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,1,present,frame)==PN_OK);
        if(mode){assert(!pn_epub_app_prerender(&app,true));
            if(mode==1){unsigned before=draws;for(unsigned i=0;i<512;i++){pn_status_t s=pn_epub_app_overlap(&app);assert(s==PN_OK || s==PN_EMPTY);}assert(draws==before);}
            inject_cancel=mode==2;warm(&app);assert(!inject_cancel);}
        if(mode==3){
            // 预绘后原文件变化不能发布旧像素；保持文件长度并在测试后恢复原字节。/ Never publish cached pixels after source mutation; preserve length and restore the test byte afterward.
            uint8_t *prior=malloc(415872);assert(prior);memcpy(prior,frame,415872);FILE *file=fopen(argv[1],"r+b");assert(file && !fseek(file,-1,SEEK_END));long at=ftell(file);int byte=fgetc(file);assert(byte!=EOF && !fseek(file,at,SEEK_SET) && fputc(byte^1,file)!=EOF && !fclose(file));
            pn_status_t s=pn_epub_app_step(&app,PN_APP_NEXT,2,present,frame);assert((s==PN_STALE_JOB || s==PN_CORRUPT) && !memcmp(prior,frame,415872));free(prior);
            file=fopen(argv[1],"r+b");assert(file && !fseek(file,at,SEEK_SET) && fputc(byte,file)!=EOF && !fclose(file));assert(!pn_epub_app_close(&app,3) && !pool.used && !pool.live);continue;
        }
        unsigned before=draws;assert(pn_epub_app_step(&app,PN_APP_NEXT,2,present,frame)==PN_OK);
        if(!mode){assert(draws>before);memcpy(oracle,frame,415872);}else{assert(draws==before && pn_epub_app_prerender_stats(&app).hits==1 && !memcmp(oracle,frame,415872));}
        assert(pn_epub_app_close(&app,3)==PN_OK && !pool.used && !pool.live);
    }
    char state[]="/tmp/pn-image-options-XXXXXX";assert(mkdtemp(state));pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));pn_epub_app_t app={0};
    assert(!pn_epub_app_open(&app,&pool,argv[1],argv[2],state,44,0) && !pn_epub_app_step(&app,PN_APP_OPEN,1,present,frame));
    pn_style_t style;assert(!pn_epub_app_style_get(&app,&style) && !style.hide_images);style.hide_images=1;
    probes=draws=0;assert(!pn_epub_app_style_apply(&app,&style,2,present,frame) && !pn_epub_app_step(&app,PN_APP_NEXT,3,present,frame));assert(!probes && !draws && memcmp(oracle,frame,415872));
    assert(!pn_epub_app_close(&app,4) && !pool.used && !pool.live);
    probes=draws=0;assert(!pn_epub_app_open(&app,&pool,argv[1],argv[2],state,44,5) && !pn_epub_app_step(&app,PN_APP_OPEN,6,present,frame));assert(!pn_epub_app_style_get(&app,&style) && style.hide_images && !probes && !draws);
    style.hide_images=0;assert(!pn_epub_app_style_apply(&app,&style,7,present,frame) && draws && probes);assert(!pn_epub_app_close(&app,8) && !pool.used && !pool.live);
    pn_test_remove_indexes(state);DIR *dir=opendir(state);assert(dir);struct dirent *entry;while((entry=readdir(dir)))if(entry->d_name[0]!='.'){char path[1024];int n=snprintf(path,sizeof path,"%s/%s",state,entry->d_name);assert(n>0 && (size_t)n<sizeof path && !unlink(path));}assert(!closedir(dir) && !rmdir(state));
    free(oracle);free(frame);puts("image options: full-frame pixels match; idle image cancellation resumes; NEXT avoids decoding; hidden images skip probes/decodes, persist and restore original rendering");return 0;
}
