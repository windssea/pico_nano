/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：六项字体菜单、真实字体检查与原文选择操作。
 * English: six-item font menus, actual font checks and anchored selection operations.
 * 冻结：绘制不读目录/记录，不悄悄替换另一项字体。
 * Frozen: painting never reads directories/records or silently replaces the other font.
 */
#include "pn_font_ui.h"
#include "pn_widgets.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
typedef struct {bool ready;pn_pool_t *pool;pn_reader_app_t *reader;pn_epub_app_t *epub;pn_catalog_page_t page;pn_font_preferences_t draft;pn_font_info_t info;char directory[PN_FONT_REFERENCE_PATH_MAX],cursor[PN_CATALOG_NAME_MAX];unsigned target,checked,missing;const char *notice;} ui_t;
static pn_status_t overlay(pn_font_ui_t *u,pn_reader_overlay_fn paint,pn_reader_present_fn present,void *ctx){ui_t *s=u->impl;return s->epub?pn_epub_app_overlay(s->epub,paint,u,present,ctx,PN_REFRESH_GC16):pn_reader_app_overlay(s->reader,paint,u,present,ctx,PN_REFRESH_GC16);}
static pn_status_t page(pn_font_ui_t *u,const char *cursor,bool reverse){ui_t *s=u->impl;pn_status_t status=s->epub?pn_epub_app_fonts_page(s->epub,s->directory,cursor,reverse,&s->page):pn_reader_app_fonts_page(s->reader,s->directory,cursor,reverse,&s->page);if(status==PN_OK){memmove(s->cursor,cursor,strlen(cursor)+1);u->selected=0;}else{s->page.count=0;s->page.more=false;}return status;}
/* 版式：列表页=顶栏、继承默认行、正文/备用切换、六行字体、内置字体行、翻页；详情页=字体信息与四个操作按钮。
 * Layout: list page = header, inherit-default row, primary/fallback switch, six font rows, a built-in row and paging; detail page = font information and four action buttons. */
