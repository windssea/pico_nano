#define _POSIX_C_SOURCE 200809L
/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读页页脚与工具栏，见pn_reader_chrome.h。
 * English: reading-page footer and toolbar, see pn_reader_chrome.h.
 */
#include "pn_reader_chrome.h"
#include <string.h>
#define TOOL_COUNT 6
#define SHEET_PADDING 32
#define BUTTON_W 196
#define BUTTON_H 96
#define COLUMN_PITCH 212
#define ROW_Y0 936
#define ROW_PITCH 120
static const char *const labels[TOOL_COUNT]={"目录","书签","搜索","排版","强刷","书架"};
pn_status_t pn_reader_footer_render(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const char *left,const char *right){
    if(!ui || !ui->impl || !frame || !frame->pixels || !left || !right)return PN_INVALID;
    int original=ui->pixels;
    pn_frame_rect(frame,PN_UI_MARGIN,PN_READER_FOOTER_Y,PN_UI_WIDTH-2*PN_UI_MARGIN,2,PN_UI_RULE);
    pn_w_set_fallback(fallback);
    pn_status_t status=pn_font_size(ui,28);
    int right_width=0;
    if(status==PN_OK)status=pn_w_text_width(ui,right,&right_width);
    int left_room=PN_UI_WIDTH-2*PN_UI_MARGIN-(right_width?right_width+24:0);
    if(status==PN_OK)status=pn_w_text(ui,frame,left,PN_UI_MARGIN,PN_READER_FOOTER_Y+46,left_room,PN_ALIGN_LEFT);
    if(status==PN_OK && *right)status=pn_w_text(ui,frame,right,PN_UI_WIDTH-PN_UI_MARGIN-right_width,PN_READER_FOOTER_Y+46,right_width,PN_ALIGN_RIGHT);
    pn_w_set_fallback(NULL);
    pn_status_t restored=pn_font_size(ui,original);return status==PN_OK?restored:status;
}
pn_status_t pn_reader_toolbar_render(pn_font_t *ui,pn_frame_t *frame,unsigned unavailable){
    if(!ui || !ui->impl || !frame || !frame->pixels || frame->width!=PN_UI_WIDTH || frame->height!=PN_UI_HEIGHT)return PN_INVALID;
    int original=ui->pixels;
    pn_frame_rect(frame,0,PN_TOOLBAR_Y,PN_UI_WIDTH,PN_UI_HEIGHT-PN_TOOLBAR_Y,PN_UI_PAPER);
    pn_frame_rect(frame,0,PN_TOOLBAR_Y,PN_UI_WIDTH,2,PN_UI_INK);
    pn_status_t status=pn_font_size(ui,34);
    for(unsigned i=0;i<TOOL_COUNT && status==PN_OK;i++){
        int x=SHEET_PADDING+(int)(i%3)*COLUMN_PITCH,y=ROW_Y0+(int)(i/3)*ROW_PITCH;
        status=pn_w_button(ui,frame,labels[i],x,y,BUTTON_W,BUTTON_H,(unavailable&(1u<<i))?PN_W_DISABLED:0u);
    }
    if(status==PN_OK)status=pn_font_size(ui,26);
    if(status==PN_OK)status=pn_w_text(ui,frame,"点上方正文或按中间键关闭",PN_UI_MARGIN,PN_UI_HEIGHT-16,PN_UI_WIDTH-2*PN_UI_MARGIN,PN_ALIGN_CENTER);
    pn_status_t restored=pn_font_size(ui,original);return status==PN_OK?restored:status;
}
int pn_reader_toolbar_hit(int x,int y,unsigned unavailable){
    if(x<0 || x>=PN_UI_WIDTH || y<0 || y>=PN_UI_HEIGHT)return -1;
    if(y<PN_TOOLBAR_Y)return PN_TOOL_CLOSE;
    for(unsigned i=0;i<TOOL_COUNT;i++){
        int bx=SHEET_PADDING+(int)(i%3)*COLUMN_PITCH,by=ROW_Y0+(int)(i/3)*ROW_PITCH;
        if(x>=bx && x<bx+BUTTON_W && y>=by && y<by+BUTTON_H)return (unavailable&(1u<<i))?-1:(int)(PN_TOOL_TOC+i);
    }
    return -1;
}
