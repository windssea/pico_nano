/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：传书一级页，见pn_transfer_hub.h。
 * English: the Transfer root tab, see pn_transfer_hub.h.
 */
#include "pn_transfer_hub.h"
#include <stdio.h>
#include "pn_widgets.h"
#define CHANNEL_Y 230
#define ROW_H PN_W_CARD_ROW_H
#define STORAGE_Y 610
#define FORMAT_Y 760
#define TAB_Y 1104
#define TAB_H 112
pn_status_t pn_transfer_hub_render(pn_font_t *font,pn_frame_t *frame,const pn_transfer_hub_info_t *info){
    if(!font || !font->impl || !frame || !frame->pixels || !info || frame->width!=PN_UI_WIDTH || frame->height!=PN_UI_HEIGHT)return PN_INVALID;
    int original=font->pixels;pn_frame_clear(frame,PN_UI_PAPER);
    pn_status_t s=pn_w_status(font,frame);
    if(s==PN_OK)s=pn_font_size(font,50);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,"传书",PN_UI_MARGIN,124,400,PN_ALIGN_LEFT,PN_UI_INK,true);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,"把手机或电脑上的书、字体和壁纸传到设备。",PN_UI_MARGIN,166,620,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    if(s==PN_OK)s=pn_w_group(font,frame,"传输方式",CHANNEL_Y-14);
    pn_w_card(frame,CHANNEL_Y,3);
    // 可用通道带箭头；不可用的只写原因（规范T01：不展示虚假地址或入口）。/ Available channels get a chevron; unavailable ones only state the reason (spec T01: no fake addresses or entries).
    const char *wireless_off=info->unavailable?info->unavailable:"当前设备暂不支持无线传书";
    if(s==PN_OK)s=pn_w_card_row(font,frame,"热点传书",info->hotspot?"手机连设备热点，扫码打开传书网页":wireless_off,NULL,PN_ICON_WIFI,info->hotspot?PN_ROW_CHEVRON:0u,CHANNEL_Y,false);
    char lan[96];
    if(info->lan)snprintf(lan,sizeof lan,"通过家里的网络「%s」",info->lan_name && *info->lan_name?info->lan_name:"已保存网络");
    else snprintf(lan,sizeof lan,"%s",info->hotspot?"先在热点传书网页里保存家庭网络":wireless_off);
    if(s==PN_OK)s=pn_w_card_row(font,frame,"局域网传书",lan,NULL,PN_ICON_TRANSFER,info->lan?PN_ROW_CHEVRON:0u,CHANNEL_Y+ROW_H,false);
    if(s==PN_OK)s=pn_w_card_row(font,frame,"USB 传输","暂不支持：阅读与 USB 不能同时使用存储卡",NULL,PN_ICON_IMPORT,0u,CHANNEL_Y+2*ROW_H,true);
    if(s==PN_OK)s=pn_w_group(font,frame,"存储",STORAGE_Y-14);
    pn_w_card(frame,STORAGE_Y,1);
    if(s==PN_OK)s=pn_w_card_row(font,frame,"存储卡",info->storage && *info->storage?info->storage:"未读取到存储卡",NULL,-1,0u,STORAGE_Y,true);
    if(s==PN_OK)s=pn_w_group(font,frame,"支持的文件",FORMAT_Y-14);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text_lines_ex(font,frame,"图书 EPUB / TXT · 字体 TTF · 壁纸 JPG / PNG。传完回到书架即可看到。",PN_UI_MARGIN+8,FORMAT_Y+22,604,3,34,PN_UI_MUTED,false);
    if(s==PN_OK){static const char *const tabs[]={"书架","传书","设置"};static const pn_icon_t icons[]={PN_ICON_SHELF,PN_ICON_TRANSFER,PN_ICON_SETTINGS};s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_tabbar_icons(font,frame,tabs,icons,3,1,0u,TAB_Y,TAB_H);}
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
int pn_transfer_hub_hit(const pn_transfer_hub_info_t *info,int x,int y){
    if(!info || x<0 || x>=PN_UI_WIDTH || y<0 || y>=PN_UI_HEIGHT)return -1;
    if(y>=TAB_Y){int tab=pn_w_tabbar_hit(3,TAB_Y,TAB_H,x,y);return tab==0?PN_HUB_SHELF:tab==2?PN_HUB_SETTINGS:-1;}
    if(x<PN_UI_MARGIN || x>=PN_UI_WIDTH-PN_UI_MARGIN)return -1;
    if(y>=CHANNEL_Y && y<CHANNEL_Y+ROW_H)return info->hotspot?PN_HUB_HOTSPOT:-1;
    if(y>=CHANNEL_Y+ROW_H && y<CHANNEL_Y+2*ROW_H)return info->lan?PN_HUB_LAN:-1;
    return -1;
}
