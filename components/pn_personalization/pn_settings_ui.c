/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设置（一级页）与“按键与手势”“刷新与屏幕”“存储与关于”三个子页；开关与刷新策略立即保存，保存走内部状态介质WRITE租约。
 * English: Settings (a root tab) with the "keys and gestures", "refresh and screen" and "storage and about" sub-pages; switches and the refresh policy save at once through a WRITE lease on the internal state media.
 */
#include "pn_settings_ui.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <string.h>
#define W 684
#define H 1216
enum {SCREEN_MAIN=0,SCREEN_KEYS,SCREEN_REFRESH,SCREEN_ABOUT};
typedef struct {pn_pool_t *pool;pn_media_t *state;char dir[PN_JOURNAL_PATH_MAX];pn_font_t font;uint8_t *pixels;pn_frame_t canvas;char message[96];char lan[40];int screen;pn_settings_about_t about;} su_t;
static const char *const names[]={"左手模式","滑动翻页","边缘点按翻页","屏下三键翻页"};
static const char *const hints[]={"点左边缘翻到下一页","左右滑动翻页","点屏幕两侧边缘翻页","KEY1 上一页，KEY3 下一页"};
static const uint8_t bits[]={PN_INPUT_LEFT_HAND,PN_INPUT_NO_SWIPE,PN_INPUT_NO_EDGE_TAP,PN_INPUT_NO_KEYS};
static bool on(uint8_t flags,unsigned i){bool set=(flags&bits[i])!=0;return i==0?set:!set;}
/* 刷新策略：标志位4–5，0均衡、1清晰、2省电。/ Refresh policy in flag bits 4–5: 0 balanced, 1 crisp, 2 saver. */
static unsigned level_of(uint8_t flags){unsigned level=(flags&PN_INPUT_REFRESH_MASK)>>4;return level>2?0u:level;}
static const char *const level_names[3]={"均衡","清晰","省电"};
unsigned pn_settings_refresh_pages(uint8_t flags){static const unsigned pages[3]={14,6,24};return pages[level_of(flags)];}
/* 版式：一级页大标题与分组卡片，底部固定三栏；子页统一顶栏返回设置。/ Layout: the root page has a large title and grouped cards over the fixed three-tab bar; sub-pages share a header that returns to Settings. */
#define ROW_H PN_W_CARD_ROW_H
#define DISPLAY_Y 190
#define READING_Y 446
#define DEVICE_Y 702
#define TAB_Y 1104
#define TAB_H 112
#define TOGGLE_Y 640
#define LEVEL_Y 196
#define LEVEL_H 92
#define FULL_Y 520
#define ABOUT_Y 190
static int device_rows(const su_t *u){return *u->lan?2:1;}
static pn_status_t note(su_t *u,const char *text,int baseline){
    int original=u->font.pixels;pn_status_t s=pn_font_size(&u->font,24);
    if(s==PN_OK)s=pn_w_text_lines_ex(&u->font,&u->canvas,text,PN_UI_MARGIN+8,baseline,604,3,34,PN_UI_MUTED,false);
    pn_status_t restored=pn_font_size(&u->font,original);return s==PN_OK?restored:s;
}
static pn_status_t draw_main(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_status(font,f);
    if(s==PN_OK)s=pn_font_size(font,50);
    if(s==PN_OK)s=pn_w_text_ex(font,f,"设置",PN_UI_MARGIN,124,400,PN_ALIGN_LEFT,PN_UI_INK,true);
    if(s==PN_OK)s=pn_w_group(font,f,"显示与字体",DISPLAY_Y-14);
    pn_w_card(f,DISPLAY_Y,2);
    if(s==PN_OK)s=pn_w_card_row(font,f,"字体管理","正文字体、全局默认与删除",NULL,PN_ICON_FONT,PN_ROW_CHEVRON,DISPLAY_Y,false);
    if(s==PN_OK)s=pn_w_card_row(font,f,"锁屏壁纸","系统默认、简洁、当前书封面或自定义",NULL,PN_ICON_IMAGE,PN_ROW_CHEVRON,DISPLAY_Y+ROW_H,true);
    if(s==PN_OK)s=pn_w_group(font,f,"阅读",READING_Y-14);
    pn_w_card(f,READING_Y,2);
    char line[64];snprintf(line,sizeof line,"%s · 每 %u 页整屏刷新",level_names[level_of(ui->flags)],pn_settings_refresh_pages(ui->flags));
    if(s==PN_OK)s=pn_w_card_row(font,f,"按键与手势","翻页方式、左手模式与屏下三键",NULL,PN_ICON_SETTINGS,PN_ROW_CHEVRON,READING_Y,false);
    if(s==PN_OK)s=pn_w_card_row(font,f,"刷新与屏幕",line,NULL,PN_ICON_REFRESH,PN_ROW_CHEVRON,READING_Y+ROW_H,true);
    if(s==PN_OK)s=pn_w_group(font,f,"设备",DEVICE_Y-14);
    pn_w_card(f,DEVICE_Y,device_rows(u));
    int y=DEVICE_Y;
    if(s==PN_OK && *u->lan){s=pn_w_card_row(font,f,"局域网传书",u->lan,NULL,PN_ICON_WIFI,PN_ROW_CHEVRON,y,false);y+=ROW_H;}
    if(s==PN_OK)s=pn_w_card_row(font,f,"存储与关于","版本、存储与设备信息",NULL,PN_ICON_LOCK,PN_ROW_CHEVRON,y,true);
    // 正常时不显示内部说明，只在有问题时提示。/ No internal note when all is well; only problems are shown.
    const char *problem=*u->message?u->message:u->state?"":"内部存储不可用，设置不能保存";
    if(s==PN_OK && *problem)s=note(u,problem,y+ROW_H+52);
    if(s==PN_OK){static const char *const tabs[]={"书架","传书","设置"};static const pn_icon_t icons[]={PN_ICON_SHELF,PN_ICON_TRANSFER,PN_ICON_SETTINGS};
        int original=font->pixels;s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_tabbar_icons(font,f,tabs,icons,3,2,0u,TAB_Y,TAB_H);pn_status_t restored=pn_font_size(font,original);if(s==PN_OK)s=restored;}
    return s;
}
/* 机身示意：屏幕下方三个触摸键与顶部电源键，按本机实际布局画。/ Device sketch: three touch keys under the screen and the power key on top, drawn after the actual hardware. */
static pn_status_t draw_keys(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_header(font,f,"< 设置","按键与手势",NULL);
    int dx=262,dy=150,dw=160,dh=250;
    pn_w_glass(f,dx,dy,dw,dh,22);pn_w_round_fill(f,dx+14,dy+16,dw-28,dh-74,8,PN_UI_PAPER);pn_w_round_stroke(f,dx+14,dy+16,dw-28,dh-74,8,2.0f,PN_UI_STROKE);
    for(int i=0;i<3;i++)pn_w_round_fill(f,dx+24+i*42,dy+dh-46,28,14,7,PN_UI_INK);
    pn_w_round_fill(f,dx+dw-50,dy-8,30,10,5,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,22);
    if(s==PN_OK)s=pn_w_text_ex(font,f,"1    2    3",dx,dy+dh+30,dw,PN_ALIGN_CENTER,PN_UI_MUTED,true);
    if(s==PN_OK)s=pn_w_text_ex(font,f,"电源键",dx+dw+12,dy+4,110,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    static const char *const legend[4][2]={{"KEY1","上一页 · 上一项 · 弹窗里取消"},{"KEY2","阅读中打开工具栏 · 确认；长按回书架"},{"KEY3","下一页 · 下一项 · 弹窗里切换焦点"},{"电源键","锁屏并保存，再按回到原处"}};
    for(int i=0;i<4 && s==PN_OK;i++){
        int base=476+i*36;s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_ex(font,f,legend[i][0],PN_UI_MARGIN+8,base,110,PN_ALIGN_LEFT,PN_UI_INK,true);
        if(s==PN_OK)s=pn_w_text_ex(font,f,legend[i][1],PN_UI_MARGIN+128,base,484,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    }
    if(s==PN_OK)s=pn_w_group(font,f,"触控与翻页",TOGGLE_Y-14);
    pn_w_card(f,TOGGLE_Y,4);
    for(unsigned i=0;i<4 && s==PN_OK;i++)s=pn_w_card_row(font,f,names[i],hints[i],NULL,-1,on(ui->flags,i)?PN_ROW_ON:PN_ROW_OFF,TOGGLE_Y+(int)i*ROW_H,i==3);
    if(s==PN_OK && *u->message)s=note(u,u->message,TOGGLE_Y+4*ROW_H+48);
    return s;
}
static pn_status_t draw_refresh(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_header(font,f,"< 设置","刷新与屏幕",NULL);
    static const char *const labels[3]={"清晰","均衡","省电"},*const notes[3]={"每 6 页全刷","每 14 页全刷","每 24 页全刷"};
    static const int order[3]={1,0,2}; // 显示顺序：清晰、均衡、省电 / Display order: crisp, balanced, saver
    int chosen=0;for(int i=0;i<3;i++)if(order[i]==(int)level_of(ui->flags))chosen=i;
    if(s==PN_OK)s=pn_w_group(font,f,"自动清残影",LEVEL_Y-18);
    if(s==PN_OK)s=pn_w_segments(font,f,labels,notes,3,chosen,LEVEL_Y,LEVEL_H);
    if(s==PN_OK)s=note(u,"阅读翻页用快速刷新，累计到设定页数后做一次整屏刷新压掉残影。数字越小越干净，闪屏也越多。每本书还可以在排版里单独设置。",LEVEL_Y+LEVEL_H+50);
    if(s==PN_OK)s=pn_w_group(font,f,"手动刷新",FULL_Y-18);
    if(s==PN_OK){int original=font->pixels;s=pn_font_size(font,30);if(s==PN_OK)s=pn_w_button(font,f,"立即整屏刷新",PN_UI_MARGIN,FULL_Y,620,88,PN_W_SELECTED);pn_status_t restored=pn_font_size(font,original);if(s==PN_OK)s=restored;}
    if(s==PN_OK)s=pn_w_group(font,f,"屏幕",FULL_Y+150);
    pn_w_card(f,FULL_Y+162,1);
    if(s==PN_OK)s=pn_w_card_row(font,f,*u->about.screen?u->about.screen:"4.7 英寸墨水屏","16 级灰阶，阅读页保持纯白底",NULL,-1,0u,FULL_Y+162,true);
    if(s==PN_OK && *u->message)s=note(u,u->message,FULL_Y+162+ROW_H+48);
    return s;
}
static pn_status_t draw_about(su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_header(font,f,"< 设置","存储与关于",NULL);
    const char *rows[4][2]={{"PicoNano",*u->about.version?u->about.version:"版本未知"},{"屏幕",*u->about.screen?u->about.screen:"4.7 英寸墨水屏 · 16 级灰阶"},
        {"存储卡",*u->about.storage?u->about.storage:"未读取到存储卡"},{"内部存储",*u->about.internal?u->about.internal:(u->state?"可用":"不可用")}};
    pn_w_card(f,ABOUT_Y,4);
    for(int i=0;i<4 && s==PN_OK;i++)s=pn_w_card_row(font,f,rows[i][0],rows[i][1],NULL,-1,0u,ABOUT_Y+i*ROW_H,i==3);
    if(s==PN_OK)s=note(u,"阅读记录、书签与设置只保存在本机。设备信息按实际读取显示，读不到的项目不会显示猜测值。",ABOUT_Y+4*ROW_H+52);
    return s;
}
static pn_status_t show_profile(pn_settings_ui_t *ui,pn_settings_present_fn present,void *ctx,pn_refresh_t profile){
    su_t *u=ui->impl;ui->presented=false;pn_frame_clear(&u->canvas,15);
    pn_status_t s=u->screen==SCREEN_KEYS?draw_keys(ui,u):u->screen==SCREEN_REFRESH?draw_refresh(ui,u):u->screen==SCREEN_ABOUT?draw_about(u):draw_main(ui,u);
    if(s==PN_OK)s=present(ctx,&u->canvas,profile);
    if(s==PN_OK)ui->presented=true;
    return s;
}
static pn_status_t show(pn_settings_ui_t *ui,pn_settings_present_fn present,void *ctx){return show_profile(ui,present,ctx,PN_REFRESH_GL16);}
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
    if(state && pn_settings_load_flags(state,dir,&flags)!=PN_OK){flags=0;snprintf(u->message,sizeof u->message,"设置记录不可读，显示默认值");}
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
/* 保存新标志；失败时重读并以已保存值为准。/ Save new flags; on failure re-read and keep the saved value. */
static pn_status_t commit(pn_settings_ui_t *ui,su_t *u,uint8_t next){
    pn_status_t status=save(u,next);
    if(status==PN_OK)ui->flags=next;
    else{uint8_t reread;if(u->state && pn_settings_load_flags(u->state,u->dir,&reread)==PN_OK)ui->flags=reread;snprintf(u->message,sizeof u->message,status==PN_UNSUPPORTED?"内部存储不可用，设置不能保存":"保存失败，请重试");}
    return status;
}
pn_status_t pn_settings_ui_event(pn_settings_ui_t *ui,int command,pn_settings_present_fn present,void *ctx){
    if(!ui || !ui->impl || !ui->active || !present)return PN_INVALID;
    su_t *u=ui->impl;u->message[0]=0;pn_status_t status=PN_OK;
    if(command==PN_SETUI_BACK){if(u->screen!=SCREEN_MAIN){u->screen=SCREEN_MAIN;return show(ui,present,ctx);}ui->active=false;return PN_OK;}
    if(command==PN_SETUI_WALLPAPER || command==PN_SETUI_FONTS || (command==PN_SETUI_LAN && *u->lan) || command==PN_SETUI_SHELF || command==PN_SETUI_TRANSFER){ui->request=command;return PN_OK;}
    if(command==PN_SETUI_KEYS || command==PN_SETUI_REFRESH || command==PN_SETUI_ABOUT){u->screen=command==PN_SETUI_KEYS?SCREEN_KEYS:command==PN_SETUI_REFRESH?SCREEN_REFRESH:SCREEN_ABOUT;return show(ui,present,ctx);}
    if(command==PN_SETUI_FULL_REFRESH)return show_profile(ui,present,ctx,PN_REFRESH_GC16);
    if(command>=PN_SETUI_LEVEL && command<PN_SETUI_LEVEL+3){
        uint8_t next=(uint8_t)((ui->flags&~PN_INPUT_REFRESH_MASK)|((unsigned)(command-PN_SETUI_LEVEL)<<4));
        if(next!=ui->flags)status=commit(ui,u,next);
        pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;
    }
    if(command<PN_SETUI_TOGGLE || command>PN_SETUI_TOGGLE+3)return PN_EMPTY;
    uint8_t next=(uint8_t)(ui->flags^bits[command-PN_SETUI_TOGGLE]);
    // 关闭全部翻页方式会让正文无法翻页，拒绝。/ Turning every page-turn method off would leave no way to turn pages, so refuse it.
    if((next&(PN_INPUT_NO_SWIPE|PN_INPUT_NO_EDGE_TAP|PN_INPUT_NO_KEYS))==(PN_INPUT_NO_SWIPE|PN_INPUT_NO_EDGE_TAP|PN_INPUT_NO_KEYS)){snprintf(u->message,sizeof u->message,"至少保留一种翻页方式");status=PN_LIMIT;}
    else status=commit(ui,u,next);
    pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;
}
pn_status_t pn_settings_ui_present(pn_settings_ui_t *ui,pn_settings_present_fn present,void *ctx){if(!ui || !ui->impl || !present)return PN_INVALID;return show(ui,present,ctx);}
int pn_settings_ui_hit(const pn_settings_ui_t *ui,int x,int y){
    if(!ui || !ui->impl || x<0 || x>=W || y<0 || y>=H)return -1;
    const su_t *u=ui->impl;
    if(u->screen==SCREEN_MAIN){
        if(y>=TAB_Y){int tab=pn_w_tabbar_hit(3,TAB_Y,TAB_H,x,y);return tab==0?PN_SETUI_SHELF:tab==1?PN_SETUI_TRANSFER:-1;}
        if(x<32 || x>=652)return -1;
        if(y>=DISPLAY_Y && y<DISPLAY_Y+ROW_H)return PN_SETUI_FONTS;
        if(y>=DISPLAY_Y+ROW_H && y<DISPLAY_Y+2*ROW_H)return PN_SETUI_WALLPAPER;
        if(y>=READING_Y && y<READING_Y+ROW_H)return PN_SETUI_KEYS;
        if(y>=READING_Y+ROW_H && y<READING_Y+2*ROW_H)return PN_SETUI_REFRESH;
        if(y>=DEVICE_Y && y<DEVICE_Y+device_rows(u)*ROW_H){int row=(y-DEVICE_Y)/ROW_H;return *u->lan && row==0?PN_SETUI_LAN:PN_SETUI_ABOUT;}
        return -1;
    }
    if(pn_w_header_hit(x,y,false)==1)return PN_SETUI_BACK;
    if(x<32 || x>=652)return -1;
    if(u->screen==SCREEN_KEYS){if(y>=TOGGLE_Y && y<TOGGLE_Y+4*ROW_H)return PN_SETUI_TOGGLE+(y-TOGGLE_Y)/ROW_H;return -1;}
    if(u->screen==SCREEN_REFRESH){
        static const int order[3]={1,0,2};
        int cell=pn_w_segments_hit(3,LEVEL_Y-6,LEVEL_H+12,x,y);if(cell>=0)return PN_SETUI_LEVEL+order[cell];
        if(y>=FULL_Y && y<FULL_Y+88)return PN_SETUI_FULL_REFRESH;
        return -1;
    }
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
void pn_settings_ui_set_about(pn_settings_ui_t *ui,const pn_settings_about_t *about){
    if(!ui || !ui->impl || !about)return;
    su_t *u=ui->impl;u->about=*about;
    // 外部字符串保证结尾。/ Make sure caller strings are terminated.
    u->about.version[sizeof u->about.version-1]=0;u->about.storage[sizeof u->about.storage-1]=0;u->about.internal[sizeof u->about.internal-1]=0;u->about.screen[sizeof u->about.screen-1]=0;
}
int pn_settings_ui_screen(const pn_settings_ui_t *ui){return ui && ui->impl?((const su_t *)ui->impl)->screen:0;}
