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
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#define W PN_WALLPAPER_WIDTH
#define H PN_WALLPAPER_HEIGHT
#define PW (W/2)
#define PH (H/2)
#define PX ((W-PW)/2)
#define PY 40
#define RESERVE (256u*1024u)
typedef struct {
    pn_pool_t *pool;pn_media_t *images;char directory[PN_CATALOG_PATH_MAX];
    pn_wallpaper_store_t store;bool has_store;
    pn_font_t font;uint8_t *canvas_px,*bitmap_px,*small_px;pn_frame_t canvas,bitmap,small;
    pn_catalog_page_t page;pn_lock_selection_t saved,draft;bool saved_known,bitmap_valid;
    pn_wallpaper_transform_t prepared;char prepared_path[PN_WALLPAPER_PATH_MAX];
    char message[96];
} ui_t;
static pn_status_t string_read(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t size=strlen(s);if(off>size)return PN_INVALID;size_t take=size-(size_t)off;if(take>cap)take=cap;memcpy(out,s+off,take);*n=take;return PN_OK;}
/* 单行文字，超宽截断；缺字方框。/ One text line, truncated at width; boxes for missing glyphs. */
static pn_status_t text(ui_t *u,const char *s,int x,int y,int width){
    pn_font_t *font=&u->font;pn_text_source_t source={(void *)s,strlen(s),string_read,NULL};pn_text_reader_t reader;pn_status_t status=pn_text_open(&reader,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    pn_text_char_t c;int32_t cursor=x*64,limit=(x+width)*64;
    while((status=pn_text_next(&reader,&c))==PN_OK){int32_t advance;pn_status_t measured=pn_font_advance(font,c.codepoint,&advance);if(measured==PN_EMPTY)advance=font->pixels*64;else if(measured!=PN_OK)return measured;
        if(advance<0)return PN_LIMIT;
        if(advance>limit-cursor)break;
        if(measured==PN_EMPTY){int left=cursor/64;pn_frame_rect(&u->canvas,left+2,y-font->pixels+4,font->pixels-6,1,0);pn_frame_rect(&u->canvas,left+2,y-2,font->pixels-6,1,0);pn_frame_rect(&u->canvas,left+2,y-font->pixels+4,1,font->pixels-6,0);pn_frame_rect(&u->canvas,left+font->pixels-5,y-font->pixels+4,1,font->pixels-6,0);}
        else{status=pn_font_draw(font,&u->canvas,c.codepoint,cursor,y,PN_FONT_GRAY);if(status!=PN_OK)return status;}
        cursor+=advance;
    }
    return status==PN_EMPTY?PN_OK:status;
}
static void box(pn_frame_t *f,int x,int y,int w,int h,uint8_t shade){pn_frame_rect(f,x,y,w,1,shade);pn_frame_rect(f,x,y+h-1,w,1,shade);pn_frame_rect(f,x,y,1,h,shade);pn_frame_rect(f,x+w-1,y,1,h,shade);}
static pn_status_t button(ui_t *u,const char *label,int x,int y,int w,bool selected){
    box(&u->canvas,x,y,w,90,selected?0:5);if(selected)box(&u->canvas,x+1,y+1,w-2,88,0);return text(u,label,x+24,y+58,w-40);
}
static const char *base(const char *path){const char *slash=strrchr(path,'/');return slash?slash+1:path;}
static const char *mode_name(const pn_lock_selection_t *s){return s->mode==PN_LOCK_CUSTOM?"自定义图片":s->mode==PN_LOCK_SIMPLE?"简洁锁屏":"系统默认";}
static pn_status_t load_page(ui_t *u,const char *cursor,bool previous){
    pn_catalog_page_t *next=pn_alloc(u->pool,sizeof *next);if(!next)return PN_NO_MEMORY;
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(u->images,PN_MEDIA_READ,&lease);
    if(status==PN_OK){status=previous?pn_catalog_image_page_before(u->images,&lease,u->directory,cursor,next):pn_catalog_image_page(u->images,&lease,u->directory,cursor,next);(void)pn_media_release(u->images,&lease);}
    if(status==PN_OK && (next->count || !*cursor))u->page=*next;
    pn_free(next);return status;
}
static void reload_saved(ui_t *u){
    // 读不到时保留已知值；只有读到内容才更新。/ Keep known values when unreadable; update only after a real read.
    if(!u->has_store)return;
    pn_media_lease_t lease;
    if(pn_media_acquire(u->store.media,PN_MEDIA_READ,&lease)!=PN_OK)return;
    pn_lock_selection_t s;pn_status_t status=pn_wallpaper_load(&u->store,&lease,&s,NULL);(void)pn_media_release(u->store.media,&lease);
    if(status==PN_OK){u->saved=s;u->saved_known=true;}else if(status==PN_EMPTY){u->saved=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=true};u->saved_known=true;}
    else if(status==PN_CORRUPT)u->saved_known=false;
}
/* 按草稿生成预览：自定义时仅在路径/参数变化后重新预处理。/ Build the draft preview; custom sources are re-preprocessed only when path/parameters change. */
static pn_status_t build_preview(ui_t *u){
    pn_status_t status=PN_OK;
    if(u->draft.mode==PN_LOCK_CUSTOM && (!u->bitmap_valid || strcmp(u->prepared_path,u->draft.source) || memcmp(&u->prepared,&u->draft.transform,sizeof u->prepared))){
        u->bitmap_valid=false;pn_media_lease_t lease;status=pn_media_acquire(u->images,PN_MEDIA_READ,&lease);
        size_t free_bytes=u->pool->limit>u->pool->used?u->pool->limit-u->pool->used:0;
        if(status==PN_OK){status=pn_wallpaper_prepare(u->pool,u->images,&lease,u->draft.source,u->draft.transform,free_bytes>RESERVE?free_bytes-RESERVE:1,&u->bitmap);(void)pn_media_release(u->images,&lease);}
        if(status==PN_OK){u->bitmap_valid=true;u->prepared=u->draft.transform;strcpy(u->prepared_path,u->draft.source);}
    }
    if(status!=PN_OK)return status;
    if(u->draft.mode==PN_LOCK_CUSTOM)memcpy(u->canvas_px,u->bitmap_px,PN_WALLPAPER_BYTES);
    status=pn_lock_render(&u->draft,&u->font,"再按电源键继续阅读",&u->canvas);
    if(status==PN_OK){for(int y=0;y<PH;y++)for(int x=0;x<PW;x++){unsigned sum=pn_frame_get(&u->canvas,2*x,2*y)+pn_frame_get(&u->canvas,2*x+1,2*y)+pn_frame_get(&u->canvas,2*x,2*y+1)+pn_frame_get(&u->canvas,2*x+1,2*y+1);pn_frame_pixel(&u->small,x,y,(uint8_t)((sum+2)/4));}}
    return status;
}
static pn_status_t draw(ui_t *u,pn_wallpaper_screen_t screen){
    pn_frame_clear(&u->canvas,15);pn_status_t s=PN_OK;char line[PN_WALLPAPER_PATH_MAX+64];
    if(screen==PN_WUI_LIST){
        s=text(u,"小纸 Pico",32,64,620);if(s==PN_OK)s=text(u,"锁屏壁纸",32,150,620);
        if(s==PN_OK){if(!u->saved_known)snprintf(line,sizeof line,"当前：系统默认（内部记录不可用）");else if(u->saved.mode==PN_LOCK_CUSTOM)snprintf(line,sizeof line,"当前：%s",base(u->saved.source));else snprintf(line,sizeof line,"当前：%s",mode_name(&u->saved));s=text(u,line,32,220,620);}
        if(s==PN_OK)s=button(u,"系统默认",32,260,300,false);
        if(s==PN_OK)s=button(u,"简洁锁屏",352,260,300,false);
        if(s==PN_OK)s=text(u,u->page.count?"图片":"图片：wallpapers目录暂无JPEG/PNG",32,420,620);
        for(size_t i=0;i<u->page.count && s==PN_OK;i++){int y=450+(int)i*100;box(&u->canvas,32,y,620,90,8);s=text(u,u->page.items[i].name,56,y+58,580);}
        if(s==PN_OK && *u->message)s=text(u,u->message,32,1086,620);
        if(s==PN_OK)s=button(u,"上一页",32,1110,190,false);
        if(s==PN_OK)s=button(u,"返回",247,1110,190,false);
        if(s==PN_OK)s=button(u,"下一页",462,1110,190,false);
    }else{
        for(int y=0;y<PH;y++)for(int x=0;x<PW;x++)pn_frame_pixel(&u->canvas,PX+x,PY+y,pn_frame_get(&u->small,x,y));
        box(&u->canvas,PX-1,PY-1,PW+2,PH+2,0);
        bool custom=u->draft.mode==PN_LOCK_CUSTOM;
        if(custom){s=button(u,u->draft.transform.fit==PN_WALLPAPER_COVER?"铺满裁切":"完整显示",32,680,300,false);
            if(s==PN_OK)s=button(u,"旋转90°",352,680,300,false);
            if(s==PN_OK && u->draft.transform.fit==PN_WALLPAPER_COVER){s=button(u,"裁切位置 −",32,790,300,false);if(s==PN_OK)s=button(u,"裁切位置 +",352,790,300,false);}}
        if(s==PN_OK)s=button(u,u->draft.hint?"底部提示：开":"底部提示：关",32,900,620,false);
        if(s==PN_OK){snprintf(line,sizeof line,"预览未保存：%s",custom?base(u->draft.source):mode_name(&u->draft));s=text(u,*u->message?u->message:line,32,1060,620);}
        if(s==PN_OK)s=button(u,"取消",32,1110,300,false);
        if(s==PN_OK)s=button(u,"应用",352,1110,300,true);
    }
    return s;
}
static pn_status_t show(pn_wallpaper_ui_t *ui,pn_wallpaper_present_fn present,void *ctx){
    ui_t *u=ui->impl;ui->presented=false;pn_status_t s=draw(u,ui->screen);
    if(s==PN_OK)s=present(ctx,&u->canvas,ui->screen==PN_WUI_PREVIEW?PN_REFRESH_GC16:PN_REFRESH_GL16);
    if(s==PN_OK)ui->presented=true;
    return s;
}
pn_status_t pn_wallpaper_ui_open(pn_wallpaper_ui_t *ui,pn_pool_t *pool,pn_media_t *images_media,const char *directory,
    const pn_wallpaper_store_t *store,pn_wallpaper_present_fn present,void *ctx){
    if(!ui || !pool || !images_media || !directory || !*directory || !present)return PN_INVALID;
    if(ui->impl)return PN_BUSY;
    if(strlen(directory)>=PN_CATALOG_PATH_MAX)return PN_LIMIT;
    ui_t *u=pn_alloc(pool,sizeof *u);if(!u)return PN_NO_MEMORY;memset(u,0,sizeof *u);u->pool=pool;u->images=images_media;strcpy(u->directory,directory);
    if(store){u->store=*store;u->has_store=true;}
    u->canvas_px=pn_alloc(pool,PN_WALLPAPER_BYTES);u->bitmap_px=pn_alloc(pool,PN_WALLPAPER_BYTES);u->small_px=pn_alloc(pool,(size_t)PW/2*PH);
    pn_status_t status=u->canvas_px && u->bitmap_px && u->small_px?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK){(void)pn_frame_bind(&u->canvas,u->canvas_px,PN_WALLPAPER_BYTES,W,H);(void)pn_frame_bind(&u->bitmap,u->bitmap_px,PN_WALLPAPER_BYTES,W,H);(void)pn_frame_bind(&u->small,u->small_px,(size_t)PW/2*PH,PW,PH);
        pn_text_source_t builtin=pn_font_builtin_source();status=pn_font_open(&u->font,pool,&builtin,28);}
    if(status!=PN_OK){pn_free(u->canvas_px);pn_free(u->bitmap_px);pn_free(u->small_px);pn_free(u);return status;}
    reload_saved(u);
    if(load_page(u,"",false)!=PN_OK){memset(&u->page,0,sizeof u->page);}
    *ui=(pn_wallpaper_ui_t){.impl=u,.active=true,.screen=PN_WUI_LIST,.last=PN_EMPTY};
    (void)show(ui,present,ctx);return PN_OK;
}
static pn_status_t apply(pn_wallpaper_ui_t *ui){
    ui_t *u=ui->impl;
    if(!u->has_store){snprintf(u->message,sizeof u->message,"内部壁纸分区不可用，锁屏未更改");return PN_UNSUPPORTED;}
    if(u->draft.mode==PN_LOCK_CUSTOM && !u->bitmap_valid){snprintf(u->message,sizeof u->message,"预览尚未生成，锁屏未更改");return PN_INVALID;}
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(u->store.media,PN_MEDIA_WRITE,&lease);
    pn_lock_selection_t record=u->draft;
    if(status==PN_OK){status=pn_wallpaper_save(&u->store,&lease,&record,u->draft.mode==PN_LOCK_CUSTOM?&u->bitmap:NULL);(void)pn_media_release(u->store.media,&lease);
        // 保存失败也可能已提交，以读回为准。/ A failed save may still have committed; trust the readback.
        reload_saved(u);}
    if(status==PN_OK){ui->screen=PN_WUI_LIST;snprintf(u->message,sizeof u->message,"已应用：%s",mode_name(&record));}
    else snprintf(u->message,sizeof u->message,"应用失败（%d），保留原锁屏",(int)status);
    return status;
}
static void begin_preview(ui_t *u,pn_lock_selection_t draft){u->draft=draft;u->message[0]=0;}
pn_status_t pn_wallpaper_ui_event(pn_wallpaper_ui_t *ui,int command,pn_wallpaper_present_fn present,void *ctx){
    if(!ui || !ui->impl || !ui->active || !present)return PN_INVALID;
    ui_t *u=ui->impl;pn_status_t status=PN_OK;bool rebuild=false;
    if(ui->screen==PN_WUI_LIST){
        u->message[0]=0;
        if(command==PN_WUI_BACK){ui->active=false;return PN_OK;}
        else if(command==PN_WUI_DEFAULT || command==PN_WUI_SIMPLE){begin_preview(u,(pn_lock_selection_t){.mode=command==PN_WUI_DEFAULT?PN_LOCK_DEFAULT:PN_LOCK_SIMPLE,.hint=true});rebuild=true;}
        else if(command>=PN_WUI_ROW && command<PN_WUI_ROW+(int)u->page.count){const pn_catalog_item_t *item=&u->page.items[command-PN_WUI_ROW];
            if(strlen(item->path)>=PN_WALLPAPER_PATH_MAX)return PN_LIMIT;
            pn_lock_selection_t d={.mode=PN_LOCK_CUSTOM,.hint=true,.transform={PN_WALLPAPER_CONTAIN,0,0},.source_size=item->size};strcpy(d.source,item->path);
            struct stat info;if(stat(item->path,&info)==0)d.source_mtime=(int64_t)info.st_mtime;
            if(u->saved_known && u->saved.mode==PN_LOCK_CUSTOM && !strcmp(u->saved.source,d.source)){d.transform=u->saved.transform;d.hint=u->saved.hint;}
            begin_preview(u,d);rebuild=true;}
        else if((command==PN_WUI_NEXT || command==PN_WUI_PREVIOUS) && u->page.count){char cursor[PN_CATALOG_NAME_MAX];strcpy(cursor,u->page.items[command==PN_WUI_NEXT?u->page.count-1:0].name);
            if(command==PN_WUI_PREVIOUS || u->page.more)status=load_page(u,cursor,command==PN_WUI_PREVIOUS);}
        else return PN_EMPTY;
    }else{
        bool custom=u->draft.mode==PN_LOCK_CUSTOM,cover=custom && u->draft.transform.fit==PN_WALLPAPER_COVER;
        if(command==PN_WUI_CANCEL){ui->screen=PN_WUI_LIST;u->message[0]=0;}
        else if(command==PN_WUI_APPLY){status=apply(ui);ui->last=status;}
        else if(command==PN_WUI_HINT){u->draft.hint=!u->draft.hint;rebuild=true;}
        else if(command==PN_WUI_FIT && custom){u->draft.transform.fit=cover?PN_WALLPAPER_CONTAIN:PN_WALLPAPER_COVER;u->draft.transform.shift=0;rebuild=true;}
        else if(command==PN_WUI_ROTATE && custom){u->draft.transform.rotation=(uint8_t)((u->draft.transform.rotation+1)&3);rebuild=true;}
        else if((command==PN_WUI_LEFT || command==PN_WUI_RIGHT) && cover){int shift=u->draft.transform.shift+(command==PN_WUI_LEFT?-1:1);
            if(shift<-PN_WALLPAPER_SHIFT_MAX || shift>PN_WALLPAPER_SHIFT_MAX)return PN_EMPTY;
            u->draft.transform.shift=(int8_t)shift;rebuild=true;}
        else return PN_EMPTY;
    }
    if(rebuild){status=build_preview(u);
        if(status==PN_OK){ui->screen=PN_WUI_PREVIEW;u->message[0]=0;}
        else{ui->screen=PN_WUI_LIST;snprintf(u->message,sizeof u->message,status==PN_NO_MEMORY?"图片过大，内存不足":status==PN_UNSUPPORTED?"不是可用的JPEG/PNG":"图片无法读取（%d）",(int)status);}}
    pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;
}
pn_status_t pn_wallpaper_ui_present(pn_wallpaper_ui_t *ui,pn_wallpaper_present_fn present,void *ctx){if(!ui || !ui->impl || !present)return PN_INVALID;return show(ui,present,ctx);}
int pn_wallpaper_ui_hit(const pn_wallpaper_ui_t *ui,int x,int y){
    if(!ui || !ui->impl || x<32 || x>=652)return -1;
    const ui_t *u=ui->impl;
    if(ui->screen==PN_WUI_LIST){
        if(y>=260 && y<350)return x<332?PN_WUI_DEFAULT:x>=352?PN_WUI_SIMPLE:-1;
        if(y>=450 && y<1050){int row=(y-450)/100;if((y-450)%100<90 && row<(int)u->page.count)return PN_WUI_ROW+row;return -1;}
        if(y>=1110 && y<1200)return x<222?PN_WUI_PREVIOUS:x>=247 && x<437?PN_WUI_BACK:x>=462?PN_WUI_NEXT:-1;
        return -1;
    }
    bool custom=u->draft.mode==PN_LOCK_CUSTOM;
    if(custom && y>=680 && y<770)return x<332?PN_WUI_FIT:x>=352?PN_WUI_ROTATE:-1;
    if(custom && u->draft.transform.fit==PN_WALLPAPER_COVER && y>=790 && y<880)return x<332?PN_WUI_LEFT:x>=352?PN_WUI_RIGHT:-1;
    if(y>=900 && y<990)return PN_WUI_HINT;
    if(y>=1110 && y<1200)return x<332?PN_WUI_CANCEL:x>=352?PN_WUI_APPLY:-1;
    return -1;
}
bool pn_wallpaper_ui_current(const pn_wallpaper_ui_t *ui,pn_lock_selection_t *out){if(!ui || !ui->impl || !out)return false;const ui_t *u=ui->impl;if(!u->saved_known)return false;*out=u->saved;return true;}
void pn_wallpaper_ui_close(pn_wallpaper_ui_t *ui){
    if(!ui || !ui->impl)return;
    ui_t *u=ui->impl;pn_font_close(&u->font);pn_free(u->canvas_px);pn_free(u->bitmap_px);pn_free(u->small_px);pn_free(u);
    *ui=(pn_wallpaper_ui_t){0};
}
