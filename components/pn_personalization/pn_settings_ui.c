/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设置页绘制与开关保存；保存走内部状态介质WRITE租约。
 * English: settings page drawing and switch saving through a WRITE lease on the internal state media.
 */
#include "pn_settings_ui.h"
#include <stdio.h>
#include <string.h>
#define W 684
#define H 1216
typedef struct {pn_pool_t *pool;pn_media_t *state;char dir[PN_JOURNAL_PATH_MAX];pn_font_t font;uint8_t *pixels;pn_frame_t canvas;char message[96];} su_t;
static const char *const names[]={"左手模式（左边缘下一页）","滑动翻页","边缘点按翻页","屏下三键翻页"};
static const uint8_t bits[]={PN_INPUT_LEFT_HAND,PN_INPUT_NO_SWIPE,PN_INPUT_NO_EDGE_TAP,PN_INPUT_NO_KEYS};
static bool on(uint8_t flags,unsigned i){bool set=(flags&bits[i])!=0;return i==0?set:!set;}
static pn_status_t string_read(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t size=strlen(s);if(off>size)return PN_INVALID;size_t take=size-(size_t)off;if(take>cap)take=cap;memcpy(out,s+off,take);*n=take;return PN_OK;}
static pn_status_t text(su_t *u,const char *s,int x,int y,int width){
    pn_font_t *font=&u->font;pn_text_source_t source={(void *)s,strlen(s),string_read,NULL};pn_text_reader_t reader;pn_status_t status=pn_text_open(&reader,&source,PN_TEXT_UTF8);
    if(status!=PN_OK)return status;
    pn_text_char_t c;int32_t cursor=x*64,limit=(x+width)*64;
    while((status=pn_text_next(&reader,&c))==PN_OK){int32_t advance;pn_status_t measured=pn_font_advance(font,c.codepoint,&advance);
        if(measured==PN_EMPTY)advance=font->pixels*64;
        else if(measured!=PN_OK)return measured;
        if(advance<0)return PN_LIMIT;
        if(advance>limit-cursor)break;
        if(measured!=PN_EMPTY){status=pn_font_draw(font,&u->canvas,c.codepoint,cursor,y,PN_FONT_GRAY);if(status!=PN_OK)return status;}
        cursor+=advance;
    }
    return status==PN_EMPTY?PN_OK:status;
}
static void box(pn_frame_t *f,int x,int y,int w,int h,uint8_t shade){pn_frame_rect(f,x,y,w,1,shade);pn_frame_rect(f,x,y+h-1,w,1,shade);pn_frame_rect(f,x,y,1,h,shade);pn_frame_rect(f,x+w-1,y,1,h,shade);}
static pn_status_t row(su_t *u,const char *label,const char *value,int y){
    box(&u->canvas,32,y,620,96,5);pn_status_t s=text(u,label,56,y+60,440);
    if(s==PN_OK && value)s=text(u,value,560,y+60,80);
    return s;
}
static pn_status_t show(pn_settings_ui_t *ui,pn_settings_present_fn present,void *ctx){
    su_t *u=ui->impl;ui->presented=false;pn_frame_clear(&u->canvas,15);
    pn_status_t s=text(u,"小纸 Pico",32,64,620);
    if(s==PN_OK)s=text(u,"设置",32,150,620);
    if(s==PN_OK)s=row(u,"锁屏壁纸",">",200);
    if(s==PN_OK)s=row(u,"字体管理",">",310);
    if(s==PN_OK)s=text(u,"翻页",32,460,620);
    for(unsigned i=0;i<4 && s==PN_OK;i++)s=row(u,names[i],on(ui->flags,i)?"开":"关",490+(int)i*110);
    if(s==PN_OK)s=text(u,*u->message?u->message:(u->state?"开关立即保存":"内部存储不可用，开关不能保存"),32,1000,620);
    if(s==PN_OK){box(&u->canvas,32,1110,620,90,5);s=text(u,"返回",300,1168,200);}
    if(s==PN_OK)s=present(ctx,&u->canvas,PN_REFRESH_GL16);
    if(s==PN_OK)ui->presented=true;
    return s;
}
pn_status_t pn_settings_load_flags(pn_media_t *state,const char *dir,uint8_t *flags){
    if(!state || !dir || !*dir || !flags)return PN_INVALID;
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(state,PN_MEDIA_READ,&lease);
    if(status!=PN_OK)return status;
    pn_journal_files_t files;pn_journal_io_t io;uint8_t value=0;
    status=pn_input_prefs_files(&files,state,&lease,dir,&io);
    if(status==PN_OK)status=pn_input_prefs_load(&io,&value);
    (void)pn_media_release(state,&lease);
    if(status==PN_EMPTY){value=0;status=PN_OK;}
    if(status==PN_OK)*flags=value;
    return status;
}
pn_status_t pn_settings_ui_open(pn_settings_ui_t *ui,pn_pool_t *pool,pn_media_t *state,const char *dir,pn_settings_present_fn present,void *ctx){
    if(!ui || !pool || !present || (state && (!dir || !*dir)))return PN_INVALID;
    if(ui->impl)return PN_BUSY;
    if(dir && strlen(dir)>=PN_JOURNAL_PATH_MAX)return PN_LIMIT;
    su_t *u=pn_alloc(pool,sizeof *u);
    if(!u)return PN_NO_MEMORY;
    memset(u,0,sizeof *u);u->pool=pool;u->state=state;
    if(state)strcpy(u->dir,dir);
    u->pixels=pn_alloc(pool,(size_t)W/2*H);
    pn_status_t status=u->pixels?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK){(void)pn_frame_bind(&u->canvas,u->pixels,(size_t)W/2*H,W,H);pn_text_source_t builtin=pn_font_builtin_source();status=pn_font_open(&u->font,pool,&builtin,30);}
    if(status!=PN_OK){pn_free(u->pixels);pn_free(u);return status;}
    uint8_t flags=0;
    if(state && pn_settings_load_flags(state,dir,&flags)!=PN_OK){flags=0;snprintf(u->message,sizeof u->message,"翻页记录不可读，显示默认值");}
    *ui=(pn_settings_ui_t){.impl=u,.active=true,.flags=flags};
    (void)show(ui,present,ctx);return PN_OK;
}
static pn_status_t save(su_t *u,uint8_t flags){
    if(!u->state)return PN_UNSUPPORTED;
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(u->state,PN_MEDIA_WRITE,&lease);
    if(status!=PN_OK)return status;
    pn_journal_files_t files;pn_journal_io_t io;
    status=pn_input_prefs_files(&files,u->state,&lease,u->dir,&io);
    if(status==PN_OK)status=pn_input_prefs_save(&io,flags);
    (void)pn_media_release(u->state,&lease);return status;
}
pn_status_t pn_settings_ui_event(pn_settings_ui_t *ui,int command,pn_settings_present_fn present,void *ctx){
    if(!ui || !ui->impl || !ui->active || !present)return PN_INVALID;
    su_t *u=ui->impl;u->message[0]=0;pn_status_t status=PN_OK;
    if(command==PN_SETUI_BACK){ui->active=false;return PN_OK;}
    if(command==PN_SETUI_WALLPAPER || command==PN_SETUI_FONTS){ui->request=command;return PN_OK;}
    if(command<PN_SETUI_TOGGLE || command>PN_SETUI_TOGGLE+3)return PN_EMPTY;
    uint8_t next=(uint8_t)(ui->flags^bits[command-PN_SETUI_TOGGLE]);
    // 关闭全部翻页方式会让正文无法翻页，拒绝。/ Turning every page-turn method off would leave no way to turn pages, so refuse it.
    if((next&(PN_INPUT_NO_SWIPE|PN_INPUT_NO_EDGE_TAP|PN_INPUT_NO_KEYS))==(PN_INPUT_NO_SWIPE|PN_INPUT_NO_EDGE_TAP|PN_INPUT_NO_KEYS)){snprintf(u->message,sizeof u->message,"至少保留一种翻页方式");status=PN_LIMIT;}
    else{status=save(u,next);
        if(status==PN_OK)ui->flags=next;
        else{uint8_t reread;if(u->state && pn_settings_load_flags(u->state,u->dir,&reread)==PN_OK)ui->flags=reread;snprintf(u->message,sizeof u->message,"保存失败（%d），以当前显示为准",(int)status);}}
    pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;
}
pn_status_t pn_settings_ui_present(pn_settings_ui_t *ui,pn_settings_present_fn present,void *ctx){if(!ui || !ui->impl || !present)return PN_INVALID;return show(ui,present,ctx);}
int pn_settings_ui_hit(const pn_settings_ui_t *ui,int x,int y){
    if(!ui || !ui->impl || x<32 || x>=652)return -1;
    if(y>=200 && y<296)return PN_SETUI_WALLPAPER;
    if(y>=310 && y<406)return PN_SETUI_FONTS;
    if(y>=490 && y<930 && (y-490)%110<96)return PN_SETUI_TOGGLE+(y-490)/110;
    if(y>=1110 && y<1200)return PN_SETUI_BACK;
    return -1;
}
void pn_settings_ui_close(pn_settings_ui_t *ui){
    if(!ui || !ui->impl)return;
    su_t *u=ui->impl;pn_font_close(&u->font);pn_free(u->pixels);pn_free(u);*ui=(pn_settings_ui_t){0};
}
