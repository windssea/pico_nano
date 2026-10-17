/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设置（一级页）与“按键与手势”“刷新与屏幕”“存储与关于”三个子页；开关与刷新策略立即保存，保存走内部状态介质WRITE租约。
 * English: Settings (a root tab) with the "keys and gestures", "refresh and screen" and "storage and about" sub-pages; switches and the refresh policy save at once through a WRITE lease on the internal state media.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_brand.h"
#include "pn_settings_ui.h"
#include "pn_widgets.h"
#include "pn_keyboard.h"
#include "pn_network_store.h"
#include "pn_shelf_view.h"
#include "pn_profile.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#define W 684
#define H 1216
enum {SCREEN_MAIN=0,SCREEN_KEYS,SCREEN_REFRESH,SCREEN_ABOUT,SCREEN_APPEARANCE,SCREEN_TIME,SCREEN_UPDATE,SCREEN_WIFI,SCREEN_WIFI_EDIT};
typedef struct {pn_pool_t *pool;pn_media_t *state;char dir[PN_JOURNAL_PATH_MAX];pn_font_t font;uint8_t *pixels;pn_frame_t canvas;char message[96];char lan[40];int screen;pn_settings_about_t about;pn_settings_time_t time;
    pn_settings_update_t update; ///< 控制层升级快照 / Controller update snapshot
    pn_font_t *return_font; ///< 单owner移交的空槽，close归还或正常释放 / Empty slot transferred by the single owner, returned on close or normally released
    bool snapshot_dirty; ///< 外部快照更新后先完整重画 / Repaint completely after external snapshot changes
    pn_w_status_snapshot_t status; ///< 最近成功画布的实时状态带 / Live status band of the last successful canvas
    pn_keyboard_t kb; ///< WiFi输入键盘 / WiFi entry keyboard
    char ssid[PN_NETWORK_SSID_MAX+1]; ///< 编辑中的网络名 / Network name being edited
    char password[PN_NETWORK_PASSWORD_MAX+1]; ///< 编辑中的密码，离开即清零 / Password being edited, wiped on leaving
    bool password_step,reveal,forget_armed; ///< 第二步/明文显示/忘记待确认 / Second step, plain display, forget awaiting confirmation
    bool testing,test_failed; ///< 连接测试中/上次测试失败可仍然保存 / Connection test running; last test failed and saving anyway is offered
} su_t;
static const char *const names[]={"左手模式","滑动翻页","点按翻页","屏下三键翻页"};
static const char *const hints[]={"点左半屏翻到下一页","左右滑动翻页","点屏幕左半或右半翻页","KEY1 上一页，KEY3 下一页"};
static const uint8_t bits[]={PN_INPUT_LEFT_HAND,PN_INPUT_NO_SWIPE,PN_INPUT_NO_EDGE_TAP,PN_INPUT_NO_KEYS};
static bool on(uint8_t flags,unsigned i){bool set=(flags&bits[i])!=0;return i==0?set:!set;}
/* 刷新策略：标志位4–5，0均衡、1清晰、2省电。/ Refresh policy in flag bits 4–5: 0 balanced, 1 crisp, 2 saver. */
static unsigned level_of(uint8_t flags){unsigned level=(flags&PN_INPUT_REFRESH_MASK)>>4;return level>2?0u:level;}
static const char *const level_names[3]={"均衡","清晰","省电"};
static const char *const style_names[4]={"默认","极简","拟物书架","纸页"};
unsigned pn_settings_refresh_pages(uint8_t flags){static const unsigned pages[3]={14,6,24};return pages[level_of(flags)];}
/* 版式：一级页大标题与分组卡片，底部固定三栏；子页统一顶栏返回设置。/ Layout: the root page has a large title and grouped cards over the fixed three-tab bar; sub-pages share a header that returns to Settings. */
#define ROW_H PN_W_CARD_ROW_H
#define DISPLAY_Y 190
#define READING_Y 550
#define DEVICE_Y 806
#define TAB_Y 1104
#define TAB_H 112
#define TOGGLE_Y 640
#define LEVEL_Y 196
#define LEVEL_H 92
#define FULL_Y 520
#define ABOUT_Y 190
#define TURN_Y 900
#define TURN_H 92
// 传书只从底栏进入，设置页不再放局域网传书行；设备卡为WiFi网络与存储关于。
// Transfer is entered only from the bottom bar; the device card holds WiFi and storage/about.
static int device_rows(const su_t *u){(void)u;return 2;}
#define WIFI_CARD_Y 196
#define WIFI_EDIT_Y 340
#define WIFI_FORGET_Y 450
#define FIELD_Y 152
#define FIELD_H 96
#define EDIT_BTN_Y 588
#define FORCE_Y 500
#define KB_Y (H-PN_KB_HEIGHT-24)
static pn_status_t note(su_t *u,const char *text,int baseline){
    int original=u->font.pixels;pn_status_t s=pn_font_size(&u->font,24);
    if(s==PN_OK)s=pn_w_text_lines_ex(&u->font,&u->canvas,text,PN_UI_MARGIN+8,baseline,604,3,34,PN_UI_MUTED,false);
    pn_status_t restored=pn_font_size(&u->font,original);return s==PN_OK?restored:s;
}
static pn_status_t draw_main(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;pn_profile_span_t span;
    pn_profile_begin(&span,NULL,"ui.settings.status");pn_status_t s=pn_w_status(font,f);pn_profile_end(&span,s);
    pn_profile_begin(&span,NULL,"ui.settings.title");
    if(s==PN_OK)s=pn_font_size(font,50);
    if(s==PN_OK)s=pn_w_text_ex(font,f,"设置",PN_UI_MARGIN,124,400,PN_ALIGN_LEFT,PN_UI_INK,true);
    pn_profile_end(&span,s);pn_profile_begin(&span,NULL,"ui.settings.card");
    if(s==PN_OK)s=pn_w_group(font,f,"显示与字体",DISPLAY_Y-14);
    pn_w_card(f,DISPLAY_Y,3);pn_profile_end(&span,s);pn_profile_begin(&span,NULL,"ui.settings.rows");
    if(s==PN_OK)s=pn_w_card_row(font,f,"字体管理","系统字体、全局阅读字体与删除",NULL,PN_ICON_FONT,PN_ROW_CHEVRON,DISPLAY_Y,false);
    if(s==PN_OK)s=pn_w_card_row(font,f,"锁屏壁纸","系统默认、简洁、当前书封面或自定义",NULL,PN_ICON_IMAGE,PN_ROW_CHEVRON,DISPLAY_Y+ROW_H,false);
    char appearance[64];snprintf(appearance,sizeof appearance,"每行 %u 本 · %s",ui->shelf.columns,style_names[ui->shelf.style]);
    if(s==PN_OK)s=pn_w_card_row(font,f,"书架外观",appearance,NULL,PN_ICON_SHELF,PN_ROW_CHEVRON,DISPLAY_Y+2*ROW_H,true);
    if(s==PN_OK)s=pn_w_group(font,f,"阅读",READING_Y-14);
    pn_w_card(f,READING_Y,2);
    char line[64];snprintf(line,sizeof line,"%s · 每 %u 页整屏刷新",level_names[level_of(ui->flags)],pn_settings_refresh_pages(ui->flags));
    if(s==PN_OK)s=pn_w_card_row(font,f,"按键与手势","翻页方式、左手模式与屏下三键",NULL,PN_ICON_SETTINGS,PN_ROW_CHEVRON,READING_Y,false);
    if(s==PN_OK)s=pn_w_card_row(font,f,"刷新与屏幕",line,NULL,PN_ICON_REFRESH,PN_ROW_CHEVRON,READING_Y+ROW_H,true);
    if(s==PN_OK)s=pn_w_group(font,f,"设备",DEVICE_Y-14);
    pn_w_card(f,DEVICE_Y,device_rows(u));
    int y=DEVICE_Y;char wifi[64];
    if(*u->lan)snprintf(wifi,sizeof wifi,"已保存：%s",u->lan);else snprintf(wifi,sizeof wifi,"未设置 · 用于传书、对时与更新");
    if(s==PN_OK)s=pn_w_card_row(font,f,"WiFi 网络",wifi,NULL,PN_ICON_WIFI,PN_ROW_CHEVRON,y,false);
    y+=ROW_H;
    if(s==PN_OK)s=pn_w_card_row(font,f,"存储与关于","版本、存储与设备信息",NULL,PN_ICON_LOCK,PN_ROW_CHEVRON,y,true);
    // 正常时不显示内部说明，只在有问题时提示。/ No internal note when all is well; only problems are shown.
    const char *problem=*u->message?u->message:u->state?"":"内部存储不可用，设置不能保存";
    if(s==PN_OK && *problem)s=note(u,problem,y+ROW_H+52);
    pn_profile_end(&span,s);pn_profile_begin(&span,NULL,"ui.settings.nav");
    if(s==PN_OK){
        int original=font->pixels;s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_navigation(font,f,PN_NAV_SETTINGS);pn_status_t restored=pn_font_size(font,original);if(s==PN_OK)s=restored;}
    pn_profile_end(&span,s);
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
static pn_status_t draw_levels(pn_settings_ui_t *ui,su_t *u){
    static const char *const labels[3]={"清晰","均衡","省电"},*const notes[3]={"每 6 页全刷","每 14 页全刷","每 24 页全刷"};
    static const int order[3]={1,0,2}; // 显示顺序：清晰、均衡、省电 / Display order: crisp, balanced, saver
    int chosen=0;for(int i=0;i<3;i++)if(order[i]==(int)level_of(ui->flags))chosen=i;
    return pn_w_segments(&u->font,&u->canvas,labels,notes,3,chosen,LEVEL_Y,LEVEL_H);
}
/// 翻页样式只有直接与水波纹；旧扫除/页折记录按直接显示。/ Turn styles are direct or ripple; legacy sweep/fold records show as direct.
static bool ripple(uint8_t flags){return (flags&PN_INPUT_TURN_MASK)==PN_INPUT_TURN_MASK;}
static pn_status_t draw_turns(pn_settings_ui_t *ui,su_t *u){
    static const char *const labels[2]={"直接","水波纹"},*const notes[2]={"最快","柔和过渡"};
    return pn_w_segments(&u->font,&u->canvas,labels,notes,2,ripple(ui->flags)?1:0,TURN_Y,TURN_H);
}
static pn_status_t draw_refresh(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_header(font,f,"< 设置","刷新与屏幕",NULL);
    if(s==PN_OK)s=pn_w_group(font,f,"自动清残影",LEVEL_Y-18);
    if(s==PN_OK)s=draw_levels(ui,u);
    if(s==PN_OK)s=note(u,"所有书籍共用此设置。成功翻到第 6、14 或 24 页时整屏刷新；工具栏、排版预览和重复点击不计页。周期越短，闪屏越多。",LEVEL_Y+LEVEL_H+50);
    if(s==PN_OK)s=pn_w_group(font,f,"手动刷新",FULL_Y-18);
    if(s==PN_OK){int original=font->pixels;s=pn_font_size(font,30);if(s==PN_OK)s=pn_w_button(font,f,"立即整屏刷新",PN_UI_MARGIN,FULL_Y,620,88,PN_W_SELECTED);pn_status_t restored=pn_font_size(font,original);if(s==PN_OK)s=restored;}
    if(s==PN_OK)s=pn_w_group(font,f,"屏幕",FULL_Y+150);
    pn_w_card(f,FULL_Y+162,1);
    if(s==PN_OK)s=pn_w_card_row(font,f,*u->about.screen?u->about.screen:"4.7 英寸墨水屏","16 级灰阶，阅读页保持纯白底",NULL,-1,0u,FULL_Y+162,true);
    if(s==PN_OK)s=pn_w_group(font,f,"翻页效果",TURN_Y-18);
    if(s==PN_OK)s=draw_turns(ui,u);
    if(s==PN_OK)s=note(u,ripple(ui->flags)?"翻页时文字从翻页一侧随弧形波纹逐步替换，只刷新变化的文字；到清残影的页数时也以波纹完成，不整屏同时闪。":"直接显示完整 16 灰阶正文，按上方周期整屏刷新。",TURN_Y+TURN_H+50);
    if(s==PN_OK && *u->message)s=note(u,u->message,TURN_Y+TURN_H+150);
    return s;
}
static pn_status_t draw_about(su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_header(font,f,"< 设置","存储与关于",NULL);
    const char *rows[4][2]={{PN_BRAND_NAME,*u->about.version?u->about.version:"版本未知"},{"屏幕",*u->about.screen?u->about.screen:"4.7 英寸墨水屏 · 16 级灰阶"},
        {"存储卡",*u->about.storage?u->about.storage:"未读取到存储卡"},{"内部存储",*u->about.internal?u->about.internal:(u->state?"可用":"不可用")}};
    pn_w_card(f,ABOUT_Y,4);
    for(int i=0;i<4 && s==PN_OK;i++)s=pn_w_card_row(font,f,rows[i][0],rows[i][1],NULL,-1,0u,ABOUT_Y+i*ROW_H,i==3);
    if(s==PN_OK)s=note(u,"本固件使用 HarmonyOS Sans 字体。阅读记录、书签与设置只保存在本机。设备信息按实际读取显示。",ABOUT_Y+4*ROW_H+52);
    pn_w_card(f,840,1);if(s==PN_OK)s=pn_w_card_row(font,f,"日期与时间",*u->time.datetime?u->time.datetime:"时钟尚未校准",NULL,PN_ICON_SETTINGS,PN_ROW_CHEVRON,840,true);
    pn_w_card(f,960,1);if(s==PN_OK)s=pn_w_card_row(font,f,"系统更新","通过家庭 WiFi 获取 Gitee 更新",NULL,PN_ICON_SETTINGS,PN_ROW_CHEVRON,960,true);
    return s;
}
static pn_status_t draw_update(su_t *u){
    pn_status_t s=pn_w_header(&u->font,&u->canvas,"< 关于","系统更新",NULL);
    if(s==PN_OK)s=note(u,*u->update.version?u->update.version:"PicoNano 固件更新",280);
    if(s==PN_OK)s=note(u,*u->update.message?u->update.message:"连接已保存的家庭 WiFi，检查 Gitee 最新版本。",390);
    const char *label=u->update.running?"正在处理…":u->update.reboot?"重启并完成升级":u->update.install?"安装这个版本":"检查更新";
    if(s==PN_OK)s=pn_font_size(&u->font,32);
    if(s==PN_OK)s=pn_w_button(&u->font,&u->canvas,label,32,700,620,88,!u->update.running && (u->update.available || u->update.install || u->update.reboot)?PN_W_SELECTED:PN_W_DISABLED);
    if(s==PN_OK && (u->update.running || u->update.install || u->update.reboot))s=pn_w_button(&u->font,&u->canvas,"取消更新",32,810,620,88,0u);
    if(s==PN_OK)s=note(u,"更新会保留阅读记录与设置，请保持供电。",970);
    return s;
}
static pn_status_t draw_wifi(su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_header(font,f,"< 设置","WiFi 网络",NULL);
    if(s==PN_OK)s=pn_w_group(font,f,"已保存的网络",WIFI_CARD_Y-14);
    pn_w_card(f,WIFI_CARD_Y,1);
    if(s==PN_OK)s=pn_w_card_row(font,f,*u->lan?u->lan:"尚未保存",*u->lan?"传书、联网校时与系统更新共用":"保存后本机可直接联网",NULL,PN_ICON_WIFI,0u,WIFI_CARD_Y,true);
    if(s==PN_OK)s=pn_font_size(font,32);
    if(s==PN_OK)s=pn_w_button(font,f,*u->lan?"更改网络":"设置网络",PN_UI_MARGIN,WIFI_EDIT_Y,620,88,PN_W_SELECTED);
    if(s==PN_OK && *u->lan)s=pn_w_button(font,f,u->forget_armed?"再点一次确认忘记":"忘记网络",PN_UI_MARGIN,WIFI_FORGET_Y,620,88,0u);
    if(s==PN_OK)s=note(u,"用屏幕键盘输入网络名称和密码。密码只保存在本机内部存储，不上传也不会显示在这里。也可以在手机传书网页中保存。",600);
    if(s==PN_OK && *u->message)s=note(u,u->message,760);
    return s;
}
/// 输入框只显示末尾能放下的部分，前面以省略号代替。/ The field shows only the tail that fits, replacing the head with an ellipsis.
static pn_status_t field_text(su_t *u,const char *text,int x,int baseline,int width){
    char shown[PN_NETWORK_PASSWORD_MAX+8];const char *tail=text;int w=0;
    pn_status_t s=pn_w_text_width(&u->font,tail,&w);
    while(s==PN_OK && w>width && *tail){do tail++;while(((unsigned char)*tail&0xc0)==0x80);snprintf(shown,sizeof shown,"…%s",tail);s=pn_w_text_width(&u->font,shown,&w);}
    if(s!=PN_OK)return s;
    if(tail!=text)snprintf(shown,sizeof shown,"…%s",tail);else snprintf(shown,sizeof shown,"%s",text);
    s=pn_w_text_ex(&u->font,&u->canvas,shown,x,baseline,width+8,PN_ALIGN_LEFT,PN_UI_INK,false);
    // 末尾光标提示当前输入位置。/ A trailing caret shows where typing continues.
    if(s==PN_OK)s=pn_w_text_width(&u->font,shown,&w);
    if(s==PN_OK)pn_frame_rect(&u->canvas,x+w+4,baseline-u->font.pixels+4,3,u->font.pixels,PN_UI_INK);
    return s;
}
static pn_status_t draw_wifi_edit(su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;bool pw=u->password_step;const char *value=pw?u->password:u->ssid;
    pn_status_t s=pn_w_header(font,f,"< 返回",pw?"WiFi 密码":"网络名称",NULL);
    pn_w_round_fill(f,PN_UI_MARGIN,FIELD_Y,620,FIELD_H,PN_UI_RADIUS,14);pn_w_round_stroke(f,PN_UI_MARGIN,FIELD_Y,620,FIELD_H,PN_UI_RADIUS,2.0f,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,38);
    if(s==PN_OK){
        if(!*value)s=pn_w_text_ex(font,f,pw?"输入密码，开放网络留空":"输入网络名称",56,FIELD_Y+62,560,PN_ALIGN_LEFT,PN_UI_MUTED,false);
        else if(pw && !u->reveal){char stars[PN_NETWORK_PASSWORD_MAX+1];size_t n=strlen(value);memset(stars,'*',n);stars[n]=0;s=field_text(u,stars,56,FIELD_Y+62,560);}
        else s=field_text(u,value,56,FIELD_Y+62,560);
    }
    char step[48],count[16];snprintf(step,sizeof step,"第 %d 步，共 2 步 · 区分大小写",pw?2:1);snprintf(count,sizeof count,"%u/%u",(unsigned)strlen(value),pw?PN_NETWORK_PASSWORD_MAX:PN_NETWORK_SSID_MAX);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text_ex(font,f,step,PN_UI_MARGIN+8,FIELD_Y+FIELD_H+40,460,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    if(s==PN_OK)s=pn_w_text_ex(font,f,count,PN_UI_MARGIN+460,FIELD_Y+FIELD_H+40,152,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
    char info[96];
    if(pw)snprintf(info,sizeof info,"网络：%s。密码 8–63 位。",u->ssid);else snprintf(info,sizeof info,"名称需与路由器显示的一致，最多 32 字节。");
    if(s==PN_OK)s=note(u,info,FIELD_Y+FIELD_H+96);
    if(s==PN_OK && *u->message)s=note(u,u->message,FIELD_Y+FIELD_H+170);
    if(s==PN_OK)s=pn_font_size(font,30);
    if(s==PN_OK && pw && u->test_failed)s=pn_w_button(font,f,"仍然保存（不验证）",PN_UI_MARGIN,FORCE_Y,620,72,0u);
    if(s==PN_OK)s=pn_w_button(font,f,"清空",PN_UI_MARGIN,EDIT_BTN_Y,196,88,*value && !u->testing?0u:PN_W_DISABLED);
    if(s==PN_OK && pw)s=pn_w_button(font,f,u->testing?"取消测试":u->reveal?"隐藏密码":"显示密码",240,EDIT_BTN_Y,196,88,0u);
    if(s==PN_OK)s=pn_w_button(font,f,u->testing?"连接中…":pw?"测试并保存":"下一步",448,EDIT_BTN_Y,204,88,u->testing?0u:PN_W_SELECTED); // 测试中为普通样式：进行中而非不可用 / Plain style while testing: in progress, not unavailable
    if(s==PN_OK)s=pn_keyboard_render(&u->kb,font,f,KB_Y);
    return s;
}
static pn_status_t draw_time(su_t *u){
    pn_status_t s=pn_w_header(&u->font,&u->canvas,"< 关于","日期与时间",NULL);
    if(s==PN_OK)s=pn_font_size(&u->font,38);
    if(s==PN_OK)s=pn_w_text(&u->font,&u->canvas,*u->time.datetime?u->time.datetime:"时间尚未校准",32,290,620,PN_ALIGN_CENTER);
    if(s==PN_OK)s=note(u,"北京时间 · UTC+8",348);
    if(s==PN_OK)s=note(u,*u->time.message?u->time.message:u->time.available?"使用已保存的家庭WiFi获取网络时间。完成后由设备时钟继续走时。":"请先在设置的“WiFi 网络”或传书网页中保存家庭WiFi，再联网校时。",450);
    if(s==PN_OK)s=pn_font_size(&u->font,32);
    if(s==PN_OK)s=pn_w_button(&u->font,&u->canvas,u->time.running?"结束对时":"立即联网校时",32,700,620,88,u->time.running || u->time.available?PN_W_SELECTED:PN_W_DISABLED);
    if(s==PN_OK)s=note(u,"对时期间可以操作其他设置。网络失败时保留原显示时间，稍后可重试。",862);
    return s;
}
/* 设置中直接预览静态样式；无贴图、无动画或额外封面解码。/ Preview static styles in settings without textures, animation or extra cover decoding. */
static pn_status_t draw_columns(pn_settings_ui_t *ui,su_t *u){
    static const char *const labels[]={"每行 3 本","每行 4 本"},*const hints[]={"封面更大 · 每页 6 本","浏览更多 · 每页 8 本"};
    return pn_w_segments(&u->font,&u->canvas,labels,hints,2,ui->shelf.columns==4?1:0,196,96);
}
static pn_status_t draw_style(pn_settings_ui_t *ui,su_t *u,unsigned i){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    int x=32+(int)(i%2)*318,y=460+(int)(i/2)*230;
    pn_w_round_fill(f,x,y,302,210,16,PN_UI_SURFACE);
    pn_w_round_stroke(f,x,y,302,210,16,ui->shelf.style==i?3:1,ui->shelf.style==i?PN_UI_INK:PN_UI_STROKE);
    pn_shelf_style_preview(f,i,ui->shelf.columns,x+18,y+20,266,128);
    pn_status_t s=pn_font_size(font,28);if(s==PN_OK)s=pn_w_text_ex(font,f,style_names[i],x+20,y+184,240,PN_ALIGN_LEFT,PN_UI_INK,ui->shelf.style==i);
    if(ui->shelf.style==i)pn_w_icon(f,PN_ICON_CHECK,x+254,y+160,28,PN_UI_INK);
    return s;
}
static pn_status_t draw_appearance(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;pn_status_t s=pn_w_header(font,f,"< 设置","书架外观",NULL);
    if(s==PN_OK)s=pn_w_group(font,f,"书架密度",176);
    if(s==PN_OK)s=draw_columns(ui,u);
    if(s==PN_OK)s=note(u,"选项会自动保存。网格使用此密度，列表与搜索结果仍每页显示 5 本。",342);
    if(s==PN_OK)s=pn_w_group(font,f,"书架样式",432);
    for(unsigned i=0;i<PN_SHELF_STYLE_COUNT && s==PN_OK;i++)s=draw_style(ui,u,i);
    if(s==PN_OK)s=note(u,"封面角标：空心标记未读，实心点已开始，勾号已读完。细线显示阅读进度；长按封面可查看格式与详情。",958);
    if(s==PN_OK && *u->message)s=note(u,u->message,1100);
    return s;
}
static pn_status_t show_profile(pn_settings_ui_t *ui,pn_settings_present_fn present,void *ctx,pn_refresh_t profile){
    su_t *u=ui->impl;pn_w_status_snapshot_t snapshot=pn_w_status_snapshot();ui->presented=false;
    // 只计时清屏与绘制，供真机拆分菜单准备耗时。/ Time clearing and drawing only, to split menu preparation on hardware.
    pn_profile_span_t span;pn_profile_begin(&span,NULL,"ui.settings.clear");pn_frame_clear(&u->canvas,15);pn_profile_end(&span,PN_OK);
    pn_profile_begin(&span,NULL,"ui.settings.draw");
    pn_status_t s=u->screen==SCREEN_KEYS?draw_keys(ui,u):u->screen==SCREEN_REFRESH?draw_refresh(ui,u):u->screen==SCREEN_ABOUT?draw_about(u):u->screen==SCREEN_TIME?draw_time(u):u->screen==SCREEN_UPDATE?draw_update(u):u->screen==SCREEN_WIFI?draw_wifi(u):u->screen==SCREEN_WIFI_EDIT?draw_wifi_edit(u):u->screen==SCREEN_APPEARANCE?draw_appearance(ui,u):draw_main(ui,u);
    pn_profile_end(&span,s);
    if(s==PN_OK)s=present(ctx,&u->canvas,profile);
    if(s==PN_OK){ui->presented=true;u->snapshot_dirty=false;u->status=snapshot;}
    return s;
}
static pn_status_t show(pn_settings_ui_t *ui,pn_settings_present_fn present,void *ctx){return show_profile(ui,present,ctx,PN_REFRESH_GL16);}
/* 只重画改变的控件；旧提示、外部快照变化或失败回执走完整路径。
 * Repaint only changed controls; old notices, changed external snapshots or failed receipts use the complete path. */
static pn_status_t show_controls(pn_settings_ui_t *ui,su_t *u,uint8_t old_flags,pn_shelf_prefs_t old_shelf,int command,bool local,pn_settings_present_fn present,void *ctx){
    if(!local || !ui->presented || u->snapshot_dirty || *u->message)return show(ui,present,ctx);
    pn_status_t s=PN_OK;bool changed=false;
    if(u->screen==SCREEN_KEYS && command>=PN_SETUI_TOGGLE && command<PN_SETUI_TOGGLE+4){
        unsigned i=(unsigned)(command-PN_SETUI_TOGGLE);changed=old_flags!=ui->flags;
        if(changed){int x=PN_UI_WIDTH-PN_UI_MARGIN-16-64,y=TOGGLE_Y+(int)i*ROW_H+32;
            pn_frame_rect(&u->canvas,x,y,64,36,PN_UI_SURFACE);pn_w_toggle(&u->canvas,x,y,on(ui->flags,i));}
    }else if(u->screen==SCREEN_REFRESH && (command==PN_SETUI_TURN || command==PN_SETUI_TURN+1)){
        // 说明文字随样式改变，整页重画。/ The note changes with the style, so repaint the page.
        return show(ui,present,ctx);
    }else if(u->screen==SCREEN_REFRESH && command>=PN_SETUI_LEVEL && command<PN_SETUI_LEVEL+3){
        changed=old_flags!=ui->flags;if(changed){pn_frame_rect(&u->canvas,26,LEVEL_Y-6,632,LEVEL_H+12,PN_UI_PAPER);s=draw_levels(ui,u);}
    }else if(u->screen==SCREEN_APPEARANCE && command>=PN_SETUI_COLUMNS && command<PN_SETUI_STYLE+PN_SHELF_STYLE_COUNT){
        bool columns=old_shelf.columns!=ui->shelf.columns;changed=columns || old_shelf.style!=ui->shelf.style;
        if(columns){pn_frame_rect(&u->canvas,26,190,632,108,PN_UI_PAPER);s=draw_columns(ui,u);}
        for(unsigned i=0;i<PN_SHELF_STYLE_COUNT && s==PN_OK && changed;i++)if(columns || old_shelf.style==i || ui->shelf.style==i){
            int x=32+(int)(i%2)*318,y=460+(int)(i/2)*230;pn_frame_rect(&u->canvas,x,y,302,210,PN_UI_PAPER);s=draw_style(ui,u,i);}
    }else return show(ui,present,ctx);
    pn_w_status_snapshot_t snapshot=pn_w_status_snapshot();
    // 状态带变化不迫使其他静态内容重画，失败仍废弃本次画布的有效标记。
    // Status changes do not force static content to repaint; failures still invalidate the canvas receipt.
    if(s==PN_OK && (snapshot.battery!=u->status.battery || snapshot.wifi!=u->status.wifi || strcmp(snapshot.clock,u->status.clock))){
        changed=true;pn_frame_rect(&u->canvas,0,0,W,40,PN_UI_PAPER);s=pn_w_status(&u->font,&u->canvas);}
    if(!changed)return PN_OK;
    ui->presented=false;if(s==PN_OK)s=present(ctx,&u->canvas,PN_REFRESH_GL16);if(s==PN_OK){ui->presented=true;u->status=snapshot;}return s;
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
pn_status_t pn_settings_load_shelf(pn_media_t *state,const char *dir,pn_shelf_prefs_t *out){
    if(!state || !dir || !*dir || !out)return PN_INVALID;
    pn_media_lease_t lease;pn_status_t s=pn_media_acquire(state,PN_MEDIA_READ,&lease);if(s!=PN_OK)return s;
    pn_journal_files_t files;pn_journal_io_t io;pn_shelf_prefs_t value=pn_shelf_prefs_default();
    s=pn_shelf_prefs_files(&files,state,&lease,dir,&io);if(s==PN_OK)s=pn_shelf_prefs_load(&io,&value);
    (void)pn_media_release(state,&lease);if(s==PN_EMPTY)s=PN_OK;if(s==PN_OK)*out=value;return s;
}
/// 编辑缓冲含明文密码，离开输入页、保存或关闭时清零。/ Edit buffers hold a plaintext password; wipe them on leaving the entry page, saving or closing.
static void wipe_edit(su_t *u){volatile char *p=u->password;for(size_t i=0;i<sizeof u->password;i++)p[i]=0;memset(u->ssid,0,sizeof u->ssid);u->password_step=false;}
static void set_lan_text(su_t *u,const char *ssid){size_t n=strnlen(ssid,PN_NETWORK_SSID_MAX);memcpy(u->lan,ssid,n);u->lan[n]=0;}
/// 只读已保存网络名；口令读出后立即清零。/ Read only the saved network name, wiping the password immediately.
static void reload_lan(su_t *u){
    if(!u->state)return;
    pn_media_lease_t lease;if(pn_media_acquire(u->state,PN_MEDIA_READ,&lease)!=PN_OK)return;
    pn_journal_files_t files;pn_journal_io_t io;pn_network_credentials_t c={0};
    pn_status_t s=pn_network_files(&files,u->state,&lease,u->dir,&io);if(s==PN_OK)s=pn_network_load(&io,&c);
    (void)pn_media_release(u->state,&lease);
    if(s==PN_OK)set_lan_text(u,c.ssid);else if(s==PN_EMPTY)u->lan[0]=0;
    pn_network_wipe(&c);
}
/// 保存（空名表示忘记）；失败可能已提交，调用方重读。/ Save (an empty name forgets); failures may have committed, so callers reload.
static pn_status_t save_network(su_t *u,const char *ssid,const char *password){
    if(!u->state)return PN_UNSUPPORTED;
    pn_network_credentials_t c={0};snprintf(c.ssid,sizeof c.ssid,"%s",ssid);snprintf(c.password,sizeof c.password,"%s",password);
    pn_status_t s=pn_network_validate(&c,true);pn_media_lease_t lease;
    if(s==PN_OK)s=pn_media_acquire(u->state,PN_MEDIA_WRITE,&lease);
    if(s==PN_OK){pn_journal_files_t files;pn_journal_io_t io;s=pn_network_files(&files,u->state,&lease,u->dir,&io);if(s==PN_OK)s=pn_network_save(&io,&c);(void)pn_media_release(u->state,&lease);}
    pn_network_wipe(&c);return s;
}
/// 写入当前编辑的名称/密码；成功回网络页并清零输入，失败重读并停留。/ Save the edited name and password; success returns to the network page wiping input, failure reloads and stays.
static pn_status_t commit_wifi(pn_settings_ui_t *ui,su_t *u,pn_settings_present_fn present,void *ctx){
    pn_status_t s=save_network(u,u->ssid,u->password);
    if(s!=PN_OK){reload_lan(u);ui->network_changed=true;snprintf(u->message,sizeof u->message,"%s",s==PN_UNSUPPORTED?"内部存储不可用，不能保存网络":s==PN_INVALID?"网络名称或密码格式不受支持":"保存失败，请重试");pn_status_t shown=show(ui,present,ctx);return shown==PN_OK?s:shown;}
    set_lan_text(u,u->ssid);ui->network_changed=true;wipe_edit(u);u->test_failed=false;u->screen=SCREEN_WIFI;
    snprintf(u->message,sizeof u->message,"已保存“%.32s”，传书、对时与更新会用它。",u->lan);
    return show(ui,present,ctx);
}
static pn_status_t wifi_event(pn_settings_ui_t *ui,su_t *u,int command,pn_settings_present_fn present,void *ctx){
    if(u->screen==SCREEN_WIFI){
        if(command==PN_SETUI_WIFI_EDIT){
            u->forget_armed=false;wipe_edit(u);u->reveal=true;pn_keyboard_reset(&u->kb);
            // 预填已保存的名称，便于只改密码。/ Prefill the saved name so only the password needs changing.
            if(strlen(u->lan)<=PN_NETWORK_SSID_MAX)strcpy(u->ssid,u->lan);
            u->screen=SCREEN_WIFI_EDIT;return show(ui,present,ctx);
        }
        if(command==PN_SETUI_WIFI_FORGET && *u->lan){
            if(!u->forget_armed){u->forget_armed=true;snprintf(u->message,sizeof u->message,"再点一次确认。忘记后需重新设置才能联网。");return show(ui,present,ctx);}
            u->forget_armed=false;pn_status_t s=save_network(u,"","");reload_lan(u);ui->network_changed=true;
            snprintf(u->message,sizeof u->message,"%s",s==PN_OK?"已忘记网络。":s==PN_UNSUPPORTED?"内部存储不可用，不能修改网络":"操作失败，请重试");
            pn_status_t shown=show(ui,present,ctx);return s==PN_OK?shown:s;
        }
        return PN_EMPTY;
    }
    // 测试中只接受取消；结果由控制层交回。/ While testing only cancellation is accepted; the controller reports the result.
    if(u->testing){if(command!=PN_SETUI_WIFI_TEST_CANCEL)return PN_BUSY;ui->request=PN_SETUI_WIFI_TEST_CANCEL;snprintf(u->message,sizeof u->message,"正在取消测试…");return show(ui,present,ctx);}
    if(command==PN_SETUI_WIFI_FORCE && u->password_step && u->test_failed)return commit_wifi(ui,u,present,ctx);
    char *field=u->password_step?u->password:u->ssid;size_t cap=u->password_step?sizeof u->password:sizeof u->ssid,max=u->password_step?PN_NETWORK_PASSWORD_MAX:PN_NETWORK_SSID_MAX;
    bool changed=false;
    if(command>=PN_SETUI_CHAR+32 || command==PN_SETUI_KB_DELETE || command==PN_SETUI_WIFI_CLEAR)u->test_failed=false;
    if(command>=PN_SETUI_CHAR+32 && command<=PN_SETUI_CHAR+126){
        changed=pn_text_field_append(field,cap,max,command-PN_SETUI_CHAR);
        if(!changed){snprintf(u->message,sizeof u->message,u->password_step?"密码最多 63 位":"网络名称最多 32 字节");changed=true;}
    }
    else if(command==PN_SETUI_KB_DELETE)changed=pn_text_field_delete(field);
    else if(command==PN_SETUI_KB_SHIFT)changed=pn_keyboard_toggle(&u->kb,PN_KB_SHIFT);
    else if(command==PN_SETUI_KB_LAYER)changed=pn_keyboard_toggle(&u->kb,PN_KB_LAYER);
    else if(command==PN_SETUI_WIFI_CLEAR){changed=*field!=0;if(u->password_step){volatile char *p=u->password;for(size_t i=0;i<sizeof u->password;i++)p[i]=0;}else u->ssid[0]=0;}
    else if(command==PN_SETUI_WIFI_REVEAL && u->password_step){u->reveal=!u->reveal;changed=true;}
    else if(command==PN_SETUI_WIFI_NEXT && !u->password_step){
        if(!*u->ssid){snprintf(u->message,sizeof u->message,"请先输入网络名称");}
        else{u->password_step=true;pn_keyboard_reset(&u->kb);}
        changed=true;
    }
    else if(command==PN_SETUI_WIFI_NEXT){
        size_t n=strlen(u->password);
        if(n && n<8){snprintf(u->message,sizeof u->message,"密码需 8–63 位；开放网络请留空");return show(ui,present,ctx);}
        // 先连接测试，成功才保存；旧记录在此之前保持不变。/ Test the connection first and save only on success; the old record stays untouched until then.
        if(!u->state){snprintf(u->message,sizeof u->message,"内部存储不可用，不能保存网络");pn_status_t shown=show(ui,present,ctx);return shown==PN_OK?PN_UNSUPPORTED:shown;}
        u->testing=true;u->test_failed=false;ui->request=PN_SETUI_WIFI_TEST;
        snprintf(u->message,sizeof u->message,"正在连接“%.32s”验证，最长约 30 秒…",u->ssid);
        return show(ui,present,ctx);
    }
    else return PN_EMPTY;
    return changed?show(ui,present,ctx):PN_OK;
}
pn_status_t pn_settings_ui_open_cached(pn_settings_ui_t *ui,pn_pool_t *pool,pn_media_t *state,const char *dir,pn_font_t *return_slot,pn_settings_present_fn present,void *ctx){
    if(!ui || !pool || !present || (state && (!dir || !*dir)))return PN_INVALID;
    if(ui->impl)return PN_BUSY;
    if(dir && strlen(dir)>=PN_JOURNAL_PATH_MAX)return PN_LIMIT;
    su_t *u=pn_alloc_tagged(pool,PN_MEM_UI,sizeof *u);
    if(!u)return PN_NO_MEMORY;
    memset(u,0,sizeof *u);u->pool=pool;u->state=state;
    if(state)strcpy(u->dir,dir);
    u->pixels=pn_alloc_tagged(pool,PN_MEM_UI,(size_t)W/2*H);
    pn_status_t status=u->pixels?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK){
        (void)pn_frame_bind(&u->canvas,u->pixels,(size_t)W/2*H,W,H);
        if(return_slot && return_slot->impl){status=pn_font_source_ready(return_slot);if(status==PN_OK)status=pn_font_move(&u->font,return_slot,pool);if(status==PN_OK)status=pn_font_size(&u->font,30);}
        else{pn_text_source_t builtin=pn_font_builtin_source();status=pn_font_open(&u->font,pool,&builtin,30);}
    }
    if(status!=PN_OK){if(u->font.impl && (!return_slot || pn_font_move(return_slot,&u->font,pool)!=PN_OK))pn_font_close(&u->font);pn_free(u->pixels);pn_free(u);return status;}
    u->return_font=return_slot;
    uint8_t flags=0;
    if(state && pn_settings_load_flags(state,dir,&flags)!=PN_OK){flags=0;snprintf(u->message,sizeof u->message,"设置记录不可读，显示默认值");}
    pn_shelf_prefs_t shelf=pn_shelf_prefs_default();
    if(state && pn_settings_load_shelf(state,dir,&shelf)!=PN_OK)snprintf(u->message,sizeof u->message,"书架设置不可读，显示默认值");
    reload_lan(u);
    *ui=(pn_settings_ui_t){.impl=u,.active=true,.flags=flags,.shelf=shelf};
    (void)show(ui,present,ctx);return PN_OK;
}
pn_status_t pn_settings_ui_open(pn_settings_ui_t *ui,pn_pool_t *pool,pn_media_t *state,const char *dir,pn_settings_present_fn present,void *ctx){return pn_settings_ui_open_cached(ui,pool,state,dir,NULL,present,ctx);}
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
static pn_status_t commit_shelf(pn_settings_ui_t *ui,su_t *u,pn_shelf_prefs_t next){
    pn_status_t s=PN_UNSUPPORTED;
    if(u->state){pn_media_lease_t lease;s=pn_media_acquire(u->state,PN_MEDIA_WRITE,&lease);
        if(s==PN_OK){pn_journal_files_t files;pn_journal_io_t io;s=pn_shelf_prefs_files(&files,u->state,&lease,u->dir,&io);
            if(s==PN_OK)s=pn_shelf_prefs_save(&io,&next);
            (void)pn_media_release(u->state,&lease);}}
    if(s==PN_OK)ui->shelf=next;
    else {pn_shelf_prefs_t reread;if(u->state && pn_settings_load_shelf(u->state,u->dir,&reread)==PN_OK)ui->shelf=reread;
        snprintf(u->message,sizeof u->message,s==PN_UNSUPPORTED?"内部存储不可用，设置不能保存":"保存失败，请重试");}
    return s;
}
pn_status_t pn_settings_ui_event(pn_settings_ui_t *ui,int command,pn_settings_present_fn present,void *ctx){
    if(!ui || !ui->impl || !ui->active || !present)return PN_INVALID;
    su_t *u=ui->impl;
    // 测试中忽略的输入不清掉“正在连接”提示。/ Inputs ignored during a test keep the connecting notice.
    if(u->testing && command!=PN_SETUI_WIFI_TEST_CANCEL && command!=PN_SETUI_BACK)return PN_BUSY;
    bool local=!*u->message;uint8_t old_flags=ui->flags;pn_shelf_prefs_t old_shelf=ui->shelf;u->message[0]=0;pn_status_t status=PN_OK;
    if(u->update.running && command!=PN_SETUI_UPDATE_CANCEL){if(command==PN_SETUI_BACK)ui->request=PN_SETUI_UPDATE_CANCEL;return PN_BUSY;}
    if(u->testing && command!=PN_SETUI_WIFI_TEST_CANCEL){if(command==PN_SETUI_BACK){ui->request=PN_SETUI_WIFI_TEST_CANCEL;snprintf(u->message,sizeof u->message,"正在取消测试…");pn_status_t shown=show(ui,present,ctx);return shown==PN_OK?PN_BUSY:shown;}return PN_BUSY;}
    // 输入页返回先回到名称步，再回网络页；离开即清零输入。/ Back on the entry page returns to the name step, then the network page, wiping input on leaving.
    if(command==PN_SETUI_BACK && u->screen==SCREEN_WIFI_EDIT){u->test_failed=false;if(u->password_step){volatile char *p=u->password;for(size_t i=0;i<sizeof u->password;i++)p[i]=0;u->password_step=false;pn_keyboard_reset(&u->kb);}else{wipe_edit(u);u->screen=SCREEN_WIFI;}return show(ui,present,ctx);}
    if(command==PN_SETUI_BACK){if(u->screen!=SCREEN_MAIN){u->forget_armed=false;u->screen=u->screen==SCREEN_TIME || u->screen==SCREEN_UPDATE?SCREEN_ABOUT:SCREEN_MAIN;return show(ui,present,ctx);}ui->active=false;return PN_OK;}
    if(command==PN_SETUI_WIFI){u->forget_armed=false;u->screen=SCREEN_WIFI;return show(ui,present,ctx);}
    if(u->screen==SCREEN_WIFI || u->screen==SCREEN_WIFI_EDIT){pn_status_t s=wifi_event(ui,u,command,present,ctx);if(s!=PN_EMPTY || u->screen==SCREEN_WIFI_EDIT)return s;}
    if(command==PN_SETUI_UPDATE){u->screen=SCREEN_UPDATE;return show(ui,present,ctx);}
    if(command>=PN_SETUI_UPDATE_CHECK && command<=PN_SETUI_UPDATE_CANCEL){
        if(u->screen!=SCREEN_UPDATE || !ui->presented)return PN_BUSY;
        bool valid=command==PN_SETUI_UPDATE_CHECK?u->update.available && !u->update.running && !u->update.install && !u->update.reboot:command==PN_SETUI_UPDATE_INSTALL?u->update.install:command==PN_SETUI_UPDATE_REBOOT?u->update.reboot:u->update.running || u->update.install || u->update.reboot;
        if(!valid)return PN_EMPTY;
        ui->request=command;return PN_OK;
    }
    if(command==PN_SETUI_TIME){u->screen=SCREEN_TIME;return show(ui,present,ctx);}
    if(command==PN_SETUI_TIME_SYNC || command==PN_SETUI_TIME_CANCEL){if(!ui->presented)return PN_BUSY;if(u->screen!=SCREEN_TIME || (command==PN_SETUI_TIME_SYNC?(!u->time.available || u->time.running):!u->time.running))return PN_EMPTY;ui->request=command;return PN_OK;}
    if(command==PN_SETUI_WALLPAPER || command==PN_SETUI_FONTS || command==PN_SETUI_SHELF || command==PN_SETUI_TRANSFER || command==PN_SETUI_HOME){ui->request=command;return PN_OK;}
    if(command==PN_SETUI_APPEARANCE){u->screen=SCREEN_APPEARANCE;return show(ui,present,ctx);}
    if(command==PN_SETUI_KEYS || command==PN_SETUI_REFRESH || command==PN_SETUI_ABOUT){u->screen=command==PN_SETUI_KEYS?SCREEN_KEYS:command==PN_SETUI_REFRESH?SCREEN_REFRESH:SCREEN_ABOUT;return show(ui,present,ctx);}
    if(command>=PN_SETUI_COLUMNS && command<PN_SETUI_STYLE+PN_SHELF_STYLE_COUNT){
        pn_shelf_prefs_t next=ui->shelf;if(command<PN_SETUI_STYLE)next.columns=(uint8_t)(3+command-PN_SETUI_COLUMNS);else next.style=(uint8_t)(command-PN_SETUI_STYLE);
        if(next.columns!=ui->shelf.columns || next.style!=ui->shelf.style)status=commit_shelf(ui,u,next);
        pn_status_t shown=show_controls(ui,u,old_flags,old_shelf,command,local,present,ctx);return status==PN_OK?shown:status;
    }
    if(command==PN_SETUI_FULL_REFRESH)return show_profile(ui,present,ctx,PN_REFRESH_GC16);
    if(command==PN_SETUI_TURN || command==PN_SETUI_TURN+1){
        uint8_t next=(uint8_t)((ui->flags&~PN_INPUT_TURN_MASK)|(command==PN_SETUI_TURN?0u:PN_INPUT_TURN_MASK));
        if(next==ui->flags)return PN_EMPTY;
        status=commit(ui,u,next);
        pn_status_t shown=show_controls(ui,u,old_flags,old_shelf,command,local,present,ctx);return status==PN_OK?shown:status;
    }
    if(command>=PN_SETUI_LEVEL && command<PN_SETUI_LEVEL+3){
        uint8_t next=(uint8_t)((ui->flags&~PN_INPUT_REFRESH_MASK)|((unsigned)(command-PN_SETUI_LEVEL)<<4));
        if(next!=ui->flags)status=commit(ui,u,next);
        pn_status_t shown=show_controls(ui,u,old_flags,old_shelf,command,local,present,ctx);return status==PN_OK?shown:status;
    }
    if(command<PN_SETUI_TOGGLE || command>PN_SETUI_TOGGLE+3)return PN_EMPTY;
    uint8_t next=(uint8_t)(ui->flags^bits[command-PN_SETUI_TOGGLE]);
    // 关闭全部翻页方式会让正文无法翻页，拒绝。/ Turning every page-turn method off would leave no way to turn pages, so refuse it.
    if((next&(PN_INPUT_NO_SWIPE|PN_INPUT_NO_EDGE_TAP|PN_INPUT_NO_KEYS))==(PN_INPUT_NO_SWIPE|PN_INPUT_NO_EDGE_TAP|PN_INPUT_NO_KEYS)){snprintf(u->message,sizeof u->message,"至少保留一种翻页方式");status=PN_LIMIT;}
    else status=commit(ui,u,next);
    pn_status_t shown=show_controls(ui,u,old_flags,old_shelf,command,local,present,ctx);return status==PN_OK?shown:status;
}
pn_status_t pn_settings_ui_present(pn_settings_ui_t *ui,pn_settings_present_fn present,void *ctx){if(!ui || !ui->impl || !present)return PN_INVALID;return show(ui,present,ctx);}
int pn_settings_ui_hit(const pn_settings_ui_t *ui,int x,int y){
    if(!ui || !ui->impl || x<0 || x>=W || y<0 || y>=H)return -1;
    const su_t *u=ui->impl;
    if(u->screen==SCREEN_MAIN){
        if(y>=TAB_Y){int tab=pn_w_navigation_hit(x,y);return tab==PN_NAV_HOME?PN_SETUI_HOME:tab==PN_NAV_LIBRARY?PN_SETUI_SHELF:tab==PN_NAV_TRANSFER?PN_SETUI_TRANSFER:-1;}
        if(x<32 || x>=652)return -1;
        if(y>=DISPLAY_Y && y<DISPLAY_Y+ROW_H)return PN_SETUI_FONTS;
        if(y>=DISPLAY_Y+ROW_H && y<DISPLAY_Y+2*ROW_H)return PN_SETUI_WALLPAPER;
        if(y>=DISPLAY_Y+2*ROW_H && y<DISPLAY_Y+3*ROW_H)return PN_SETUI_APPEARANCE;
        if(y>=READING_Y && y<READING_Y+ROW_H)return PN_SETUI_KEYS;
        if(y>=READING_Y+ROW_H && y<READING_Y+2*ROW_H)return PN_SETUI_REFRESH;
        if(y>=DEVICE_Y && y<DEVICE_Y+ROW_H)return PN_SETUI_WIFI;
        if(y>=DEVICE_Y+ROW_H && y<DEVICE_Y+device_rows(u)*ROW_H)return PN_SETUI_ABOUT;
        return -1;
    }
    if(pn_w_header_hit(x,y,false)==1)return PN_SETUI_BACK;
    if(u->screen==SCREEN_WIFI_EDIT){
        int key=pn_keyboard_hit(&u->kb,KB_Y,x,y);
        if(key>=32)return PN_SETUI_CHAR+key;
        if(key!=PN_KB_NONE)return key==PN_KB_SHIFT?PN_SETUI_KB_SHIFT:key==PN_KB_LAYER?PN_SETUI_KB_LAYER:PN_SETUI_KB_DELETE;
        if(u->testing)return y>=EDIT_BTN_Y && y<EDIT_BTN_Y+88 && x>=240 && x<436?PN_SETUI_WIFI_TEST_CANCEL:-1;
        if(u->password_step && u->test_failed && y>=FORCE_Y && y<FORCE_Y+72 && x>=PN_UI_MARGIN && x<652)return PN_SETUI_WIFI_FORCE;
        if(y>=EDIT_BTN_Y && y<EDIT_BTN_Y+88){if(x>=PN_UI_MARGIN && x<228)return PN_SETUI_WIFI_CLEAR;if(x>=240 && x<436 && u->password_step)return PN_SETUI_WIFI_REVEAL;if(x>=448 && x<652)return PN_SETUI_WIFI_NEXT;}
        return -1;
    }
    if(x<32 || x>=652)return -1;
    if(u->screen==SCREEN_WIFI){if(y>=WIFI_EDIT_Y && y<WIFI_EDIT_Y+88)return PN_SETUI_WIFI_EDIT;if(*u->lan && y>=WIFI_FORGET_Y && y<WIFI_FORGET_Y+88)return PN_SETUI_WIFI_FORGET;return -1;}
    if(u->screen==SCREEN_ABOUT && y>=840 && y<840+ROW_H)return PN_SETUI_TIME;
    if(u->screen==SCREEN_ABOUT && y>=960 && y<960+ROW_H)return PN_SETUI_UPDATE;
    if(u->screen==SCREEN_UPDATE){if(y>=810 && y<898 && (u->update.running || u->update.install || u->update.reboot))return PN_SETUI_UPDATE_CANCEL;if(y>=700 && y<788 && !u->update.running)return u->update.reboot?PN_SETUI_UPDATE_REBOOT:u->update.install?PN_SETUI_UPDATE_INSTALL:u->update.available?PN_SETUI_UPDATE_CHECK:-1;return -1;}
    if(u->screen==SCREEN_TIME){if(y>=700 && y<788)return u->time.running?PN_SETUI_TIME_CANCEL:u->time.available?PN_SETUI_TIME_SYNC:-1;return -1;}
    if(u->screen==SCREEN_APPEARANCE){int cell=pn_w_segments_hit(2,190,108,x,y);if(cell>=0)return PN_SETUI_COLUMNS+cell;
        if(y>=460 && y<900){int row=(y-460)/230,col=(x-32)/318;
            if(row<2 && col<2 && (y-460)%230<210 && (x-32)%318<302)return PN_SETUI_STYLE+row*2+col;}
        return -1;
    }
    if(u->screen==SCREEN_KEYS){if(y>=TOGGLE_Y && y<TOGGLE_Y+4*ROW_H)return PN_SETUI_TOGGLE+(y-TOGGLE_Y)/ROW_H;return -1;}
    if(u->screen==SCREEN_REFRESH){
        static const int order[3]={1,0,2};
        int cell=pn_w_segments_hit(3,LEVEL_Y-6,LEVEL_H+12,x,y);if(cell>=0)return PN_SETUI_LEVEL+order[cell];
        if(y>=FULL_Y && y<FULL_Y+88)return PN_SETUI_FULL_REFRESH;
        cell=pn_w_segments_hit(2,TURN_Y-6,TURN_H+12,x,y);if(cell>=0)return PN_SETUI_TURN+cell;
        return -1;
    }
    return -1;
}
void pn_settings_ui_close(pn_settings_ui_t *ui){
    if(!ui || !ui->impl)return;
    su_t *u=ui->impl;
    // 引擎保持地址与缓存owner，归还不读文件、不分配，也不受取消状态阻挡。
    // Keep the engine address and cache owner; returning it neither reads nor allocates and is not blocked by cancellation.
    if(!u->return_font || pn_font_move(u->return_font,&u->font,u->pool)!=PN_OK)pn_font_close(&u->font);
    wipe_edit(u);pn_free(u->pixels);pn_free(u);*ui=(pn_settings_ui_t){0};
}
pn_status_t pn_settings_ui_set_lan(pn_settings_ui_t *ui,const char *ssid,pn_settings_present_fn present,void *ctx){
    if(!ui || !ui->impl || !present)return PN_INVALID;
    su_t *u=ui->impl;
    // 网络名按UTF-8字符边界截短到缓冲区。/ Clip the network name to the buffer at a UTF-8 character boundary.
    size_t n=ssid?strlen(ssid):0;if(n>=sizeof u->lan){n=sizeof u->lan-1;while(n>0 && ((unsigned char)ssid[n]&0xc0)==0x80)n--;}
    // 设备卡WiFi行显示网络名：只有名称变化才标记下次完整重画，相同快照不重复绘制/转换整帧。
    // The device card's WiFi row shows the name: only a changed name marks the next show for a full repaint; identical snapshots never repaint/convert a frame.
    if(strlen(u->lan)!=n || memcmp(u->lan,ssid?ssid:"",n))u->snapshot_dirty=true;
    if(n)memcpy(u->lan,ssid,n);
    u->lan[n]=0;
    (void)ctx;return PN_OK;
}
void pn_settings_ui_set_about(pn_settings_ui_t *ui,const pn_settings_about_t *about){
    if(!ui || !ui->impl || !about)return;
    su_t *u=ui->impl;u->about=*about;u->snapshot_dirty=true;
    // 外部字符串保证结尾。/ Make sure caller strings are terminated.
    u->about.version[sizeof u->about.version-1]=0;u->about.storage[sizeof u->about.storage-1]=0;u->about.internal[sizeof u->about.internal-1]=0;u->about.screen[sizeof u->about.screen-1]=0;
}
int pn_settings_ui_screen(const pn_settings_ui_t *ui){return ui && ui->impl?((const su_t *)ui->impl)->screen:0;}
void pn_settings_ui_set_time(pn_settings_ui_t *ui,const pn_settings_time_t *time){if(!ui || !ui->impl || !time)return;su_t *u=ui->impl;u->time=*time;u->snapshot_dirty=true;u->time.datetime[sizeof u->time.datetime-1]=0;u->time.message[sizeof u->time.message-1]=0;}
pn_status_t pn_settings_ui_wifi_candidate(const pn_settings_ui_t *ui,pn_network_credentials_t *out){
    if(!ui || !ui->impl || !out)return PN_INVALID;
    const su_t *u=ui->impl;if(!u->testing)return PN_EMPTY;
    memset(out,0,sizeof *out);memcpy(out->ssid,u->ssid,sizeof out->ssid);memcpy(out->password,u->password,sizeof out->password);return PN_OK;
}
pn_status_t pn_settings_ui_wifi_result(pn_settings_ui_t *ui,pn_status_t result,pn_settings_present_fn present,void *ctx){
    if(!ui || !ui->impl || !present)return PN_INVALID;
    su_t *u=ui->impl;if(!u->testing)return PN_EMPTY;
    u->testing=false;u->message[0]=0;
    if(result==PN_OK)return commit_wifi(ui,u,present,ctx);
    u->test_failed=result!=PN_CANCELLED;
    const char *why=result==PN_CANCELLED?"已取消测试，未保存，原网络记录不变。":result==PN_EMPTY?"找不到这个网络：请核对名称（区分大小写）或靠近路由器。":result==PN_INVALID?"连接被拒绝：密码错误，或路由器加密方式不受支持。":result==PN_BUSY?"无线正被传书、对时或更新使用，请结束后再试。":result==PN_UNSUPPORTED?"此环境不能测试连接。":"连接超时：信号弱或路由器没有分配地址。";
    snprintf(u->message,sizeof u->message,"%s%s",why,u->test_failed?"未保存。":"");
    return show(ui,present,ctx);
}
void pn_settings_ui_set_update(pn_settings_ui_t *ui,const pn_settings_update_t *value){if(!ui || !ui->impl || !value)return;su_t *u=ui->impl;u->update=*value;u->update.message[sizeof u->update.message-1]=0;u->update.version[sizeof u->update.version-1]=0;u->snapshot_dirty=true;}

pn_status_t pn_settings_time_format(pn_settings_time_t *view,uint32_t utc){
    if(!view || utc<1704067200u || utc>4102444799u)return PN_INVALID;
    time_t local=(time_t)((uint64_t)utc+8u*3600u);struct tm date;char text[32];
    if(!gmtime_r(&local,&date) || !strftime(text,sizeof text,"%Y-%m-%d %H:%M",&date))return PN_IO;
    memset(view->datetime,0,sizeof view->datetime);memcpy(view->datetime,text,strlen(text)+1);return PN_OK;
}
