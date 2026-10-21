/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：锁屏壁纸设置页状态机与绘制，原图READ预处理，应用时WRITE内部记录。
 * English: lock-wallpaper settings state machine and drawing; READ preprocessing of sources, WRITE of internal records on apply.
 * 冻结：预览草稿与已保存选择分开；应用失败重新读取已保存选择，不假定未提交。
 * Frozen: the preview draft is separate from the saved selection; failed applies reload the saved selection instead of assuming nothing committed.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_wallpaper_ui.h"
#include "pn_widgets.h"
#include "pn_image.h"
#include "pn_text_file.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#define W PN_WALLPAPER_WIDTH
#define H PN_WALLPAPER_HEIGHT
#define PW (W/2)
#define PH (H/2)
#define PX ((W-PW)/2)
#define PY 140
#define RESERVE (256u*1024u)
#define IMAGE_ROWS 6
#define TW 144
#define TH 256
#define THUMB_BYTES (TW*TH/2)
typedef struct {
    pn_pool_t *pool;pn_media_t *images;char directory[PN_CATALOG_PATH_MAX],cache_dir[PN_WALLPAPER_PATH_MAX];
    pn_wallpaper_store_t store;bool has_store,busy;
    pn_font_t font;uint8_t *canvas_px,*bitmap_px,*small_px,*alpha_px;pn_frame_t canvas,bitmap,small,alpha;
    pn_catalog_page_t page;pn_lock_selection_t saved,draft;pn_bookmark_lock_t bookmark;bool saved_known,bitmap_valid;
    pn_wallpaper_transform_t prepared;char prepared_path[PN_WALLPAPER_PATH_MAX];
    uint64_t prepared_size;int64_t prepared_mtime;
    pn_lock_mode_t chosen;
    uint8_t thumbs[IMAGE_ROWS][THUMB_BYTES];bool thumb_valid[IMAGE_ROWS];size_t thumb_next;
    pn_image_job_t thumb_job;pn_text_file_t thumb_file;pn_text_source_t thumb_source;pn_pool_t thumb_pool;
    pn_media_lease_t thumb_lease;bool thumb_held;uint64_t thumb_offset;
    struct stat thumb_stat;pn_frame_t thumb_frame;
    char message[96];
} ui_t;
static const char *mode_name(const pn_lock_selection_t *s){return s->mode==PN_LOCK_BOOKMARK?"书签锁屏":s->mode==PN_LOCK_MASK?"透明遮罩":s->mode==PN_LOCK_CUSTOM?"全屏壁纸":"系统默认";}
// 分步任务独占源租约；任何导航先关闭，避免占用后续应用/传输。/ Incremental jobs own their source lease; navigation closes it before apply/transfer.
static void thumbnail_close(ui_t *u){
    pn_image_job_close(&u->thumb_job);(void)pn_text_file_close(&u->thumb_file);
    if(u->thumb_held)(void)pn_media_release(u->images,&u->thumb_lease);
    u->thumb_held=false;
}
static void thumbnail_reset(ui_t *u){thumbnail_close(u);u->thumb_next=0;memset(u->thumb_valid,0,sizeof u->thumb_valid);}
// 命中整CRC的缩略图在首帧前装入，未命中留给原分步任务。/ Load CRC-verified thumbnails before the first frame; leave misses to the incremental decoder.
static void thumbnails_cached(ui_t *u){
    if(!*u->cache_dir)return;
    pn_media_lease_t lease;if(pn_media_acquire(u->images,PN_MEDIA_READ,&lease)!=PN_OK)return;
    for(size_t i=0;i<u->page.count && i<IMAGE_ROWS;i++){
        pn_frame_t thumb;(void)pn_frame_bind(&thumb,u->thumbs[i],THUMB_BYTES,TW,TH);
        u->thumb_valid[i]=pn_wallpaper_thumbnail_load(u->images,&lease,u->cache_dir,u->page.items[i].path,&thumb)==PN_OK;
    }
    (void)pn_media_release(u->images,&lease);
    while(u->thumb_next<u->page.count && u->thumb_next<IMAGE_ROWS && u->thumb_valid[u->thumb_next])u->thumb_next++;
}
static pn_status_t thumbnail_read(void *ctx,uint8_t *out,size_t cap,size_t *n){
    ui_t *u=ctx;if(u->thumb_offset>=u->thumb_source.size){*n=0;return PN_EMPTY;}
    pn_status_t s=u->thumb_source.read_at(u->thumb_source.ctx,u->thumb_offset,out,cap,n);
    if(s==PN_OK)u->thumb_offset+=*n;
    return s;
}
static void *thumbnail_alloc(void *ctx,size_t bytes){return pn_alloc_tagged(ctx,PN_MEM_UI,bytes);}
static void thumbnail_free(void *ctx,void *ptr){(void)ctx;pn_free(ptr);}
static pn_status_t load_page(ui_t *u,const char *cursor,bool previous){
    pn_catalog_page_t *next=pn_alloc_tagged(u->pool,PN_MEM_UI,sizeof *next);if(!next)return PN_NO_MEMORY;
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(u->images,PN_MEDIA_READ,&lease);
    if(status==PN_OK){status=previous?pn_catalog_image_page_before(u->images,&lease,u->directory,cursor,next):pn_catalog_image_page(u->images,&lease,u->directory,cursor,next);(void)pn_media_release(u->images,&lease);}
    if(status==PN_OK && (next->count || !*cursor)){thumbnail_reset(u);u->page=*next;thumbnails_cached(u);}
    pn_free(next);return status;
}
static void reload_saved(ui_t *u){
    // 读不到时保留已知值；只有读到内容才更新。/ Keep known values when unreadable; update only after a real read.
    if(!u->has_store)return;
    pn_media_lease_t lease;
    if(pn_media_acquire(u->store.media,PN_MEDIA_READ,&lease)!=PN_OK)return;
    pn_lock_selection_t s;pn_status_t status=pn_wallpaper_load(&u->store,&lease,&s,NULL);(void)pn_media_release(u->store.media,&lease);
    if(status==PN_OK){if(s.mode==PN_LOCK_SIMPLE || s.mode==PN_LOCK_BOOK)s.mode=PN_LOCK_DEFAULT;s.hint=false;u->saved=s;u->saved_known=true;}else if(status==PN_EMPTY){u->saved=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=false};u->saved_known=true;}
    else if(status==PN_CORRUPT)u->saved_known=false;
}
/* 按草稿生成预览：自定义时仅在路径/参数变化后重新预处理。/ Build the draft preview; custom sources are re-preprocessed only when path/parameters change. */
static pn_status_t build_preview(ui_t *u){
    pn_status_t status=PN_OK;
    if((u->draft.mode==PN_LOCK_CUSTOM || u->draft.mode==PN_LOCK_MASK) && (!u->bitmap_valid || strcmp(u->prepared_path,u->draft.source) || memcmp(&u->prepared,&u->draft.transform,sizeof u->prepared))){
        u->bitmap_valid=false;pn_media_lease_t lease;status=pn_media_acquire(u->images,PN_MEDIA_READ,&lease);
        size_t free_bytes=u->pool->limit>u->pool->used?u->pool->limit-u->pool->used:0;
        if(status==PN_OK){
            struct stat before,after;
            if(stat(u->draft.source,&before) || !S_ISREG(before.st_mode))status=PN_IO;
            if(status==PN_OK && u->draft.mode==PN_LOCK_MASK){
                if(!u->alpha_px)u->alpha_px=pn_alloc_tagged(u->pool,PN_MEM_UI,PN_WALLPAPER_BYTES);
                if(!u->alpha_px)status=PN_NO_MEMORY;
                else{(void)pn_frame_bind(&u->alpha,u->alpha_px,PN_WALLPAPER_BYTES,W,H);free_bytes=u->pool->limit>u->pool->used?u->pool->limit-u->pool->used:0;
                    status=pn_wallpaper_mask_prepare_cached(u->pool,u->images,&lease,u->draft.source,u->draft.transform,free_bytes>RESERVE?free_bytes-RESERVE:1,&u->bitmap,&u->alpha,*u->cache_dir?u->cache_dir:NULL);}
            }else if(status==PN_OK)status=pn_wallpaper_prepare_cached(u->pool,u->images,&lease,u->draft.source,u->draft.transform,free_bytes>RESERVE?free_bytes-RESERVE:1,&u->bitmap,*u->cache_dir?u->cache_dir:NULL);
            if(status==PN_OK && (stat(u->draft.source,&after) || before.st_size!=after.st_size || before.st_mtime!=after.st_mtime))status=PN_STALE_MEDIA;
            if(status==PN_OK){u->prepared_size=(uint64_t)after.st_size;u->prepared_mtime=(int64_t)after.st_mtime;u->draft.source_size=u->prepared_size;u->draft.source_mtime=u->prepared_mtime;}
            (void)pn_media_release(u->images,&lease);}
        if(status==PN_OK){u->bitmap_valid=true;u->prepared=u->draft.transform;strcpy(u->prepared_path,u->draft.source);}
    }
    if(status!=PN_OK)return status;
    if(u->draft.mode==PN_LOCK_CUSTOM || u->draft.cached_book)memcpy(u->canvas_px,u->bitmap_px,PN_WALLPAPER_BYTES);
    if(u->draft.mode==PN_LOCK_MASK){
        pn_frame_clear(&u->canvas,15);
        for(int y=180;y<1060;y+=70){pn_status_t text=pn_w_text(&u->font,&u->canvas,"PicoNano 阅读文字预览",48,y,580,PN_ALIGN_LEFT);if(text!=PN_OK)return text;}
        status=pn_wallpaper_mask_render(&u->bitmap,&u->alpha,&u->canvas);if(status!=PN_OK)return status;
    }
    // 有效书封与自定义使用相同预览/持久路径；无候选保留系统默认占位。/ Valid book covers share the custom preview/persistence path; no candidate retains default artwork.
    pn_lock_selection_t shown=u->draft;if(shown.mode==PN_LOCK_BOOK && !shown.cached_book)shown.mode=PN_LOCK_DEFAULT;
    status=shown.mode==PN_LOCK_BOOKMARK?pn_bookmark_lock_render(&u->bookmark,NULL,&u->font,"",&u->canvas):pn_lock_render(&shown,&u->font,"",&u->canvas);
    if(status==PN_OK){for(int y=0;y<PH;y++)for(int x=0;x<PW;x++){unsigned sum=pn_frame_get(&u->canvas,2*x,2*y)+pn_frame_get(&u->canvas,2*x+1,2*y)+pn_frame_get(&u->canvas,2*x,2*y+1)+pn_frame_get(&u->canvas,2*x+1,2*y+1);pn_frame_pixel(&u->small,x,y,(uint8_t)((sum+2)/4));}}
    return status;
}
/* 版式：列表页=顶栏、三个模式按钮、图片行、翻页；预览页=顶栏（取消/应用）、半尺寸预览、调整按钮。
 * Layout: list page = header, three mode buttons, image rows, paging; preview page = header (cancel/apply), half-size preview and adjustment buttons. */
