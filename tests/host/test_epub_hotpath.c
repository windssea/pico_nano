/* SPDX-License-Identifier: Apache-2.0
 * 中文：大目录翻页I/O预算与不重读原书的热恢复。/ English: large-spine page I/O budget and warm restore without rereading the book. */
#include "pn_epub_app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static size_t reads;
static size_t spine_queries;
static uint64_t frame_hash;
static bool fail;
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *p,size_t size,size_t n,FILE *f){reads++;return __real_fread(p,size,n,f);}
pn_status_t __real_pn_epub_spine(pn_epub_t *,size_t,pn_epub_item_t *);
pn_status_t __wrap_pn_epub_spine(pn_epub_t *e,size_t i,pn_epub_item_t *out){spine_queries++;return __real_pn_epub_spine(e,i,out);}
static pn_status_t present(void *ctx,const pn_frame_t *f,pn_refresh_t profile){
    (void)ctx;(void)profile;frame_hash=14695981039346656037ull;
    for(int y=0;y<f->height;y++)for(int x=0;x<f->width/2;x++)frame_hash=(frame_hash^f->pixels[(size_t)y*f->stride+x])*1099511628211ull;
    return fail?PN_IO:PN_OK;
}
int main(int argc,char **argv){
    assert(argc==3);pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_epub_app_t app={0};assert(pn_epub_app_open_on_media(&app,&pool,&media,argv[1],NULL,argv[2],44,0)==PN_OK);
    assert(pn_epub_app_step(&app,PN_APP_OPEN,1,present,NULL)==PN_OK);
    reads=spine_queries=0;assert(pn_epub_app_step(&app,PN_APP_NEXT,2,present,NULL)==PN_OK);
    assert(reads<80 && spine_queries<20); // 不能随400章节反复扫目录。/ Must not rescan a 400-section directory on every page.
    pn_epub_progress_t before,after;assert(pn_epub_app_progress(&app,&before)==PN_OK);
    assert(pn_epub_app_suspend(&app,3)==PN_OK);uint64_t saved=frame_hash;
    assert(pn_epub_app_step(&app,PN_APP_NEXT,3,present,NULL)==PN_BUSY);
    reads=0;assert(pn_epub_app_resume(&app,4,present,NULL)==PN_OK && reads==0 && frame_hash==saved);
    assert(pn_epub_app_progress(&app,&after)==PN_OK && !memcmp(&before.location,&after.location,sizeof before.location));
    assert(pn_epub_app_suspend(&app,5)==PN_OK);fail=true;assert(pn_epub_app_resume(&app,6,present,NULL)==PN_IO);fail=false;
    assert(pn_epub_app_progress(&app,&after)==PN_OK && !memcmp(&before.location,&after.location,sizeof before.location));
    assert(pn_media_detach(&media)==PN_OK && pn_epub_app_resume(&app,7,present,NULL)==PN_STALE_MEDIA);
    assert(pn_epub_app_media_lost(&app)==PN_OK && pn_epub_app_close(&app,8)==PN_OK);
    assert(!pool.used && !pool.live);return 0;
}
