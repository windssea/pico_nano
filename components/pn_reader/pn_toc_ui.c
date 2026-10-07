/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：中文目录页、层级条目和确认后的跳转，绘制不改位置。
 * English: Chinese TOC pages, hierarchy and confirmed jumps with paint-only rendering.
 * 冻结：未呈现不接导航，失败保留菜单与原位置。/ Frozen: no unpresented navigation; failures retain menu/location.
 */
#include "pn_toc_ui.h"
#include <string.h>
#include <stdio.h>
static pn_status_t read_string(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t size=strlen(s);if(off>size)return PN_INVALID;size_t count=size-(size_t)off;if(count>cap)count=cap;memcpy(out,s+off,count);*n=count;return PN_OK;}
static pn_status_t label(pn_font_t *ui,pn_font_t *metadata,pn_frame_t *frame,const char *value,int x,int y,int width,unsigned lines){
    pn_text_source_t source={(void *)value,strlen(value),read_string,NULL};pn_text_reader_t r;pn_status_t status=pn_text_open(&r,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    pn_frame_t part={frame->pixels+(size_t)y*frame->stride+(size_t)x/2,width,(int)(lines*56),frame->stride};pn_text_char_t c;int32_t at=0;unsigned row=0;
    while((status=pn_text_next(&r,&c))==PN_OK){pn_font_t *font=metadata?metadata:ui;int32_t advance;pn_status_t measured=pn_font_advance(font,c.codepoint,&advance);
        if(measured==PN_EMPTY && font!=ui){font=ui;measured=pn_font_advance(font,c.codepoint,&advance);}
        if(measured==PN_EMPTY)advance=36*64;else if(measured!=PN_OK)return measured;
        if(advance<0 || advance>width*64)return PN_LIMIT;
        if(at>width*64-advance){at=0;if(++row==lines)return PN_OK;}
        int baseline=40+(int)row*56;
        if(measured==PN_EMPTY){int px=at/64;pn_frame_rect(&part,px,baseline-32,28,1,0);pn_frame_rect(&part,px,baseline-4,28,1,0);pn_frame_rect(&part,px,baseline-32,1,28,0);pn_frame_rect(&part,px+27,baseline-32,1,28,0);}
        else{status=pn_font_draw(font,&part,c.codepoint,at,baseline,PN_FONT_GRAY);if(status!=PN_OK)return status;}
        at+=advance;
    }
    return status==PN_EMPTY?PN_OK:status;
}
static pn_status_t paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    pn_toc_ui_t *u=ctx;pn_frame_clear(frame,15);pn_status_t status=label(font,NULL,frame,"返回",32,24,140,1);if(status!=PN_OK)return status;
    status=label(font,NULL,frame,"目录",282,24,180,1);if(status!=PN_OK)return status;
    status=label(font,NULL,frame,"重试",532,24,120,1);if(status!=PN_OK)return status;pn_frame_rect(frame,32,110,620,1,8);
    if(!u->total){status=label(font,NULL,frame,"暂无目录",48,180,584,1);if(status!=PN_OK)return status;}
    for(size_t i=0;i<u->count;i++){unsigned level=u->rows[i].level>3?3:u->rows[i].level;int indent=(int)level*24,y=160+(int)i*144;
        status=label(font,metadata,frame,u->rows[i].label,48+indent,y,584-indent,2);if(status!=PN_OK)return status;
        if(i==u->selected)pn_frame_rect(frame,32,y,2,128,0);
        pn_frame_rect(frame,48,y+136,584,1,10);
    }
    if(u->notice){status=label(font,NULL,frame,u->notice,48,1040,584,1);if(status!=PN_OK)return status;}
    char page[64];snprintf(page,sizeof page,"%zu / %zu",u->total?u->start/6+1:0,(u->total+5)/6);
    status=label(font,NULL,frame,"上页",32,1130,180,1);if(status!=PN_OK)return status;
    status=label(font,NULL,frame,page,250,1130,260,1);if(status!=PN_OK)return status;
    return label(font,NULL,frame,"下页",532,1130,120,1);
}
static pn_status_t load(pn_toc_ui_t *u){
    u->count=0;for(size_t i=0;i<PN_TOC_UI_ROWS && u->start+i<u->total;i++){pn_toc_entry_t entry;pn_status_t status=pn_epub_app_toc_get(u->reader,u->start+i,&entry);if(status!=PN_OK)return status;
        strcpy(u->rows[i].label,entry.label);u->rows[i].level=entry.level;u->rows[i].target=entry.target;u->count++;}
    if(u->selected>=u->count)u->selected=0;
    return PN_OK;
}
pn_status_t pn_toc_ui_present(pn_toc_ui_t *u,pn_reader_present_fn present,void *ctx){if(!u || !u->active || !u->reader)return PN_INVALID;u->presented=false;pn_status_t status=pn_epub_app_toc_count(u->reader,&u->total);if(status==PN_EMPTY){u->total=0;status=PN_OK;}if(status==PN_OK)status=load(u);if(status==PN_OK)status=pn_epub_app_overlay(u->reader,paint,u,present,ctx,PN_REFRESH_GC16);u->presented=status==PN_OK;return status;}
pn_status_t pn_toc_ui_open(pn_toc_ui_t *u,pn_epub_app_t *reader,pn_reader_present_fn present,void *ctx){
    if(!u || !reader || !reader->impl)return PN_INVALID;
    memset(u,0,sizeof *u);u->reader=reader;u->active=true;pn_status_t status=pn_epub_app_toc_count(reader,&u->total);
    if(status==PN_EMPTY){u->total=0;status=PN_OK;}if(status!=PN_OK){u->notice="目录读取失败，位置保留";return status;}
    return pn_toc_ui_present(u,present,ctx);
}
pn_status_t pn_toc_ui_event(pn_toc_ui_t *u,int command,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(!u || !u->active || !u->reader)return PN_INVALID;
    if(!u->presented && command!=PN_TOC_UI_RETRY && command!=PN_TOC_UI_BACK)return PN_BUSY;
    if(command==PN_TOC_UI_RETRY){pn_status_t status=pn_epub_app_toc_count(u->reader,&u->total);if(status!=PN_OK && status!=PN_EMPTY)return status;u->notice=NULL;return pn_toc_ui_present(u,present,ctx);}
    if(command==PN_TOC_UI_BACK){pn_status_t status=pn_epub_app_step(u->reader,PN_APP_OPEN,now,present,ctx);if(pn_epub_app_last_confirmed(u->reader))u->active=false;return status;}
    if(command==PN_TOC_UI_NEXT){if(u->start+6>=u->total)return PN_EMPTY;u->start+=6;u->selected=0;u->notice=NULL;return pn_toc_ui_present(u,present,ctx);}
    if(command==PN_TOC_UI_PREVIOUS){if(!u->start)return PN_EMPTY;u->start=u->start>=6?u->start-6:0;u->selected=0;u->notice=NULL;return pn_toc_ui_present(u,present,ctx);}
    if(command<PN_TOC_UI_ROW || (size_t)(command-PN_TOC_UI_ROW)>=u->count)return PN_INVALID;
    unsigned row=(unsigned)(command-PN_TOC_UI_ROW);u->selected=row;
    if(!u->rows[row].target){u->notice="此项为目录分组";pn_status_t painted=pn_toc_ui_present(u,present,ctx);return painted==PN_OK?PN_EMPTY:painted;}
    pn_status_t status=pn_epub_app_toc_jump(u->reader,u->start+row,now,present,ctx);
    if(pn_epub_app_last_confirmed(u->reader)){u->active=false;return status;}
    u->notice="跳转未完成，位置保留";pn_status_t painted=pn_toc_ui_present(u,present,ctx);return painted==PN_OK?status:painted;
}
int pn_toc_ui_hit(const pn_toc_ui_t *u,int x,int y){
    if(!u || !u->active || x<0 || x>=684 || y<0 || y>=1216)return -1;
    if(y>=16 && y<96){if(x>=32 && x<220)return PN_TOC_UI_BACK;if(x>=532 && x<652)return PN_TOC_UI_RETRY;}
    if(x>=32 && x<652 && y>=160 && y<1024 && (y-160)%144<136){int row=(y-160)/144;if((size_t)row<u->count)return PN_TOC_UI_ROW+row;}
    if(y>=1120 && y<1210){if(x>=32 && x<228)return PN_TOC_UI_PREVIOUS;if(x>=532 && x<652)return PN_TOC_UI_NEXT;}return -1;
}
void pn_toc_ui_close(pn_toc_ui_t *u){if(u)memset(u,0,sizeof *u);}
