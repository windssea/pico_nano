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
#include "pn_builtin_art.h"
#include "pn_wallpaper.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#define W 684
#define H 1216
enum {SCREEN_MAIN=0,SCREEN_KEYS,SCREEN_REFRESH,SCREEN_ABOUT,SCREEN_APPEARANCE,SCREEN_TIME,SCREEN_UPDATE,SCREEN_WIFI,SCREEN_WIFI_EDIT,SCREEN_GLOBAL_STYLE,SCREEN_GLOBAL_MARGINS,SCREEN_GLOBAL_BG,SCREEN_HOME_MOTTO,SCREEN_HOME_MOTTO_EDIT,SCREEN_HOME_BANNER};
typedef struct {pn_pool_t *pool;pn_media_t *state;char dir[PN_JOURNAL_PATH_MAX];pn_font_t font;uint8_t *pixels;pn_frame_t canvas;char message[96];char lan[40];int screen;pn_settings_about_t about;pn_settings_time_t time;
    pn_settings_update_t update; ///< 控制层升级快照 / Controller update snapshot
    pn_font_t *return_font; ///< 单owner移交的空槽，close归还或正常释放 / Empty slot transferred by the single owner, returned on close or normally released
    bool index_known; ///< 索引偏好可读才允许修改 / Permit changes only when the index preference is readable
    bool vertical_known; ///< 翻页方向偏好可读才允许修改 / Permit changes only when the turn-direction preference is readable
    bool snapshot_dirty; ///< 外部快照更新后先完整重画 / Repaint completely after external snapshot changes
    pn_w_status_snapshot_t status; ///< 最近成功画布的实时状态带 / Live status band of the last successful canvas
    pn_keyboard_t kb; ///< WiFi与主页文案共用输入键盘 / Shared on-screen keyboard for WiFi and Home motto
    char ssid[PN_NETWORK_SSID_MAX+1]; ///< 编辑中的网络名 / Network name being edited
    char password[PN_NETWORK_PASSWORD_MAX+1]; ///< 编辑中的密码，离开即清零 / Password being edited, wiped on leaving
    bool password_step,reveal,forget_armed; ///< 第二步/明文显示/忘记待确认 / Second step, plain display, forget awaiting confirmation
    bool testing,test_failed; ///< 连接测试中/上次测试失败可仍然保存 / Connection test running; last test failed and saving anyway is offered
    pn_style_t global_style; ///< 全局默认排版配置 / Global default typesetting configuration
    bool global_style_known; ///< 全局排版存储可读 / Global style storage readable
    char motto_draft[PN_HOME_HEADER_MAX]; ///< 编辑中的主页左上角文案 / Draft custom Home top-left text
    pn_media_t *wallpaper_media; ///< 可选壁纸介质，不持长期租约 / Optional wallpaper media without long-lived leases
    char wallpaper_dir[PN_JOURNAL_PATH_MAX]; ///< 壁纸目录路径 / Wallpaper directory path
    char banner_names[6][PN_HOME_BANNER_MAX]; ///< 当前页最多6张可选背景文件名 / Up to 6 banner image basenames on current page
    size_t banner_count; ///< 当前页图片数 / Banner image count on current page
    bool banner_more,banner_has_prev; ///< 是否有下一页/上一页 / Whether next/previous banner pages exist
    uint8_t *banner_preview_px; ///< 正在阅读上方背景页的实时预览缓冲（684×146）/ Live banner preview buffer for Home banner picker
    pn_frame_t banner_preview; ///< 预览画布视图 / Preview frame view
    char banner_preview_name[PN_HOME_BANNER_MAX]; ///< 当前预览缓冲对应的文件名 / Filename currently loaded in preview buffer
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
#define MAIN_ROW_H 92
#define DISPLAY_Y 172
#define READING_Y 504
#define DEVICE_Y 836
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
/* 设置主页分组标题与柔和卡片行：无双重黑框，灰阶层次更舒缓，单行92px保持易点按。
 * Root settings group titles and soft card rows: avoids double dark borders, softens gray hierarchy, keeps 92px touch height. */
static pn_status_t draw_main_group(pn_font_t *font,pn_frame_t *f,const char *title,int baseline){
    int original=font->pixels;pn_status_t s=pn_font_size(font,22);
    if(s==PN_OK)s=pn_w_text_ex(font,f,title,PN_UI_MARGIN+10,baseline,PN_UI_WIDTH-2*PN_UI_MARGIN-20,PN_ALIGN_LEFT,5,true);
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
static void draw_main_card(pn_frame_t *f,int y,int rows){
    if(!f || rows<=0)return;
    int h=rows*MAIN_ROW_H,w=PN_UI_WIDTH-2*PN_UI_MARGIN;
    pn_w_round_fill(f,PN_UI_MARGIN+2,y+2,w-4,h,20,PN_UI_SELECT);
    pn_w_round_fill(f,PN_UI_MARGIN,y,w,h,20,PN_UI_PAPER);
    pn_w_round_stroke(f,PN_UI_MARGIN,y,w,h,20,1.5f,9);
}
static pn_status_t draw_main_row(pn_font_t *font,pn_frame_t *f,const char *label,const char *subtitle,pn_icon_t icon,int y,bool last){
    int original=font->pixels,text_x=112,room=PN_UI_WIDTH-PN_UI_MARGIN-48-text_x;
    pn_w_round_fill(f,50,y+23,46,46,14,PN_UI_SURFACE);
    pn_w_icon(f,icon,60,y+33,26,PN_UI_INK);
    pn_status_t s=pn_font_size(font,30);
    if(s==PN_OK)s=pn_w_text_ex(font,f,label,text_x,y+42,room,PN_ALIGN_LEFT,PN_UI_INK,false);
    if(s==PN_OK && subtitle && *subtitle){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,f,subtitle,text_x,y+72,room,PN_ALIGN_LEFT,5,false);}
    pn_w_icon(f,PN_ICON_CHEVRON,PN_UI_WIDTH-PN_UI_MARGIN-18-24,y+34,24,6);
    if(!last)pn_frame_rect(f,text_x,y+MAIN_ROW_H-1,PN_UI_WIDTH-PN_UI_MARGIN-20-text_x,1,PN_UI_SELECT);
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
static pn_status_t draw_main(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;pn_profile_span_t span;
    pn_profile_begin(&span,NULL,"ui.settings.status");pn_status_t s=pn_w_status(font,f);pn_profile_end(&span,s);
    pn_profile_begin(&span,NULL,"ui.settings.title");
    if(s==PN_OK)s=pn_font_size(font,48);
    if(s==PN_OK)s=pn_w_text_ex(font,f,"设置",PN_UI_MARGIN,108,400,PN_ALIGN_LEFT,PN_UI_INK,true);
    pn_profile_end(&span,s);pn_profile_begin(&span,NULL,"ui.settings.card");
    if(s==PN_OK)s=draw_main_group(font,f,"显示与字体",DISPLAY_Y-14);
    draw_main_card(f,DISPLAY_Y,3);pn_profile_end(&span,s);pn_profile_begin(&span,NULL,"ui.settings.rows");
    if(s==PN_OK)s=draw_main_row(font,f,"字体管理","系统字体、全局阅读字体与删除",PN_ICON_FONT,DISPLAY_Y,false);
    if(s==PN_OK)s=draw_main_row(font,f,"锁屏壁纸","系统默认、书签或自定义图片",PN_ICON_IMAGE,DISPLAY_Y+MAIN_ROW_H,false);
    char appearance[64];snprintf(appearance,sizeof appearance,"每行 %u 本 · %s · 主页个性",ui->shelf.columns,style_names[ui->shelf.style]);
    if(s==PN_OK)s=draw_main_row(font,f,"书架与主页",appearance,PN_ICON_SHELF,DISPLAY_Y+2*MAIN_ROW_H,true);
    if(s==PN_OK)s=draw_main_group(font,f,"阅读",READING_Y-14);
    draw_main_card(f,READING_Y,3);
    char gstyle[64];snprintf(gstyle,sizeof gstyle,"字号 %u · 行距 %u%% · 边距 %u%s",u->global_style.pixels,u->global_style.line_percent,u->global_style.margin,u->global_style.hide_bars?" · 无栏":"");
    char line[64];snprintf(line,sizeof line,"%s · 每 %u 页整屏刷新",level_names[level_of(ui->flags)],pn_settings_refresh_pages(ui->flags));
    if(s==PN_OK)s=draw_main_row(font,f,"全局排版",gstyle,PN_ICON_TYPESET,READING_Y,false);
    if(s==PN_OK)s=draw_main_row(font,f,"按键与手势","翻页方式、左手模式与屏下三键",PN_ICON_SETTINGS,READING_Y+MAIN_ROW_H,false);
    if(s==PN_OK)s=draw_main_row(font,f,"刷新与屏幕",line,PN_ICON_REFRESH,READING_Y+2*MAIN_ROW_H,true);
    if(s==PN_OK)s=draw_main_group(font,f,"设备",DEVICE_Y-14);
    draw_main_card(f,DEVICE_Y,device_rows(u));
    int y=DEVICE_Y;char wifi[64];
    if(*u->lan)snprintf(wifi,sizeof wifi,"已保存：%s",u->lan);else snprintf(wifi,sizeof wifi,"未设置 · 用于传书、对时与更新");
    if(s==PN_OK)s=draw_main_row(font,f,"WiFi 网络",wifi,PN_ICON_WIFI,y,false);
    y+=MAIN_ROW_H;
    if(s==PN_OK)s=draw_main_row(font,f,"存储与关于","版本、存储与设备信息",PN_ICON_LOCK,y,true);
    // 正常时不显示内部说明，只在有问题时提示。/ No internal note when all is well; only problems are shown.
    const char *problem=*u->message?u->message:u->state?"":"内部存储不可用，设置不能保存";
    if(s==PN_OK && *problem)s=note(u,problem,y+MAIN_ROW_H+44);
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
    static const char *const legend[4][2]={{"KEY1","上一页 · 焦点上移/左移"},{"KEY2","确认/回书架 · 长按返回/工具栏"},{"KEY3","下一页 · 焦点下移/右移"},{"电源键","锁屏并保存，再按回到原处"}};
    for(int i=0;i<4 && s==PN_OK;i++){
        int base=476+i*36;s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_ex(font,f,legend[i][0],PN_UI_MARGIN+8,base,110,PN_ALIGN_LEFT,PN_UI_INK,true);
        if(s==PN_OK)s=pn_w_text_ex(font,f,legend[i][1],PN_UI_MARGIN+128,base,484,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    }
    if(s==PN_OK)s=pn_w_group(font,f,"触控与翻页",TOGGLE_Y-14);
    pn_w_card(f,TOGGLE_Y,5);
    for(unsigned i=0;i<4 && s==PN_OK;i++){
        const char *hint=hints[i];
        if(ui->vertical_turn && i==1)hint="上下滑动翻页";
        else if(ui->vertical_turn && i==2)hint="点屏幕上半或下半翻页";
        s=pn_w_card_row(font,f,names[i],hint,NULL,-1,on(ui->flags,i)?PN_ROW_ON:PN_ROW_OFF,TOGGLE_Y+(int)i*ROW_H,false);
    }
    if(s==PN_OK)s=pn_w_card_row(font,f,"上下翻页","开启后改为上下滑动与上下点按翻页",NULL,-1,ui->vertical_turn?PN_ROW_ON:PN_ROW_OFF,TOGGLE_Y+4*ROW_H,true);
    if(s==PN_OK && *u->message)s=note(u,u->message,TOGGLE_Y+5*ROW_H+42);
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
static pn_status_t draw_about(pn_settings_ui_t *ui,su_t *u){
    (void)ui;
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_header(font,f,"< 设置","存储与关于",NULL);
    const char *rows[4][2]={{PN_BRAND_NAME,*u->about.version?u->about.version:"版本未知"},{"屏幕",*u->about.screen?u->about.screen:"4.7 英寸墨水屏 · 16 级灰阶"},
        {"存储卡",*u->about.storage?u->about.storage:"未读取到存储卡"},{"内部存储",*u->about.internal?u->about.internal:(u->state?"可用":"不可用")}};
    pn_w_card(f,ABOUT_Y,4);
    for(int i=0;i<4 && s==PN_OK;i++)s=pn_w_card_row(font,f,rows[i][0],rows[i][1],NULL,-1,0u,ABOUT_Y+i*ROW_H,i==3);
    if(s==PN_OK)s=note(u,"HarmonyOS Sans 字体 · 数据仅保存在本机",ABOUT_Y+4*ROW_H+52);
    pn_w_card(f,710,1);if(s==PN_OK)s=pn_w_card_row(font,f,"日期与时间",*u->time.datetime?u->time.datetime:"时钟尚未校准",NULL,PN_ICON_SETTINGS,PN_ROW_CHEVRON,710,true);
    pn_w_card(f,840,1);if(s==PN_OK)s=pn_w_card_row(font,f,"系统更新","通过家庭 WiFi 获取 Gitee 更新",NULL,PN_ICON_SETTINGS,PN_ROW_CHEVRON,840,true);
    return s;
}
static pn_status_t draw_update(su_t *u){
    pn_status_t s=pn_w_header(&u->font,&u->canvas,"< 关于","系统更新",NULL);
    if(s==PN_OK)s=note(u,*u->update.version?u->update.version:"PicoNano 固件更新",280);
    if(s==PN_OK){if(u->update.running)s=pn_w_loading(&u->font,&u->canvas,"正在处理更新",u->update.message,u->update.determinate?(int)u->update.percent:-1,350);else s=note(u,*u->update.message?u->update.message:"连接已保存的家庭 WiFi，检查 Gitee 最新版本。",390);}
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
    if(s==PN_OK)s=pn_w_button(font,f,u->testing?"连接中…":pw?"测试并保存":"下一步",448,EDIT_BTN_Y,204,88,u->testing?PN_W_DISABLED:PN_W_SELECTED);
    if(s==PN_OK && u->testing)s=pn_w_loading(font,f,"正在连接网络",u->message,-1,720);
    if(s==PN_OK && !u->testing)s=pn_keyboard_render(&u->kb,font,f,KB_Y);
    return s;
}
static pn_status_t draw_time(su_t *u){
    pn_status_t s=pn_w_header(&u->font,&u->canvas,"< 关于","日期与时间",NULL);
    if(s==PN_OK)s=pn_font_size(&u->font,38);
    if(s==PN_OK)s=pn_w_text(&u->font,&u->canvas,*u->time.datetime?u->time.datetime:"时间尚未校准",32,290,620,PN_ALIGN_CENTER);
    if(s==PN_OK)s=note(u,"北京时间 · UTC+8",348);
    if(s==PN_OK && u->time.running)s=pn_w_loading(&u->font,&u->canvas,"正在联网校时",u->time.message,-1,380);
    if(s==PN_OK && !u->time.running)s=note(u,*u->time.message?u->time.message:u->time.available?"使用已保存的家庭WiFi获取网络时间。完成后由设备时钟继续走时。":"请先在设置的“WiFi 网络”或传书网页中保存家庭WiFi，再联网校时。",450);
    if(s==PN_OK)s=pn_font_size(&u->font,32);
    if(s==PN_OK)s=pn_w_button(&u->font,&u->canvas,u->time.running?"结束对时":"立即联网校时",32,700,620,88,u->time.running || u->time.available?PN_W_SELECTED:PN_W_DISABLED);
    if(s==PN_OK)s=note(u,"对时期间可以操作其他设置。网络失败时保留原显示时间，稍后可重试。",862);
    return s;
}
/* 设置中直接预览静态样式；无贴图、无动画或额外封面解码。/ Preview static styles in settings without textures, animation or extra cover decoding. */
static pn_status_t draw_columns(pn_settings_ui_t *ui,su_t *u){
    static const char *const labels[]={"每行 3 本","每行 4 本"},*const hints[]={"封面更大 · 每页 9 本","浏览更多 · 每页 12 本"};
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
static const char *const motto_presets[6]={"腹有诗书气自华","开卷有益","行万里路，读万卷书","静以修身，俭以养德","且将新火试新茶","山高月小，水落石出"};
static pn_status_t draw_appearance(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;pn_status_t s=pn_w_header(font,f,"< 设置","书架与主页",NULL);
    if(s==PN_OK)s=pn_w_group(font,f,"书架密度",176);
    if(s==PN_OK)s=draw_columns(ui,u);
    if(s==PN_OK)s=note(u,"选项会自动保存。网格使用此密度，列表与搜索结果仍每页显示 5 本。",342);
    if(s==PN_OK)s=pn_w_group(font,f,"书架样式",432);
    for(unsigned i=0;i<PN_SHELF_STYLE_COUNT && s==PN_OK;i++)s=draw_style(ui,u,i);
    if(s==PN_OK)s=pn_w_group(font,f,"主页个性设置",928);
    pn_w_card(f,944,2);
    if(s==PN_OK)s=pn_w_card_row(font,f,"左上角文案",*ui->home.header?ui->home.header:"默认：PicoNano",NULL,PN_ICON_FONT,PN_ROW_CHEVRON,944,false);
    if(s==PN_OK)s=pn_w_card_row(font,f,"正在阅读上方背景",*ui->home.banner?ui->home.banner:"默认山水",NULL,PN_ICON_IMAGE,PN_ROW_CHEVRON,944+ROW_H,true);
    if(s==PN_OK && *u->message)s=note(u,u->message,1178);
    return s;
}
/* 全局排版：与书内排版完全同源的12格+图片/上下栏开关+实时预览，外加边距详调与内置背景子页。
 * Global typesetting: full parity with in-book typesetting (12 cells, image/bars toggles, live preview, 4-side margins & built-in backgrounds). */
static void draw_guide_preview_row(pn_frame_t *frame,unsigned style,unsigned shade,int x0,int width,int y){
    if(style<1 || style>2 || y<0 || y>=frame->height)return;
    static const uint8_t inks[4]={10,7,4,0};uint8_t ink=inks[shade<4?shade:0];
    int dash=style==1?19:2,period=style==1?31:13;
    for(int x=0;x<width;x+=period){int w=width-x;if(w>dash)w=dash;pn_frame_rect(frame,x0+x,y,w,1,ink);}
}
static pn_status_t draw_gstyle_cell(pn_font_t *font,pn_frame_t *f,const char *label,const char *value,const char *extra_right,bool choice,bool chevron_only,int x,int y){
    pn_w_round_stroke(f,x,y,300,76,12,1.5f,9);
    pn_status_t s=pn_font_size(font,20);
    if(s==PN_OK)s=pn_w_text_ex(font,f,label,x+12,y+23,276,PN_ALIGN_LEFT,5,false);
    if(s==PN_OK && extra_right)s=pn_w_text_ex(font,f,extra_right,x+12,y+23,276,PN_ALIGN_RIGHT,4,true);
    if(chevron_only){
        pn_w_icon(f,PN_ICON_CHEVRON,x+262,y+41,22,6);
        if(s==PN_OK)s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_ex(font,f,value,x+12,y+63,244,PN_ALIGN_LEFT,PN_UI_INK,true);
        return s;
    }
    pn_w_round_fill(f,x+8,y+30,62,38,8,PN_UI_SURFACE);pn_w_round_stroke(f,x+8,y+30,62,38,8,1.0f,11);
    pn_w_round_fill(f,x+230,y+30,62,38,8,PN_UI_SURFACE);pn_w_round_stroke(f,x+230,y+30,62,38,8,1.0f,11);
    pn_w_icon(f,choice?PN_ICON_BACK:PN_ICON_MINUS,x+28,y+38,22,PN_UI_INK);
    pn_w_icon(f,choice?PN_ICON_CHEVRON:PN_ICON_PLUS,x+250,y+38,22,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text_ex(font,f,value,x+72,y+58,156,PN_ALIGN_CENTER,PN_UI_INK,true);
    return s;
}
static pn_status_t draw_global_style(su_t *u){
    static const char *const gpresets[3]={"舒适","紧凑","大字"};
    static const char *const weights[5]={"细","标准","稍粗","粗","特粗"};
    static const char *const clarities[4]={"标准","清晰","锐利","黑白"};
    static const char *const shades[4]={"浅","中","深","黑"};
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;const pn_style_t *st=&u->global_style;
    pn_status_t s=pn_w_header(font,f,"< 设置","全局排版",NULL);
    if(s==PN_OK){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,f,"恢复默认",480,100,172,PN_ALIGN_RIGHT,5,false);}
    // 顶部实时段落预览（含阅读线与左右边距示意）。/ Top live paragraph preview including reading guide lines and margins.
    pn_w_round_fill(f,34,140,616,154,16,PN_UI_SELECT);
    pn_w_round_fill(f,32,138,620,154,16,PN_UI_PAPER);
    pn_w_round_stroke(f,32,138,620,154,16,1.5f,9);
    if(s==PN_OK){s=pn_font_size(font,20);if(s==PN_OK)s=pn_w_text_ex(font,f,"全局排版实时预览",48,164,300,PN_ALIGN_LEFT,5,true);}
    if(s==PN_OK)s=pn_w_text_ex(font,f,"未特配书籍自动生效",360,164,276,PN_ALIGN_RIGHT,5,false);
    if(s==PN_OK){
        int saved_px=font->pixels,px=(int)st->pixels*2/3;if(px>34)px=34;if(px<22)px=22;
        int inset=pn_style_margin_left(st)/2;if(inset<14)inset=14;if(inset>52)inset=52;
        int pitch=px*(int)st->line_percent/100;if(pitch<px+4)pitch=px+4;
        unsigned room=(unsigned)(112/pitch);if(room<2 && pitch<=56)room=2;if(room<1)room=1;if(room>3)room=3;
        unsigned used=0;
        s=pn_font_size(font,px);
        if(s==PN_OK)s=pn_w_text_flow(font,f,"字与字之间，留出恰好的距离。让阅读回到舒适，把时间留给文字。",32+inset,174+px,620-2*inset,room,pitch,px*(int)st->indent_em,px*64*(int)st->tracking_percent/100,&used);
        if(s==PN_OK && st->reading_line){
            int ascent=px*4/5,descent=px/5;(void)pn_font_vertical(font,&ascent,&descent);
            int gap=pitch-ascent-descent;if(gap<0)gap=0;
            for(unsigned li=0;li<used;li++){
                int gy=174+px+(int)li*pitch+descent+gap/2+st->reading_line_offset;
                if(gy>174 && gy<288)draw_guide_preview_row(f,st->reading_line,st->reading_line_shade,32+inset,620-2*inset,gy);
            }
        }
        pn_status_t restored=pn_font_size(font,saved_px);if(s==PN_OK)s=restored;
    }
    int chosen=st->pixels==44 && st->line_percent==145 && st->gap_percent==40 && st->margin==32?0:st->pixels==36 && st->line_percent==125 && st->gap_percent==20 && st->margin==24?1:st->pixels==56 && st->line_percent==160 && st->gap_percent==40 && st->margin==32?2:-1;
    if(s==PN_OK)s=pn_w_segments(font,f,gpresets,NULL,3,chosen,306,56);
    // 与书内排版完全一致的12项双列网格。/ Exact 12-cell two-column grid matching in-book typesetting.
    char vals[12][32];
    snprintf(vals[0],sizeof vals[0],"%u",(unsigned)st->pixels);
    snprintf(vals[1],sizeof vals[1],"%u.%02u",(unsigned)(st->line_percent/100u),(unsigned)(st->line_percent%100u));
    snprintf(vals[2],sizeof vals[2],"%u.%02u",(unsigned)(st->gap_percent/100u),(unsigned)(st->gap_percent%100u));
    if(st->margin_top || st->margin_bottom || st->margin_right_delta || st->margin<16 || st->margin>80)snprintf(vals[3],sizeof vals[3],"自定义");
    else snprintf(vals[3],sizeof vals[3],st->margin<=20?"窄":st->margin<=32?"中":"宽");
    snprintf(vals[4],sizeof vals[4],"%u 字",(unsigned)st->indent_em);
    snprintf(vals[5],sizeof vals[5],"%u%%",(unsigned)st->tracking_percent);
    snprintf(vals[6],sizeof vals[6],"%s",weights[(st->weight+1)>=0 && (st->weight+1)<5?st->weight+1:1]);
    snprintf(vals[7],sizeof vals[7],"%s",clarities[st->clarity<4?st->clarity:0]);
    snprintf(vals[8],sizeof vals[8],"%s",st->reading_line==1?"虚线":st->reading_line==2?"点线":"关闭");
    snprintf(vals[9],sizeof vals[9],"%s",shades[st->reading_line_shade<4?st->reading_line_shade:0]);
    snprintf(vals[10],sizeof vals[10],"%+d px",(int)st->reading_line_offset);
    snprintf(vals[11],sizeof vals[11],"%s",st->background==PN_READING_BACKGROUND_EXTERNAL?"自定义":pn_reading_background_name(st->background));
    static const char *const glabels[12]={"字号","行距","段距","边距","首行缩进","字间距","字重","清晰度","阅读线","阅读线深浅","上下位置","背景"};
    static const bool gchoice[12]={false,false,false,true,false,false,true,true,true,true,false,false};
    for(unsigned i=0;i<12 && s==PN_OK;i++){
        s=draw_gstyle_cell(font,f,glabels[i],vals[i],i==3?"详调 >":NULL,gchoice[i],i==11,32+(int)(i%2)*320,378+(int)(i/2)*84);
    }
    // 底行并排开关：书内图片 + 上下栏。/ Bottom toggle row: book images + top/bottom bars.
    pn_w_round_stroke(f,32,892,300,72,12,1.5f,9);
    if(s==PN_OK){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text(font,f,"书内图片",44,921,170,PN_ALIGN_LEFT);}
    if(s==PN_OK)s=pn_w_text_ex(font,f,st->hide_images?"不显示":"显示",44,934,276,PN_ALIGN_RIGHT,PN_UI_INK,true);
    if(s==PN_OK){s=pn_font_size(font,18);if(s==PN_OK)s=pn_w_text_ex(font,f,"关闭后保留占位",44,948,180,PN_ALIGN_LEFT,5,false);}
    pn_w_round_stroke(f,352,892,300,72,12,1.5f,9);
    if(s==PN_OK){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text(font,f,"上下栏",364,921,170,PN_ALIGN_LEFT);}
    if(s==PN_OK)s=pn_w_text_ex(font,f,st->hide_bars?"隐藏":"显示",364,934,276,PN_ALIGN_RIGHT,PN_UI_INK,true);
    if(s==PN_OK){s=pn_font_size(font,18);if(s==PN_OK)s=pn_w_text_ex(font,f,"状态栏与页脚",364,948,180,PN_ALIGN_LEFT,5,false);}
    if(s==PN_OK)s=note(u,"所有更改自动保存为全局默认；未单独排版的书籍开书即生效，单书可在阅读内改回“使用全局”。",1004);
    if(s==PN_OK && *u->message)s=note(u,u->message,1112);
    return s;
}
static pn_status_t draw_global_margin_cell(pn_font_t *font,pn_frame_t *f,const char *label,int px,int x,int y){
    char val[24];snprintf(val,sizeof val,"%d px",px);
    pn_w_round_stroke(f,x,y,300,108,12,1.5f,9);
    pn_status_t s=pn_font_size(font,22);
    if(s==PN_OK)s=pn_w_text_ex(font,f,label,x+14,y+28,272,PN_ALIGN_CENTER,5,false);
    pn_w_round_fill(f,x+8,y+36,96,64,10,PN_UI_SURFACE);pn_w_round_stroke(f,x+8,y+36,96,64,10,1.5f,9);
    pn_w_icon(f,PN_ICON_MINUS,x+42,y+54,28,PN_UI_INK);
    pn_w_round_fill(f,x+196,y+36,96,64,10,PN_UI_SURFACE);pn_w_round_stroke(f,x+196,y+36,96,64,10,1.5f,9);
    pn_w_icon(f,PN_ICON_PLUS,x+230,y+54,28,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_text_ex(font,f,val,x+104,y+78,92,PN_ALIGN_CENTER,PN_UI_INK,true);
    return s;
}
static pn_status_t draw_global_margins(su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;const pn_style_t *st=&u->global_style;
    pn_status_t s=pn_w_header(font,f,"< 返回","全局边距与栏位",NULL);
    if(s==PN_OK){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,f,"重置边距",480,100,172,PN_ALIGN_RIGHT,5,false);}
    const int px=44,py=138,pw=132,ph=222;
    pn_w_round_fill(f,px,py,pw,ph,8,PN_UI_PAPER);pn_w_round_stroke(f,px,py,pw,ph,8,2.0f,PN_UI_INK);
    if(st->background){
        int bx=px+st->bg_margin_left*pw/684,by=py+st->bg_margin_top*ph/1216;
        int bw=(684-(int)st->bg_margin_left-(int)st->bg_margin_right)*pw/684,bh=(1216-(int)st->bg_margin_top-(int)st->bg_margin_bottom)*ph/1216;
        if(bw>2 && bh>2){pn_frame_rect(f,bx,by,bw,bh,13);pn_w_outline(f,bx,by,bw,bh,1,8);}
    }
    int tx=px+pn_style_margin_left(st)*pw/684,ty=py+pn_style_body_top(st)*ph/1216;
    int tw=pn_style_body_width(st)*pw/684,th=pn_style_body_height(st)*ph/1216;
    if(tw>4 && th>4){pn_w_outline(f,tx,ty,tw,th,1,PN_UI_INK);for(int ly=ty+12;ly<ty+th-8;ly+=14)pn_frame_rect(f,tx+6,ly,tw-12,2,8);}
    if(s==PN_OK){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,f,"正文与阅读背景四周边距",204,196,448,PN_ALIGN_LEFT,PN_UI_INK,true);}
    if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_lines_ex(font,f,"支持分别精调正文与背景四向边距，自动保存为全局默认。",204,238,448,3,32,5,false);}
    if(s==PN_OK)s=pn_w_group(font,f,"正文边距",432);
    if(s==PN_OK)s=draw_global_margin_cell(font,f,"上边距",pn_style_margin_top(st),32,450);
    if(s==PN_OK)s=draw_global_margin_cell(font,f,"下边距",pn_style_margin_bottom(st),352,450);
    if(s==PN_OK)s=draw_global_margin_cell(font,f,"左边距",pn_style_margin_left(st),32,568);
    if(s==PN_OK)s=draw_global_margin_cell(font,f,"右边距",pn_style_margin_right(st),352,568);
    if(s==PN_OK)s=pn_w_group(font,f,"背景边距",714);
    if(s==PN_OK)s=draw_global_margin_cell(font,f,"背景上",st->bg_margin_top,32,732);
    if(s==PN_OK)s=draw_global_margin_cell(font,f,"背景下",st->bg_margin_bottom,352,732);
    if(s==PN_OK)s=draw_global_margin_cell(font,f,"背景左",st->bg_margin_left,32,850);
    if(s==PN_OK)s=draw_global_margin_cell(font,f,"背景右",st->bg_margin_right,352,850);
    if(s==PN_OK && *u->message)s=note(u,u->message,1020);
    return s;
}
static pn_status_t draw_global_bg(su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;const pn_style_t *st=&u->global_style;
    pn_status_t s=pn_w_header(font,f,"< 返回","全局阅读背景",NULL);
    if(s==PN_OK)s=pn_w_group(font,f,"选择内置阅读底纹（新书自动生效）",174);
    for(unsigned i=0;i<=PN_READING_BACKGROUND_COUNT && s==PN_OK;i++){
        unsigned id=pn_reading_background_id(i);
        bool chosen=st->background==id;
        int x=32+(int)(i%3)*212,y=194+(int)(i/3)*284;
        pn_w_round_fill(f,x,y,196,264,14,PN_UI_SURFACE);
        pn_w_round_stroke(f,x,y,196,264,14,chosen?3.0f:1.5f,chosen?PN_UI_INK:9);
        pn_frame_rect(f,x+18,y+16,160,196,PN_UI_PAPER);
        pn_w_outline(f,x+18,y+16,160,196,1,10);
        if(id){
            // 迷你预览内置纸纹。/ Miniature preview of built-in reading paper.
            pn_frame_t sub={f->pixels+(size_t)(y+18)*f->stride+(size_t)(x+20)/2u,156,192,f->stride};
            (void)pn_reading_background_draw(id,&sub);
        }
        s=pn_font_size(font,22);
        if(s==PN_OK)s=pn_w_text_ex(font,f,pn_reading_background_name(id),x+8,y+244,180,PN_ALIGN_CENTER,PN_UI_INK,chosen);
    }
    if(s==PN_OK)s=note(u,"外部自定义背景图片可在单书阅读排版中选用；此处全局底纹适用于所有未特配书籍。",1086);
    return s;
}
static pn_status_t draw_home_motto(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_header(font,f,"< 返回","左上角文案",NULL);
    if(s==PN_OK)s=pn_w_group(font,f,"当前显示",176);
    pn_w_round_fill(f,32,190,620,96,16,PN_UI_SURFACE);pn_w_round_stroke(f,32,190,620,96,16,2.0f,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,30);
    if(s==PN_OK)s=pn_w_text_ex(font,f,*ui->home.header?ui->home.header:PN_BRAND_NAME,52,250,580,PN_ALIGN_CENTER,PN_UI_INK,true);
    if(s==PN_OK)s=pn_w_group(font,f,"精选名言（轻点即生效）",328);
    for(unsigned i=0;i<6 && s==PN_OK;i++){
        int x=32+(int)(i%2)*318,y=344+(int)(i/2)*108;bool active=!strcmp(ui->home.header,motto_presets[i]);
        pn_w_round_fill(f,x,y,302,92,14,active?PN_UI_INK:PN_UI_SURFACE);
        pn_w_round_stroke(f,x,y,302,92,14,active?2.0f:1.0f,active?PN_UI_INK:PN_UI_STROKE);
        s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,f,motto_presets[i],x+12,y+55,278,PN_ALIGN_CENTER,active?PN_UI_PAPER:PN_UI_INK,active);
    }
    if(s==PN_OK)s=pn_font_size(font,30);
    if(s==PN_OK)s=pn_w_button(font,f,"自定义键盘输入",PN_UI_MARGIN,712,620,88,PN_W_SELECTED);
    if(s==PN_OK)s=pn_w_button(font,f,"恢复默认（PicoNano）",PN_UI_MARGIN,822,620,88,0u);
    if(s==PN_OK)s=note(u,"所有页面状态栏左上角同步显示此文案，留空或恢复默认将显示 PicoNano。",956);
    if(s==PN_OK && *u->message)s=note(u,u->message,1080);
    return s;
}
static pn_status_t draw_home_motto_edit(su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_header(font,f,"< 返回","自定义文案",NULL);
    pn_w_round_fill(f,PN_UI_MARGIN,FIELD_Y,620,FIELD_H,PN_UI_RADIUS,14);pn_w_round_stroke(f,PN_UI_MARGIN,FIELD_Y,620,FIELD_H,PN_UI_RADIUS,2.0f,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,34);
    if(s==PN_OK){
        if(!*u->motto_draft)s=pn_w_text_ex(font,f,"输入自定义座右铭或签名",56,FIELD_Y+60,560,PN_ALIGN_LEFT,PN_UI_MUTED,false);
        else s=field_text(u,u->motto_draft,56,FIELD_Y+60,560);
    }
    if(s==PN_OK)s=note(u,"支持中文全拼、英文、数字与标点，留空保存将恢复默认 PicoNano。",FIELD_Y+FIELD_H+64);
    if(s==PN_OK && *u->message)s=note(u,u->message,FIELD_Y+FIELD_H+140);
    if(s==PN_OK)s=pn_font_size(font,30);
    if(s==PN_OK)s=pn_w_button(font,f,"清空",PN_UI_MARGIN,EDIT_BTN_Y,296,88,(*u->motto_draft || *u->kb.pinyin)?0u:PN_W_DISABLED);
    if(s==PN_OK)s=pn_w_button(font,f,"保存",356,EDIT_BTN_Y,296,88,PN_W_SELECTED);
    if(s==PN_OK)s=pn_keyboard_render(&u->kb,font,f,KB_Y);
    return s;
}
static void banner_preview_release(su_t *u){
    pn_free(u->banner_preview_px);u->banner_preview_px=NULL;u->banner_preview=(pn_frame_t){0};u->banner_preview_name[0]=0;
}
/// 为“正在阅读上方背景”准备实时横幅预览，并同步写入内部缓存加速主页加载。
/// Prepare live top-banner preview and persist the decoded 4bpp cache for lag-free Home visits.
static void banner_preview_load(su_t *u,const char *name){
    if(!name || !*name){banner_preview_release(u);return;}
    if(u->banner_preview_px && !strcmp(u->banner_preview_name,name))return;
    if(!u->banner_preview_px){
        u->banner_preview_px=pn_alloc_optional(u->pool,PN_MEM_UI,PN_HOME_BANNER_BYTES);
        if(!u->banner_preview_px)return;
        if(!pn_frame_bind(&u->banner_preview,u->banner_preview_px,PN_HOME_BANNER_BYTES,PN_HOME_BANNER_WIDTH,PN_HOME_BANNER_HEIGHT)){banner_preview_release(u);return;}
    }
    u->banner_preview_name[0]=0;
    if(u->state && pn_home_banner_cache_load(u->state,u->dir,name,u->banner_preview_px,PN_HOME_BANNER_BYTES)==PN_OK){
        strcpy(u->banner_preview_name,name);return;
    }
    if(!u->wallpaper_media || !u->wallpaper_media->available || !*u->wallpaper_dir){banner_preview_release(u);return;}
    char path[PN_WALLPAPER_PATH_MAX];
    if(!pn_scan_dirs_find_file(PN_SCAN_CAT_WALLPAPERS,u->wallpaper_dir,name,path,sizeof path)){
        if((size_t)snprintf(path,sizeof path,"%s/%s",u->wallpaper_dir,name)>=sizeof path){banner_preview_release(u);return;}
    }
    pn_media_lease_t lease={0};pn_status_t st=pn_media_acquire(u->wallpaper_media,PN_MEDIA_READ,&lease);
    if(st==PN_OK){st=pn_wallpaper_prepare_banner(u->pool,u->wallpaper_media,&lease,path,1024u*1024u,&u->banner_preview);(void)pn_media_release(u->wallpaper_media,&lease);}
    if(st==PN_OK){
        strcpy(u->banner_preview_name,name);
        if(u->state)(void)pn_home_banner_cache_save(u->state,u->dir,name,u->banner_preview_px,PN_HOME_BANNER_BYTES);
    }else banner_preview_release(u);
}
static pn_status_t draw_home_banner(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_header(font,f,"< 返回","正在阅读上方背景",NULL);
    // 顶部实时效果预览卡（620×132，按原图等比缩放并叠加主页标题效果）。/ Top live preview card (620x132, scaled from decoded banner with Home title overlay).
    const int px=32,py=140,pw=620,ph=132;
    pn_w_round_fill(f,px+2,py+2,pw-4,ph,16,PN_UI_SELECT);
    pn_w_round_fill(f,px,py,pw,ph,16,PN_UI_PAPER);
    if(*ui->home.banner && u->banner_preview_px && !strcmp(u->banner_preview_name,ui->home.banner)){
        for(int y=0;y<ph;y++){
            int sy=y*PN_HOME_BANNER_HEIGHT/ph;
            for(int x=0;x<pw;x++){int sx=x*PN_HOME_BANNER_WIDTH/pw;pn_frame_pixel(f,px+x,py+y,pn_frame_get(&u->banner_preview,sx,sy));}
        }
        pn_w_mask_corners(f,px,py,pw,ph,16,PN_UI_PAPER);
    }else{
        // 默认远山预览剪影。/ Default mountain preview silhouette.
        static const int pts[][2]={{280,130},{360,64},{410,92},{468,36},{530,84},{580,62},{620,104}};
        for(size_t i=1;i<sizeof pts/sizeof *pts;i++)for(int x=pts[i-1][0];x<pts[i][0];x++){
            int y=pts[i-1][1]+(pts[i][1]-pts[i-1][1])*(x-pts[i-1][0])/(pts[i][0]-pts[i-1][0]);
            pn_frame_rect(f,px+x,py+y,1,ph-y,14);
        }
        pn_w_mask_corners(f,px,py,pw,ph,16,PN_UI_PAPER);
    }
    pn_w_round_stroke(f,px,py,pw,ph,16,1.5f,9);
    if(s==PN_OK){s=pn_font_size(font,34);if(s==PN_OK)s=pn_w_text_ex(font,f,"读书",px+24,py+74,260,PN_ALIGN_LEFT,PN_UI_INK,true);}
    if(s==PN_OK){s=pn_font_size(font,20);if(s==PN_OK)s=pn_w_text_ex(font,f,"拾起上次的阅读",px+24,py+108,260,PN_ALIGN_LEFT,5,false);}
    if(s==PN_OK){s=pn_font_size(font,20);if(s==PN_OK)s=pn_w_text_ex(font,f,*ui->home.banner?ui->home.banner:"默认山水效果",px+280,py+34,316,PN_ALIGN_RIGHT,4,true);}
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_button(font,f,*ui->home.banner?"恢复默认山水背景":"当前已使用默认山水背景",PN_UI_MARGIN,288,620,68,*ui->home.banner?0u:PN_W_SELECTED);
    if(s==PN_OK)s=pn_w_group(font,f,"从壁纸目录选择图片（点击即预览并应用）",394);
    if(!u->banner_count){
        if(s==PN_OK)s=note(u,"壁纸目录暂无可用图片（支持 PNG/JPG）。通过传书上传锁屏壁纸后即可在此选用。",444);
    }else{
        size_t visible=u->banner_count>5?5:u->banner_count;
        pn_w_card(f,410,(int)visible);
        for(size_t i=0;i<visible && s==PN_OK;i++){
            bool active=!strcmp(ui->home.banner,u->banner_names[i]);
            s=pn_w_card_row(font,f,u->banner_names[i],active?"当前已选用 · 顶部已预览":"轻点预览并设为主页顶部背景",NULL,PN_ICON_IMAGE,active?PN_ROW_ON:0u,410+(int)i*ROW_H,i+1==visible);
        }
    }
    if(s==PN_OK && (u->banner_has_prev || u->banner_more)){
        s=pn_font_size(font,28);
        if(s==PN_OK)s=pn_w_button(font,f,"上一页",32,960,296,80,u->banner_has_prev?0u:PN_W_DISABLED);
        if(s==PN_OK)s=pn_w_button(font,f,"下一页",356,960,296,80,u->banner_more?0u:PN_W_DISABLED);
    }
    if(s==PN_OK && *u->message)s=note(u,u->message,1088);
    return s;
}
static pn_status_t show_profile(pn_settings_ui_t *ui,pn_settings_present_fn present,void *ctx,pn_refresh_t profile){
    su_t *u=ui->impl;pn_w_status_snapshot_t snapshot=pn_w_status_snapshot();ui->presented=false;
    // 只计时清屏与绘制，供真机拆分菜单准备耗时。/ Time clearing and drawing only, to split menu preparation on hardware.
    pn_profile_span_t span;pn_profile_begin(&span,NULL,"ui.settings.clear");pn_frame_clear(&u->canvas,15);pn_profile_end(&span,PN_OK);
    pn_profile_begin(&span,NULL,"ui.settings.draw");
    // 仅在需要显示任意中文文案/全拼候选/用户文件名时借用常驻中文字体，避免默认设置主页每次重绘重复开闭字库。
    // Borrow the resident Chinese face only when rendering custom CJK text, Pinyin candidates or user filenames, avoiding redundant font opens on default Settings root.
    bool need_scope=*pn_w_brand()!=0 || u->screen==SCREEN_HOME_MOTTO || u->screen==SCREEN_HOME_MOTTO_EDIT
        || u->screen==SCREEN_GLOBAL_STYLE || u->screen==SCREEN_GLOBAL_BG || u->screen==SCREEN_HOME_BANNER;
    pn_w_font_scope_t scope={0};if(need_scope)(void)pn_w_font_scope_begin(&scope,u->pool,NULL);
    pn_status_t s=u->screen==SCREEN_KEYS?draw_keys(ui,u):u->screen==SCREEN_REFRESH?draw_refresh(ui,u):u->screen==SCREEN_ABOUT?draw_about(ui,u):u->screen==SCREEN_TIME?draw_time(u):u->screen==SCREEN_UPDATE?draw_update(u):u->screen==SCREEN_WIFI?draw_wifi(u):u->screen==SCREEN_WIFI_EDIT?draw_wifi_edit(u):u->screen==SCREEN_APPEARANCE?draw_appearance(ui,u):u->screen==SCREEN_GLOBAL_STYLE?draw_global_style(u):u->screen==SCREEN_GLOBAL_MARGINS?draw_global_margins(u):u->screen==SCREEN_GLOBAL_BG?draw_global_bg(u):u->screen==SCREEN_HOME_MOTTO?draw_home_motto(ui,u):u->screen==SCREEN_HOME_MOTTO_EDIT?draw_home_motto_edit(u):u->screen==SCREEN_HOME_BANNER?draw_home_banner(ui,u):draw_main(ui,u);
    if(need_scope)pn_w_font_scope_end(&scope);
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
            pn_frame_rect(&u->canvas,x,y,64,36,PN_UI_PAPER);pn_w_toggle(&u->canvas,x,y,on(ui->flags,i));}
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
    if(status==PN_EMPTY){value=PN_INPUT_FLAGS_DEFAULT;status=PN_OK;}
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
    bool index_wait=false;u->index_known=!state || pn_settings_load_index(state,dir,&index_wait)==PN_OK;
    bool vertical_turn=false;u->vertical_known=!state || pn_settings_load_vertical_turn(state,dir,&vertical_turn)==PN_OK;
    pn_home_prefs_t home=pn_home_prefs_default();
    if(state)(void)pn_home_prefs_load_dir(state,dir,&home);
    pn_w_set_brand(home.header);
    u->global_style=pn_style_default(PN_STYLE_DEFAULT_PIXELS);
    if(state){pn_style_t gs;pn_status_t gstatus=pn_style_global_load(state,dir,&gs);if(gstatus==PN_OK)u->global_style=gs;u->global_style_known=gstatus==PN_OK || gstatus==PN_EMPTY;}
    else u->global_style_known=false;
    *ui=(pn_settings_ui_t){.impl=u,.active=true,.flags=flags,.shelf=shelf,.home=home,.index_wait=index_wait,.vertical_turn=vertical_turn};
    (void)show(ui,present,ctx);return PN_OK;
}
pn_status_t pn_settings_ui_open(pn_settings_ui_t *ui,pn_pool_t *pool,pn_media_t *state,const char *dir,pn_settings_present_fn present,void *ctx){return pn_settings_ui_open_cached(ui,pool,state,dir,NULL,present,ctx);}
pn_status_t pn_settings_load_index(pn_media_t *state,const char *dir,bool *wait){
    if(!state || !dir || !wait)return PN_INVALID;
    pn_media_lease_t lease;pn_status_t s=pn_media_acquire(state,PN_MEDIA_READ,&lease);if(s!=PN_OK)return s;
    pn_journal_files_t files;pn_journal_io_t io;bool value=false;s=pn_index_prefs_files(&files,state,&lease,dir,&io);if(s==PN_OK)s=pn_index_prefs_load(&io,&value);(void)pn_media_release(state,&lease);
    if(s==PN_EMPTY)s=PN_OK;
    if(s==PN_OK)*wait=value;
    return s;
}
pn_status_t pn_settings_load_vertical_turn(pn_media_t *state,const char *dir,bool *vertical){
    if(!state || !dir || !vertical)return PN_INVALID;
    pn_media_lease_t lease;pn_status_t s=pn_media_acquire(state,PN_MEDIA_READ,&lease);if(s!=PN_OK)return s;
    pn_journal_files_t files;pn_journal_io_t io;bool value=false;s=pn_turn_dir_prefs_files(&files,state,&lease,dir,&io);if(s==PN_OK)s=pn_turn_dir_prefs_load(&io,&value);(void)pn_media_release(state,&lease);
    if(s==PN_EMPTY)s=PN_OK;
    if(s==PN_OK)*vertical=value;
    return s;
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
static pn_status_t commit_home(pn_settings_ui_t *ui,su_t *u,const pn_home_prefs_t *next){
    pn_status_t s=u->state?pn_home_prefs_save_dir(u->state,u->dir,next):PN_UNSUPPORTED;
    if(s==PN_OK){ui->home=*next;pn_w_set_brand(ui->home.header);}
    else{pn_home_prefs_t reread;if(u->state && pn_home_prefs_load_dir(u->state,u->dir,&reread)==PN_OK){ui->home=reread;pn_w_set_brand(ui->home.header);}
        snprintf(u->message,sizeof u->message,s==PN_UNSUPPORTED?"内部存储不可用，设置不能保存":"保存失败，请重试");}
    return s;
}
static pn_status_t commit_global_style(su_t *u,const pn_style_t *next){
    if(pn_style_validate(next)!=PN_OK)return PN_EMPTY;
    pn_status_t s=u->state?pn_style_global_save(u->state,u->dir,next):PN_UNSUPPORTED;
    if(s==PN_OK){u->global_style=*next;u->global_style_known=true;}
    else{pn_style_t reread;if(u->state && pn_style_global_load(u->state,u->dir,&reread)==PN_OK)u->global_style=reread;
        snprintf(u->message,sizeof u->message,s==PN_UNSUPPORTED?"内部存储不可用，设置不能保存":"保存失败，请重试");}
    return s;
}
static void banner_load_page(su_t *u,const char *cursor,bool previous){
    u->banner_count=0;u->banner_more=false;u->banner_has_prev=false;
    if(!u->wallpaper_media || !u->wallpaper_media->available || !*u->wallpaper_dir)return;
    pn_catalog_page_t *page=pn_alloc_tagged(u->pool,PN_MEM_UI,sizeof *page);if(!page)return;
    pn_media_lease_t lease={0};
    if(pn_media_acquire(u->wallpaper_media,PN_MEDIA_READ,&lease)==PN_OK){
        pn_status_t s=previous?pn_catalog_image_page_before(u->wallpaper_media,&lease,u->wallpaper_dir,cursor,page):pn_catalog_image_page(u->wallpaper_media,&lease,u->wallpaper_dir,cursor,page);
        (void)pn_media_release(u->wallpaper_media,&lease);
        if(s==PN_OK){
            for(size_t i=0;i<page->count && u->banner_count<6;i++){
                if(strlen(page->items[i].name)<PN_HOME_BANNER_MAX){strcpy(u->banner_names[u->banner_count++],page->items[i].name);}
            }
            u->banner_more=page->more;u->banner_has_prev=page->index>0;
        }
    }
    pn_free(page);
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
    if(command==PN_SETUI_INDEX_WAIT && u->screen==SCREEN_ABOUT){
        pn_status_t status=PN_UNSUPPORTED;pn_media_lease_t lease={0};bool next=!ui->index_wait;
        if(u->state && u->index_known){status=pn_media_acquire(u->state,PN_MEDIA_WRITE,&lease);if(status==PN_OK){pn_journal_files_t files;pn_journal_io_t io;status=pn_index_prefs_files(&files,u->state,&lease,u->dir,&io);if(status==PN_OK)status=pn_index_prefs_save(&io,next);(void)pn_media_release(u->state,&lease);}}
        if(status==PN_OK){ui->index_wait=next;u->message[0]=0;}else{bool saved;if(u->state && pn_settings_load_index(u->state,u->dir,&saved)==PN_OK)ui->index_wait=saved;snprintf(u->message,sizeof u->message,"设置未保存，请重试");}
        pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;
    }
    if(command==PN_SETUI_BACK){if(u->screen!=SCREEN_MAIN){u->forget_armed=false;u->screen=(u->screen==SCREEN_TIME || u->screen==SCREEN_UPDATE)?SCREEN_ABOUT:(u->screen==SCREEN_GLOBAL_MARGINS || u->screen==SCREEN_GLOBAL_BG)?SCREEN_GLOBAL_STYLE:u->screen==SCREEN_HOME_MOTTO_EDIT?SCREEN_HOME_MOTTO:(u->screen==SCREEN_HOME_MOTTO || u->screen==SCREEN_HOME_BANNER)?SCREEN_APPEARANCE:SCREEN_MAIN;return show(ui,present,ctx);}ui->active=false;return PN_OK;}
    if(command==PN_SETUI_GLOBAL_STYLE){u->screen=SCREEN_GLOBAL_STYLE;return show(ui,present,ctx);}
    if(u->screen==SCREEN_GLOBAL_STYLE){
        if(command==PN_SETUI_GSTYLE_MARGINS){u->screen=SCREEN_GLOBAL_MARGINS;return show(ui,present,ctx);}
        if(command==PN_SETUI_GSTYLE_BG){u->screen=SCREEN_GLOBAL_BG;return show(ui,present,ctx);}
        pn_style_t next=u->global_style;bool handled=true;
        if(command==PN_SETUI_GSTYLE_RESET){next=pn_style_default(PN_STYLE_DEFAULT_PIXELS);}
        else if(command==PN_SETUI_GSTYLE_BARS){next.hide_bars=next.hide_bars?0:1;}
        else if(command==PN_SETUI_GSTYLE_IMAGES){next.hide_images=next.hide_images?0:1;}
        else if(command>=PN_SETUI_GSTYLE_PRESET && command<PN_SETUI_GSTYLE_PRESET+3){
            static const uint16_t p_px[3]={44,36,56},p_ln[3]={145,125,160},p_gp[3]={40,20,40},p_mg[3]={32,24,32};
            unsigned idx=(unsigned)(command-PN_SETUI_GSTYLE_PRESET);
            next.pixels=p_px[idx];next.line_percent=p_ln[idx];next.gap_percent=p_gp[idx];next.indent_em=2;pn_style_set_margins_lr(&next,(int)p_mg[idx]);next.tracking_percent=0;
        }else if(command>=PN_SETUI_GSTYLE_ADJ && command<PN_SETUI_GSTYLE_ADJ+16){
            unsigned item=(unsigned)(command-PN_SETUI_GSTYLE_ADJ)/2;bool up=((command-PN_SETUI_GSTYLE_ADJ)&1)!=0;
            if(item==0){int v=(int)next.pixels+(up?2:-2);if(v>=28 && v<=72)next.pixels=(uint16_t)v;}
            else if(item==1){int v=(int)next.line_percent+(up?5:-5);if(v>=100 && v<=220)next.line_percent=(uint16_t)v;}
            else if(item==2){int v=(int)next.gap_percent+(up?5:-5);if(v>=0 && v<=100)next.gap_percent=(uint16_t)v;}
            else if(item==3){unsigned level=next.margin<=20?0u:next.margin<=32?1u:2u;static const int ms[3]={20,32,48};level=up?(level+1u)%3u:(level+2u)%3u;pn_style_set_margins_lr(&next,ms[level]);}
            else if(item==4){int v=(int)next.indent_em+(up?1:-1);if(v>=0 && v<=2)next.indent_em=(uint16_t)v;}
            else if(item==5){int v=(int)next.tracking_percent+(up?5:-5);if(v>=0 && v<=50)next.tracking_percent=(uint16_t)v;}
            else if(item==6){int v=(int)next.weight+(up?1:-1);if(v<-1)v=3;if(v>3)v=-1;next.weight=(int16_t)v;}
            else if(item==7){next.clarity=(uint16_t)((next.clarity+(up?1u:3u))%4u);}
        }else if(command>=PN_SETUI_GSTYLE_EXT && command<PN_SETUI_GSTYLE_EXT+6){
            unsigned item=(unsigned)(command-PN_SETUI_GSTYLE_EXT)/2;bool up=((command-PN_SETUI_GSTYLE_EXT)&1)!=0;
            if(item==0)next.reading_line=(uint16_t)((next.reading_line+(up?1u:2u))%3u);
            else if(item==1)next.reading_line_shade=(uint16_t)((next.reading_line_shade+(up?1u:3u))%4u);
            else{int v=(int)next.reading_line_offset+(up?2:-2);if(v>=-8 && v<=8)next.reading_line_offset=(int16_t)v;}
        }else handled=false;
        if(handled){status=commit_global_style(u,&next);pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;}
    }
    if(u->screen==SCREEN_GLOBAL_MARGINS){
        pn_style_t next=u->global_style;bool handled=true;
        if(command==PN_SETUI_GSTYLE_MARGINS_RESET){
            next.margin_top=0;next.margin_bottom=0;pn_style_set_margins_lr(&next,32);
            next.bg_margin_top=0;next.bg_margin_bottom=0;next.bg_margin_left=0;next.bg_margin_right=0;
        }else if(command>=PN_SETUI_GSTYLE_MADJ && command<PN_SETUI_GSTYLE_MADJ+16){
            unsigned item=(unsigned)(command-PN_SETUI_GSTYLE_MADJ)/2;bool up=((command-PN_SETUI_GSTYLE_MADJ)&1)!=0;
            int delta=up?4:-4,val=0;
            switch(item){
                case 0:val=pn_style_margin_top(&next)+delta;if(val>=0 && val<=200)next.margin_top=(uint16_t)val;break;
                case 1:val=pn_style_margin_bottom(&next)+delta;if(val>=0 && val<=200)next.margin_bottom=(uint16_t)val;break;
                case 2:val=pn_style_margin_left(&next)+delta;if(val>=0 && val<=160)pn_style_set_margin_left(&next,val);break;
                case 3:val=pn_style_margin_right(&next)+delta;if(val>=0 && val<=160)pn_style_set_margin_right(&next,val);break;
                case 4:val=(int)next.bg_margin_top+delta;if(val>=0 && val<=200)next.bg_margin_top=(uint16_t)val;break;
                case 5:val=(int)next.bg_margin_bottom+delta;if(val>=0 && val<=200)next.bg_margin_bottom=(uint16_t)val;break;
                case 6:val=(int)next.bg_margin_left+delta;if(val>=0 && val<=160)next.bg_margin_left=(uint16_t)val;break;
                default:val=(int)next.bg_margin_right+delta;if(val>=0 && val<=160)next.bg_margin_right=(uint16_t)val;break;
            }
        }else handled=false;
        if(handled){if(pn_style_validate(&next)==PN_OK)status=commit_global_style(u,&next);pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;}
    }
    if(u->screen==SCREEN_GLOBAL_BG){
        if(command>=PN_SETUI_GSTYLE_BG_ITEM && command<=PN_SETUI_GSTYLE_BG_ITEM+(int)PN_READING_BACKGROUND_COUNT){
            pn_style_t next=u->global_style;
            next.background=(uint16_t)pn_reading_background_id((unsigned)(command-PN_SETUI_GSTYLE_BG_ITEM));
            memset(next.background_name,0,sizeof next.background_name);memset(&next.background_identity,0,sizeof next.background_identity);
            status=commit_global_style(u,&next);u->screen=SCREEN_GLOBAL_STYLE;
            pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;
        }
    }
    if(command==PN_SETUI_HOME_MOTTO){u->screen=SCREEN_HOME_MOTTO;return show(ui,present,ctx);}
    if(u->screen==SCREEN_HOME_MOTTO){
        if(command==PN_SETUI_MOTTO_RESET){pn_home_prefs_t next=ui->home;next.header[0]=0;status=commit_home(ui,u,&next);pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;}
        if(command>=PN_SETUI_MOTTO_PRESET && command<PN_SETUI_MOTTO_PRESET+6){pn_home_prefs_t next=ui->home;snprintf(next.header,sizeof next.header,"%s",motto_presets[command-PN_SETUI_MOTTO_PRESET]);status=commit_home(ui,u,&next);pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;}
        if(command==PN_SETUI_MOTTO_EDIT){snprintf(u->motto_draft,sizeof u->motto_draft,"%s",ui->home.header);pn_keyboard_set_ime(&u->kb,true,true);u->screen=SCREEN_HOME_MOTTO_EDIT;return show(ui,present,ctx);}
    }
    if(u->screen==SCREEN_HOME_MOTTO_EDIT){
        bool changed=false;
        int kb_code=PN_KB_NONE;
        if(command>=PN_SETUI_CHAR+32 && command<=PN_SETUI_CHAR+126)kb_code=command-PN_SETUI_CHAR;
        else if(command==PN_SETUI_KB_DELETE)kb_code=PN_KB_DELETE;
        else if(command==PN_SETUI_KB_SHIFT)kb_code=PN_KB_SHIFT;
        else if(command==PN_SETUI_KB_LAYER)kb_code=PN_KB_LAYER;
        else if(command>PN_SETUI_KB_IME_BASE && command<=PN_SETUI_KB_IME_BASE+60)kb_code=-(command-PN_SETUI_KB_IME_BASE);
        if(kb_code!=PN_KB_NONE)changed=pn_keyboard_apply(&u->kb,kb_code,u->motto_draft,sizeof u->motto_draft,PN_HOME_HEADER_MAX-1);
        else if(command==PN_SETUI_MOTTO_CLEAR){changed=*u->motto_draft!=0 || *u->kb.pinyin!=0;u->motto_draft[0]=0;u->kb.pinyin[0]=0;u->kb.cand_page=0;}
        else if(command==PN_SETUI_MOTTO_SAVE){(void)pn_keyboard_flush(&u->kb,u->motto_draft,sizeof u->motto_draft,PN_HOME_HEADER_MAX-1);pn_home_prefs_t next=ui->home;snprintf(next.header,sizeof next.header,"%s",u->motto_draft);status=commit_home(ui,u,&next);if(status==PN_OK)u->screen=SCREEN_HOME_MOTTO;pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;}
        else return PN_EMPTY;
        return changed?show(ui,present,ctx):PN_OK;
    }
    if(command==PN_SETUI_HOME_BANNER){banner_load_page(u,"",false);banner_preview_load(u,ui->home.banner);u->screen=SCREEN_HOME_BANNER;return show(ui,present,ctx);}
    if(u->screen==SCREEN_HOME_BANNER){
        if(command==PN_SETUI_BANNER_DEFAULT){pn_home_prefs_t next=ui->home;next.banner[0]=0;status=commit_home(ui,u,&next);if(status==PN_OK){banner_preview_release(u);if(u->state)pn_home_banner_cache_clear(u->state,u->dir);}pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;}
        if(command==PN_SETUI_BANNER_PREV && u->banner_has_prev && u->banner_count){char cur[PN_HOME_BANNER_MAX];strcpy(cur,u->banner_names[0]);banner_load_page(u,cur,true);return show(ui,present,ctx);}
        if(command==PN_SETUI_BANNER_NEXT && u->banner_more && u->banner_count){size_t last_idx=u->banner_count>5?4:u->banner_count-1;char cur[PN_HOME_BANNER_MAX];strcpy(cur,u->banner_names[last_idx]);banner_load_page(u,cur,false);return show(ui,present,ctx);}
        if(command>=PN_SETUI_BANNER_ITEM && command<PN_SETUI_BANNER_ITEM+(int)u->banner_count){pn_home_prefs_t next=ui->home;snprintf(next.banner,sizeof next.banner,"%s",u->banner_names[command-PN_SETUI_BANNER_ITEM]);status=commit_home(ui,u,&next);if(status==PN_OK)banner_preview_load(u,next.banner);pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;}
    }
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
    if(command==PN_SETUI_VERTICAL_TURN && u->screen==SCREEN_KEYS){
        pn_status_t saved_status=PN_UNSUPPORTED;pn_media_lease_t lease={0};bool next=!ui->vertical_turn;
        if(u->state && u->vertical_known){saved_status=pn_media_acquire(u->state,PN_MEDIA_WRITE,&lease);if(saved_status==PN_OK){pn_journal_files_t files;pn_journal_io_t io;saved_status=pn_turn_dir_prefs_files(&files,u->state,&lease,u->dir,&io);if(saved_status==PN_OK)saved_status=pn_turn_dir_prefs_save(&io,next);(void)pn_media_release(u->state,&lease);}}
        if(saved_status==PN_OK){ui->vertical_turn=next;u->message[0]=0;}
        else{bool reread;if(u->state && pn_settings_load_vertical_turn(u->state,u->dir,&reread)==PN_OK)ui->vertical_turn=reread;snprintf(u->message,sizeof u->message,saved_status==PN_UNSUPPORTED?"内部存储不可用，设置不能保存":"保存失败，请重试");}
        pn_status_t shown=show(ui,present,ctx);return saved_status==PN_OK?shown:saved_status;
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
        if(y>=DISPLAY_Y && y<DISPLAY_Y+MAIN_ROW_H)return PN_SETUI_FONTS;
        if(y>=DISPLAY_Y+MAIN_ROW_H && y<DISPLAY_Y+2*MAIN_ROW_H)return PN_SETUI_WALLPAPER;
        if(y>=DISPLAY_Y+2*MAIN_ROW_H && y<DISPLAY_Y+3*MAIN_ROW_H)return PN_SETUI_APPEARANCE;
        if(y>=READING_Y && y<READING_Y+MAIN_ROW_H)return PN_SETUI_GLOBAL_STYLE;
        if(y>=READING_Y+MAIN_ROW_H && y<READING_Y+2*MAIN_ROW_H)return PN_SETUI_KEYS;
        if(y>=READING_Y+2*MAIN_ROW_H && y<READING_Y+3*MAIN_ROW_H)return PN_SETUI_REFRESH;
        if(y>=DEVICE_Y && y<DEVICE_Y+MAIN_ROW_H)return PN_SETUI_WIFI;
        if(y>=DEVICE_Y+MAIN_ROW_H && y<DEVICE_Y+device_rows(u)*MAIN_ROW_H)return PN_SETUI_ABOUT;
        return -1;
    }
    if(pn_w_header_hit(x,y,false)==1)return PN_SETUI_BACK;
    if(u->screen==SCREEN_GLOBAL_STYLE && y<PN_W_HEADER_H && x>=480 && y>=44)return PN_SETUI_GSTYLE_RESET;
    if(u->screen==SCREEN_GLOBAL_MARGINS && y<PN_W_HEADER_H && x>=480 && y>=44)return PN_SETUI_GSTYLE_MARGINS_RESET;
    if(u->screen==SCREEN_WIFI_EDIT){
        if(u->testing)return y>=EDIT_BTN_Y && y<EDIT_BTN_Y+88 && x>=240 && x<436?PN_SETUI_WIFI_TEST_CANCEL:-1;
        int key=pn_keyboard_hit(&u->kb,KB_Y,x,y);
        if(key>=32)return PN_SETUI_CHAR+key;
        if(key!=PN_KB_NONE)return key==PN_KB_SHIFT?PN_SETUI_KB_SHIFT:key==PN_KB_LAYER?PN_SETUI_KB_LAYER:PN_SETUI_KB_DELETE;
        if(u->password_step && u->test_failed && y>=FORCE_Y && y<FORCE_Y+72 && x>=PN_UI_MARGIN && x<652)return PN_SETUI_WIFI_FORCE;
        if(y>=EDIT_BTN_Y && y<EDIT_BTN_Y+88){if(x>=PN_UI_MARGIN && x<228)return PN_SETUI_WIFI_CLEAR;if(x>=240 && x<436 && u->password_step)return PN_SETUI_WIFI_REVEAL;if(x>=448 && x<652)return PN_SETUI_WIFI_NEXT;}
        return -1;
    }
    if(u->screen==SCREEN_HOME_MOTTO_EDIT){
        int key=pn_keyboard_hit(&u->kb,KB_Y,x,y);
        if(key>=32)return PN_SETUI_CHAR+key;
        if(key==PN_KB_SHIFT)return PN_SETUI_KB_SHIFT;
        if(key==PN_KB_LAYER)return PN_SETUI_KB_LAYER;
        if(key==PN_KB_DELETE)return PN_SETUI_KB_DELETE;
        if(key<=PN_KB_IME_TOGGLE && key>=PN_KB_PUNCT_BASE-PN_KB_PUNCT_COUNT)return PN_SETUI_KB_IME_BASE+(-key);
        if(y>=EDIT_BTN_Y && y<EDIT_BTN_Y+88){if(x>=PN_UI_MARGIN && x<328)return PN_SETUI_MOTTO_CLEAR;if(x>=356 && x<652)return PN_SETUI_MOTTO_SAVE;}
        return -1;
    }
    if(x<32 || x>=652)return -1;
    if(u->screen==SCREEN_GLOBAL_STYLE){
        int preset=pn_w_segments_hit(3,306,56,x,y);if(preset>=0)return PN_SETUI_GSTYLE_PRESET+preset;
        for(unsigned i=0;i<12;i++){
            int cx=32+(int)(i%2)*320,cy=378+(int)(i/2)*84;
            if(y>=cy && y<cy+76 && x>=cx && x<cx+300){
                if(i==11)return PN_SETUI_GSTYLE_BG;
                if(y>=cy+30){
                    if(x<cx+82)return i<8?PN_SETUI_GSTYLE_ADJ+(int)i*2:PN_SETUI_GSTYLE_EXT+(int)(i-8)*2;
                    if(x>=cx+218)return i<8?PN_SETUI_GSTYLE_ADJ+(int)i*2+1:PN_SETUI_GSTYLE_EXT+(int)(i-8)*2+1;
                }
                if(i==3)return PN_SETUI_GSTYLE_MARGINS;
                return -1;
            }
        }
        if(y>=892 && y<964){if(x<332)return PN_SETUI_GSTYLE_IMAGES;if(x>=352)return PN_SETUI_GSTYLE_BARS;}
        return -1;
    }
    if(u->screen==SCREEN_GLOBAL_MARGINS){
        static const int tops[4]={450,568,732,850};
        for(unsigned row=0;row<4;row++){
            if(y>=tops[row] && y<tops[row]+108){
                for(unsigned col=0;col<2;col++){
                    int cx=32+(int)col*320;unsigned item=row*2+col;
                    if(x>=cx && x<cx+108)return PN_SETUI_GSTYLE_MADJ+(int)item*2;
                    if(x>=cx+192 && x<cx+300)return PN_SETUI_GSTYLE_MADJ+(int)item*2+1;
                }
            }
        }
        return -1;
    }
    if(u->screen==SCREEN_GLOBAL_BG){
        if(y>=194 && y<194+3*284){
            int col=(x-32)/212,row=(y-194)/284;
            if(col>=0 && col<3 && row>=0 && row<3 && (x-32)%212<196 && (y-194)%284<264){
                unsigned slot=(unsigned)(row*3+col);
                if(slot<=PN_READING_BACKGROUND_COUNT)return PN_SETUI_GSTYLE_BG_ITEM+(int)slot;
            }
        }
        return -1;
    }
    if(u->screen==SCREEN_HOME_MOTTO){
        for(unsigned i=0;i<6;i++){int px=32+(int)(i%2)*318,py=344+(int)(i/2)*108;
            if(x>=px && x<px+302 && y>=py && y<py+92)return PN_SETUI_MOTTO_PRESET+(int)i;}
        if(y>=712 && y<800)return PN_SETUI_MOTTO_EDIT;
        if(y>=822 && y<910)return PN_SETUI_MOTTO_RESET;
        return -1;
    }
    if(u->screen==SCREEN_HOME_BANNER){
        size_t visible=u->banner_count>5?5:u->banner_count;
        if(y>=288 && y<356)return PN_SETUI_BANNER_DEFAULT;
        if(visible && y>=410 && y<410+(int)visible*ROW_H)return PN_SETUI_BANNER_ITEM+(y-410)/ROW_H;
        if(y>=960 && y<1040){if(x<328 && u->banner_has_prev)return PN_SETUI_BANNER_PREV;if(x>=356 && u->banner_more)return PN_SETUI_BANNER_NEXT;}
        return -1;
    }
    if(u->screen==SCREEN_WIFI){if(y>=WIFI_EDIT_Y && y<WIFI_EDIT_Y+88)return PN_SETUI_WIFI_EDIT;if(*u->lan && y>=WIFI_FORGET_Y && y<WIFI_FORGET_Y+88)return PN_SETUI_WIFI_FORGET;return -1;}
    if(u->screen==SCREEN_ABOUT && y>=710 && y<710+ROW_H)return PN_SETUI_TIME;
    if(u->screen==SCREEN_ABOUT && y>=840 && y<840+ROW_H)return PN_SETUI_UPDATE;
    if(u->screen==SCREEN_UPDATE){if(y>=810 && y<898 && (u->update.running || u->update.install || u->update.reboot))return PN_SETUI_UPDATE_CANCEL;if(y>=700 && y<788 && !u->update.running)return u->update.reboot?PN_SETUI_UPDATE_REBOOT:u->update.install?PN_SETUI_UPDATE_INSTALL:u->update.available?PN_SETUI_UPDATE_CHECK:-1;return -1;}
    if(u->screen==SCREEN_TIME){if(y>=700 && y<788)return u->time.running?PN_SETUI_TIME_CANCEL:u->time.available?PN_SETUI_TIME_SYNC:-1;return -1;}
    if(u->screen==SCREEN_APPEARANCE){int cell=pn_w_segments_hit(2,190,108,x,y);if(cell>=0)return PN_SETUI_COLUMNS+cell;
        if(y>=460 && y<900){int row=(y-460)/230,col=(x-32)/318;
            if(row<2 && col<2 && (y-460)%230<210 && (x-32)%318<302)return PN_SETUI_STYLE+row*2+col;}
        if(y>=944 && y<944+ROW_H)return PN_SETUI_HOME_MOTTO;
        if(y>=944+ROW_H && y<944+2*ROW_H)return PN_SETUI_HOME_BANNER;
        return -1;
    }
    if(u->screen==SCREEN_KEYS){if(y>=TOGGLE_Y && y<TOGGLE_Y+4*ROW_H)return PN_SETUI_TOGGLE+(y-TOGGLE_Y)/ROW_H;if(y>=TOGGLE_Y+4*ROW_H && y<TOGGLE_Y+5*ROW_H)return PN_SETUI_VERTICAL_TURN;return -1;}
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
    banner_preview_release(u);wipe_edit(u);pn_free(u->pixels);pn_free(u);*ui=(pn_settings_ui_t){0};
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
void pn_settings_ui_set_wallpaper_dir(pn_settings_ui_t *ui,pn_media_t *media,const char *directory){
    if(!ui || !ui->impl)return;
    su_t *u=ui->impl;u->wallpaper_media=media;u->wallpaper_dir[0]=0;
    if(directory && *directory && strlen(directory)<sizeof u->wallpaper_dir)strcpy(u->wallpaper_dir,directory);
}
