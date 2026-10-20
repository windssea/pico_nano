/* SPDX-License-Identifier: Apache-2.0
 * 中文：缓存命中保持原生像素，CRC损坏/同属性换图/参数变化不能绕过原解码。
 * English: cache hits preserve native pixels; damage, same-stat replacement and transform changes cannot bypass decoding. */
#define _POSIX_C_SOURCE 200809L
#include "pn_wallpaper_ui.h"
#include "pn_cancel.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <utime.h>
static uint8_t *shown;
static bool cancel_now(void *ctx){(void)ctx;return true;}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)ctx;(void)profile;memcpy(shown,frame->pixels,PN_WALLPAPER_BYTES);return PN_OK;}
static void damage(const char *dir,const char *suffix){
    DIR *d=opendir(dir);assert(d);struct dirent *e;bool done=false;
    while((e=readdir(d))){size_t n=strlen(e->d_name),s=strlen(suffix);if(n<s || strcmp(e->d_name+n-s,suffix))continue;
        char path[2048];assert(snprintf(path,sizeof path,"%s/%s",dir,e->d_name)>0);FILE *f=fopen(path,"r+b");assert(f && !fseek(f,-1,SEEK_END));int c=fgetc(f);assert(c>=0 && !fseek(f,-1,SEEK_END) && fputc(c^1,f)>=0 && !fclose(f));done=true;break;
    }
    assert(!closedir(d) && done);
}
// 模拟缓存替换中断，仅移动专用临时目录里的缓存。/ Simulate interrupted replacement, moving only cache files inside the dedicated temporary directory.
static void backup(const char *dir,bool restore){
    DIR *d=opendir(dir);assert(d);struct dirent *e;bool done=false;const char *suffix=restore?"-2.pni.bak":"-2.pni";
    while((e=readdir(d))){size_t n=strlen(e->d_name),s=strlen(suffix);if(n<s || strcmp(e->d_name+n-s,suffix))continue;
        char from[2048],to[2052];assert(snprintf(from,sizeof from,"%s/%s",dir,e->d_name)>0);snprintf(to,sizeof to,"%s%s",from,restore?"":".bak");if(restore)to[strlen(to)-4]=0;assert(!rename(from,to));done=true;break;
    }
    assert(!closedir(d) && done);
}
int main(int argc,char **argv){
    assert(argc==4);const char *source=argv[1],*replacement=argv[2],*dir=argv[3];pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    uint8_t *g=pn_alloc(&pool,PN_WALLPAPER_BYTES),*a=pn_alloc(&pool,PN_WALLPAPER_BYTES),*rg=pn_alloc(&pool,PN_WALLPAPER_BYTES),*ra=pn_alloc(&pool,PN_WALLPAPER_BYTES);assert(g && a && rg && ra);
    pn_frame_t gray,alpha;assert(pn_frame_bind(&gray,g,PN_WALLPAPER_BYTES,684,1216) && pn_frame_bind(&alpha,a,PN_WALLPAPER_BYTES,684,1216));
    pn_wallpaper_transform_t t={0};size_t budget=4u*1024u*1024u;
    assert(pn_wallpaper_prepare(&pool,&media,&lease,source,t,budget,&gray)==PN_OK);memcpy(rg,g,PN_WALLPAPER_BYTES);
    assert(pn_wallpaper_prepare_cached(&pool,&media,&lease,source,t,budget,&gray,dir)==PN_OK && !memcmp(g,rg,PN_WALLPAPER_BYTES));
    backup(dir,false);assert(pn_wallpaper_prepare_cached(&pool,&media,&lease,source,t,1,&gray,dir)==PN_OK && !memcmp(g,rg,PN_WALLPAPER_BYTES));backup(dir,true);
    // 极小解码预算仍能命中，证明不重复启动PNG解码器。/ A tiny decoder budget still hits, proving that no PNG decoder restarts.
    memset(g,0,PN_WALLPAPER_BYTES);assert(pn_wallpaper_prepare_cached(&pool,&media,&lease,source,t,1,&gray,dir)==PN_OK && !memcmp(g,rg,PN_WALLPAPER_BYTES));
    damage(dir,"-2.pni");assert(pn_wallpaper_prepare_cached(&pool,&media,&lease,source,t,1,&gray,dir)!=PN_OK);
    assert(pn_wallpaper_prepare_cached(&pool,&media,&lease,source,t,budget,&gray,dir)==PN_OK && !memcmp(g,rg,PN_WALLPAPER_BYTES));
    pn_wallpaper_transform_t rotated={PN_WALLPAPER_COVER,1,1};assert(pn_wallpaper_prepare_cached(&pool,&media,&lease,source,rotated,1,&gray,dir)!=PN_OK);
    assert(pn_wallpaper_mask_prepare(&pool,&media,&lease,source,t,budget,&gray,&alpha)==PN_OK);memcpy(rg,g,PN_WALLPAPER_BYTES);memcpy(ra,a,PN_WALLPAPER_BYTES);
    assert(pn_wallpaper_mask_prepare_cached(&pool,&media,&lease,source,t,budget,&gray,&alpha,dir)==PN_OK);
    memset(g,0,PN_WALLPAPER_BYTES);memset(a,0,PN_WALLPAPER_BYTES);
    assert(pn_wallpaper_mask_prepare_cached(&pool,&media,&lease,source,t,1,&gray,&alpha,dir)==PN_OK && !memcmp(g,rg,PN_WALLPAPER_BYTES) && !memcmp(a,ra,PN_WALLPAPER_BYTES));
    struct stat old,other;assert(!stat(source,&old) && !stat(replacement,&other) && old.st_size==other.st_size);
    FILE *in=fopen(replacement,"rb"),*out=fopen(source,"wb");assert(in && out);uint8_t block[1024];size_t n;
    while((n=fread(block,1,sizeof block,in)))assert(fwrite(block,1,n,out)==n);
    assert(!ferror(in) && !fclose(in) && !fclose(out));struct utimbuf times={old.st_atime,old.st_mtime};assert(!utime(source,&times));
    // size/mtime相同也必须重新计算源SHA，旧全屏缓存不得冒充新源。/ Equal size/mtime still require a fresh source SHA; old native pixels cannot impersonate the new source.
    assert(pn_wallpaper_prepare_cached(&pool,&media,&lease,source,t,1,&gray,dir)!=PN_OK);
    assert(pn_wallpaper_prepare_cached(&pool,&media,&lease,source,t,budget,&gray,dir)==PN_OK);
    memcpy(rg,g,PN_WALLPAPER_BYTES);assert(pn_wallpaper_prepare(&pool,&media,&lease,source,t,budget,&gray)==PN_OK && !memcmp(g,rg,PN_WALLPAPER_BYTES));
    uint8_t *small=pn_alloc(&pool,144u*256u/2),*reference=pn_alloc(&pool,144u*256u/2);assert(small && reference);pn_frame_t thumb;assert(pn_frame_bind(&thumb,small,144u*256u/2,144,256));
    for(int y=0;y<256;y++)for(int x=0;x<144;x++)pn_frame_pixel(&thumb,x,y,pn_frame_get(&gray,x*684/144,y*1216/256));
    memcpy(reference,small,144u*256u/2);
    assert(pn_wallpaper_thumbnail_save(&media,dir,source,&thumb)==PN_OK);memset(small,0,144u*256u/2);assert(pn_wallpaper_thumbnail_load(&media,&lease,dir,source,&thumb)==PN_OK && !memcmp(small,reference,144u*256u/2));
    damage(dir,"-1.pni");assert(pn_wallpaper_thumbnail_load(&media,&lease,dir,source,&thumb)==PN_CORRUPT);
    assert(pn_wallpaper_thumbnail_save(&media,dir,source,&thumb)==PN_OK);times.modtime+=2;assert(!utime(source,&times));assert(pn_wallpaper_thumbnail_load(&media,&lease,dir,source,&thumb)!=PN_OK);
    assert(media.writes==0 && pn_media_release(&media,&lease)==PN_OK && pn_media_active(&media)==0);
    pn_free(small);pn_free(reference);pn_free(g);pn_free(a);pn_free(rg);pn_free(ra);assert(!pool.used && !pool.live);
    char images[1024];assert(strlen(source)<sizeof images);strcpy(images,source);char *slash=strrchr(images,'/');assert(slash);*slash=0;
    shown=malloc(PN_WALLPAPER_BYTES);uint8_t *first=malloc(PN_WALLPAPER_BYTES);assert(shown && first);pn_wallpaper_ui_t ui={0};
    assert(pn_wallpaper_ui_open_cached(&ui,&pool,&media,images,NULL,present,NULL,dir)==PN_OK);unsigned steps=0;
    assert(pn_wallpaper_ui_tick(&ui,present,NULL)==PN_BUSY);
    pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel_now,NULL);assert(pn_wallpaper_ui_tick(&ui,present,NULL)==PN_BUSY && pn_wallpaper_ui_pending(&ui));pn_cancel_leave(&scope);
    while(pn_wallpaper_ui_pending(&ui)){pn_status_t s=pn_wallpaper_ui_tick(&ui,present,NULL);assert(s==PN_BUSY || s==PN_OK);assert(++steps<20000);}
    memcpy(first,shown,PN_WALLPAPER_BYTES);pn_wallpaper_ui_close(&ui);
    assert(pn_wallpaper_ui_open_cached(&ui,&pool,&media,images,NULL,present,NULL,dir)==PN_OK && !pn_wallpaper_ui_pending(&ui) && !memcmp(first,shown,PN_WALLPAPER_BYTES));
    pn_wallpaper_ui_close(&ui);assert(!pool.used && !pool.live && !pn_media_active(&media));free(shown);free(first);puts("Native/alpha/thumbnail cache, corruption, same-stat replacement and first-frame warm list passed");return 0;
}
