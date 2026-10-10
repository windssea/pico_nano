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
typedef struct {
    pn_pool_t *pool;pn_media_t *images;char directory[PN_CATALOG_PATH_MAX];
    pn_wallpaper_store_t store;bool has_store;
    pn_font_t font;uint8_t *canvas_px,*bitmap_px,*small_px;pn_frame_t canvas,bitmap,small;
    pn_catalog_page_t page;pn_lock_selection_t saved,draft;bool saved_known,bitmap_valid;
    pn_wallpaper_transform_t prepared;char prepared_path[PN_WALLPAPER_PATH_MAX];
    char message[96];
} ui_t;
static const char *base(const char *path){const char *slash=strrchr(path,'/');return slash?slash+1:path;}
static const char *mode_name(const pn_lock_selection_t *s){return s->mode==PN_LOCK_CUSTOM?"自定义图片":s->mode==PN_LOCK_SIMPLE?"简洁锁屏":s->mode==PN_LOCK_BOOK?"当前书封面":"系统默认";}
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
    // 当前书封面在锁屏时才取，预览用系统默认图占位。/ The book cover is fetched at lock time, so the preview uses the system default art as a placeholder.
    pn_lock_selection_t shown=u->draft;if(shown.mode==PN_LOCK_BOOK)shown.mode=PN_LOCK_DEFAULT;
    status=pn_lock_render(&shown,&u->font,"再按电源键继续阅读",&u->canvas);
    if(status==PN_OK){for(int y=0;y<PH;y++)for(int x=0;x<PW;x++){unsigned sum=pn_frame_get(&u->canvas,2*x,2*y)+pn_frame_get(&u->canvas,2*x+1,2*y)+pn_frame_get(&u->canvas,2*x,2*y+1)+pn_frame_get(&u->canvas,2*x+1,2*y+1);pn_frame_pixel(&u->small,x,y,(uint8_t)((sum+2)/4));}}
    return status;
}
/* 版式：列表页=顶栏、三个模式按钮、图片行、翻页；预览页=顶栏（取消/应用）、半尺寸预览、调整按钮。
 * Layout: list page = header, three mode buttons, image rows, paging; preview page = header (cancel/apply), half-size preview and adjustment buttons. */
