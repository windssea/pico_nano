/* SPDX-License-Identifier: Apache-2.0
 * 中文：复用图片解码器与CRC缩略缓存；只在owner空闲时推进。 / English: share image decoding and CRC thumbnail caches, advancing only during owner idle. */
#define _POSIX_C_SOURCE 200809L
#include "pn_background_gallery.h"
#include "pn_catalog.h"
#include "pn_image.h"
#include "pn_text_file.h"
#include "pn_widgets.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#define N 6u
#define BYTES (144u*256u/2u)
typedef struct {
    pn_pool_t *pool;pn_media_t *media;pn_catalog_page_t page;char directory[PN_CATALOG_PATH_MAX],cache[PN_CATALOG_PATH_MAX];bool crop,invalidate;
    uint8_t pixels[N][BYTES];bool valid[N];size_t next;
    pn_image_job_t job;pn_text_file_t file;pn_text_source_t source;pn_pool_t scratch;
    pn_media_lease_t lease;bool held;uint64_t offset;struct stat before;pn_frame_t frame;
} gallery_t;
static void stop(gallery_t *g){pn_image_job_close(&g->job);(void)pn_text_file_close(&g->file);if(g->held)(void)pn_media_release(g->media,&g->lease);g->held=false;}
static void *allocate(void *ctx,size_t n){return pn_alloc_tagged(ctx,PN_MEM_UI,n);}
static void release(void *ctx,void *p){(void)ctx;pn_free(p);}
static pn_status_t read_image(void *ctx,uint8_t *out,size_t cap,size_t *n){gallery_t *g=ctx;*n=0;if(g->offset>=g->source.size)return PN_EMPTY;pn_status_t s=g->source.read_at(g->source.ctx,g->offset,out,cap,n);if(s==PN_OK)g->offset+=*n;return s;}
void pn_background_gallery_invalidate(pn_background_gallery_t *ui){gallery_t *g=ui?ui->impl:NULL;if(!g)return;stop(g);g->invalidate=true;g->next=0;memset(g->valid,0,sizeof g->valid);for(size_t i=0;i<g->page.count && i<N;i++)(void)pn_wallpaper_thumbnail_invalidate(g->media,g->cache,g->page.items[i].path);}
pn_status_t pn_background_gallery_page(pn_background_gallery_t *ui,bool previous){
    gallery_t *g=ui?ui->impl:NULL;if(!g)return PN_INVALID;
    pn_catalog_page_t *next=pn_alloc_tagged(g->pool,PN_MEM_UI,sizeof *next);if(!next)return PN_NO_MEMORY;
    const char *cursor=!g->page.count?"":previous?g->page.items[0].name:g->page.items[g->page.count-1].name;
    pn_media_lease_t lease;pn_status_t s=pn_media_acquire(g->media,PN_MEDIA_READ,&lease);
    if(s==PN_OK){s=previous?pn_catalog_image_page_before(g->media,&lease,g->directory,cursor,next):pn_catalog_image_page(g->media,&lease,g->directory,cursor,next);(void)pn_media_release(g->media,&lease);}
    if(s==PN_OK){stop(g);g->page=*next;g->next=0;memset(g->valid,0,sizeof g->valid);
        if(g->invalidate)pn_background_gallery_invalidate(ui);
        else if(pn_media_acquire(g->media,PN_MEDIA_READ,&lease)==PN_OK){for(size_t i=0;i<g->page.count && i<N;i++){pn_frame_t f;(void)pn_frame_bind(&f,g->pixels[i],BYTES,144,256);g->valid[i]=pn_wallpaper_thumbnail_load(g->media,&lease,g->cache,g->page.items[i].path,&f)==PN_OK;}(void)pn_media_release(g->media,&lease);}}
    pn_free(next);return s;
}
pn_status_t pn_background_gallery_open_at(pn_background_gallery_t *ui,pn_pool_t *pool,pn_media_t *media,const char *directory,const char *cache,bool crop){
    if(!ui || ui->impl || !pool || !media || !directory || !cache || strlen(directory)>=PN_CATALOG_PATH_MAX || strlen(cache)>=PN_CATALOG_PATH_MAX)return PN_INVALID;
    gallery_t *g=pn_alloc_tagged(pool,PN_MEM_UI,sizeof *g);if(!g)return PN_NO_MEMORY;memset(g,0,sizeof *g);g->pool=pool;g->media=media;strcpy(g->directory,directory);strcpy(g->cache,cache);g->crop=crop;ui->impl=g;
    struct stat st;if(stat(g->directory,&st) && errno==ENOENT)return PN_OK;
    pn_status_t s=pn_background_gallery_page(ui,false);if(s!=PN_OK)pn_background_gallery_close(ui);return s;
}
pn_status_t pn_background_gallery_tick(pn_background_gallery_t *ui){
    gallery_t *g=ui?ui->impl:NULL;if(!g)return PN_EMPTY;
    while(g->next<g->page.count && g->next<N && g->valid[g->next])g->next++;
    if(g->next>=g->page.count || g->next>=N)return PN_EMPTY;
    pn_status_t s=PN_OK;
    if(!g->job.impl){
        size_t free=g->pool->limit>g->pool->used?g->pool->limit-g->pool->used:0,budget=free>262144?free-262144:1;if(budget>2097152)budget=2097152;
        (void)pn_pool_init(&g->scratch,budget,allocate,release,g->pool);
        s=pn_media_acquire(g->media,PN_MEDIA_READ,&g->lease);g->held=s==PN_OK;
        const char *path=g->page.items[g->next].path;
        if(s==PN_OK && (stat(path,&g->before) || g->before.st_size>8*1024*1024))s=PN_LIMIT;
        if(s==PN_OK)s=pn_text_file_open(&g->file,g->media,&g->lease,path,&g->source);
        pn_image_input_t input={g,read_image};pn_image_info_t info;g->offset=0;
        if(s==PN_OK)s=pn_image_probe(&g->scratch,&input,&info);
        if(s==PN_OK){
            (void)pn_frame_bind(&g->frame,g->pixels[g->next],BYTES,144,256);pn_frame_clear(&g->frame,15);
            uint64_t w=144,h=256;if(((uint64_t)info.width*256>info.height*144u)==g->crop)w=(uint64_t)info.width*256/info.height;else h=(uint64_t)info.height*144/info.width;if(!w)w=1;if(!h)h=1;
            if(w>4096 || h>4096)s=PN_LIMIT;
            else{g->offset=0;s=pn_image_job_begin(&g->job,&g->scratch,&input,&g->frame,(pn_image_rect_t){(144-(int)w)/2,(256-(int)h)/2,(int)w,(int)h});if(s==PN_OK)return PN_BUSY;}
        }
    }else{pn_image_info_t info;s=pn_image_job_step(&g->job,&info);if(s==PN_BUSY)return s;
        struct stat after;if(s==PN_OK && (stat(g->page.items[g->next].path,&after) || after.st_size!=g->before.st_size || after.st_mtime!=g->before.st_mtime))s=PN_STALE_MEDIA;}
    stop(g);if(s==PN_CANCELLED)return PN_BUSY;
    g->valid[g->next]=s==PN_OK;if(s==PN_OK)(void)pn_wallpaper_thumbnail_save(g->media,g->cache,g->page.items[g->next].path,&g->frame);
    g->next++;return s==PN_OK || g->next>=g->page.count?PN_OK:PN_BUSY;
}
size_t pn_background_gallery_count(const pn_background_gallery_t *ui){const gallery_t *g=ui?ui->impl:NULL;return g?g->page.count:0;}
bool pn_background_gallery_can_page(const pn_background_gallery_t *ui,bool previous){const gallery_t *g=ui?ui->impl:NULL;return g && (previous?g->page.index!=0:g->page.more);}
void pn_background_gallery_paint(const pn_background_gallery_t *ui,const pn_style_t *s,pn_frame_t *f){
    const gallery_t *g=ui?ui->impl:NULL;if(!g)return;
    for(size_t i=0;i<g->page.count && i<N;i++){int x=32+(int)(i%3)*212,y=476+(int)(i/3)*286;
        bool selected=s->background==PN_READING_BACKGROUND_EXTERNAL && !strcmp(s->background_name,g->page.items[i].name);
        pn_w_round_stroke(f,x,y,196,266,10,selected?3:1,selected?PN_UI_INK:PN_UI_RULE);
        if(g->valid[i]){pn_frame_t thumb={(uint8_t *)g->pixels[i],144,256,72};for(int row=0;row<220;row++)for(int col=0;col<124;col++)pn_frame_pixel(f,x+36+col,y+12+row,pn_frame_get(&thumb,col*144/124,row*256/220));}
        else pn_w_icon(f,PN_ICON_IMAGE,x+78,y+100,40,PN_UI_RULE);
    }
}
pn_status_t pn_background_gallery_choose(pn_background_gallery_t *ui,unsigned slot,pn_style_t *s){
    gallery_t *g=ui?ui->impl:NULL;if(!g || !s || slot>=g->page.count || slot>=N)return PN_INVALID;
    const pn_catalog_item_t *item=&g->page.items[slot];if(strlen(item->name)>=PN_BACKGROUND_NAME_MAX || item->size>8*1024*1024)return PN_LIMIT;
    stop(g);pn_media_lease_t lease;pn_status_t status=pn_media_acquire(g->media,PN_MEDIA_READ,&lease);pn_book_id_t id;
    if(status==PN_OK){status=pn_identity_file_pooled(g->pool,g->media,&lease,item->path,8u*1024u*1024u,&id);(void)pn_media_release(g->media,&lease);}
    if(status==PN_OK){s->background=PN_READING_BACKGROUND_EXTERNAL;memset(s->background_name,0,sizeof s->background_name);strcpy(s->background_name,item->name);s->background_identity=id;}return status;
}
void pn_background_gallery_close(pn_background_gallery_t *ui){if(ui && ui->impl){gallery_t *g=ui->impl;stop(g);pn_free(g);ui->impl=NULL;}}

pn_status_t pn_background_gallery_open(pn_background_gallery_t *g,pn_pool_t *p,pn_media_t *m){return pn_background_gallery_open_at(g,p,m,pn_paper_directory(),pn_paper_cache_directory(),true);}
