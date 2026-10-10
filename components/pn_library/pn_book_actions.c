/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书籍操作面板，见pn_book_actions.h。
 * English: book actions sheet, see pn_book_actions.h.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_book_actions.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "pn_widgets.h"
#define SHEET_Y 640
#define BUTTON_X 32
#define BUTTON_W 620
#define BUTTON_H 88
#define OPEN_Y 846
#define FAVORITE_Y 944
#define DELETE_Y 1042
#define CONFIRM_Y 900
static const char *kind(pn_book_format_t f){switch(f){case PN_BOOK_TXT:return "TXT";case PN_BOOK_EPUB:return "EPUB";case PN_BOOK_PDF:return "PDF";case PN_BOOK_FB2:return "FB2";case PN_BOOK_CBZ:return "CBZ";default:return "文件";}}
static void title_of(const pn_catalog_item_t *item,char *out,size_t cap){
    snprintf(out,cap,"%s",item->name);char *dot=strrchr(out,'.');
    if(dot && (!strcasecmp(dot,".txt") || !strcasecmp(dot,".epub") || !strcasecmp(dot,".pdf") || !strcasecmp(dot,".fb2") || !strcasecmp(dot,".cbz")))*dot=0;
}
static void info_of(const pn_catalog_item_t *item,char *out,size_t cap){
    char size[32];if(item->size>=1024u*1024u)snprintf(size,sizeof size,"%llu.%llu MB",(unsigned long long)(item->size>>20),(unsigned long long)((item->size%(1u<<20))*10>>20));else snprintf(size,sizeof size,"%llu KB",(unsigned long long)((item->size+1023)/1024));
    if((item->identified || item->has_progress) && item->progress<=10000)snprintf(out,cap,"%s · %s · 已读 %u%%",kind(item->format),size,(unsigned)(item->progress/100));
    else if(item->identified || item->has_progress)snprintf(out,cap,"%s · %s · 已开始",kind(item->format),size);
    else snprintf(out,cap,"%s · %s · 未读",kind(item->format),size);
}
pn_status_t pn_book_actions_render(const pn_book_actions_t *a,pn_font_t *font,pn_frame_t *frame){
    if(!a || !font || !font->impl || !frame || !frame->pixels || frame->width!=PN_UI_WIDTH || frame->height!=PN_UI_HEIGHT)return PN_INVALID;
    int original=font->pixels;char title[PN_CATALOG_NAME_MAX],info[96];title_of(&a->item,title,sizeof title);info_of(&a->item,info,sizeof info);
    pn_status_t s=PN_OK;
    if(a->confirming){
        // 删除确认整页：说明后果，取消在左、删除在右，删除不用黑底（规范11.2）。/ Full-page delete confirmation: state the consequence, Cancel on the left and Delete on the right, Delete not in black (spec 11.2).
        pn_frame_clear(frame,PN_UI_PAPER);
        s=pn_w_header(font,frame,"","删除图书",NULL);
        pn_w_icon(frame,PN_ICON_TRASH,(PN_UI_WIDTH-88)/2,260,88,PN_UI_INK);
        if(s==PN_OK)s=pn_font_size(font,34);
        if(s==PN_OK)s=pn_w_text_lines_ex(font,frame,title,PN_UI_MARGIN,440,620,2,46,PN_UI_INK,true);
        if(s==PN_OK)s=pn_font_size(font,26);
        if(s==PN_OK)s=pn_w_text_lines_ex(font,frame,"文件将从存储卡删除，无法撤销。阅读记录与书签会保留，重新导入同一本书后可以继续。",PN_UI_MARGIN,560,620,3,38,PN_UI_MUTED,false);
        if(s==PN_OK)s=pn_font_size(font,32);
        if(s==PN_OK)s=pn_w_button(font,frame,"取消",BUTTON_X,CONFIRM_Y,300,BUTTON_H,0u);
        if(s==PN_OK){pn_w_round_stroke(frame,352,CONFIRM_Y,300,BUTTON_H,PN_UI_RADIUS,3.0f,PN_UI_INK);s=pn_w_text_ex(font,frame,"删除",352,CONFIRM_Y+BUTTON_H/2+12,300,PN_ALIGN_CENTER,PN_UI_INK,true);}
    }else{
        // 底部面板：不加遮罩，书名、信息、路径与三个操作。/ Bottom sheet without a mask: title, information, path and three actions.
        pn_w_round_fill(frame,-2,SHEET_Y-4,PN_UI_WIDTH+4,PN_UI_HEIGHT-SHEET_Y+44,PN_UI_CHIP_RADIUS,PN_UI_SELECT);
        pn_w_round_fill(frame,0,SHEET_Y,PN_UI_WIDTH,PN_UI_HEIGHT-SHEET_Y+40,PN_UI_CHIP_RADIUS,PN_UI_PAPER);
        pn_w_round_stroke(frame,-2,SHEET_Y,PN_UI_WIDTH+4,PN_UI_HEIGHT-SHEET_Y+40,PN_UI_CHIP_RADIUS,2.0f,PN_UI_STROKE);
        pn_w_round_fill(frame,(PN_UI_WIDTH-64)/2,SHEET_Y+12,64,6,3,PN_UI_STROKE);
        s=pn_font_size(font,34);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,title,PN_UI_MARGIN,SHEET_Y+74,620,PN_ALIGN_LEFT,PN_UI_INK,true);
        if(s==PN_OK)s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,info,PN_UI_MARGIN,SHEET_Y+114,620,PN_ALIGN_LEFT,PN_UI_MUTED,false);
        if(s==PN_OK)s=pn_font_size(font,20);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,a->item.path,PN_UI_MARGIN,SHEET_Y+148,620,PN_ALIGN_LEFT,PN_UI_MUTED,false);
        if(s==PN_OK)s=pn_font_size(font,32);
        if(s==PN_OK)s=pn_w_button(font,frame,"打开",BUTTON_X,OPEN_Y,BUTTON_W,BUTTON_H,PN_W_SELECTED);
        if(s==PN_OK)s=pn_w_button(font,frame,!a->favorites_known?"收藏不可用":a->favorite?"取消收藏":"加入收藏",BUTTON_X,FAVORITE_Y,BUTTON_W,BUTTON_H,a->favorites_known?0u:PN_W_DISABLED);
        if(s==PN_OK){pn_w_round_stroke(frame,BUTTON_X,DELETE_Y,BUTTON_W,BUTTON_H,PN_UI_RADIUS,2.0f,PN_UI_INK);pn_w_icon(frame,PN_ICON_TRASH,BUTTON_X+226,DELETE_Y+26,36,PN_UI_INK);s=pn_w_text(font,frame,"删除文件",BUTTON_X+272,DELETE_Y+BUTTON_H/2+12,200,PN_ALIGN_LEFT);}
        if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,frame,"点上方空白处关闭",PN_UI_MARGIN,PN_UI_HEIGHT-14,620,PN_ALIGN_CENTER,PN_UI_MUTED,false);}
    }
    if(s==PN_OK && a->notice){s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text_ex(font,frame,a->notice,PN_UI_MARGIN,a->confirming?CONFIRM_Y-30:OPEN_Y-16,620,PN_ALIGN_CENTER,PN_UI_INK,true);}
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
int pn_book_actions_hit(const pn_book_actions_t *a,int x,int y){
    if(!a || x<0 || x>=PN_UI_WIDTH || y<0 || y>=PN_UI_HEIGHT)return -1;
    if(a->confirming){
        if(pn_w_header_hit(x,y,false)==1)return PN_BA_CANCEL;
        if(y>=CONFIRM_Y && y<CONFIRM_Y+BUTTON_H){if(x>=BUTTON_X && x<BUTTON_X+300)return PN_BA_CANCEL;if(x>=352 && x<652)return PN_BA_CONFIRM;}
        return -1;
    }
    if(y<SHEET_Y)return PN_BA_CANCEL;
    if(x<BUTTON_X || x>=BUTTON_X+BUTTON_W)return -1;
    if(y>=OPEN_Y && y<OPEN_Y+BUTTON_H)return PN_BA_OPEN;
    if(y>=FAVORITE_Y && y<FAVORITE_Y+BUTTON_H)return a->favorites_known?PN_BA_FAVORITE:-1;
    if(y>=DELETE_Y && y<DELETE_Y+BUTTON_H)return PN_BA_DELETE;
    return -1;
}
