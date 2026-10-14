/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：六项字体菜单、真实字体检查与原文选择操作。
 * English: six-item font menus, actual font checks and anchored selection operations.
 * 冻结：用户选择仅针对阅读正文；按简化字体需求使用系统缺字回退，不暴露备用配置。
 * Frozen: user choices affect reading text only; the simplified product uses automatic fallback without exposing secondary-font configuration.
 */
#include "pn_brand.h"
#include "pn_font_ui.h"
#include "pn_widgets.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
typedef struct {bool ready,explicit_directory,focus_visible,inherited;pn_pool_t *pool;pn_reader_app_t *reader;pn_epub_app_t *epub;pn_catalog_page_t page;pn_font_preferences_t draft,current;pn_font_info_t info;char directory[PN_FONT_REFERENCE_PATH_MAX],cursor[PN_CATALOG_NAME_MAX];unsigned checked,missing;const char *notice;pn_reader_overlay_fn return_paint;void *return_ctx;} ui_t;
/* 调用方目录优先，重试读取偏好不能改回旧字体所在目录。/ The caller's directory wins; retrying preferences cannot restore the old font's folder. */
static pn_status_t preferences(ui_t *s){
 char inferred[PN_FONT_REFERENCE_PATH_MAX];pn_status_t status=s->epub?pn_epub_app_fonts_get(s->epub,&s->draft,inferred,sizeof inferred):pn_reader_app_fonts_get(s->reader,&s->draft,inferred,sizeof inferred);
 if(status==PN_OK && !s->explicit_directory)strcpy(s->directory,inferred);
 if(status==PN_OK){s->current=s->draft;s->inherited=s->epub?pn_epub_app_font_is_inherited(s->epub):pn_reader_app_font_is_inherited(s->reader);}
 s->ready=status==PN_OK;return status;
}
static pn_status_t overlay(pn_font_ui_t *u,pn_reader_overlay_fn paint,pn_reader_present_fn present,void *ctx){ui_t *s=u->impl;return s->epub?pn_epub_app_overlay(s->epub,paint,u,present,ctx,PN_REFRESH_GL16):pn_reader_app_overlay(s->reader,paint,u,present,ctx,PN_REFRESH_GL16);}
static pn_status_t page(pn_font_ui_t *u,const char *cursor,bool reverse){ui_t *s=u->impl;pn_status_t status=s->epub?pn_epub_app_fonts_page(s->epub,s->directory,cursor,reverse,&s->page):pn_reader_app_fonts_page(s->reader,s->directory,cursor,reverse,&s->page);if(status==PN_OK){memmove(s->cursor,cursor,strlen(cursor)+1);u->selected=0;s->focus_visible=false;}else{s->page.count=0;s->page.more=false;}return status;}
/* 列表：默认阅读字体和用户外置字体；详情只配置阅读正文。
 * List: default reading font and user external fonts; details configure reading text only. */
