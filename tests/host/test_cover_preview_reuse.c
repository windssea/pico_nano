/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实封面生成、常驻预览与来源替换/介质/USB失效；计数不是TF实测。
 * English: real cover generation, resident previews and source/media/USB invalidation; counts are not TF measurements. */
#define _POSIX_C_SOURCE 200809L
#include "pn_cover.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool cancel(void *ctx){(void)ctx;return true;}
static size_t stats;
int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *p,struct stat *s){stats++;return __real_stat(p,s);}
int __real_lstat(const char *,struct stat *);
int __wrap_lstat(const char *p,struct stat *s){stats++;return __real_lstat(p,s);}
static void fill(pn_shelf_covers_t *c,pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,const char *dir){
    pn_shelf_covers_reset(c,page);pn_cover_work_t work={0};bool changed;uint8_t salt[16]={1};pn_status_t s;unsigned steps=0;
    do{s=pn_shelf_covers_work(&work,c,page,pool,media,dir,salt,pool->limit,&changed);assert(++steps<65536);}while(s==PN_OK || s==PN_BUSY);
    assert(s==PN_EMPTY && c->state[0]==PN_COVER_READY);pn_cover_work_abort(&work);assert(!pn_media_active(media));
}
int main(int argc,char **argv){
    assert(argc==3);pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_catalog_page_t *page=calloc(1,sizeof *page);pn_shelf_covers_t *covers=calloc(1,sizeof *covers);pn_book_preview_t *out=calloc(1,sizeof *out);assert(page && covers && out);
    page->count=1;page->items[0].format=PN_BOOK_TXT;snprintf(page->items[0].path,sizeof page->items[0].path,"%s",argv[2]);fill(covers,page,&pool,&media,argv[1]);
    size_t attempts=pool.attempts;stats=0;
    assert(pn_cover_preview(&pool,&media,NULL,&page->items[0],covers,out)==PN_OK && out->ready && !memcmp(out->pixels,covers->pixels[0],PN_COVER_BYTES) && !memcmp(&out->display,&covers->display[0],sizeof out->display));
    printf("preview_stats=%zu attempts_delta=%zu exact=1\n",stats,pool.attempts-attempts);
#if defined(PN_TEST_IMMUTABLE_FILE)
    if(!getenv("PN_PREVIEW_BASELINE"))assert(stats==0 && pool.attempts==attempts);
#else
    assert(stats && pool.attempts==attempts);
#endif
    uint8_t pixel=out->pixels[0];covers->pixels[0][0]^=1;assert(out->pixels[0]==pixel);covers->pixels[0][0]^=1;
    pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&lease)==PN_OK);stats=0;
    assert(pn_cover_preview(&pool,&media,NULL,&page->items[0],covers,out)==PN_OK);
#if defined(PN_TEST_IMMUTABLE_FILE)
    if(!getenv("PN_PREVIEW_BASELINE"))assert(!stats);
    pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,NULL);assert(pn_cover_preview(&pool,&media,NULL,&page->items[0],covers,out)==PN_CANCELLED && !out->ready);pn_cancel_leave(&scope);
#else
    assert(stats);(void)cancel;
#endif
    assert(pn_media_release(&media,&lease)==PN_OK);
    covers->resident_valid=0;assert(pn_cover_preview(&pool,&media,NULL,&page->items[0],covers,out)==PN_EMPTY && !out->ready);fill(covers,page,&pool,&media,argv[1]);
    pn_catalog_item_t wrong_format=page->items[0];wrong_format.format=PN_BOOK_EPUB;assert(pn_cover_preview(&pool,&media,NULL,&wrong_format,covers,out)==PN_EMPTY && !out->ready);
    assert(pn_media_request_usb(&media)==PN_OK && pn_cover_preview(&pool,&media,NULL,&page->items[0],covers,out)==PN_BUSY && !out->ready);assert(pn_media_cancel_usb(&media)==PN_OK);
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);FILE *file=fopen(argv[2],"ab");assert(file && fputs("changed",file)>=0 && !fclose(file));assert(pn_media_release(&media,&lease)==PN_OK);stats=0;
    assert(pn_cover_preview(&pool,&media,argv[1],&page->items[0],covers,out)!=PN_OK && !out->ready && stats);fill(covers,page,&pool,&media,argv[1]);
    assert(pn_media_detach(&media)==PN_OK && pn_cover_preview(&pool,&media,NULL,&page->items[0],covers,out)==PN_STALE_MEDIA && !out->ready);assert(pn_media_attach(&media,1)==PN_OK);stats=0;
    assert(pn_cover_preview(&pool,&media,NULL,&page->items[0],covers,out)==PN_EMPTY && stats && !out->ready);
    assert(pn_cover_preview(&pool,&media,argv[1],&page->items[0],NULL,out)==PN_OK && out->ready);
#if !defined(PN_TEST_IMMUTABLE_FILE)
    fill(covers,page,&pool,&media,argv[1]);file=fopen(argv[2],"ab");assert(file && fputs("external",file)>=0 && !fclose(file));
    assert(pn_cover_preview(&pool,&media,argv[1],&page->items[0],covers,out)!=PN_OK && !out->ready);
#endif
    pn_catalog_item_t other=page->items[0];strcat(other.path,".missing");assert(pn_cover_preview(&pool,&media,NULL,&other,covers,out)!=PN_OK && !out->ready);
    free(page);free(covers);free(out);assert(!pn_media_active(&media) && !pool.used && !pool.live);printf("source_write=1 cache_write=1 usb=1 media=1 missing=1 owned_snapshot=1 peak=%zu used=0 live=0\n",pool.peak);return 0;
}