#define INHERIT_Y 148
#define TARGET_Y 244
#define LIST_Y0 348
#define LIST_ROWS 6
#define RESIDENT_Y 884
#define PAGER_Y 1084
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
static pn_status_t paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
 pn_font_ui_t *u=ctx;ui_t *s=u->impl;if(!s || frame->width!=684 || frame->height!=1216)return PN_INVALID;pn_frame_clear(frame,15);
 pn_status_t status=pn_w_header(font,frame,u->mode==0?"< 取消":"< 返回列表",u->mode==0?"选择字体":"字体详情",s->notice?"重试":NULL);
 pn_w_set_fallback(metadata);
 if(status==PN_OK && u->mode==0){
  status=pn_w_row(font,frame,"继承全局默认字体",NULL,true,INHERIT_Y);
  if(status==PN_OK)status=sized_button(font,frame,"正文字体",32,TARGET_Y,300,80,s->target?0u:PN_W_SELECTED);
  if(status==PN_OK)status=sized_button(font,frame,"备用字体",352,TARGET_Y,300,80,s->target?PN_W_SELECTED:0u);
  for(size_t i=0;i<s->page.count && i<LIST_ROWS && status==PN_OK;i++){
   status=pn_w_row(font,frame,s->page.items[i].name,NULL,true,LIST_Y0+(int)i*PN_W_ROW_H);
   if(status==PN_OK && i==u->selected)pn_frame_rect(frame,PN_UI_MARGIN-12,LIST_Y0+(int)i*PN_W_ROW_H+8,4,PN_W_ROW_H-20,PN_UI_INK);
  }
  if(status==PN_OK && !s->page.count)status=sized_text(font,frame,"fonts 目录暂无可用字体",30,LIST_Y0+60,PN_ALIGN_LEFT);
  if(status==PN_OK)status=pn_w_row(font,frame,s->target?"不使用额外备用字体":"内置界面字体",NULL,true,RESIDENT_Y);
  if(status==PN_OK)status=sized_button(font,frame,"上一页",32,PAGER_Y,196,80,0u);
  if(status==PN_OK)status=sized_button(font,frame,"下一页",456,PAGER_Y,196,80,0u);
 }else if(status==PN_OK){
  int original=font->pixels;status=pn_font_size(font,36);
  if(status==PN_OK)status=pn_w_text_lines(font,frame,*s->info.family?s->info.family:s->target?"不使用额外备用字库":"内置界面字库",PN_UI_MARGIN,196,620,2,48,NULL);
  pn_status_t restored=pn_font_size(font,original);if(status==PN_OK)status=restored;
  char value[128];
  if(status==PN_OK){if(s->info.glyphs)snprintf(value,sizeof value,"字重 %u · 字形 %" PRIu32,s->info.weight,s->info.glyphs);else snprintf(value,sizeof value,"%s",s->target?"仍保留内置回退":"仅包含常用界面文字");status=sized_text(font,frame,value,28,332,PN_ALIGN_LEFT);}
  if(status==PN_OK){if(s->checked)snprintf(value,sizeof value,"样例检查：缺字 %u / %u",s->missing,s->checked);else snprintf(value,sizeof value,"请预览原文检查缺字");status=sized_text(font,frame,value,28,384,PN_ALIGN_LEFT);}
  if(status==PN_OK)status=sized_text(font,frame,s->target?"将用作备用字体":"将用作正文字体",28,436,PN_ALIGN_LEFT);
  const char *labels[]={"预览原文","应用本书","设为全局默认","取消"};
  for(int i=0;i<4 && status==PN_OK;i++)status=sized_button(font,frame,labels[i],32,DETAIL_Y0+i*DETAIL_PITCH,620,96,i==1?PN_W_SELECTED:0u);
 }
 pn_w_set_fallback(NULL);
 if(status==PN_OK && s->notice)status=sized_text(font,frame,s->notice,28,u->mode==0?PAGER_Y-24:DETAIL_Y0+4*DETAIL_PITCH+24,PN_ALIGN_LEFT);
 return status;
}
pn_status_t pn_font_ui_present(pn_font_ui_t *u,pn_reader_present_fn present,void *ctx){if(!u || !u->impl || !u->active)return PN_INVALID;u->preview=false;u->presented=false;pn_status_t status=overlay(u,paint,present,ctx);u->presented=status==PN_OK;return status;}
pn_status_t pn_font_ui_open(pn_font_ui_t *u,pn_pool_t *pool,pn_reader_app_t *reader,pn_epub_app_t *epub,const char *directory,pn_reader_present_fn present,void *ctx){
 if(!u || !pool || !present || (!!reader==!!epub) || (reader && !reader->impl) || (epub && !epub->impl))return PN_INVALID;
 if(u->impl)return PN_BUSY;
 ui_t *s=pn_alloc(pool,sizeof *s);if(!s)return PN_NO_MEMORY;*s=(ui_t){.pool=pool};*u=(pn_font_ui_t){.impl=s,.active=true};s->reader=reader;s->epub=epub;pn_status_t status=epub?pn_epub_app_fonts_get(epub,&s->draft,s->directory,sizeof s->directory):pn_reader_app_fonts_get(reader,&s->draft,s->directory,sizeof s->directory);
 s->ready=status==PN_OK;
 if(status==PN_OK && directory){if(strlen(directory)>=sizeof s->directory)status=PN_LIMIT;else strcpy(s->directory,directory);}if(status==PN_OK)status=page(u,"",false);if(status!=PN_OK)s->notice="字体目录读取失败，可重试";pn_status_t drawn=pn_font_ui_present(u,present,ctx);return drawn!=PN_OK?drawn:status;
}
static bool confirmed(ui_t *s){return s->epub?pn_epub_app_last_confirmed(s->epub):pn_reader_app_last_confirmed(s->reader);}
pn_status_t pn_font_ui_event(pn_font_ui_t *u,int command,uint64_t now,pn_reader_present_fn present,void *ctx){
 if(!u || !u->impl || !u->active)return PN_INVALID;
 ui_t *s=u->impl;if(!u->presented && command!=PN_FUI_RETRY)return PN_BUSY;pn_status_t status=PN_OK;s->notice=NULL;
 if(!s->ready && command!=PN_FUI_BACK && command!=PN_FUI_CANCEL && command!=PN_FUI_RETRY)return PN_BUSY;
 if(command==PN_FUI_RETRY){if(!s->ready){status=s->epub?pn_epub_app_fonts_get(s->epub,&s->draft,s->directory,sizeof s->directory):pn_reader_app_fonts_get(s->reader,&s->draft,s->directory,sizeof s->directory);s->ready=status==PN_OK;}if(s->ready && u->mode==0)status=page(u,s->cursor,false);if(status!=PN_OK)s->notice="字体目录读取失败，可重试";return pn_font_ui_present(u,present,ctx);}if(command==PN_FUI_FORM)return pn_font_ui_present(u,present,ctx);
 if(command==PN_FUI_CANCEL || (command==PN_FUI_BACK && u->mode==0)){status=s->epub?pn_epub_app_font_cancel(s->epub,now,present,ctx):pn_reader_app_font_cancel(s->reader,now,present,ctx);if(confirmed(s)){u->active=false;return status;}}
 else if(command==PN_FUI_INHERIT){status=s->epub?pn_epub_app_font_inherit(s->epub,now,present,ctx):pn_reader_app_font_inherit(s->reader,now,present,ctx);if(status==PN_OK){u->active=false;return status;}if(status==PN_EMPTY)s->notice="请先设置全局默认字体";}
 else if(command==PN_FUI_DEFAULT && u->mode==1){status=s->epub?pn_epub_app_font_default(s->epub,&s->draft):pn_reader_app_font_default(s->reader,&s->draft);if(status==PN_OK)s->notice="默认已保存，下次打开生效";}
 else if(command==PN_FUI_BACK){u->mode=0;}
 else if(command==PN_FUI_PRIMARY || command==PN_FUI_FALLBACK){s->target=command==PN_FUI_FALLBACK;u->mode=0;}
 else if(command==PN_FUI_NEXT || command==PN_FUI_PREVIOUS){char cursor[PN_CATALOG_NAME_MAX];if(!s->page.count || (command==PN_FUI_NEXT && !s->page.more))status=PN_EMPTY;else{strcpy(cursor,s->page.items[command==PN_FUI_NEXT?s->page.count-1:0].name);status=page(u,cursor,command==PN_FUI_PREVIOUS);}}
 else if(command==PN_FUI_UP || command==PN_FUI_DOWN){int next=(int)u->selected+(command==PN_FUI_UP?-1:1);if(next>=0 && (size_t)next<s->page.count)u->selected=(unsigned)next;else status=PN_EMPTY;}
 else if(command==PN_FUI_RESIDENT){if(s->target)s->draft.fallback=(pn_font_reference_t){0};else s->draft.primary=(pn_font_reference_t){0};s->info=(pn_font_info_t){0};s->checked=s->missing=0;u->mode=1;}
 else if(command==PN_FUI_SELECT || (command>=PN_FUI_ROW && command<PN_FUI_ROW+6)){unsigned row=command==PN_FUI_SELECT?u->selected:(unsigned)(command-PN_FUI_ROW);if(row>=s->page.count)status=PN_EMPTY;else{pn_font_reference_t ref;pn_font_info_t info;unsigned checked,missing;status=s->epub?pn_epub_app_fonts_probe(s->epub,&s->page.items[row],&ref,&info,&checked,&missing):pn_reader_app_fonts_probe(s->reader,&s->page.items[row],&ref,&info,&checked,&missing);if(status==PN_OK){u->selected=row;if(s->target)s->draft.fallback=ref;else s->draft.primary=ref;s->info=info;s->checked=checked;s->missing=missing;u->mode=1;}}}
 else if(command==PN_FUI_PREVIEW && u->mode==1){status=s->epub?pn_epub_app_font_preview(s->epub,&s->draft,now,present,ctx):pn_reader_app_font_preview(s->reader,&s->draft,now,present,ctx);if(confirmed(s)){u->preview=true;u->presented=true;return status;}}
 else if(command==PN_FUI_APPLY && u->mode==1){status=s->epub?pn_epub_app_font_apply(s->epub,&s->draft,now,present,ctx):pn_reader_app_font_apply(s->reader,&s->draft,now,present,ctx);if(status==PN_OK){u->active=false;return status;}}
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
  if(y>=INHERIT_Y && y<INHERIT_Y+PN_W_ROW_H)return PN_FUI_INHERIT;
  if(y>=TARGET_Y && y<TARGET_Y+80)return x<332?PN_FUI_PRIMARY:x>=352?PN_FUI_FALLBACK:-1;
  if(y>=LIST_Y0 && y<LIST_Y0+LIST_ROWS*PN_W_ROW_H){unsigned row=(unsigned)(y-LIST_Y0)/PN_W_ROW_H;return row<s->page.count?PN_FUI_ROW+(int)row:-1;}
  if(y>=RESIDENT_Y && y<RESIDENT_Y+PN_W_ROW_H)return PN_FUI_RESIDENT;
  if(y>=PAGER_Y && y<PAGER_Y+80)return x<228?PN_FUI_PREVIOUS:x>=456?PN_FUI_NEXT:-1;
  return -1;
 }
 for(int i=0;i<4;i++)if(y>=DETAIL_Y0+i*DETAIL_PITCH && y<DETAIL_Y0+i*DETAIL_PITCH+96)return i==0?PN_FUI_PREVIEW:i==1?PN_FUI_APPLY:i==2?PN_FUI_DEFAULT:PN_FUI_CANCEL;
 return -1;
}
void pn_font_ui_close(pn_font_ui_t *u){if(u){pn_free(u->impl);*u=(pn_font_ui_t){0};}}
