#define _POSIX_C_SOURCE 200809L
/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读页页脚与工具栏，见pn_reader_chrome.h。
 * English: reading-page footer and toolbar, see pn_reader_chrome.h.
 */
#include "pn_reader_chrome.h"
#include "pn_brand.h"
#include "pn_builtin_art.h"
#include <stdio.h>
#include <string.h>
#define TOOL_COUNT 6
#define SHEET_PADDING 32
#define BUTTON_W 196
#define BUTTON_H 96
#define COLUMN_PITCH 212
#define ROW_Y0 936
#define ROW_PITCH 120
static const char *const labels[TOOL_COUNT]={"目录","书签","跳转","排版","刷新","返回"};
static const pn_icon_t icons[TOOL_COUNT]={PN_ICON_TOC,PN_ICON_BOOKMARK,PN_ICON_PROGRESS,PN_ICON_TYPESET,PN_ICON_REFRESH,PN_ICON_BACK};
pn_status_t pn_reader_status_text(pn_font_t *ui,pn_frame_t *frame){
    if(!ui || !ui->impl || !frame || !frame->pixels)return PN_INVALID;
    pn_w_status_snapshot_t state=pn_w_status_snapshot();char text[32];
    if(state.battery>=0)snprintf(text,sizeof text,"%s   %d%%",*state.clock?state.clock:"--:--",state.battery);
    else snprintf(text,sizeof text,"%s   --%%",*state.clock?state.clock:"--:--");
    int original=ui->pixels;
    pn_status_t s=pn_font_size(ui,20);if(s==PN_OK)s=pn_w_text_ex(ui,frame,text,410,24,242,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
    pn_status_t restored=pn_font_size(ui,original);return s==PN_OK?restored:s;
}
pn_status_t pn_reader_status_render(pn_font_t *ui,pn_frame_t *frame,unsigned background){
    if(!ui || !ui->impl)return PN_INVALID;
    pn_status_t s=pn_reading_background_region(background,frame,0,PN_READER_TOP);
    return s==PN_OK?pn_reader_status_text(ui,frame):s;
}
pn_status_t pn_reader_footer_progress(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const char *left,const char *right,int basis,unsigned background){
    if(!ui || !ui->impl || !frame || !frame->pixels || !left || !right)return PN_INVALID;
    int original=ui->pixels;pn_status_t top=background?pn_reader_status_text(ui,frame):pn_reader_status_render(ui,frame,0);if(top!=PN_OK)return top;
    // 进度轨：细灰轨、黑色已读段与小圆点；没有进度时退回一条细线。/ Progress track: a thin gray rail, a black read part and a small knob; a plain hairline without progress.
    if(basis>=0 && basis<=10000){
        int track=PN_UI_WIDTH-2*PN_UI_MARGIN,fill=track*basis/10000;
        pn_w_round_fill(frame,PN_UI_MARGIN,PN_READER_FOOTER_BAND_Y+6,track,4,2,10);
        if(fill>0)pn_w_round_fill(frame,PN_UI_MARGIN,PN_READER_FOOTER_BAND_Y+6,fill,4,2,PN_UI_INK);
        pn_w_dot(frame,(float)(PN_UI_MARGIN+fill),(float)(PN_READER_FOOTER_BAND_Y+8),7.0f,PN_UI_INK);
    }else pn_frame_rect(frame,PN_UI_MARGIN,PN_READER_FOOTER_Y,PN_UI_WIDTH-2*PN_UI_MARGIN,2,PN_UI_RULE);
    pn_w_set_fallback(fallback);
    pn_status_t status=pn_font_size(ui,26);
    int right_width=0;
    if(status==PN_OK)status=pn_w_text_width(ui,right,&right_width);
    int left_room=PN_UI_WIDTH-2*PN_UI_MARGIN-(right_width?right_width+24:0);
    if(status==PN_OK)status=pn_w_text(ui,frame,left,PN_UI_MARGIN,PN_READER_FOOTER_Y+52,left_room,PN_ALIGN_LEFT);
    if(status==PN_OK && *right)status=pn_w_text(ui,frame,right,PN_UI_WIDTH-PN_UI_MARGIN-right_width,PN_READER_FOOTER_Y+52,right_width,PN_ALIGN_RIGHT);
    pn_w_set_fallback(NULL);
    pn_status_t restored=pn_font_size(ui,original);return status==PN_OK?restored:status;
}
pn_status_t pn_reader_footer_render(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const char *left,const char *right){
    return pn_reader_footer_progress(ui,fallback,frame,left,right,-1,0);
}
pn_status_t pn_reader_toolbar_render(pn_font_t *ui,pn_frame_t *frame,unsigned unavailable){
    if(!ui || !ui->impl || !frame || !frame->pixels || frame->width!=PN_UI_WIDTH || frame->height!=PN_UI_HEIGHT)return PN_INVALID;
    int original=ui->pixels;
    // 底部面板：圆角顶边、细描边与一条抓手；每个入口是图标在上、文字在下的圆角卡片。
    // Bottom sheet: rounded top edge, a hairline and a grab handle; every entry is a rounded card with the icon above its label.
    // 不加灰色遮罩：底部面板为不透明雾面，上沿圆角、细描边、抓手（产品决定，减少残影）。/ No gray mask: the bottom sheet is an opaque fog surface with rounded top corners, a hairline and a grab handle (product decision, to limit ghosting).
    pn_w_round_fill(frame,-2,PN_TOOLBAR_Y-4,PN_UI_WIDTH+4,PN_UI_HEIGHT-PN_TOOLBAR_Y+44,PN_UI_CHIP_RADIUS,PN_UI_SELECT);
    pn_w_round_fill(frame,0,PN_TOOLBAR_Y,PN_UI_WIDTH,PN_UI_HEIGHT-PN_TOOLBAR_Y+40,PN_UI_CHIP_RADIUS,PN_UI_PAPER);
    pn_w_round_stroke(frame,-2,PN_TOOLBAR_Y,PN_UI_WIDTH+4,PN_UI_HEIGHT-PN_TOOLBAR_Y+40,PN_UI_CHIP_RADIUS,2.0f,PN_UI_STROKE);
    pn_w_round_fill(frame,(PN_UI_WIDTH-64)/2,PN_TOOLBAR_Y+12,64,6,3,PN_UI_STROKE);
    pn_status_t status=pn_font_size(ui,26);
    for(unsigned i=0;i<TOOL_COUNT && status==PN_OK;i++){
        int x=SHEET_PADDING+(int)(i%3)*COLUMN_PITCH,y=ROW_Y0+(int)(i/3)*ROW_PITCH;bool off=(unavailable&(1u<<i))!=0;
        pn_w_glass(frame,x,y,BUTTON_W,BUTTON_H,PN_UI_RADIUS);
        pn_w_icon(frame,icons[i],x+(BUTTON_W-36)/2,y+10,36,off?PN_UI_DISABLED:PN_UI_INK);
        status=pn_w_text_ex(ui,frame,labels[i],x+8,y+BUTTON_H-12,BUTTON_W-16,PN_ALIGN_CENTER,off?PN_UI_DISABLED:PN_UI_INK,false);
    }
    if(status==PN_OK)status=pn_w_text(ui,frame,"点击空白处或按中间键关闭",PN_UI_MARGIN,PN_UI_HEIGHT-16,PN_UI_WIDTH-2*PN_UI_MARGIN,PN_ALIGN_CENTER);
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
bool pn_reader_loading_needed(bool text_format,uint64_t source_bytes){return !text_format || source_bytes>65536u;}
pn_status_t pn_reader_jump_loading_render(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    (void)ctx;(void)metadata;pn_frame_clear(frame,PN_UI_PAPER);
    pn_status_t s=pn_w_header(font,frame,"","阅读",NULL);
    return s==PN_OK?pn_w_loading(font,frame,"正在跳转书页","正在准备目标页面，请稍候",-1,-1):s;
}
void pn_reader_guide_row(pn_frame_t *frame,unsigned style,unsigned shade,int y){
    if(!frame || !frame->pixels || style<1 || style>2 || y<0 || y>=frame->height || frame->width<1)return;
    // 浅档保留原灰阶10；越界深浅按浅处理。/ The light level keeps the original gray 10; out-of-range shades fall back to light.
    static const uint8_t inks[4]={10,7,4,0};uint8_t ink=inks[shade<4?shade:0];
    int dash=style==1?19:2,period=style==1?31:13;
    for(int x=0;x<frame->width;x+=period){int width=frame->width-x;if(width>dash)width=dash;pn_frame_rect(frame,x,y,width,1,ink);}
}
pn_status_t pn_reader_loading_render(pn_font_t *ui,pn_frame_t *frame,const char *title){
    return pn_reader_loading_render_phase(ui,frame,title,0);
}
pn_status_t pn_reader_loading_render_phase(pn_font_t *ui,pn_frame_t *frame,const char *title,unsigned phase){
    if(!ui || !ui->impl || !frame || !frame->pixels || !title)return PN_INVALID;
    int original=ui->pixels;pn_frame_clear(frame,PN_UI_PAPER);
    pn_status_t status=pn_font_size(ui,28);
    if(status==PN_OK)status=pn_w_text(ui,frame,PN_BRAND_NAME,32,72,frame->width-64,PN_ALIGN_CENTER);
    if(status==PN_OK)status=pn_w_loading_phase(ui,frame,"正在打开书籍",title,-1,-1,phase);
    pn_status_t restored=pn_font_size(ui,original);return status==PN_OK?restored:status;
}