#define INHERIT_Y 144
#define DEFAULT_H 80
#define RESIDENT_Y 232
#define TARGET_Y 354
#define LIST_Y0 390
#define LIST_PITCH 88
#define LIST_ROWS 6
#define PAGER_Y 1004
#define DETAIL_Y0 520
#define DETAIL_PITCH 120
static pn_status_t sized_text(pn_font_t *font,pn_frame_t *frame,const char *value,int size,int baseline,pn_align_t align){
 int original=font->pixels;pn_status_t status=pn_font_size(font,size);
 if(status==PN_OK)status=pn_w_text(font,frame,value,PN_UI_MARGIN,baseline,620,align);
 pn_status_t restored=pn_font_size(font,original);return status==PN_OK?restored:status;
}
static pn_status_t sized_button(pn_font_t *font,pn_frame_t *frame,const char *label,int x,int y,int w,int h,unsigned style){
 int original=font->pixels;pn_status_t status=pn_font_size(font,30);
 if(status==PN_OK)status=pn_w_button(font,frame,label,x,y,w,h,style);
 pn_status_t restored=pn_font_size(font,original);return status==PN_OK?restored:status;
}
/* 卡片统一字号和基线，选中与按键焦点分开；不打开外置字库。/ Cards share sizes/baselines and separate the accepted choice from keyboard focus, without opening external fonts. */
static pn_status_t label(pn_font_t *font,pn_frame_t *frame,const char *text,int x,int y,int width,int pixels,uint8_t ink,pn_align_t align){
 int original=font->pixels;pn_status_t status=pn_font_size(font,pixels);
 if(status==PN_OK)status=pn_w_text_ex(font,frame,text,x,y,width,align,ink,false);
 pn_status_t restored=pn_font_size(font,original);return status==PN_OK?restored:status;
}
static pn_status_t font_card(pn_font_t *font,pn_frame_t *frame,const char *name,const char *note,int y,int height,bool active,bool focus,bool details){
 pn_w_round_fill(frame,32,y,620,height,18,active?PN_UI_SELECT:PN_UI_SURFACE);
 if(focus)pn_w_round_stroke(frame,32,y,620,height,18,2,PN_UI_MUTED);
 pn_w_round_fill(frame,48,y+(height-46)/2,46,46,12,PN_UI_PAPER);
 pn_w_icon(frame,PN_ICON_FONT,57,y+(height-28)/2,28,PN_UI_MUTED);
 int top=y+(height-70)/2;
 pn_status_t status=label(font,frame,pn_brand_label(name),112,top+32,active?404:446,30,PN_UI_INK,PN_ALIGN_LEFT);
 if(status==PN_OK)status=label(font,frame,note,112,top+62,416,22,PN_UI_MUTED,PN_ALIGN_LEFT);
 if(active){pn_w_dot(frame,542,y+height/2,17,PN_UI_INK);pn_w_icon(frame,PN_ICON_CHECK,531,y+height/2-11,22,PN_UI_PAPER);}
 if(details){pn_frame_rect(frame,584,y+20,1,height-40,PN_UI_RULE);pn_w_icon(frame,PN_ICON_CHEVRON,610,y+(height-26)/2,26,PN_UI_MUTED);}
 return status;
}
static pn_status_t paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
 pn_font_ui_t *u=ctx;ui_t *s=u->impl;if(!s || frame->width!=684 || frame->height!=1216)return PN_INVALID;pn_frame_clear(frame,15);
 pn_status_t status=pn_w_header(font,frame,u->mode==0?"< 返回":"< 返回列表",u->mode==0?"阅读字体":"阅读字体详情",s->notice?"重试":NULL);
 (void)metadata;
 if(status==PN_OK && u->mode==0){
  bool system=s->current.primary.kind==PN_FONT_RESIDENT;
  status=font_card(font,frame,"跟随全局默认",s->inherited?"已跟随 · 重开读取全局设置":"取消本书覆盖，采用全局默认",INHERIT_Y,DEFAULT_H,s->inherited,false,false);
  if(status==PN_OK)status=font_card(font,frame,"内置字体","仅设置本书 · 自动补字",RESIDENT_Y,DEFAULT_H,system && !s->inherited,false,true);
  char count[128];snprintf(count,sizeof count,"已安装 %zu 款",s->page.total);
  if(status==PN_OK)status=label(font,frame,"外置字体",32,TARGET_Y,300,26,PN_UI_MUTED,PN_ALIGN_LEFT);
  if(status==PN_OK)status=label(font,frame,count,352,TARGET_Y,300,24,PN_UI_MUTED,PN_ALIGN_RIGHT);
  for(size_t i=0;i<s->page.count && i<LIST_ROWS && status==PN_OK;i++){
   char name[PN_CATALOG_NAME_MAX],note[128];strcpy(name,s->page.items[i].name);char *dot=strrchr(name,'.');if(dot && dot!=name)*dot=0;
   bool active=s->current.primary.kind==PN_FONT_FILE && !strcmp(s->current.primary.path,s->page.items[i].path);
   if(active)snprintf(note,sizeof note,"%s",s->inherited?"当前全局默认":"本书单独指定");else snprintf(note,sizeof note,"%llu KB · 点选应用",(unsigned long long)((s->page.items[i].size+1023)/1024));
   status=font_card(font,frame,name,note,LIST_Y0+(int)i*LIST_PITCH,LIST_PITCH-8,active,s->focus_visible && i==u->selected,true);
  }
  if(status==PN_OK && !s->page.count)status=label(font,frame,"暂无外置字体，请在传书中上传",32,LIST_Y0+64,620,28,PN_UI_MUTED,PN_ALIGN_LEFT);
  if(status==PN_OK)status=label(font,frame,"点名称应用本书，点右侧查看详情",32,948,620,24,PN_UI_MUTED,PN_ALIGN_CENTER);
  snprintf(count,sizeof count,"%zu / %zu",s->page.total?s->page.index/PN_CATALOG_PAGE_DEFAULT+1:1,s->page.total?(s->page.total+PN_CATALOG_PAGE_DEFAULT-1)/PN_CATALOG_PAGE_DEFAULT:1);
  if(status==PN_OK)status=label(font,frame,count,228,PAGER_Y+54,228,28,PN_UI_INK,PN_ALIGN_CENTER);
  bool previous=s->page.index>0,next=s->page.more;
  pn_w_round_fill(frame,32,PAGER_Y,176,88,20,previous?PN_UI_SURFACE:PN_UI_PAPER);
  pn_w_round_fill(frame,476,PAGER_Y,176,88,20,next?PN_UI_SURFACE:PN_UI_PAPER);
  pn_w_icon(frame,PN_ICON_BACK,102,PAGER_Y+26,36,previous?PN_UI_INK:PN_UI_RULE);
  pn_w_icon(frame,PN_ICON_CHEVRON,546,PAGER_Y+26,36,next?PN_UI_INK:PN_UI_RULE);
  if(status==PN_OK)status=label(font,frame,"阅读字体独立设置，界面保持固定",32,1170,620,22,PN_UI_MUTED,PN_ALIGN_CENTER);
 }else if(status==PN_OK){
  int original=font->pixels;status=pn_font_size(font,36);
  if(status==PN_OK)status=pn_w_text_lines(font,frame,*s->info.family?pn_brand_label(s->info.family):"内置字体",PN_UI_MARGIN,196,620,2,48,NULL);
  pn_status_t restored=pn_font_size(font,original);if(status==PN_OK)status=restored;
  char value[128];
  if(status==PN_OK){if(s->info.glyphs)snprintf(value,sizeof value,"字重 %u · 字形 %" PRIu32,s->info.weight,s->info.glyphs);else snprintf(value,sizeof value,"%s","默认阅读字库，缺字由系统回退");status=sized_text(font,frame,value,28,332,PN_ALIGN_LEFT);}
  if(status==PN_OK){if(s->checked)snprintf(value,sizeof value,"样例检查：缺字 %u / %u",s->missing,s->checked);else snprintf(value,sizeof value,"请预览原文检查缺字");status=sized_text(font,frame,value,28,384,PN_ALIGN_LEFT);}
  if(status==PN_OK)status=sized_text(font,frame,"仅用于电子书阅读，不改变界面字体",28,436,PN_ALIGN_LEFT);
  const char *labels[]={"预览原文","应用本书","设为默认阅读字体","取消"};
  for(int i=0;i<4 && status==PN_OK;i++)status=sized_button(font,frame,labels[i],32,DETAIL_Y0+i*DETAIL_PITCH,620,96,i==1?PN_W_SELECTED:0u);
 }

 if(status==PN_OK && s->notice)status=sized_text(font,frame,s->notice,28,u->mode==0?PAGER_Y+126:DETAIL_Y0+4*DETAIL_PITCH+24,PN_ALIGN_LEFT);
 return status;
}
pn_status_t pn_font_ui_present(pn_font_ui_t *u,pn_reader_present_fn present,void *ctx){if(!u || !u->impl || !u->active)return PN_INVALID;u->preview=false;u->presented=false;pn_status_t status=overlay(u,paint,present,ctx);u->presented=status==PN_OK;return status;}
pn_status_t pn_font_ui_open(pn_font_ui_t *u,pn_pool_t *pool,pn_reader_app_t *reader,pn_epub_app_t *epub,const char *directory,pn_reader_present_fn present,void *ctx){
 if(!u || !pool || !present || (!!reader==!!epub) || (reader && !reader->impl) || (epub && !epub->impl))return PN_INVALID;
 if(u->impl)return PN_BUSY;
 if(directory && (!*directory || strlen(directory)>=PN_FONT_REFERENCE_PATH_MAX))return PN_LIMIT;
 ui_t *s=pn_alloc_tagged(pool,PN_MEM_LAYOUT,sizeof *s);if(!s)return PN_NO_MEMORY;*s=(ui_t){.pool=pool,.explicit_directory=directory!=NULL};*u=(pn_font_ui_t){.impl=s,.active=true};s->reader=reader;s->epub=epub;
 if(directory)strcpy(s->directory,directory);
 pn_status_t status=preferences(s);
 if(status==PN_OK)status=page(u,"",false);
 if(status!=PN_OK)s->notice="字体目录读取失败，可重试";
 pn_status_t drawn=pn_font_ui_present(u,present,ctx);return drawn!=PN_OK?drawn:status;
}
void pn_font_ui_return_to(pn_font_ui_t *u,pn_reader_overlay_fn paint,void *ctx){if(u && u->impl){ui_t *s=u->impl;s->return_paint=paint;s->return_ctx=ctx;}}
static pn_status_t apply_font(ui_t *s,const pn_font_preferences_t *prefs,uint64_t now,pn_reader_present_fn present,void *ctx){
 if(s->return_paint)return s->epub?pn_epub_app_font_apply_view(s->epub,prefs,s->return_paint,s->return_ctx,now,present,ctx):pn_reader_app_font_apply_view(s->reader,prefs,s->return_paint,s->return_ctx,now,present,ctx);
 return s->epub?pn_epub_app_font_apply(s->epub,prefs,now,present,ctx):pn_reader_app_font_apply(s->reader,prefs,now,present,ctx);
}
static pn_status_t inherit_font(ui_t *s,uint64_t now,pn_reader_present_fn present,void *ctx){
 if(s->return_paint)return s->epub?pn_epub_app_font_inherit_view(s->epub,s->return_paint,s->return_ctx,now,present,ctx):pn_reader_app_font_inherit_view(s->reader,s->return_paint,s->return_ctx,now,present,ctx);
 return s->epub?pn_epub_app_font_inherit(s->epub,now,present,ctx):pn_reader_app_font_inherit(s->reader,now,present,ctx);
}
static pn_status_t cancel_font(ui_t *s,uint64_t now,pn_reader_present_fn present,void *ctx){
 if(s->return_paint)return s->epub?pn_epub_app_font_cancel_view(s->epub,s->return_paint,s->return_ctx,now,present,ctx):pn_reader_app_font_cancel_view(s->reader,s->return_paint,s->return_ctx,now,present,ctx);
 return s->epub?pn_epub_app_font_cancel(s->epub,now,present,ctx):pn_reader_app_font_cancel(s->reader,now,present,ctx);
}
static bool confirmed(ui_t *s){return s->epub?pn_epub_app_last_confirmed(s->epub):pn_reader_app_last_confirmed(s->reader);}
pn_status_t pn_font_ui_event(pn_font_ui_t *u,int command,uint64_t now,pn_reader_present_fn present,void *ctx){
 if(!u || !u->impl || !u->active)return PN_INVALID;
 ui_t *s=u->impl;if(!u->presented && command!=PN_FUI_RETRY)return PN_BUSY;pn_status_t status=PN_OK;s->notice=NULL;
 if(!s->ready && command!=PN_FUI_BACK && command!=PN_FUI_CANCEL && command!=PN_FUI_RETRY)return PN_BUSY;
 if(command==PN_FUI_RETRY){if(!s->ready)status=preferences(s);if(s->ready && u->mode==0)status=page(u,s->cursor,false);if(status!=PN_OK)s->notice="字体目录读取失败，可重试";return pn_font_ui_present(u,present,ctx);}if(command==PN_FUI_FORM)return pn_font_ui_present(u,present,ctx);
 if(command==PN_FUI_CANCEL || (command==PN_FUI_BACK && u->mode==0)){status=cancel_font(s,now,present,ctx);if(confirmed(s)){u->return_presented=s->return_paint!=NULL;u->active=false;return status;}}
 else if(command==PN_FUI_RESTORE_DEFAULT){status=inherit_font(s,now,present,ctx);if(status==PN_OK){u->return_presented=s->return_paint!=NULL;u->active=false;return status;}}
 else if(command==PN_FUI_DEFAULT && u->mode==1){status=s->epub?pn_epub_app_font_default(s->epub,&s->draft):pn_reader_app_font_default(s->reader,&s->draft);if(status==PN_OK)s->notice="全局已保存，跟随默认的书重开后生效";}
 else if(command==PN_FUI_BACK){u->mode=0;}
 else if(command==PN_FUI_PRIMARY){u->mode=0;}
 else if(command==PN_FUI_FALLBACK){status=PN_UNSUPPORTED;}
 else if(command==PN_FUI_NEXT || command==PN_FUI_PREVIOUS){char cursor[PN_CATALOG_NAME_MAX];if(!s->page.count || (command==PN_FUI_NEXT && !s->page.more))status=PN_EMPTY;else{strcpy(cursor,s->page.items[command==PN_FUI_NEXT?s->page.count-1:0].name);status=page(u,cursor,command==PN_FUI_PREVIOUS);}}
 else if(command==PN_FUI_UP || command==PN_FUI_DOWN){int next=(int)u->selected+(command==PN_FUI_UP?-1:1);if(next>=0 && (size_t)next<s->page.count){u->selected=(unsigned)next;s->focus_visible=true;}else status=PN_EMPTY;}
 else if(command==PN_FUI_RESIDENT){s->draft=(pn_font_preferences_t){0};s->info=(pn_font_info_t){0};s->checked=s->missing=0;u->mode=1;}
 else if(command==PN_FUI_SELECT || (command>=PN_FUI_ROW && command<PN_FUI_ROW+6) || (command>=PN_FUI_INFO && command<PN_FUI_INFO+6)){bool direct=command>=PN_FUI_ROW && command<PN_FUI_ROW+6;unsigned row=command==PN_FUI_SELECT?u->selected:direct?(unsigned)(command-PN_FUI_ROW):(unsigned)(command-PN_FUI_INFO);if(row>=s->page.count)status=PN_EMPTY;else{pn_font_reference_t ref;pn_font_info_t info;unsigned checked,missing;status=s->epub?pn_epub_app_fonts_probe(s->epub,&s->page.items[row],&ref,&info,&checked,&missing):pn_reader_app_fonts_probe(s->reader,&s->page.items[row],&ref,&info,&checked,&missing);if(status==PN_OK){u->selected=row;s->draft=(pn_font_preferences_t){.primary=ref};s->info=info;s->checked=checked;s->missing=missing;u->mode=1;
   // 点行即应用到本书并返回排版页看效果；右侧箭头才进详情。/ Tapping a row applies it to this book and returns to typesetting to show the result; only the chevron opens details.
   if(direct){status=apply_font(s,&s->draft,now,present,ctx);if(status==PN_OK){u->return_presented=s->return_paint!=NULL;u->active=false;return status;}u->mode=0;}}}}
 else if(command==PN_FUI_PREVIEW && u->mode==1){status=s->epub?pn_epub_app_font_preview(s->epub,&s->draft,now,present,ctx):pn_reader_app_font_preview(s->reader,&s->draft,now,present,ctx);if(confirmed(s)){u->preview=true;u->presented=true;return status;}}
 else if(command==PN_FUI_APPLY && u->mode==1){status=apply_font(s,&s->draft,now,present,ctx);if(status==PN_OK){u->return_presented=s->return_paint!=NULL;u->active=false;return status;}}
 else status=PN_INVALID;
 if(status!=PN_OK && status!=PN_EMPTY)s->notice="操作失败，选择仍保留";
 pn_status_t drawn=pn_font_ui_present(u,present,ctx);return drawn!=PN_OK?drawn:status;
}
int pn_font_ui_hit(const pn_font_ui_t *u,int x,int y){
 if(!u || !u->impl || !u->active || x<0 || x>=684 || y<0 || y>=1216)return -1;
 if(u->preview)return PN_FUI_FORM;
 const ui_t *s=u->impl;
 int header=pn_w_header_hit(x,y,s->notice!=NULL);
 if(header==1)return u->mode==0?PN_FUI_CANCEL:PN_FUI_BACK;
 if(header==2)return PN_FUI_RETRY;
 if(x<32 || x>=652)return -1;
 if(u->mode==0){
  if(y>=INHERIT_Y && y<INHERIT_Y+DEFAULT_H)return PN_FUI_RESTORE_DEFAULT;
  if(y>=RESIDENT_Y && y<RESIDENT_Y+DEFAULT_H)return PN_FUI_RESIDENT;
  if(y>=LIST_Y0 && y<LIST_Y0+LIST_ROWS*LIST_PITCH){unsigned row=(unsigned)(y-LIST_Y0)/LIST_PITCH;return row<s->page.count?(x>=564?PN_FUI_INFO:PN_FUI_ROW)+(int)row:-1;}
  if(y>=PAGER_Y && y<PAGER_Y+88)return x<208 && s->page.index>0?PN_FUI_PREVIOUS:x>=476 && s->page.more?PN_FUI_NEXT:-1;
  return -1;
 }
 for(int i=0;i<4;i++)if(y>=DETAIL_Y0+i*DETAIL_PITCH && y<DETAIL_Y0+i*DETAIL_PITCH+96)return i==0?PN_FUI_PREVIEW:i==1?PN_FUI_APPLY:i==2?PN_FUI_DEFAULT:PN_FUI_CANCEL;
 return -1;
}
void pn_font_ui_close(pn_font_ui_t *u){if(u){pn_free(u->impl);*u=(pn_font_ui_t){0};}}
