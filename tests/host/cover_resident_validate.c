/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际封面生成/PNCV路径与内存重绑逐像素对照，观察真实缓存读取。
 * English: pixel-exact comparison of real cover/PNCV paths and resident rebinding, observing actual cache reads. */
#define _POSIX_C_SOURCE 200809L
#include "pn_cover.h"
#include "pn_shelf_view.h"
#include "pn_cancel.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
static bool watching;static size_t cache_opens,cache_bytes,source_stats;
int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *path,struct stat *info){if(watching)source_stats++;return __real_stat(path,info);}
int __real_lstat(const char *,struct stat *);
int __wrap_lstat(const char *path,struct stat *info){if(watching)source_stats++;return __real_lstat(path,info);}
static FILE *files[64];static bool cached[64];
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *path,const char *mode){FILE *f=__real_fopen(path,mode);if(!f)return f;for(size_t i=0;i<64;i++)if(!files[i]){files[i]=f;cached[i]=strstr(path,".pnc") && mode[0]=='r';if(watching && cached[i])cache_opens++;return f;}assert(false);return f;}
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *out,size_t size,size_t count,FILE *f){size_t n=__real_fread(out,size,count,f);if(watching)for(size_t i=0;i<64;i++)if(files[i]==f && cached[i]){cache_bytes+=n*size;break;}return n;}
int __real_fclose(FILE *);
int __wrap_fclose(FILE *f){for(size_t i=0;i<64;i++)if(files[i]==f){files[i]=NULL;cached[i]=false;break;}return __real_fclose(f);}
static void legacy(pn_shelf_covers_t *c,const pn_catalog_page_t *p,const pn_catalog_item_t *last,pn_pool_t *pool,pn_media_t *media,const char *directory){bool changed;pn_shelf_covers_set_last(c,last);pn_shelf_covers_reset(c,p);assert(pn_shelf_covers_cached(c,p,pool,media,directory,&changed)==PN_OK);}
static void fill(pn_shelf_covers_t *c,const pn_catalog_page_t *p,const pn_catalog_item_t *last,pn_pool_t *pool,pn_media_t *media,const char *directory){
    pn_shelf_covers_set_last(c,last);pn_shelf_covers_reset(c,p);pn_cover_work_t work={0};uint8_t salt[16]={1};bool changed;pn_status_t s;unsigned steps=0;
    do{s=pn_shelf_covers_work(&work,c,p,pool,media,directory,salt,pool->limit,&changed);assert(++steps<65536);}while(s==PN_OK || s==PN_BUSY);
    assert(s==PN_EMPTY && !c->cache_dirty);pn_cover_work_abort(&work);assert(!pn_media_active(media));
}
static void compare(const char *name,pn_shelf_covers_t *actual,pn_shelf_covers_t *expected,const pn_catalog_page_t *page,const pn_catalog_item_t *last,pn_pool_t *pool,pn_media_t *media,const char *directory,pn_font_t *font,pn_frame_t *a,pn_frame_t *b,bool zero){
    cache_opens=cache_bytes=source_stats=0;watching=true;legacy(expected,page,last,pool,media,directory);watching=false;size_t before_opens=cache_opens,before_bytes=cache_bytes,before_stats=source_stats;
    cache_opens=cache_bytes=source_stats=0;bool changed;watching=true;assert(pn_shelf_covers_bind(actual,page,last,pool,media,directory,&changed)==PN_OK);watching=false;assert(source_stats==before_stats && ((!page->count && !last) || source_stats));
    if(zero)assert(cache_opens==0 && cache_bytes==0);
    for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++)if(i<page->count || (i==PN_COVER_SLOT_LAST && last)){
        assert(actual->state[i]==expected->state[i] && actual->reason[i]==expected->reason[i] && !memcmp(&actual->display[i],&expected->display[i],sizeof actual->display[i]));
        if(actual->state[i]==PN_COVER_READY)assert(!memcmp(actual->pixels[i],expected->pixels[i],PN_COVER_BYTES));
    }
    pn_shelf_options_t options={.home=page->count==3,.columns=4,.battery_percent=-1,.favorites_tab=true,.layout_toggle=true};
    assert(pn_shelf_render_ex(page,font,a,-1,false,true,actual,&options)==PN_OK && pn_shelf_render_ex(page,font,b,-1,false,true,expected,&options)==PN_OK);
    assert(!memcmp(a->pixels,b->pixels,415872));
    printf("resident case=%s legacy_opens=%zu legacy_bytes=%zu resident_opens=%zu resident_bytes=%zu exact_frames=1 legacy_stats=%zu resident_stats=%zu\n",name,before_opens,before_bytes,cache_opens,cache_bytes,before_stats,source_stats);
}
static bool cancel(void *ctx){unsigned *calls=ctx;return ++*calls>=3;}
int main(int argc,char **argv){
    assert(argc>=3 && argc<=15);pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    assert(sizeof(pn_shelf_covers_t)<384u*1024u);pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_catalog_page_t *full=pn_alloc(&pool,sizeof *full),*shown=pn_alloc(&pool,sizeof *shown);assert(full && shown);memset(full,0,sizeof *full);
    size_t books=(size_t)argc-2;full->count=books>12?12:books;full->total=full->count;full->per_page=full->count;
    pn_catalog_item_t last={0};
    for(size_t i=0;i<books;i++){pn_catalog_item_t *item=i<full->count?&full->items[i]:&last;assert(strlen(argv[i+2])<sizeof item->path);strcpy(item->path,argv[i+2]);const char *name=strrchr(item->path,'/');name=name?name+1:item->path;size_t n=strlen(name);assert(n<sizeof item->name);memcpy(item->name,name,n+1);const char *ext=strrchr(item->path,'.');assert(ext);item->format=!strcasecmp(ext,".epub")?PN_BOOK_EPUB:PN_BOOK_TXT;}
    if(books<=12)last=full->items[0];
    pn_shelf_covers_t *actual=pn_alloc(&pool,sizeof *actual),*expected=pn_alloc(&pool,sizeof *expected);assert(actual && expected);memset(actual,0,sizeof *actual);memset(expected,0,sizeof *expected);
    fill(actual,full,&last,&pool,&media,argv[1]);
    uint8_t *x=pn_alloc(&pool,415872),*y=pn_alloc(&pool,415872);assert(x && y);pn_frame_t a,b;assert(pn_frame_bind(&a,x,415872,684,1216) && pn_frame_bind(&b,y,415872,684,1216));
    pn_font_t font={0};pn_text_source_t face=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&face,24)==PN_OK);
    compare("same",actual,expected,full,&last,&pool,&media,argv[1],&font,&a,&b,true);
    *shown=*full;for(size_t i=0;i<full->count;i++)shown->items[i]=full->items[(i+3)%full->count];
    compare("rotate",actual,expected,shown,&last,&pool,&media,argv[1],&font,&a,&b,true);
    shown->count=full->count<3?full->count:3;shown->total=shown->per_page=shown->count;
    compare("home",actual,expected,shown,&last,&pool,&media,argv[1],&font,&a,&b,true);
    compare("return",actual,expected,full,&last,&pool,&media,argv[1],&font,&a,&b,true);
    compare("duplicate",actual,expected,full,&full->items[0],&pool,&media,argv[1],&font,&a,&b,true);
    compare("last-return",actual,expected,full,&last,&pool,&media,argv[1],&font,&a,&b,false);
    // 取消和USB栅栏不能破坏原内存槽；换代必须重新校验磁盘缓存。/ Cancellation and USB fencing preserve original slots; a new media generation must revalidate disk caches.
    pn_shelf_covers_t *saved=malloc(sizeof *saved);assert(saved);memcpy(saved,actual,sizeof *saved);bool changed;unsigned calls=0;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,&calls);
    assert(pn_shelf_covers_bind(actual,full,&last,&pool,&media,argv[1],&changed)==PN_CANCELLED);pn_cancel_leave(&scope);assert(!memcmp(saved,actual,sizeof *saved));
    assert(pn_media_request_usb(&media)==PN_OK && pn_shelf_covers_bind(actual,full,&last,&pool,&media,argv[1],&changed)==PN_BUSY && !memcmp(saved,actual,sizeof *saved));assert(pn_media_cancel_usb(&media)==PN_OK);free(saved);
    assert(pn_media_detach(&media)==PN_OK && pn_shelf_covers_bind(actual,full,&last,&pool,&media,argv[1],&changed)==PN_STALE_MEDIA && pn_media_attach(&media,2)==PN_OK);
    compare("media",actual,expected,full,&last,&pool,&media,argv[1],&font,&a,&b,false);assert(cache_opens>0);
    // 只改变用户副本mtime，旧来源键立即失效；不改内容，之后恢复时间。/ Change only the supplied copy's mtime: expire the old key immediately, then restore time without changing content.
    struct stat info;assert(!stat(full->items[0].path,&info));struct timespec times[2]={info.st_atim,info.st_mtim};times[1].tv_sec+=2;assert(!utimensat(AT_FDCWD,full->items[0].path,times,0));
    compare("source-change",actual,expected,full,&last,&pool,&media,argv[1],&font,&a,&b,false);assert(actual->state[0]==PN_COVER_PENDING);
    times[1]=info.st_mtim;assert(!utimensat(AT_FDCWD,full->items[0].path,times,0));compare("source-return",actual,expected,full,&last,&pool,&media,argv[1],&font,&a,&b,false);
    // 固定随机旅程覆盖不同子集、环形换位及继续阅读重复，不以排序特例代替重绑验证。
    // Deterministic random journeys cover subsets, permutation cycles and continue-reading duplicates beyond sorted special cases.
    pn_catalog_item_t *all=calloc(books,sizeof *all);assert(all);for(size_t i=0;i<books;i++)all[i]=i<full->count?full->items[i]:last;
    uint32_t random=271828u;size_t order[PN_CATALOG_PAGE_MAX+1];
    for(unsigned run=0;run<32;run++){
        for(size_t i=0;i<books;i++)order[i]=i;
        for(size_t i=books;i>1;i--){random=random*1664525u+1013904223u;size_t j=random%i,t=order[i-1];order[i-1]=order[j];order[j]=t;}
        shown->count=run%4==0?0:run%4==1?3:run%4==2?5:full->count;if(shown->count>books)shown->count=books;shown->total=shown->per_page=shown->count;
        for(size_t i=0;i<shown->count;i++)shown->items[i]=all[order[i]];
        char name[32];snprintf(name,sizeof name,"permutation-%u",run);const pn_catalog_item_t *recent=run%3==0?NULL:&all[order[run%books]];
        compare(name,actual,expected,shown,recent,&pool,&media,argv[1],&font,&a,&b,false);
    }
    free(all);
    pn_font_close(&font);pn_free(x);pn_free(y);pn_free(actual);pn_free(expected);pn_free(full);pn_free(shown);assert(!pn_media_active(&media) && !pool.used && !pool.live);
    printf("resident done covers_bytes=%zu peak=%zu used=0 live=0 cancellation=1 usb_fence=1 source_change=1\n",sizeof(pn_shelf_covers_t),pool.peak);return 0;
}
