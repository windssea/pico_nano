/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：字体管理页状态机；READ浏览与预览，WRITE删除TF文件或保存全局字体记录。
 * English: font-management state machine; READ browsing and previews, WRITE deletion of TF files or saving the global font record.
 * 冻结：删除前关闭本页资源；写失败以读回为准；从不修改逐书记录。
 * Frozen: close this page's resources before deleting; trust readback after write failures; never modify per-book records.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_font_manage.h"
#include "pn_font_preview.h"
#include "pn_widgets.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define W 684
#define H 1216
typedef struct {
    pn_pool_t *pool;pn_media_t *fonts,*state;char font_dir[PN_CATALOG_PATH_MAX],state_dir[PN_JOURNAL_PATH_MAX];
    pn_font_t ui;uint8_t *pixels;pn_frame_t canvas;pn_catalog_page_t page;
    pn_font_preferences_t global;bool global_known,global_set;
    pn_media_lease_t guard;pn_font_asset_t asset;pn_font_reference_t reference;pn_font_info_t info;unsigned checked,missing;
    char selected[PN_CATALOG_PATH_MAX],name[PN_CATALOG_NAME_MAX],message[128];
} fm_t;
static pn_status_t string_read(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t size=strlen(s);if(off>size)return PN_INVALID;size_t take=size-(size_t)off;if(take>cap)take=cap;memcpy(out,s+off,take);*n=take;return PN_OK;}
static const char *base(const char *path){const char *slash=strrchr(path,'/');return slash?slash+1:path;}
static bool is_global(const fm_t *f,const char *path){return f->global_known && f->global_set && f->global.primary.kind==PN_FONT_FILE && !strcmp(f->global.primary.path,path);}
static void release_asset(fm_t *f){
    (void)pn_font_asset_close(&f->asset);
    if(f->guard.ticket){(void)pn_media_release(f->fonts,&f->guard);f->guard=(pn_media_lease_t){0};}
}
/* 读全局记录；读不到保留已知值。/ Read the global record; keep known values when unreadable. */
static void load_global(fm_t *f){
    if(!f->state)return;
    pn_media_lease_t lease;
    if(pn_media_acquire(f->state,PN_MEDIA_READ,&lease)!=PN_OK)return;
    pn_journal_files_t files;pn_journal_io_t io;pn_font_preferences_t g;
    pn_status_t status=pn_font_preferences_files(&files,f->state,&lease,f->state_dir,NULL,&io);
    if(status==PN_OK)status=pn_font_preferences_load(&io,f->pool,NULL,&g);
    (void)pn_media_release(f->state,&lease);
    if(status==PN_OK){f->global=g;f->global_known=f->global_set=true;}
    else if(status==PN_EMPTY){f->global_known=true;f->global_set=false;}
    else if(status==PN_CORRUPT || status==PN_UNSUPPORTED)f->global_known=false;
}
static pn_status_t load_page(fm_t *f,const char *cursor,bool previous){
    pn_catalog_page_t *next=pn_alloc(f->pool,sizeof *next);
    if(!next)return PN_NO_MEMORY;
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(f->fonts,PN_MEDIA_READ,&lease);
    if(status==PN_OK){status=previous?pn_catalog_font_page_before(f->fonts,&lease,f->font_dir,cursor,next):pn_catalog_font_page(f->fonts,&lease,f->font_dir,cursor,next);(void)pn_media_release(f->fonts,&lease);}
    if(status==PN_OK && (next->count || !*cursor))f->page=*next;
    pn_free(next);return status;
}
/* 打开候选并做样例检测；失败释放全部资源。/ Open the candidate and run the sample check; failures release everything. */
static pn_status_t inspect(fm_t *f,const pn_catalog_item_t *item){
    pn_status_t status=pn_media_acquire(f->fonts,PN_MEDIA_READ,&f->guard);
    if(status!=PN_OK){f->guard=(pn_media_lease_t){0};return status;}
    status=pn_font_asset_open(&f->asset,f->pool,f->fonts,&f->guard,item->path,item->size,40);
    const char *sample=pn_font_preview_text();pn_text_source_t source={(void *)sample,strlen(sample),string_read,NULL};pn_text_reader_t reader;uint32_t points[128];size_t count=0;pn_text_char_t c;
    if(status==PN_OK)status=pn_text_open(&reader,&source,PN_TEXT_UTF8);
    while(status==PN_OK && count<128){pn_status_t next=pn_text_next(&reader,&c);if(next==PN_EMPTY)break;if(next!=PN_OK){status=next;break;}if(c.codepoint!=10 && c.codepoint!=32)points[count++]=c.codepoint;}
    unsigned missing=0;
    if(status==PN_OK)status=pn_font_asset_sample(&f->asset,points,count,&missing);
    if(status==PN_OK)status=pn_font_asset_details(&f->asset,&f->reference,&f->info);
    if(status!=PN_OK){release_asset(f);return status;}
    f->checked=(unsigned)count;f->missing=missing;strcpy(f->selected,item->path);strcpy(f->name,item->name);return PN_OK;
}
/* 版式：列表页每行88px，底部上一页/下一页；详情页下半部为名称与两个按钮；确认页为说明与取消/确认。
 * Layout: list rows are 88 px with paging buttons at the bottom; the detail page puts the name and two buttons in its lower half; the confirmation page has a note and cancel/confirm. */
