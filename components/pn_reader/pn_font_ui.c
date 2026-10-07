/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：六项字体菜单、真实字体检查与原文选择操作。
 * English: six-item font menus, actual font checks and anchored selection operations.
 * 冻结：绘制不读目录/记录，不悄悄替换另一项字体。
 * Frozen: painting never reads directories/records or silently replaces the other font.
 */
#include "pn_font_ui.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
typedef struct {bool ready;pn_pool_t *pool;pn_reader_app_t *reader;pn_epub_app_t *epub;pn_catalog_page_t page;pn_font_preferences_t draft;pn_font_info_t info;char directory[PN_FONT_REFERENCE_PATH_MAX],cursor[PN_CATALOG_NAME_MAX];unsigned target,checked,missing;const char *notice;} ui_t;
static pn_status_t overlay(pn_font_ui_t *u,pn_reader_overlay_fn paint,pn_reader_present_fn present,void *ctx){ui_t *s=u->impl;return s->epub?pn_epub_app_overlay(s->epub,paint,u,present,ctx,PN_REFRESH_GC16):pn_reader_app_overlay(s->reader,paint,u,present,ctx,PN_REFRESH_GC16);}
static pn_status_t page(pn_font_ui_t *u,const char *cursor,bool reverse){ui_t *s=u->impl;pn_status_t status=s->epub?pn_epub_app_fonts_page(s->epub,s->directory,cursor,reverse,&s->page):pn_reader_app_fonts_page(s->reader,s->directory,cursor,reverse,&s->page);if(status==PN_OK){memmove(s->cursor,cursor,strlen(cursor)+1);u->selected=0;}else{s->page.count=0;s->page.more=false;}return status;}
static pn_status_t read_string(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t length=strlen(s);if(at>length)return PN_INVALID;size_t count=length-(size_t)at;if(count>cap)count=cap;memcpy(out,s+at,count);*n=count;return PN_OK;}
static pn_status_t label(pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame,const char *value,int x,int y,int width,int height){
 pn_frame_t part={frame->pixels+(size_t)y*frame->stride+(size_t)x/2,width,height,frame->stride};pn_text_source_t source={(void *)value,strlen(value),read_string,NULL};pn_text_reader_t decoder;pn_status_t status=pn_text_open(&decoder,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;int baseline=font->pixels+4,line=font->pixels+6;int32_t cursor=0;pn_text_char_t c;
 while((status=pn_text_next(&decoder,&c))==PN_OK){pn_font_t *used=font;int32_t advance;pn_status_t measured=pn_font_advance(used,c.codepoint,&advance);if(measured==PN_EMPTY && metadata){used=metadata;measured=pn_font_advance(used,c.codepoint,&advance);}if(measured==PN_EMPTY)advance=font->pixels*64;else if(measured!=PN_OK)return measured;if(advance<0 || advance>width*64)return PN_LIMIT;if(cursor>width*64-advance){cursor=0;baseline+=line;}if(baseline>height)return PN_OK;
  if(measured==PN_EMPTY){int size=font->pixels-4,px=cursor/64,py=baseline-font->pixels;pn_frame_rect(&part,px,py,size,1,0);pn_frame_rect(&part,px,py+size,size,1,0);pn_frame_rect(&part,px,py,1,size,0);pn_frame_rect(&part,px+size,py,1,size,0);}else{status=pn_font_draw(used,&part,c.codepoint,cursor,baseline,PN_FONT_GRAY);if(status!=PN_OK)return status;}cursor+=advance;}
 return status==PN_EMPTY?PN_OK:status;
}
static pn_status_t button(pn_font_t *font,pn_frame_t *frame,const char *value,int x,int y,int w,int h){pn_frame_rect(frame,x,y,w,1,5);pn_frame_rect(frame,x,y+h-1,w,1,5);pn_frame_rect(frame,x,y,1,h,5);pn_frame_rect(frame,x+w-1,y,1,h,5);return label(font,NULL,frame,value,x+16,y+16,w-32,h-24);}
static pn_status_t paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
 pn_font_ui_t *u=ctx;ui_t *s=u->impl;if(!s || frame->width!=684 || frame->height!=1216)return PN_INVALID;pn_frame_clear(frame,15);pn_status_t status=button(font,frame,"返回",32,20,160,80);if(status!=PN_OK)return status;status=button(font,frame,"继承默认",218,20,300,80);if(status!=PN_OK)return status;status=button(font,frame,"重试",532,20,120,80);if(status!=PN_OK)return status;pn_frame_rect(frame,32,124,620,1,8);
 if(u->mode==0){for(size_t i=0;i<s->page.count;i++){int y=156+(int)i*128;status=label(font,metadata,frame,s->page.items[i].name,48,y+6,584,92);if(status!=PN_OK)return status;if(i==u->selected)pn_frame_rect(frame,32,y,2,112,0);pn_frame_rect(frame,48,y+114,584,1,10);}if(!s->page.count){status=label(font,NULL,frame,"目录暂无可用字体",48,240,584,80);if(status!=PN_OK)return status;}
  status=button(font,frame,s->target?"正文":"正文 已选",32,960,300,80);if(status!=PN_OK)return status;status=button(font,frame,s->target?"备用 已选":"备用",352,960,300,80);if(status!=PN_OK)return status;
  const char *labels[]={"上一页","下一页","内置"};for(int i=0;i<3;i++){status=button(font,frame,labels[i],32+i*212,1130,196,80);if(status!=PN_OK)return status;}
 }else{status=label(font,metadata,frame,*s->info.family?s->info.family:s->target?"不使用额外备用字库":"内置界面字库",48,180,584,96);if(status!=PN_OK)return status;char value[128];if(s->info.glyphs)snprintf(value,sizeof value,"字重 %u  字形 %" PRIu32,s->info.weight,s->info.glyphs);else snprintf(value,sizeof value,"%s",s->target?"仍保留内置回退":"仅包含常用界面文字");status=label(font,NULL,frame,value,48,300,584,80);if(status!=PN_OK)return status;if(s->checked)snprintf(value,sizeof value,"样例检查：缺字 %u / %u",s->missing,s->checked);else strcpy(value,"请预览原文检查缺字");status=label(font,NULL,frame,value,48,396,584,80);if(status!=PN_OK)return status;status=label(font,NULL,frame,s->target?"选择用于备用字体":"选择用于正文字体",48,500,584,80);if(status!=PN_OK)return status;
  status=button(font,frame,"预览原文",32,650,620,96);if(status!=PN_OK)return status;status=button(font,frame,"应用本书",32,790,620,96);if(status!=PN_OK)return status;status=button(font,frame,"设为全局默认",32,930,620,96);if(status!=PN_OK)return status;status=button(font,frame,"返回列表",32,1130,300,80);if(status!=PN_OK)return status;status=button(font,frame,"取消",352,1130,300,80);if(status!=PN_OK)return status;
 }
 if(s->notice)status=label(font,NULL,frame,s->notice,48,1050,584,70);
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
int pn_font_ui_hit(const pn_font_ui_t *u,int x,int y){if(!u || !u->impl || !u->active || x<0 || x>=684 || y<0 || y>=1216)return -1;if(u->preview)return PN_FUI_FORM;if(y>=20 && y<100){if(x>=32 && x<192)return PN_FUI_BACK;if(x>=218 && x<518)return PN_FUI_INHERIT;if(x>=532 && x<652)return PN_FUI_RETRY;}if(u->mode==0){if(x>=32 && x<652 && y>=156 && y<924 && (y-156)%128<112){unsigned row=(unsigned)(y-156)/128;ui_t *s=u->impl;if(row<s->page.count)return PN_FUI_ROW+(int)row;}if(y>=960 && y<1040){if(x>=32 && x<332)return PN_FUI_PRIMARY;if(x>=352 && x<652)return PN_FUI_FALLBACK;}if(y>=1130 && y<1210){if(x>=32 && x<228)return PN_FUI_PREVIOUS;if(x>=244 && x<440)return PN_FUI_NEXT;if(x>=456 && x<652)return PN_FUI_RESIDENT;}}else{if(x>=32 && x<652){if(y>=650 && y<746)return PN_FUI_PREVIEW;if(y>=790 && y<886)return PN_FUI_APPLY;if(y>=930 && y<1026)return PN_FUI_DEFAULT;}if(y>=1130 && y<1210){if(x>=32 && x<332)return PN_FUI_BACK;if(x>=352 && x<652)return PN_FUI_CANCEL;}}return -1;}
void pn_font_ui_close(pn_font_ui_t *u){if(u){pn_free(u->impl);*u=(pn_font_ui_t){0};}}
