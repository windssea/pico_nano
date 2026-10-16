/* PNG遮罩：独立alpha oracle、读租约释放、失败保留底图及正文事务。/ PNG mask: independent alpha oracle, lease release, retained underlay and body transactions. */
#define _POSIX_C_SOURCE 200809L
#include "pn_wallpaper.h"
#include "pn_reader_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
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
    // 平面缓存：首次写入，命中与原解码逐字节一致；损坏或变换改变回到解码。/ Plane cache: first open stores, hits match the decode byte for byte; damage or a new transform decodes again.
    {char dir[1100],file[1200];snprintf(dir,sizeof dir,"%s/masks",argv[2]);uint8_t *decoded=malloc(PN_WALLPAPER_BYTES);assert(decoded);
     double t0=ms();pn_frame_clear(&frame,12);assert(pn_wallpaper_mask_open_cached(&mask,&pool,&sd,&restored,dir)==PN_OK && pn_wallpaper_mask_paint(&mask,&frame)==PN_OK);memcpy(decoded,px,PN_WALLPAPER_BYTES);pn_wallpaper_mask_close(&mask);double t1=ms();
     snprintf(file,sizeof file,"%s/" PN_WALLPAPER_MASK_CACHE,dir);
     // PackBits后明显小于原始两平面，内部1MiB分区放得下。/ After PackBits it is far below the raw two planes and fits a 1 MiB internal partition.
     FILE *f=fopen(file,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long cached_size=ftell(f);assert(cached_size>0 && cached_size<(long)PN_WALLPAPER_BYTES);fclose(f);
     double t2=ms();pn_frame_clear(&frame,12);assert(pn_wallpaper_mask_open_cached(&mask,&pool,&sd,&restored,dir)==PN_OK && pn_wallpaper_mask_paint(&mask,&frame)==PN_OK && !memcmp(decoded,px,PN_WALLPAPER_BYTES));pn_wallpaper_mask_close(&mask);
     printf("Mask lock: decode+store %.1f ms, cache hit %.1f ms (host, not panel timing)\n",t1-t0,ms()-t2);fflush(stdout);
     f=fopen(file,"r+b");assert(f && !fseek(f,cached_size/2,SEEK_SET));int c=fgetc(f);assert(c!=EOF && !fseek(f,cached_size/2,SEEK_SET) && fputc(c^0x5a,f)!=EOF);fclose(f);
     pn_frame_clear(&frame,12);assert(pn_wallpaper_mask_open_cached(&mask,&pool,&sd,&restored,dir)==PN_OK && pn_wallpaper_mask_paint(&mask,&frame)==PN_OK && !memcmp(decoded,px,PN_WALLPAPER_BYTES));pn_wallpaper_mask_close(&mask);
     pn_lock_selection_t moved=restored;moved.transform.fit=PN_WALLPAPER_CONTAIN;pn_frame_clear(&frame,12);assert(pn_wallpaper_mask_open_cached(&mask,&pool,&sd,&moved,dir)==PN_OK);pn_wallpaper_mask_close(&mask);
     pn_frame_clear(&frame,12);assert(pn_wallpaper_mask_open_cached(&mask,&pool,&sd,&restored,dir)==PN_OK && pn_wallpaper_mask_paint(&mask,&frame)==PN_OK && !memcmp(decoded,px,PN_WALLPAPER_BYTES));pn_wallpaper_mask_close(&mask);
     strcpy(moved.source,"/missing-mask.png");assert(pn_wallpaper_mask_open_cached(&mask,&pool,&sd,&moved,dir)!=PN_OK && !mask.impl);
     // 阅读中的书持TF读租约：TF上缓存写不进，内部介质缓存首次写入、之后命中且逐字节一致。
     // An open book holds a TF read lease: a TF cache cannot be written, while an internal-media cache stores once and then hits byte for byte.
     char internal_dir[1100],internal_file[1200];snprintf(internal_dir,sizeof internal_dir,"%s/internal",argv[2]);snprintf(internal_file,sizeof internal_file,"%s/" PN_WALLPAPER_MASK_CACHE,internal_dir);
     pn_media_lease_t book;assert(pn_media_acquire(&sd,PN_MEDIA_READ,&book)==PN_OK);(void)remove(file);
     pn_frame_clear(&frame,12);assert(pn_wallpaper_mask_open_cached(&mask,&pool,&sd,&restored,dir)==PN_OK);pn_wallpaper_mask_close(&mask);f=fopen(file,"rb");assert(!f);
     pn_frame_clear(&frame,12);assert(pn_wallpaper_mask_open_stored(&mask,&pool,&sd,&restored,&internal,internal_dir)==PN_OK && pn_wallpaper_mask_paint(&mask,&frame)==PN_OK && !memcmp(decoded,px,PN_WALLPAPER_BYTES));pn_wallpaper_mask_close(&mask);
     f=fopen(internal_file,"rb");assert(f);fclose(f);
     double t3=ms();pn_frame_clear(&frame,12);assert(pn_wallpaper_mask_open_stored(&mask,&pool,&sd,&restored,&internal,internal_dir)==PN_OK && pn_wallpaper_mask_paint(&mask,&frame)==PN_OK && !memcmp(decoded,px,PN_WALLPAPER_BYTES));pn_wallpaper_mask_close(&mask);
     printf("Mask lock with book lease: internal cache hit %.1f ms, %ld bytes\n",ms()-t3,cached_size);fflush(stdout);
     assert(pn_media_release(&sd,&book)==PN_OK);
     // 保存带位图的记录前删除同目录遮罩缓存，为位图腾出内部分区空间。/ Saving a bitmap record first removes the mask cache in the same directory to free internal space.
     pn_wallpaper_store_t internal_store;char ia[1200],ib[1200];snprintf(ia,sizeof ia,"%s/lock.a",internal_dir);snprintf(ib,sizeof ib,"%s/lock.b",internal_dir);
     assert(pn_wallpaper_store_init(&internal_store,&internal,ia,ib)==PN_OK);pn_lock_selection_t custom={.mode=PN_LOCK_CUSTOM,.hint=true,.transform={PN_WALLPAPER_COVER,0,0}};snprintf(custom.source,sizeof custom.source,"%s",argv[1]);
     pn_frame_clear(&frame,7);assert(pn_media_acquire(&internal,PN_MEDIA_WRITE,&lease)==PN_OK && pn_wallpaper_save(&internal_store,&lease,&custom,&frame)==PN_OK && pn_media_release(&internal,&lease)==PN_OK);
     f=fopen(internal_file,"rb");assert(!f);free(decoded);}
    strcpy(restored.source,"/missing-mask.png");assert(pn_wallpaper_mask_open(&mask,&pool,&sd,&restored)!=PN_OK && !mask.impl);
    free(px);free(saved);assert(!pool.used && !pool.live);puts("mask: alpha preserved, body presentation confirmed, failed pages retain underlay, missing source falls back");return 0;
}