#define LIST_ROW_Y 204
#define LIST_ROWS 6
#define PAGER_Y 1084
#define DETAIL_NAME_Y 884
#define DETAIL_BUTTON_Y 924
#define DETAIL_BACK_Y 1044
#define CONFIRM_BUTTON_Y 1084
static pn_status_t draw(pn_font_manage_t *ui){
    fm_t *f=ui->impl;pn_frame_clear(&f->canvas,15);pn_status_t s=PN_OK;char line[PN_CATALOG_PATH_MAX+128];
    if(ui->screen==PN_FMU_LIST){
        s=pn_w_header(&f->ui,&f->canvas,"< 返回","字体管理",NULL);
        if(s==PN_OK){if(!f->global_known)snprintf(line,sizeof line,"全局默认：记录不可读");else if(!f->global_set)snprintf(line,sizeof line,"全局默认：未设置");else if(f->global.primary.kind==PN_FONT_RESIDENT)snprintf(line,sizeof line,"全局默认：内置界面字体");else snprintf(line,sizeof line,"全局默认：%s",f->global.primary.path);
            int original=f->ui.pixels;s=pn_font_size(&f->ui,28);if(s==PN_OK)s=pn_w_text(&f->ui,&f->canvas,line,PN_UI_MARGIN,168,620,PN_ALIGN_LEFT);pn_status_t restored=pn_font_size(&f->ui,original);if(s==PN_OK)s=restored;}
        for(size_t i=0;i<f->page.count && i<LIST_ROWS && s==PN_OK;i++){const pn_catalog_item_t *item=&f->page.items[i];
            snprintf(line,sizeof line,"%llu KB%s",(unsigned long long)((item->size+1023)/1024),is_global(f,item->path)?" · 全局默认":"");
            s=pn_w_row(&f->ui,&f->canvas,item->name,line,true,LIST_ROW_Y+(int)i*PN_W_ROW_H);}
        if(s==PN_OK && !f->page.count){int original=f->ui.pixels;s=pn_font_size(&f->ui,30);if(s==PN_OK)s=pn_w_text_lines(&f->ui,&f->canvas,"fonts 目录里还没有 TTF 字体。点底栏“传书”，用手机把字体传进来。",PN_UI_MARGIN,LIST_ROW_Y+48,620,3,44,NULL);pn_status_t restored=pn_font_size(&f->ui,original);if(s==PN_OK)s=restored;}
        if(s==PN_OK && *f->message){int original=f->ui.pixels;s=pn_font_size(&f->ui,28);if(s==PN_OK)s=pn_w_text(&f->ui,&f->canvas,f->message,PN_UI_MARGIN,PAGER_Y-24,620,PN_ALIGN_LEFT);pn_status_t restored=pn_font_size(&f->ui,original);if(s==PN_OK)s=restored;}
        if(s==PN_OK){int original=f->ui.pixels;s=pn_font_size(&f->ui,30);
            if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"上一页",32,PAGER_Y,196,80,0u);
            if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"下一页",456,PAGER_Y,196,80,0u);
            pn_status_t restored=pn_font_size(&f->ui,original);if(s==PN_OK)s=restored;}
    }else if(ui->screen==PN_FMU_DETAIL){
        pn_font_t *body=pn_font_asset_font(&f->asset);int original=f->ui.pixels;
        s=pn_font_size(&f->ui,36);
        if(s==PN_OK)s=pn_font_preview_draw(&f->ui,body,&f->info,f->reference.size,f->checked,f->missing,&f->canvas);
        pn_status_t restored=pn_font_size(&f->ui,original);if(s==PN_OK)s=restored;
        if(s==PN_OK){pn_frame_rect(&f->canvas,0,DETAIL_NAME_Y-48,W,H-(DETAIL_NAME_Y-48),15);pn_frame_rect(&f->canvas,32,DETAIL_NAME_Y-44,620,2,PN_UI_RULE);}
        if(s==PN_OK){s=pn_font_size(&f->ui,34);if(s==PN_OK)s=pn_w_text(&f->ui,&f->canvas,f->name,PN_UI_MARGIN,DETAIL_NAME_Y,620,PN_ALIGN_LEFT);pn_status_t again=pn_font_size(&f->ui,original);if(s==PN_OK)s=again;}
        if(s==PN_OK){s=pn_font_size(&f->ui,30);
            if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,is_global(f,f->selected)?"已是全局默认":"设为全局默认",32,DETAIL_BUTTON_Y,300,96,0u);
            if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"删除",352,DETAIL_BUTTON_Y,300,96,0u);
            if(s==PN_OK && *f->message){s=pn_font_size(&f->ui,28);if(s==PN_OK)s=pn_w_text(&f->ui,&f->canvas,f->message,PN_UI_MARGIN,DETAIL_BACK_Y-20,620,PN_ALIGN_LEFT);}
            if(s==PN_OK){s=pn_font_size(&f->ui,30);if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"返回",32,DETAIL_BACK_Y,620,96,0u);}
            pn_status_t again=pn_font_size(&f->ui,original);if(s==PN_OK)s=again;}
    }else{
        s=pn_w_header(&f->ui,&f->canvas,"","删除字体？",NULL);
        int original=f->ui.pixels;
        if(s==PN_OK)s=pn_font_size(&f->ui,36);
        if(s==PN_OK)s=pn_w_text_lines(&f->ui,&f->canvas,f->name,PN_UI_MARGIN,220,620,2,48,NULL);
        if(s==PN_OK)s=pn_font_size(&f->ui,30);
        if(s==PN_OK)s=pn_w_text_lines(&f->ui,&f->canvas,"使用它的书将暂用默认字体；阅读位置与字体选择记录都会保留。删除后文件无法恢复。",PN_UI_MARGIN,360,620,4,46,NULL);
        if(s==PN_OK && is_global(f,f->selected))s=pn_w_text(&f->ui,&f->canvas,"这是当前的全局默认字体。",PN_UI_MARGIN,580,620,PN_ALIGN_LEFT);
        if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"取消",32,CONFIRM_BUTTON_Y,300,96,0u);
        if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"确认删除",352,CONFIRM_BUTTON_Y,300,96,PN_W_SELECTED);
        pn_status_t restored=pn_font_size(&f->ui,original);if(s==PN_OK)s=restored;
    }
    return s;
}
static pn_status_t show(pn_font_manage_t *ui,pn_font_manage_present_fn present,void *ctx){
    fm_t *f=ui->impl;ui->presented=false;pn_status_t s=draw(ui);
    if(s==PN_OK)s=present(ctx,&f->canvas,ui->screen==PN_FMU_DETAIL?PN_REFRESH_GC16:PN_REFRESH_GL16);
    if(s==PN_OK)ui->presented=true;
    return s;
}
pn_status_t pn_font_manage_open(pn_font_manage_t *ui,pn_pool_t *pool,pn_media_t *font_media,const char *font_dir,
    pn_media_t *state_media,const char *state_dir,pn_font_manage_present_fn present,void *ctx){
    if(!ui || !pool || !font_media || !font_dir || !*font_dir || !present || (state_media && (!state_dir || !*state_dir)))return PN_INVALID;
    if(ui->impl)return PN_BUSY;
    if(strlen(font_dir)>=PN_CATALOG_PATH_MAX || (state_dir && strlen(state_dir)>=PN_JOURNAL_PATH_MAX))return PN_LIMIT;
    fm_t *f=pn_alloc(pool,sizeof *f);
    if(!f)return PN_NO_MEMORY;
    memset(f,0,sizeof *f);f->pool=pool;f->fonts=font_media;f->state=state_media;strcpy(f->font_dir,font_dir);
    if(state_media)strcpy(f->state_dir,state_dir);
    f->pixels=pn_alloc(pool,(size_t)W/2*H);
    pn_status_t status=f->pixels?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK){(void)pn_frame_bind(&f->canvas,f->pixels,(size_t)W/2*H,W,H);pn_text_source_t builtin=pn_font_builtin_source();status=pn_font_open(&f->ui,pool,&builtin,28);}
    if(status!=PN_OK){pn_free(f->pixels);pn_free(f);return status;}
    load_global(f);
    if(load_page(f,"",false)!=PN_OK)memset(&f->page,0,sizeof f->page);
    *ui=(pn_font_manage_t){.impl=f,.active=true,.screen=PN_FMU_LIST,.last=PN_EMPTY};
    (void)show(ui,present,ctx);return PN_OK;
}
/* 全局默认：正文换成本字体，保留已有备用。/ Global default: the primary becomes this font and any existing fallback is kept. */
static pn_status_t set_default(fm_t *f){
    if(!f->state){snprintf(f->message,sizeof f->message,"内部存储不可用，未更改");return PN_UNSUPPORTED;}
    if(!f->global_known){snprintf(f->message,sizeof f->message,"全局记录不可读，未覆盖");return PN_CORRUPT;}
    pn_font_preferences_t g={0};g.primary=f->reference;g.fallback=f->global_set?f->global.fallback:(pn_font_reference_t){.kind=PN_FONT_RESIDENT};
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(f->state,PN_MEDIA_WRITE,&lease);
    if(status==PN_OK){pn_journal_files_t files;pn_journal_io_t io;status=pn_font_preferences_files(&files,f->state,&lease,f->state_dir,NULL,&io);
        if(status==PN_OK)status=pn_font_preferences_save(&io,f->pool,NULL,&g);
        (void)pn_media_release(f->state,&lease);
        load_global(f);}
    snprintf(f->message,sizeof f->message,status==PN_OK?"已设为全局默认，未单独设置的书将使用它":"保存失败（%d），全局默认以读回为准",(int)status);
    return status;
}
static pn_status_t remove_font(pn_font_manage_t *ui){
    fm_t *f=ui->impl;char path[PN_CATALOG_PATH_MAX];strcpy(path,f->selected);
    release_asset(f);
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(f->fonts,PN_MEDIA_WRITE,&lease);
    if(status==PN_OK){if(remove(path))status=errno==ENOENT?PN_EMPTY:PN_IO;
        pn_status_t valid=pn_media_validate(f->fonts,&lease);if(status==PN_OK)status=valid;
        (void)pn_media_release(f->fonts,&lease);}
    ui->screen=PN_FMU_LIST;
    if(load_page(f,"",false)!=PN_OK)memset(&f->page,0,sizeof f->page);
    if(status==PN_OK){
        // 文件名放进提示前按UTF-8字符边界截短，不会截断半个汉字。/ Clip the file name at a UTF-8 character boundary so no character is cut in half.
        char shown[96];const char *name=base(path);size_t n=strlen(name);
        if(n>=sizeof shown){n=sizeof shown-1;while(n>0 && ((unsigned char)name[n]&0xc0)==0x80)n--;}
        memcpy(shown,name,n);shown[n]=0;
        snprintf(f->message,sizeof f->message,"已删除：%.95s",shown);
    }
    else snprintf(f->message,sizeof f->message,"删除失败（%d），文件保留",(int)status);
    return status;
}
pn_status_t pn_font_manage_event(pn_font_manage_t *ui,int command,pn_font_manage_present_fn present,void *ctx){
    if(!ui || !ui->impl || !ui->active || !present)return PN_INVALID;
    fm_t *f=ui->impl;pn_status_t status=PN_OK;
    if(ui->screen==PN_FMU_LIST){
        f->message[0]=0;
        if(command==PN_FMU_BACK){ui->active=false;return PN_OK;}
        if(command>=PN_FMU_ROW && command<PN_FMU_ROW+(int)f->page.count){
            status=inspect(f,&f->page.items[command-PN_FMU_ROW]);
            if(status==PN_OK)ui->screen=PN_FMU_DETAIL;
            else snprintf(f->message,sizeof f->message,status==PN_NO_MEMORY?"字体过大，内存不足":"不是可用的TrueType字体（%d）",(int)status);}
        else if((command==PN_FMU_NEXT || command==PN_FMU_PREVIOUS) && f->page.count){char cursor[PN_CATALOG_NAME_MAX];strcpy(cursor,f->page.items[command==PN_FMU_NEXT?f->page.count-1:0].name);
            if(command==PN_FMU_PREVIOUS || f->page.more)status=load_page(f,cursor,command==PN_FMU_PREVIOUS);}
        else return PN_EMPTY;
    }else if(ui->screen==PN_FMU_DETAIL){
        if(command==PN_FMU_BACK){release_asset(f);ui->screen=PN_FMU_LIST;f->message[0]=0;}
        else if(command==PN_FMU_DEFAULT){if(is_global(f,f->selected))return PN_EMPTY;status=set_default(f);ui->last=status;}
        else if(command==PN_FMU_DELETE){ui->screen=PN_FMU_CONFIRMING;f->message[0]=0;}
        else return PN_EMPTY;
    }else{
        if(command==PN_FMU_CANCEL)ui->screen=PN_FMU_DETAIL;
        else if(command==PN_FMU_CONFIRM){status=remove_font(ui);ui->last=status;}
        else return PN_EMPTY;
    }
    pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;
}
pn_status_t pn_font_manage_present(pn_font_manage_t *ui,pn_font_manage_present_fn present,void *ctx){if(!ui || !ui->impl || !present)return PN_INVALID;return show(ui,present,ctx);}
int pn_font_manage_hit(const pn_font_manage_t *ui,int x,int y){
    if(!ui || !ui->impl || x<0 || x>=W)return -1;
    const fm_t *f=ui->impl;
    if(ui->screen==PN_FMU_LIST){
        if(pn_w_header_hit(x,y,false)==1)return PN_FMU_BACK;
        if(x<32 || x>=652)return -1;
        if(y>=LIST_ROW_Y && y<LIST_ROW_Y+LIST_ROWS*PN_W_ROW_H){int row=(y-LIST_ROW_Y)/PN_W_ROW_H;return row<(int)f->page.count?PN_FMU_ROW+row:-1;}
        if(y>=PAGER_Y && y<PAGER_Y+80)return x<228?PN_FMU_PREVIOUS:x>=456?PN_FMU_NEXT:-1;
        return -1;
    }
    if(x<32 || x>=652)return -1;
    if(ui->screen==PN_FMU_DETAIL){
        if(y>=DETAIL_BUTTON_Y && y<DETAIL_BUTTON_Y+96)return x<332?PN_FMU_DEFAULT:x>=352?PN_FMU_DELETE:-1;
        if(y>=DETAIL_BACK_Y && y<DETAIL_BACK_Y+96)return PN_FMU_BACK;
        return -1;
    }
    if(y>=CONFIRM_BUTTON_Y && y<CONFIRM_BUTTON_Y+96)return x<332?PN_FMU_CANCEL:x>=352?PN_FMU_CONFIRM:-1;
    return -1;
}
void pn_font_manage_close(pn_font_manage_t *ui){
    if(!ui || !ui->impl)return;
    fm_t *f=ui->impl;release_asset(f);pn_font_close(&f->ui);pn_free(f->pixels);pn_free(f);
    *ui=(pn_font_manage_t){0};
}