#define MODE_BUTTON_Y 196
#define IMAGE_SECTION_Y 352
#define IMAGE_ROW_Y 368
#define IMAGE_ROWS 6
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
        if(s==PN_OK){if(!u->saved_known)snprintf(line,sizeof line,"当前：系统默认（内部记录不可用）");else if(u->saved.mode==PN_LOCK_CUSTOM)snprintf(line,sizeof line,"当前：%s",base(u->saved.source));else snprintf(line,sizeof line,"当前：%s",mode_name(&u->saved));s=line_text(u,line,28,168,PN_ALIGN_LEFT);}
        const pn_lock_mode_t modes[3]={PN_LOCK_DEFAULT,PN_LOCK_SIMPLE,PN_LOCK_BOOK};const char *labels[3]={"系统默认","简洁锁屏","当前书封面"};
        for(int i=0;i<3 && s==PN_OK;i++){bool current=u->saved_known?u->saved.mode==modes[i]:modes[i]==PN_LOCK_DEFAULT;s=sized_button(u,labels[i],32+i*215,MODE_BUTTON_Y,190,96,current?PN_W_SELECTED:0u);}
        if(s==PN_OK)s=pn_w_section(&u->font,&u->canvas,u->page.count?"自定义图片":"自定义图片（wallpapers 目录暂无 JPEG/PNG）",IMAGE_SECTION_Y);
        for(size_t i=0;i<u->page.count && i<IMAGE_ROWS && s==PN_OK;i++)s=pn_w_row_icon(&u->font,&u->canvas,u->page.items[i].name,NULL,PN_ICON_IMAGE,PN_ROW_CHEVRON,IMAGE_ROW_Y+(int)i*PN_W_ROW_H);
        if(s==PN_OK && *u->message)s=line_text(u,u->message,28,PAGER_Y-24,PN_ALIGN_LEFT);
        if(s==PN_OK)s=sized_button(u,"上一页",32,PAGER_Y,196,80,0u);
        if(s==PN_OK)s=sized_button(u,"下一页",456,PAGER_Y,196,80,0u);
    }else{
        bool custom=u->draft.mode==PN_LOCK_CUSTOM,cover=custom && u->draft.transform.fit==PN_WALLPAPER_COVER;
        s=pn_w_header(&u->font,&u->canvas,"< 返回","预览",u->has_store?"应用":"应用");
        for(int y=0;y<PH;y++)for(int x=0;x<PW;x++)pn_frame_pixel(&u->canvas,PX+x,PY+y,pn_frame_get(&u->small,x,y));
        pn_w_outline(&u->canvas,PX-2,PY-2,PW+4,PH+4,2,PN_UI_INK);
        int row=0;
        if(custom && s==PN_OK){s=sized_button(u,u->draft.transform.fit==PN_WALLPAPER_COVER?"铺满裁切":"完整显示",32,CONTROL_Y0,300,80,0u);
            if(s==PN_OK)s=sized_button(u,"旋转 90°",352,CONTROL_Y0,300,80,0u);
            row=1;
            if(s==PN_OK && cover){s=sized_button(u,"裁切位置 -",32,CONTROL_Y0+CONTROL_PITCH,300,80,0u);if(s==PN_OK)s=sized_button(u,"裁切位置 +",352,CONTROL_Y0+CONTROL_PITCH,300,80,0u);row=2;}}
        if(s==PN_OK)s=sized_button(u,u->draft.hint?"底部提示：开":"底部提示：关",32,CONTROL_Y0+row*CONTROL_PITCH,620,80,0u);
        if(s==PN_OK){snprintf(line,sizeof line,"预览未保存：%s",custom?base(u->draft.source):mode_name(&u->draft));s=line_text(u,*u->message?u->message:line,26,1190,PN_ALIGN_LEFT);}
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
        else if(command==PN_WUI_DEFAULT || command==PN_WUI_SIMPLE || command==PN_WUI_BOOK){begin_preview(u,(pn_lock_selection_t){.mode=command==PN_WUI_DEFAULT?PN_LOCK_DEFAULT:command==PN_WUI_BOOK?PN_LOCK_BOOK:PN_LOCK_SIMPLE,.hint=true});rebuild=true;}
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
    if(!ui || !ui->impl || x<0 || x>=W)return -1;
    const ui_t *u=ui->impl;
    int header=pn_w_header_hit(x,y,ui->screen==PN_WUI_PREVIEW);
    if(ui->screen==PN_WUI_LIST){
        if(header==1)return PN_WUI_BACK;
        if(x<32 || x>=652)return -1;
        if(y>=MODE_BUTTON_Y && y<MODE_BUTTON_Y+96)return x<222?PN_WUI_DEFAULT:x>=247 && x<437?PN_WUI_SIMPLE:x>=462?PN_WUI_BOOK:-1;
        if(y>=IMAGE_ROW_Y && y<IMAGE_ROW_Y+IMAGE_ROWS*PN_W_ROW_H){int row=(y-IMAGE_ROW_Y)/PN_W_ROW_H;return row<(int)u->page.count?PN_WUI_ROW+row:-1;}
        if(y>=PAGER_Y && y<PAGER_Y+80)return x<228?PN_WUI_PREVIOUS:x>=456?PN_WUI_NEXT:-1;
        return -1;
    }
    if(header==1)return PN_WUI_CANCEL;
    if(header==2)return PN_WUI_APPLY;
    if(x<32 || x>=652)return -1;
    bool custom=u->draft.mode==PN_LOCK_CUSTOM,cover=custom && u->draft.transform.fit==PN_WALLPAPER_COVER;
    int row=0;
    if(custom){
        if(y>=CONTROL_Y0 && y<CONTROL_Y0+80)return x<332?PN_WUI_FIT:x>=352?PN_WUI_ROTATE:-1;
        row=1;
        if(cover){if(y>=CONTROL_Y0+CONTROL_PITCH && y<CONTROL_Y0+CONTROL_PITCH+80)return x<332?PN_WUI_LEFT:x>=352?PN_WUI_RIGHT:-1;row=2;}
    }
    if(y>=CONTROL_Y0+row*CONTROL_PITCH && y<CONTROL_Y0+row*CONTROL_PITCH+80)return PN_WUI_HINT;
    return -1;
}
bool pn_wallpaper_ui_current(const pn_wallpaper_ui_t *ui,pn_lock_selection_t *out){if(!ui || !ui->impl || !out)return false;const ui_t *u=ui->impl;if(!u->saved_known)return false;*out=u->saved;return true;}
void pn_wallpaper_ui_close(pn_wallpaper_ui_t *ui){
    if(!ui || !ui->impl)return;
    ui_t *u=ui->impl;pn_font_close(&u->font);pn_free(u->canvas_px);pn_free(u->bitmap_px);pn_free(u->small_px);pn_free(u);
    *ui=(pn_wallpaper_ui_t){0};
}
