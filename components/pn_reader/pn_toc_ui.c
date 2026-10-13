/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：中文目录页、层级条目和确认后的跳转，绘制不改位置。
 * English: Chinese TOC pages, hierarchy and confirmed jumps with paint-only rendering.
 * 冻结：未呈现不接导航，失败保留菜单与原位置。/ Frozen: no unpresented navigation; failures retain menu/location.
 */
#include "pn_toc_ui.h"
#include "pn_widgets.h"
#include <string.h>
#include <stdio.h>
/* 紧凑版式：十项目录，每项两行以内，整行保持88像素命中。/ Compact layout: ten entries, at most two lines each, with an 88-pixel whole-row target. */
#define ROW_Y0 148
#define ROW_PITCH 88
#define PAGER_Y 1084
#define PAGER_H 88
static pn_status_t paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    pn_toc_ui_t *u=ctx;pn_frame_clear(frame,15);int original=font->pixels;
    pn_status_t status=pn_w_header(font,frame,"< 返回","目录",u->notice?"重试":u->current!=SIZE_MAX?"当前":NULL);
    if(status==PN_OK)status=pn_font_size(font,28);
    if(status==PN_OK && !u->total)status=pn_w_text(font,frame,"这本书没有目录",PN_UI_MARGIN,ROW_Y0+60,620,PN_ALIGN_LEFT);
    // 当前所在章节：浅灰圆角底加左侧黑条（docs与设计规范S05）。/ The chapter being read: a light-gray rounded base with a black side bar (spec S05).
    pn_w_set_fallback(metadata);
    for(size_t i=0;i<u->count && status==PN_OK;i++){
        unsigned level=u->rows[i].level>3?3:u->rows[i].level;int indent=(int)level*24,y=ROW_Y0+(int)i*ROW_PITCH;
        if(u->current!=SIZE_MAX && u->start+i==u->current){pn_w_round_fill(frame,PN_UI_MARGIN,y+2,620,ROW_PITCH-4,PN_UI_RADIUS,13);pn_w_round_fill(frame,PN_UI_MARGIN,y+12,6,ROW_PITCH-24,3,PN_UI_INK);}
        int width=0;status=pn_w_text_width(font,u->rows[i].label,&width);
        if(status==PN_OK)status=pn_w_text_lines(font,frame,u->rows[i].label,PN_UI_MARGIN+indent+8,y+(width<=620-indent-16?54:38),620-indent-16,2,32,NULL);
        if(status==PN_OK && i==u->selected)pn_frame_rect(frame,PN_UI_MARGIN,y+8,4,ROW_PITCH-24,PN_UI_INK);
        if(status==PN_OK)pn_frame_rect(frame,PN_UI_MARGIN,y+ROW_PITCH-1,620,1,10);
    }
    pn_w_set_fallback(NULL);
    if(status==PN_OK && u->notice){status=pn_font_size(font,28);if(status==PN_OK)status=pn_w_text(font,frame,u->notice,PN_UI_MARGIN,PAGER_Y-24,620,PN_ALIGN_LEFT);}
    char page[64];snprintf(page,sizeof page,"%zu / %zu",u->total?u->start/PN_TOC_UI_ROWS+1:0,(u->total+PN_TOC_UI_ROWS-1)/PN_TOC_UI_ROWS);
    if(status==PN_OK)status=pn_font_size(font,30);
    if(status==PN_OK)status=pn_w_button(font,frame,"上一页",32,PAGER_Y,196,PAGER_H,u->start?0u:PN_W_DISABLED);
    if(status==PN_OK)status=pn_w_text(font,frame,page,228,PAGER_Y+54,228,PN_ALIGN_CENTER);
    if(status==PN_OK)status=pn_w_button(font,frame,"下一页",456,PAGER_Y,196,PAGER_H,u->start+u->count<u->total?0u:PN_W_DISABLED);
    pn_status_t restored=pn_font_size(font,original);return status==PN_OK?restored:status;
}
static pn_status_t locate(pn_toc_ui_t *u){
    u->current=SIZE_MAX;size_t sections,current;
    if(pn_epub_app_section_info(u->reader,&sections,&current)!=PN_OK)return PN_EMPTY;
    size_t found=SIZE_MAX;
    for(size_t i=0;i<u->total;i++){pn_toc_entry_t entry;pn_status_t status=pn_epub_app_toc_get(u->reader,i,&entry);if(status!=PN_OK)return status;
        if(entry.target && entry.spine_index==current){
            // 同章多个片段时不猜测正在读哪一项，也不反复解析原文定位。/ Do not guess among multiple fragments of one section or repeatedly parse text to locate them.
            if(found!=SIZE_MAX)return PN_EMPTY;
            found=i;
        }}
    u->current=found;return found==SIZE_MAX?PN_EMPTY:PN_OK;
}
static void current_page(pn_toc_ui_t *u){u->start=u->current/PN_TOC_UI_ROWS*PN_TOC_UI_ROWS;u->selected=(unsigned)(u->current%PN_TOC_UI_ROWS);}
static pn_status_t load(pn_toc_ui_t *u){
    u->count=0;for(size_t i=0;i<PN_TOC_UI_ROWS && u->start+i<u->total;i++){pn_toc_entry_t entry;pn_status_t status=pn_epub_app_toc_get(u->reader,u->start+i,&entry);if(status!=PN_OK)return status;
        strcpy(u->rows[i].label,entry.label);u->rows[i].level=entry.level;u->rows[i].target=entry.target;u->rows[i].spine=entry.spine_index;u->count++;}
    if(u->selected>=u->count)u->selected=0;
    return PN_OK;
}
pn_status_t pn_toc_ui_present(pn_toc_ui_t *u,pn_reader_present_fn present,void *ctx){if(!u || !u->active || !u->reader)return PN_INVALID;u->presented=false;pn_status_t status=pn_epub_app_toc_count(u->reader,&u->total);if(status==PN_EMPTY){u->total=0;status=PN_OK;}if(status==PN_OK)status=load(u);if(status==PN_OK)status=pn_epub_app_overlay(u->reader,paint,u,present,ctx,PN_REFRESH_GL16);u->presented=status==PN_OK;return status;}
pn_status_t pn_toc_ui_open(pn_toc_ui_t *u,pn_epub_app_t *reader,pn_reader_present_fn present,void *ctx){
    if(!u || !reader || !reader->impl)return PN_INVALID;
    memset(u,0,sizeof *u);u->current=SIZE_MAX;u->reader=reader;u->active=true;pn_status_t status=pn_epub_app_toc_count(reader,&u->total);
    if(status==PN_EMPTY){u->total=0;status=PN_OK;}if(status!=PN_OK){u->notice="目录读取失败，位置保留";return status;}
    if(locate(u)==PN_OK)current_page(u);
    return pn_toc_ui_present(u,present,ctx);
}
pn_status_t pn_toc_ui_event(pn_toc_ui_t *u,int command,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(!u || !u->active || !u->reader)return PN_INVALID;
    if(!u->presented && command!=PN_TOC_UI_RETRY && command!=PN_TOC_UI_BACK)return PN_BUSY;
    if(command==PN_TOC_UI_RETRY){pn_status_t status=pn_epub_app_toc_count(u->reader,&u->total);if(status!=PN_OK && status!=PN_EMPTY)return status;u->notice=NULL;return pn_toc_ui_present(u,present,ctx);}
    if(command==PN_TOC_UI_BACK){pn_status_t status=pn_epub_app_step(u->reader,PN_APP_OPEN,now,present,ctx);if(pn_epub_app_last_confirmed(u->reader))u->active=false;return status;}
    if(command==PN_TOC_UI_CURRENT){pn_status_t status=locate(u);if(status!=PN_OK)return status;current_page(u);u->notice=NULL;return pn_toc_ui_present(u,present,ctx);}
    if(command==PN_TOC_UI_NEXT){if(u->start+PN_TOC_UI_ROWS>=u->total)return PN_EMPTY;u->start+=PN_TOC_UI_ROWS;u->selected=0;u->notice=NULL;return pn_toc_ui_present(u,present,ctx);}
    if(command==PN_TOC_UI_PREVIOUS){if(!u->start)return PN_EMPTY;u->start=u->start>=PN_TOC_UI_ROWS?u->start-PN_TOC_UI_ROWS:0;u->selected=0;u->notice=NULL;return pn_toc_ui_present(u,present,ctx);}
    if(command<PN_TOC_UI_ROW || (size_t)(command-PN_TOC_UI_ROW)>=u->count)return PN_INVALID;
    unsigned row=(unsigned)(command-PN_TOC_UI_ROW);u->selected=row;
    if(!u->rows[row].target){u->notice="此项为目录分组";pn_status_t painted=pn_toc_ui_present(u,present,ctx);return painted==PN_OK?PN_EMPTY:painted;}
    pn_status_t status=pn_epub_app_toc_jump(u->reader,u->start+row,now,present,ctx);
    if(pn_epub_app_last_confirmed(u->reader)){u->active=false;return status;}
    u->notice="跳转未完成，位置保留";pn_status_t painted=pn_toc_ui_present(u,present,ctx);return painted==PN_OK?status:painted;
}
int pn_toc_ui_hit(const pn_toc_ui_t *u,int x,int y){
    if(!u || !u->active || x<0 || x>=684 || y<0 || y>=1216)return -1;
    int header=pn_w_header_hit(x,y,u->notice!=NULL || u->current!=SIZE_MAX);
    if(header==1)return PN_TOC_UI_BACK;
    if(header==2)return u->notice?PN_TOC_UI_RETRY:PN_TOC_UI_CURRENT;
    if(x>=32 && x<652 && y>=ROW_Y0 && y<ROW_Y0+PN_TOC_UI_ROWS*ROW_PITCH){int row=(y-ROW_Y0)/ROW_PITCH;if((size_t)row<u->count)return PN_TOC_UI_ROW+row;}
    if(y>=PAGER_Y && y<PAGER_Y+PAGER_H){if(x>=32 && x<228 && u->start)return PN_TOC_UI_PREVIOUS;if(x>=456 && x<652 && u->start+u->count<u->total)return PN_TOC_UI_NEXT;}
    return -1;
}
void pn_toc_ui_close(pn_toc_ui_t *u){if(u)memset(u,0,sizeof *u);}
