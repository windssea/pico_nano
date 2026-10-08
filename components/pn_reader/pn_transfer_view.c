/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：中文传输状态与阅读菜单绘制，未知数据字形使用边框回退。
 * English: Chinese transfer status and reading-menu painting with boxed fallback for unknown data glyphs.
 * 冻结：只绘制，不改变阅读位置、设置、网络或电源。
 * Frozen: paint only, never change reading positions, settings, networks or power.
 */
#include "pn_transfer_view.h"
#include <stdio.h>
#include "pn_qr.h"
#include <string.h>
static pn_status_t read(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t length=strlen(s);if(at>length)return PN_INVALID;size_t take=length-(size_t)at;if(take>cap)take=cap;memcpy(out,s+at,take);*n=take;return PN_OK;}
static pn_status_t text(pn_font_t *font,pn_frame_t *frame,const char *s,int x,int y){
 pn_text_source_t source={(void *)s,strlen(s),read,NULL};pn_text_reader_t decoder;pn_status_t status=pn_text_open(&decoder,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;pn_text_char_t c;int cursor=x*64;
 while((status=pn_text_next(&decoder,&c))==PN_OK){int32_t advance;pn_status_t measured=pn_font_advance(font,c.codepoint,&advance);if(measured==PN_EMPTY)advance=font->pixels*64;else if(measured!=PN_OK)return measured;if(advance<0)return PN_LIMIT;if(advance>652*64-cursor)break;
  if(measured==PN_EMPTY){int left=cursor/64;pn_frame_rect(frame,left,y-font->pixels+4,font->pixels-4,1,0);pn_frame_rect(frame,left,y-4,font->pixels-4,1,0);pn_frame_rect(frame,left,y-font->pixels+4,1,font->pixels-8,0);pn_frame_rect(frame,left+font->pixels-5,y-font->pixels+4,1,font->pixels-8,0);}
  else{pn_status_t drawn=pn_font_draw(font,frame,c.codepoint,cursor,y,PN_FONT_GRAY);if(drawn!=PN_OK)return drawn;}
  cursor+=advance;
 }
 return status==PN_OK || status==PN_EMPTY?PN_OK:status;
}
static pn_status_t button(pn_font_t *font,pn_frame_t *frame,const char *label,int y){pn_frame_rect(frame,32,y,620,1,5);pn_frame_rect(frame,32,y+100,620,1,5);pn_frame_rect(frame,32,y,1,100,5);pn_frame_rect(frame,651,y,1,100,5);return text(font,frame,label,64,y+64);}
static bool valid(pn_font_t *font,pn_frame_t *frame){return font && font->impl && frame && frame->pixels && frame->width==684 && frame->height==1216 && frame->stride>=342;}
/* 六槽菜单：继续/书架/热点/局域网（可选）/壁纸/字体。/ Six-slot menu: resume/shelf/hotspot/LAN (optional)/wallpaper/fonts. */
pn_status_t pn_reading_menu_render_lan(bool resume,const char *lan,pn_font_t *font,pn_frame_t *frame){
 if(!valid(font,frame))return PN_INVALID;
 pn_frame_clear(frame,15);pn_status_t s=text(font,frame,"小纸 Pico",32,64);
 if(s==PN_OK)s=text(font,frame,"阅读与传输",32,160);
 if(s==PN_OK)s=button(font,frame,resume?"继续阅读":"返回书架",230);
 if(s==PN_OK)s=button(font,frame,"全部书架",370);
 if(s==PN_OK)s=button(font,frame,"热点传书",510);
 if(s==PN_OK && lan){char label[96];snprintf(label,sizeof label,"局域网传书：%s",lan);s=button(font,frame,label,650);}
 if(s==PN_OK)s=button(font,frame,"锁屏壁纸",790);
 if(s==PN_OK)s=button(font,frame,"字体管理",930);
 if(s==PN_OK)s=text(font,frame,"进入前保存位置，退出后回到原书",32,1110);
 return s;
}
int pn_reading_menu_hit_lan(int x,int y,bool lan){
 if(x<32 || x>=652 || y<230 || (y-230)%140>=100)return -1;
 const int slots[]={PN_READING_MENU_RESUME,PN_READING_MENU_SHELF,PN_READING_MENU_TRANSFER,PN_READING_MENU_LAN,PN_READING_MENU_WALLPAPER,PN_READING_MENU_FONTS};
 int row=(y-230)/140;if(row>=6)return -1;
 return slots[row]==PN_READING_MENU_LAN && !lan?-1:slots[row];
}
pn_status_t pn_reading_menu_render(bool resume,pn_font_t *font,pn_frame_t *frame){return pn_reading_menu_render_lan(resume,NULL,font,frame);}
int pn_reading_menu_hit(int x,int y){return pn_reading_menu_hit_lan(x,y,false);}
pn_status_t pn_transfer_view_render(const pn_transfer_view_t *v,pn_font_t *font,pn_frame_t *frame){
 if(!v || !valid(font,frame) || !memchr(v->ssid,0,sizeof v->ssid) || !memchr(v->password,0,sizeof v->password) || !memchr(v->address,0,sizeof v->address) || !memchr(v->pin,0,sizeof v->pin))return PN_INVALID;
 pn_frame_clear(frame,15);pn_status_t s=text(font,frame,"小纸 Pico",32,64);
 if(s==PN_OK)s=text(font,frame,v->station?"局域网传书":"热点传书",32,160);
 if(v->phase==PN_TVIEW_READY && v->station){
  // 局域网：手机已在同一网络，只给网页码。/ LAN: the phone is already on the same network, so only the web-page code is shown.
  if(s==PN_OK)s=text(font,frame,"扫码打开网页",200,212);
  if(s==PN_OK)s=pn_qr_draw(v->address,frame,202,232,280);
  if(s==PN_OK)s=text(font,frame,"手机连接同一WiFi后扫码，输入配对码",32,566);
  const char *labels[]={"网络名称",v->ssid,"网页地址",v->address,"配对码",v->pin};
  for(unsigned i=0;i<6 && s==PN_OK;i++)s=text(font,frame,labels[i],i%2?220:32,636+(int)(i/2)*80);
  if(s==PN_OK)s=text(font,frame,v->busy?"正在接收或校验文件":"已连接，等待网页发送",32,1010);
 }else if(v->phase==PN_TVIEW_READY){
  char payload[256];
  if(s==PN_OK)s=text(font,frame,"扫码连接热点",32,212);
  if(s==PN_OK)s=text(font,frame,"扫码打开网页",364,212);
  if(s==PN_OK)s=pn_qr_wifi_payload(v->ssid,v->password,payload,sizeof payload);
  if(s==PN_OK)s=pn_qr_draw(payload,frame,32,232,280);
  if(s==PN_OK)s=pn_qr_draw(v->address,frame,364,232,280);
  if(s==PN_OK)s=text(font,frame,"先连热点，再打开网页输入配对码",32,566);
  const char *labels[]={"热点名称",v->ssid,"热点口令",v->password,"网页地址",v->address,"配对码",v->pin};
  for(unsigned i=0;i<8 && s==PN_OK;i++)s=text(font,frame,labels[i],i%2?220:32,636+(int)(i/2)*80);
  if(s==PN_OK)s=text(font,frame,v->busy?"正在接收或校验文件":"已连接，等待网页发送",32,1010);
 }else if(v->phase==PN_TVIEW_STARTING){if(s==PN_OK)s=text(font,frame,v->station?"正在连接已保存网络":"正在开启传输",32,330);if(s==PN_OK)s=text(font,frame,"请稍候，阅读位置已保存",32,420);}
 else if(v->phase==PN_TVIEW_STOPPING){if(s==PN_OK)s=text(font,frame,"正在关闭传输",32,330);if(s==PN_OK)s=text(font,frame,"等待文件处理结束，请勿拔卡",32,420);}
 else{if(s==PN_OK)s=text(font,frame,v->station?"无法连接已保存网络，可改用热点传书":"传输未能继续",32,330);if(s==PN_OK)s=text(font,frame,v->released?"资源已关闭，可返回阅读":"尚未归还存储，请重试关闭",32,420);}
 if(s==PN_OK && v->phase!=PN_TVIEW_STOPPING)s=button(font,frame,v->phase==PN_TVIEW_FAILED?(v->released?"返回阅读":"重试关闭"):"停止传输并返回",1070);
 return s;
}
int pn_transfer_view_hit(const pn_transfer_view_t *v,int x,int y){return v && v->phase!=PN_TVIEW_STOPPING && x>=32 && x<652 && y>=1070 && y<1170?PN_TRANSFER_VIEW_STOP:-1;}
