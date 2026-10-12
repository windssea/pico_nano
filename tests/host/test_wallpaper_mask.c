/* PNG遮罩：独立alpha oracle、读租约释放、失败保留底图及正文事务。/ PNG mask: independent alpha oracle, lease release, retained underlay and body transactions. */
#include "pn_wallpaper.h"
#include "pn_reader_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail;
static pn_status_t present(void *ctx,const pn_frame_t *f,pn_refresh_t refresh){
    (void)ctx;(void)refresh;
    assert(pn_frame_get(f,10,10)==15 && pn_frame_get(f,180,10)==10 && pn_frame_get(f,350,10)==5 && pn_frame_get(f,530,10)==0);
    return fail?PN_IO:PN_OK;
}
int main(int argc,char **argv){
    assert(argc==4);pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t sd,internal;pn_media_init(&sd);pn_media_init(&internal);assert(pn_media_attach(&sd,1)==PN_OK && pn_media_attach(&internal,2)==PN_OK);
    pn_lock_selection_t selected={.mode=PN_LOCK_MASK,.hint=true,.transform={PN_WALLPAPER_COVER,0,0}};snprintf(selected.source,sizeof selected.source,"%s",argv[1]);
    char a[1100],b[1100];snprintf(a,sizeof a,"%s/a",argv[2]);snprintf(b,sizeof b,"%s/b",argv[2]);
    pn_wallpaper_store_t store;assert(pn_wallpaper_store_init(&store,&internal,a,b)==PN_OK);pn_media_lease_t lease;
    assert(pn_media_acquire(&internal,PN_MEDIA_WRITE,&lease)==PN_OK && pn_wallpaper_save(&store,&lease,&selected,NULL)==PN_OK);
    pn_lock_selection_t restored;assert(pn_wallpaper_load(&store,&lease,&restored,NULL)==PN_OK && restored.mode==PN_LOCK_MASK && !strcmp(restored.source,selected.source));assert(pn_media_release(&internal,&lease)==PN_OK);
    pn_wallpaper_mask_t mask={0};assert(pn_wallpaper_mask_open(&mask,&pool,&sd,&restored)==PN_OK);
    assert(pn_media_acquire(&sd,PN_MEDIA_WRITE,&lease)==PN_OK && pn_media_release(&sd,&lease)==PN_OK);
    uint8_t *px=malloc(PN_WALLPAPER_BYTES),*saved=malloc(PN_WALLPAPER_BYTES);pn_frame_t frame;assert(px && saved && pn_frame_bind(&frame,px,PN_WALLPAPER_BYTES,684,1216));
    assert(!pn_wallpaper_mask_snapshot(&mask,&frame));pn_frame_clear(&frame,12);assert(pn_wallpaper_mask_paint(&mask,&frame)==PN_OK);
    assert(pn_frame_get(&frame,10,10)==12 && pn_frame_get(&frame,180,10)==8 && pn_frame_get(&frame,350,10)==4 && pn_frame_get(&frame,530,10)==0);
    pn_wallpaper_mask_complete(&mask,&frame,false);assert(!pn_wallpaper_mask_snapshot(&mask,&frame));
    pn_reader_app_t reader={0};assert(pn_reader_app_open(&reader,&pool,argv[3],NULL,NULL,32,0)==PN_OK);
    pn_reader_body_layer_t layer={&mask,pn_wallpaper_mask_paint,pn_wallpaper_mask_complete};assert(pn_reader_app_body_layer(&reader,&layer)==PN_OK);
    fail=true;assert(pn_reader_app_step(&reader,PN_APP_OPEN,1,present,NULL)==PN_IO && !pn_wallpaper_mask_snapshot(&mask,&frame));
    fail=false;assert(pn_reader_app_step(&reader,PN_APP_OPEN,2,present,NULL)==PN_OK && pn_wallpaper_mask_snapshot(&mask,&frame));memcpy(saved,px,PN_WALLPAPER_BYTES);
    pn_frame_clear(&frame,3);pn_wallpaper_mask_complete(&mask,&frame,false);assert(pn_wallpaper_mask_snapshot(&mask,&frame) && !memcmp(saved,px,PN_WALLPAPER_BYTES));
    assert(pn_reader_app_close(&reader,3)==PN_OK);pn_wallpaper_mask_close(&mask);pn_wallpaper_mask_close(&mask);
    strcpy(restored.source,"/missing-mask.png");assert(pn_wallpaper_mask_open(&mask,&pool,&sd,&restored)!=PN_OK && !mask.impl);
    free(px);free(saved);assert(!pool.used && !pool.live);puts("mask: alpha preserved, body presentation confirmed, failed pages retain underlay, missing source falls back");return 0;
}