#define MODE_BUTTON_Y 196
#define IMAGE_SECTION_Y 400
#define IMAGE_ROW_Y 432
#define IMAGE_PITCH 324
#define PAGER_Y 1084
#define CONTROL_Y0 770
#define CONTROL_PITCH 90
static pn_status_t line_text(ui_t *u,const char *value,int size,int baseline,pn_align_t align){
    int original=u->font.pixels;pn_status_t s=pn_font_size(&u->font,size);
    if(s==PN_OK)s=pn_w_text(&u->font,&u->canvas,value,PN_UI_MARGIN,baseline,620,align);
    pn_status_t restored=pn_font_size(&u->font,original);return s==PN_OK?restored:s;
}
static pn_status_t sized_button(ui_t *u,const char *label,int x,int y,int w,int h,unsigned style){
    int original=u->font.pixels;pn_status_t s=pn_font_size(&u->font,30);
    if(s==PN_OK)s=pn_w_button(&u->font,&u->canvas,label,x,y,w,h,style);
    pn_status_t restored=pn_font_size(&u->font,original);return s==PN_OK?restored:s;
}
static pn_status_t draw(ui_t *u,pn_wallpaper_screen_t screen){
    pn_frame_clear(&u->canvas,15);pn_status_t s=PN_OK;char line[PN_WALLPAPER_PATH_MAX+64];
    if(screen==PN_WUI_LIST){
        s=pn_w_header(&u->font,&u->canvas,"< 返回","锁屏壁纸",NULL);
        if(s==PN_OK){if(!u->saved_known)snprintf(line,sizeof line,"当前：系统默认（内部记录不可用）");else snprintf(line,sizeof line,"当前：%s",mode_name(&u->saved));s=line_text(u,line,28,168,PN_ALIGN_LEFT);}
        const char *names[]={"壁纸","遮罩","书签"};
        for(int i=0;i<3 && s==PN_OK;i++)s=sized_button(u,names[i],32+i*212,MODE_BUTTON_Y,196,80,u->chosen==(i==0?PN_LOCK_CUSTOM:i==1?PN_LOCK_MASK:PN_LOCK_BOOKMARK)?PN_W_SELECTED:0u);
        if(s==PN_OK)s=sized_button(u,"重置为系统默认",32,294,620,56,0u);
        bool images=u->chosen!=PN_LOCK_BOOKMARK;
        if(s==PN_OK)s=pn_w_section(&u->font,&u->canvas,images?(u->page.count?(u->thumb_next<u->page.count && u->thumb_next<IMAGE_ROWS?"选择图片 · 预览准备中":"选择图片"):"暂无图片，请先传入 JPEG/PNG"):"书签自动使用本地阅读数据，无需选择图片",IMAGE_SECTION_Y);
        for(size_t i=0;images && i<u->page.count && i<IMAGE_ROWS && s==PN_OK;i++){
            int x=32+(int)(i%3)*212,y=IMAGE_ROW_Y+(int)(i/3)*IMAGE_PITCH;
            pn_w_round_stroke(&u->canvas,x,y,196,300,10,1,PN_UI_RULE);
            if(u->thumb_valid[i]){for(int row=0;row<TH;row++)memcpy(u->canvas.pixels+(size_t)(y+22+row)*u->canvas.stride+(x+26)/2,u->thumbs[i]+row*TW/2,TW/2);}
            else pn_w_icon(&u->canvas,i>=u->thumb_next?PN_ICON_LOADING:PN_ICON_IMAGE,x+72,y+124,52,PN_UI_MUTED);
        }
        if(s==PN_OK && *u->message)s=line_text(u,u->message,22,1196,PN_ALIGN_LEFT);
        if(s==PN_OK && images)s=sized_button(u,"上一页",32,PAGER_Y,196,80,0u);
        if(s==PN_OK && images)s=sized_button(u,"下一页",456,PAGER_Y,196,80,0u);
    }else{
        bool custom=u->draft.mode==PN_LOCK_CUSTOM || u->draft.mode==PN_LOCK_MASK,cover=custom && u->draft.transform.fit==PN_WALLPAPER_COVER;
        s=pn_w_header(&u->font,&u->canvas,"< 返回","预览",u->has_store?"应用":"应用");
        for(int y=0;y<PH;y++)for(int x=0;x<PW;x++)pn_frame_pixel(&u->canvas,PX+x,PY+y,pn_frame_get(&u->small,x,y));
        pn_w_outline(&u->canvas,PX-2,PY-2,PW+4,PH+4,2,PN_UI_INK);
        if(custom && s==PN_OK){s=sized_button(u,u->draft.transform.fit==PN_WALLPAPER_COVER?"铺满裁切":"完整显示",32,CONTROL_Y0,300,80,0u);
            if(s==PN_OK)s=sized_button(u,"旋转 90°",352,CONTROL_Y0,300,80,0u);
            if(s==PN_OK && cover){s=sized_button(u,"裁切位置 -",32,CONTROL_Y0+CONTROL_PITCH,300,80,0u);if(s==PN_OK)s=sized_button(u,"裁切位置 +",352,CONTROL_Y0+CONTROL_PITCH,300,80,0u);}}
        if(s==PN_OK)s=line_text(u,custom?(u->draft.mode==PN_LOCK_MASK?"遮罩 · 叠加在阅读页面上":"壁纸 · 全屏显示所选图片"):"书签 · 自动使用当前书籍与本地数据",26,1040,PN_ALIGN_CENTER);
        if(s==PN_OK){snprintf(line,sizeof line,"预览未保存：%s",mode_name(&u->draft));s=line_text(u,*u->message?u->message:line,26,1190,PN_ALIGN_LEFT);}
    }
    return s;
}
static pn_status_t show(pn_wallpaper_ui_t *ui,pn_wallpaper_present_fn present,void *ctx){
    ui_t *u=ui->impl;ui->presented=false;pn_status_t s=draw(u,ui->screen);
    if(s==PN_OK)s=present(ctx,&u->canvas,ui->screen==PN_WUI_PREVIEW?PN_REFRESH_GC16:PN_REFRESH_GL16);
    if(s==PN_OK)ui->presented=true;
    return s;
}
// 先确认等待画面，再开始解码或保存；复用画布，不占用额外整屏内存。/ Confirm the busy frame before decoding or saving; reuse the canvas without another full frame.
static pn_status_t show_busy(pn_wallpaper_ui_t *ui,pn_wallpaper_present_fn present,void *ctx,const char *title){
    ui_t *u=ui->impl;u->busy=true;ui->presented=false;pn_frame_clear(&u->canvas,PN_UI_PAPER);
    pn_status_t s=pn_w_header(&u->font,&u->canvas,"","锁屏设置",NULL);
    if(s==PN_OK)s=pn_w_loading(&u->font,&u->canvas,title,"请稍候，完成后将自动返回",-1,380);
    if(s==PN_OK)s=present(ctx,&u->canvas,PN_REFRESH_GL16);
    return s;
}
pn_status_t pn_wallpaper_ui_open_cached(pn_wallpaper_ui_t *ui,pn_pool_t *pool,pn_media_t *images_media,const char *directory,
    const pn_wallpaper_store_t *store,pn_wallpaper_present_fn present,void *ctx,const char *cache_dir){
    if(!ui || !pool || !images_media || !directory || !*directory || !present)return PN_INVALID;
    if(ui->impl)return PN_BUSY;
    if(strlen(directory)>=PN_CATALOG_PATH_MAX)return PN_LIMIT;
    if(cache_dir && strlen(cache_dir)>=PN_WALLPAPER_PATH_MAX)return PN_LIMIT;
    ui_t *u=pn_alloc_tagged(pool,PN_MEM_UI,sizeof *u);if(!u)return PN_NO_MEMORY;memset(u,0,sizeof *u);u->pool=pool;u->bookmark.battery=-1;u->images=images_media;strcpy(u->directory,directory);
    if(store){u->store=*store;u->has_store=true;}
    if(cache_dir)strcpy(u->cache_dir,cache_dir);
    u->canvas_px=pn_alloc_tagged(pool,PN_MEM_UI,PN_WALLPAPER_BYTES);u->bitmap_px=pn_alloc_tagged(pool,PN_MEM_UI,PN_WALLPAPER_BYTES);u->small_px=pn_alloc_tagged(pool,PN_MEM_UI,(size_t)PW/2*PH);
    pn_status_t status=u->canvas_px && u->bitmap_px && u->small_px?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK){(void)pn_frame_bind(&u->canvas,u->canvas_px,PN_WALLPAPER_BYTES,W,H);(void)pn_frame_bind(&u->bitmap,u->bitmap_px,PN_WALLPAPER_BYTES,W,H);(void)pn_frame_bind(&u->small,u->small_px,(size_t)PW/2*PH,PW,PH);
        pn_text_source_t builtin=pn_font_builtin_source();status=pn_font_open(&u->font,pool,&builtin,28);}
    if(status!=PN_OK){pn_free(u->canvas_px);pn_free(u->bitmap_px);pn_free(u->small_px);pn_free(u);return status;}
    reload_saved(u);u->chosen=u->saved.mode==PN_LOCK_BOOKMARK?PN_LOCK_BOOKMARK:u->saved.mode==PN_LOCK_MASK?PN_LOCK_MASK:PN_LOCK_CUSTOM;
    if(load_page(u,"",false)!=PN_OK){memset(&u->page,0,sizeof u->page);}
    *ui=(pn_wallpaper_ui_t){.impl=u,.active=true,.screen=PN_WUI_LIST,.last=PN_EMPTY};
    (void)show(ui,present,ctx);return PN_OK;
}
pn_status_t pn_wallpaper_ui_open(pn_wallpaper_ui_t *ui,pn_pool_t *pool,pn_media_t *images_media,const char *directory,const pn_wallpaper_store_t *store,pn_wallpaper_present_fn present,void *ctx){
    return pn_wallpaper_ui_open_cached(ui,pool,images_media,directory,store,present,ctx,NULL);
}
static pn_status_t apply(pn_wallpaper_ui_t *ui){
    ui_t *u=ui->impl;
    if(!u->has_store){snprintf(u->message,sizeof u->message,"内部壁纸分区不可用，锁屏未更改");return PN_UNSUPPORTED;}
    if((u->draft.mode==PN_LOCK_CUSTOM || u->draft.mode==PN_LOCK_MASK || u->draft.cached_book) && !u->bitmap_valid){snprintf(u->message,sizeof u->message,"预览尚未生成，锁屏未更改");return PN_INVALID;}
    // 保存预览与缓存使用同一源代；改图后须重新预览，不能将旧像素标为新缓存。 / Save previews/caches from one source generation; changed files require a new preview so stale pixels never get a new cache key.
    pn_media_lease_t source_lease;bool source_held=false;pn_status_t status=PN_OK;
    if(u->draft.mode==PN_LOCK_CUSTOM || u->draft.mode==PN_LOCK_MASK){
        status=pn_media_acquire(u->images,PN_MEDIA_READ,&source_lease);source_held=status==PN_OK;struct stat info;
        if(status==PN_OK && (stat(u->draft.source,&info) || (uint64_t)info.st_size!=u->prepared_size || (int64_t)info.st_mtime!=u->prepared_mtime))status=PN_STALE_MEDIA;
    }
    pn_media_lease_t lease;pn_lock_selection_t record=u->draft;bool cache_failed=false;
    if(status==PN_OK)status=pn_media_acquire(u->store.media,PN_MEDIA_WRITE,&lease);
    if(status==PN_OK){status=pn_wallpaper_save(&u->store,&lease,&record,u->draft.mode==PN_LOCK_CUSTOM || u->draft.cached_book?&u->bitmap:NULL);
        if(status==PN_OK && record.mode==PN_LOCK_MASK)cache_failed=pn_wallpaper_mask_cache_save(&u->store,&lease,&record,&u->bitmap,&u->alpha)!=PN_OK;
        (void)pn_media_release(u->store.media,&lease);
        reload_saved(u);}
    if(source_held)(void)pn_media_release(u->images,&source_lease);
    if(status==PN_OK){ui->screen=PN_WUI_LIST;snprintf(u->message,sizeof u->message,cache_failed?"已应用：%s（缓存稍后重试）":"已应用：%s",mode_name(&record));}
    else snprintf(u->message,sizeof u->message,"应用失败（%d），保留原锁屏",(int)status);
    return status;
}
static void begin_preview(ui_t *u,pn_lock_selection_t draft){u->bitmap_valid=false;u->draft=draft;u->message[0]=0;}
pn_status_t pn_wallpaper_ui_event(pn_wallpaper_ui_t *ui,int command,pn_wallpaper_present_fn present,void *ctx){
    if(!ui || !ui->impl || !ui->active || !present)return PN_INVALID;
    ui_t *u=ui->impl;if(u->busy)return PN_BUSY;pn_status_t status=PN_OK;bool rebuild=false;
    pn_lock_selection_t old_draft=u->draft;bool old_bitmap_valid=u->bitmap_valid;pn_lock_mode_t old_chosen=u->chosen;
    if(ui->screen==PN_WUI_LIST){
        u->message[0]=0;
        if(command==PN_WUI_BACK){ui->active=false;return PN_OK;}
        else if(command==PN_WUI_DEFAULT){thumbnail_close(u);begin_preview(u,(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT});status=show_busy(ui,present,ctx,"正在应用锁屏");if(status==PN_OK)status=apply(ui);ui->last=status;if(status==PN_OK)u->chosen=PN_LOCK_CUSTOM;}
        else if(command==PN_WUI_WALLPAPER || command==PN_WUI_MASK){u->chosen=command==PN_WUI_MASK?PN_LOCK_MASK:PN_LOCK_CUSTOM;}
        else if(command==PN_WUI_BOOKMARK){u->chosen=PN_LOCK_BOOKMARK;thumbnail_close(u);begin_preview(u,(pn_lock_selection_t){.mode=PN_LOCK_BOOKMARK,.hint=false});rebuild=true;}
        else if(u->chosen!=PN_LOCK_BOOKMARK && command>=PN_WUI_ROW && command<PN_WUI_ROW+(int)u->page.count && command<PN_WUI_ROW+IMAGE_ROWS){thumbnail_close(u);const pn_catalog_item_t *item=&u->page.items[command-PN_WUI_ROW];
            if(strlen(item->path)>=PN_WALLPAPER_PATH_MAX)return PN_LIMIT;
            pn_lock_selection_t d={.mode=u->chosen==PN_LOCK_MASK?PN_LOCK_MASK:PN_LOCK_CUSTOM,.hint=false,.transform={PN_WALLPAPER_CONTAIN,0,0},.source_size=item->size};strcpy(d.source,item->path);
            struct stat info;if(stat(item->path,&info)==0)d.source_mtime=(int64_t)info.st_mtime;
            if(u->saved_known && (u->saved.mode==PN_LOCK_CUSTOM || u->saved.mode==PN_LOCK_MASK) && !strcmp(u->saved.source,d.source)){d.transform=u->saved.transform;}
            begin_preview(u,d);rebuild=true;}
        else if((command==PN_WUI_NEXT || command==PN_WUI_PREVIOUS) && u->page.count){char cursor[PN_CATALOG_NAME_MAX];strcpy(cursor,u->page.items[command==PN_WUI_NEXT?u->page.count-1:0].name);
            if(command==PN_WUI_PREVIOUS || u->page.more)status=load_page(u,cursor,command==PN_WUI_PREVIOUS);}
        else return PN_EMPTY;
    }else{
        bool custom=u->draft.mode==PN_LOCK_CUSTOM || u->draft.mode==PN_LOCK_MASK,cover=custom && u->draft.transform.fit==PN_WALLPAPER_COVER;
        if(command==PN_WUI_CANCEL){ui->screen=PN_WUI_LIST;u->message[0]=0;}
        else if(command==PN_WUI_APPLY){status=show_busy(ui,present,ctx,"正在应用锁屏");if(status==PN_OK)status=apply(ui);ui->last=status;}
        else if(command==PN_WUI_FIT && custom){u->draft.transform.fit=cover?PN_WALLPAPER_CONTAIN:PN_WALLPAPER_COVER;u->draft.transform.shift=0;rebuild=true;}
        else if(command==PN_WUI_ROTATE && custom){u->draft.transform.rotation=(uint8_t)((u->draft.transform.rotation+1)&3);rebuild=true;}
        else if((command==PN_WUI_LEFT || command==PN_WUI_RIGHT) && cover){int shift=u->draft.transform.shift+(command==PN_WUI_LEFT?-1:1);
            if(shift<-PN_WALLPAPER_SHIFT_MAX || shift>PN_WALLPAPER_SHIFT_MAX)return PN_EMPTY;
            u->draft.transform.shift=(int8_t)shift;rebuild=true;}
        else return PN_EMPTY;
    }
    if(rebuild){status=show_busy(ui,present,ctx,"正在生成预览");
        // 提示未确认时恢复草稿，不能把旧像素与新裁切参数提交。/ Restore drafts after an unconfirmed notice; never commit old pixels with new crop parameters.
        if(status!=PN_OK){u->draft=old_draft;u->bitmap_valid=old_bitmap_valid;u->chosen=old_chosen;u->busy=false;snprintf(u->message,sizeof u->message,"显示未完成，请重试");(void)show(ui,present,ctx);return status;}
        status=build_preview(u);
        if(status==PN_OK){ui->screen=PN_WUI_PREVIEW;}
        else{ui->screen=PN_WUI_LIST;snprintf(u->message,sizeof u->message,status==PN_NO_MEMORY?"图片过大，内存不足":status==PN_UNSUPPORTED?"不是可用的JPEG/PNG":"图片无法读取（%d）",(int)status);}}
    u->busy=false;pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;
}
bool pn_wallpaper_ui_pending(const pn_wallpaper_ui_t *ui){
    const ui_t *u=ui?ui->impl:NULL;
    return u && ui->active && ui->screen==PN_WUI_LIST && u->chosen!=PN_LOCK_BOOKMARK && u->thumb_next<u->page.count && u->thumb_next<IMAGE_ROWS;
}
pn_status_t pn_wallpaper_ui_tick(pn_wallpaper_ui_t *ui,pn_wallpaper_present_fn present,void *ctx){
    if(!ui || !ui->impl || !present)return PN_INVALID;
    ui_t *u=ui->impl;size_t count=u->page.count<IMAGE_ROWS?u->page.count:IMAGE_ROWS;
    if(!pn_wallpaper_ui_pending(ui))return PN_EMPTY;
    pn_status_t status=PN_OK;
    if(!u->thumb_job.impl){
        // 解码工作区最多2MiB，保留前台余量；超限仅该缩略图退回图标。/ Cap decoder scratch at 2 MiB and retain foreground headroom; an oversized thumbnail falls back to an icon.
        size_t free_bytes=u->pool->limit>u->pool->used?u->pool->limit-u->pool->used:0;
        size_t budget=free_bytes>RESERVE?free_bytes-RESERVE:1;if(budget>2u*1024u*1024u)budget=2u*1024u*1024u;
        (void)pn_pool_init(&u->thumb_pool,budget,thumbnail_alloc,thumbnail_free,u->pool);
        status=pn_media_acquire(u->images,PN_MEDIA_READ,&u->thumb_lease);u->thumb_held=status==PN_OK;
        const char *path=u->page.items[u->thumb_next].path;
        if(status==PN_OK && stat(path,&u->thumb_stat))status=PN_IO;
        if(status==PN_OK)status=pn_text_file_open(&u->thumb_file,u->images,&u->thumb_lease,path,&u->thumb_source);
        pn_image_input_t input={u,thumbnail_read};pn_image_info_t info;u->thumb_offset=0;
        if(status==PN_OK)status=pn_image_probe(&u->thumb_pool,&input,&info);
        if(status==PN_OK){
            (void)pn_frame_bind(&u->thumb_frame,u->thumbs[u->thumb_next],THUMB_BYTES,TW,TH);pn_frame_clear(&u->thumb_frame,15);
            int w=TW,h=TH;if((uint64_t)info.width*TH>(uint64_t)info.height*TW)h=(int)((uint64_t)info.height*TW/info.width);else w=(int)((uint64_t)info.width*TH/info.height);
            if(w<1)w=1;
            if(h<1)h=1;
            u->thumb_offset=0;status=pn_image_job_begin(&u->thumb_job,&u->thumb_pool,&input,&u->thumb_frame,(pn_image_rect_t){(TW-w)/2,(TH-h)/2,w,h});
            if(status==PN_OK)return PN_BUSY;
        }
    }else{pn_image_info_t info;status=pn_image_job_step(&u->thumb_job,&info);if(status==PN_BUSY)return PN_BUSY;
        struct stat after;if(status==PN_OK && (stat(u->page.items[u->thumb_next].path,&after) || after.st_size!=u->thumb_stat.st_size || after.st_mtime!=u->thumb_stat.st_mtime))status=PN_STALE_MEDIA;}
    // 输入取消不是图片无效，关闭暂定解码并保留本槽，下一轮空闲重试。/ Input cancellation is not an invalid image: discard provisional decoding, retain this slot and retry while idle.
    if(status==PN_CANCELLED){thumbnail_close(u);return PN_BUSY;}
    u->thumb_valid[u->thumb_next]=status==PN_OK;thumbnail_close(u);
    if(status==PN_OK && *u->cache_dir)(void)pn_wallpaper_thumbnail_save(u->images,u->cache_dir,u->page.items[u->thumb_next].path,&u->thumb_frame);
    u->thumb_next++;while(u->thumb_next<count && u->thumb_valid[u->thumb_next])u->thumb_next++;
    if(u->thumb_next==count)return show(ui,present,ctx);
    if(u->thumb_next==1 && status==PN_OK){pn_status_t shown=show(ui,present,ctx);if(shown!=PN_OK)return shown;}
    return PN_BUSY;
}
pn_status_t pn_wallpaper_ui_present(pn_wallpaper_ui_t *ui,pn_wallpaper_present_fn present,void *ctx){if(!ui || !ui->impl || !present)return PN_INVALID;return show(ui,present,ctx);}
int pn_wallpaper_ui_hit(const pn_wallpaper_ui_t *ui,int x,int y){
    if(!ui || !ui->impl || ((const ui_t *)ui->impl)->busy || x<0 || x>=W)return -1;
    const ui_t *u=ui->impl;
    int header=pn_w_header_hit(x,y,ui->screen==PN_WUI_PREVIEW);
    if(ui->screen==PN_WUI_LIST){
        if(header==1)return PN_WUI_BACK;
        if(x<32 || x>=652)return -1;
        if(y>=MODE_BUTTON_Y && y<MODE_BUTTON_Y+80){int column=(x-32)/212;if((x-32)%212>=196)return -1;return column==0?PN_WUI_WALLPAPER:column==1?PN_WUI_MASK:PN_WUI_BOOKMARK;}
        if(y>=294 && y<350)return PN_WUI_DEFAULT;
        if(u->chosen==PN_LOCK_BOOKMARK)return -1;
        if(y>=IMAGE_ROW_Y && y<IMAGE_ROW_Y+2*IMAGE_PITCH){int column=(x-32)/212,row=(y-IMAGE_ROW_Y)/IMAGE_PITCH,index=row*3+column;
            if((x-32)%212>=196 || (y-IMAGE_ROW_Y)%IMAGE_PITCH>=300)return -1;
            return index<(int)u->page.count?PN_WUI_ROW+index:-1;}
        if(y>=PAGER_Y && y<PAGER_Y+80)return x<228?PN_WUI_PREVIOUS:x>=456?PN_WUI_NEXT:-1;
        return -1;
    }
    if(header==1)return PN_WUI_CANCEL;
    if(header==2)return PN_WUI_APPLY;
    if(x<32 || x>=652)return -1;
    bool custom=u->draft.mode==PN_LOCK_CUSTOM || u->draft.mode==PN_LOCK_MASK,cover=custom && u->draft.transform.fit==PN_WALLPAPER_COVER;
    if(custom){
        if(y>=CONTROL_Y0 && y<CONTROL_Y0+80)return x<332?PN_WUI_FIT:x>=352?PN_WUI_ROTATE:-1;
        if(cover){if(y>=CONTROL_Y0+CONTROL_PITCH && y<CONTROL_Y0+CONTROL_PITCH+80)return x<332?PN_WUI_LEFT:x>=352?PN_WUI_RIGHT:-1;}
    }
    return -1;
}
bool pn_wallpaper_ui_current(const pn_wallpaper_ui_t *ui,pn_lock_selection_t *out){if(!ui || !ui->impl || !out)return false;const ui_t *u=ui->impl;if(!u->saved_known)return false;*out=u->saved;return true;}
void pn_wallpaper_ui_close(pn_wallpaper_ui_t *ui){
    if(!ui || !ui->impl)return;
    ui_t *u=ui->impl;thumbnail_close(u);pn_font_close(&u->font);pn_free(u->canvas_px);pn_free(u->bitmap_px);pn_free(u->small_px);pn_free(u->alpha_px);pn_free(u);
    *ui=(pn_wallpaper_ui_t){0};
}

void pn_wallpaper_ui_set_bookmark(pn_wallpaper_ui_t *ui,const pn_bookmark_lock_t *snapshot){if(ui && ui->impl && snapshot)((ui_t *)ui->impl)->bookmark=*snapshot;}
