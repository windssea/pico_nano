/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设置页绘制与开关保存；保存走内部状态介质WRITE租约。
 * English: settings page drawing and switch saving through a WRITE lease on the internal state media.
 */
#include "pn_settings_ui.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <string.h>
#define W 684
#define H 1216
typedef struct {pn_pool_t *pool;pn_media_t *state;char dir[PN_JOURNAL_PATH_MAX];pn_font_t font;uint8_t *pixels;pn_frame_t canvas;char message[96];char lan[40];} su_t;
static const char *const names[]={"左手模式（左边缘下一页）","滑动翻页","边缘点按翻页","屏下三键翻页"};
static const uint8_t bits[]={PN_INPUT_LEFT_HAND,PN_INPUT_NO_SWIPE,PN_INPUT_NO_EDGE_TAP,PN_INPUT_NO_KEYS};
static bool on(uint8_t flags,unsigned i){bool set=(flags&bits[i])!=0;return i==0?set:!set;}
/* 版式：顶栏、三个小节（显示与字体、锁屏与壁纸、翻页），行高88。/ Layout: header and three sections (display and fonts, lock and wallpaper, page turning) with 88 px rows. */
#define FONT_ROW_Y 188
#define WALLPAPER_ROW_Y 344
#define TOGGLE_ROW_Y 504
#define LAN_SECTION_Y 912
#define LAN_ROW_Y 928
static pn_status_t show(pn_settings_ui_t *ui,pn_settings_present_fn present,void *ctx){
    su_t *u=ui->impl;ui->presented=false;pn_frame_clear(&u->canvas,15);pn_font_t *font=&u->font;
    pn_status_t s=pn_w_header(font,&u->canvas,"< 返回","设置",NULL);
    if(s==PN_OK)s=pn_w_section(font,&u->canvas,"显示与字体",172);
    if(s==PN_OK)s=pn_w_row_icon(font,&u->canvas,"字体管理",NULL,PN_ICON_FONT,PN_ROW_CHEVRON,FONT_ROW_Y);
    if(s==PN_OK)s=pn_w_section(font,&u->canvas,"锁屏与壁纸",328);
    if(s==PN_OK)s=pn_w_row_icon(font,&u->canvas,"锁屏壁纸",NULL,PN_ICON_IMAGE,PN_ROW_CHEVRON,WALLPAPER_ROW_Y);
    if(s==PN_OK)s=pn_w_section(font,&u->canvas,"翻页",488);
    for(unsigned i=0;i<4 && s==PN_OK;i++)s=pn_w_row_icon(font,&u->canvas,names[i],NULL,-1,on(ui->flags,i)?PN_ROW_ON:PN_ROW_OFF,TOGGLE_ROW_Y+(int)i*PN_W_ROW_H);
    if(s==PN_OK && *u->lan)s=pn_w_section(font,&u->canvas,"传书",LAN_SECTION_Y);
    if(s==PN_OK && *u->lan)s=pn_w_row_icon(font,&u->canvas,"局域网传书",u->lan,PN_ICON_WIFI,PN_ROW_CHEVRON,LAN_ROW_Y);
    if(s==PN_OK){int original=font->pixels;s=pn_font_size(font,28);
        if(s==PN_OK)s=pn_w_text(font,&u->canvas,*u->message?u->message:(u->state?"开关立即保存":"内部存储不可用，开关不能保存"),PN_UI_MARGIN,*u->lan?LAN_ROW_Y+PN_W_ROW_H+48:TOGGLE_ROW_Y+4*PN_W_ROW_H+48,620,PN_ALIGN_LEFT);
        pn_status_t restored=pn_font_size(font,original);if(s==PN_OK)s=restored;}
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
    if(command==PN_SETUI_WALLPAPER || command==PN_SETUI_FONTS || (command==PN_SETUI_LAN && *u->lan)){ui->request=command;return PN_OK;}
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
    if(!ui || !ui->impl || x<0 || x>=W)return -1;
    if(pn_w_header_hit(x,y,false)==1)return PN_SETUI_BACK;
    if(x<32 || x>=652)return -1;
    if(y>=FONT_ROW_Y && y<FONT_ROW_Y+PN_W_ROW_H)return PN_SETUI_FONTS;
    if(y>=WALLPAPER_ROW_Y && y<WALLPAPER_ROW_Y+PN_W_ROW_H)return PN_SETUI_WALLPAPER;
    if(y>=TOGGLE_ROW_Y && y<TOGGLE_ROW_Y+4*PN_W_ROW_H)return PN_SETUI_TOGGLE+(y-TOGGLE_ROW_Y)/PN_W_ROW_H;
    if(*((const su_t *)ui->impl)->lan && y>=LAN_ROW_Y && y<LAN_ROW_Y+PN_W_ROW_H)return PN_SETUI_LAN;
    return -1;
}
void pn_settings_ui_close(pn_settings_ui_t *ui){
    if(!ui || !ui->impl)return;
    su_t *u=ui->impl;pn_font_close(&u->font);pn_free(u->pixels);pn_free(u);*ui=(pn_settings_ui_t){0};
}
pn_status_t pn_settings_ui_set_lan(pn_settings_ui_t *ui,const char *ssid,pn_settings_present_fn present,void *ctx){
    if(!ui || !ui->impl || !present)return PN_INVALID;
    su_t *u=ui->impl;
    // 网络名按UTF-8字符边界截短到缓冲区。/ Clip the network name to the buffer at a UTF-8 character boundary.
    size_t n=ssid?strlen(ssid):0;if(n>=sizeof u->lan){n=sizeof u->lan-1;while(n>0 && ((unsigned char)ssid[n]&0xc0)==0x80)n--;}
    if(n)memcpy(u->lan,ssid,n);
    u->lan[n]=0;
    return show(ui,present,ctx);
}

