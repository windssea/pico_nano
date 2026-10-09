/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读进度跳转面板，见pn_jump_ui.h；绘制只读草稿，写阅读位置只发生在确认。
 * English: reading progress jump panel, see pn_jump_ui.h; painting only reads the draft and the reading position moves only on confirmation.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_jump_ui.h"
#include <stdio.h>
#include <string.h>
#include "pn_widgets.h"

/* 版式：顶栏0–127；当前位置、目标值、进度条、预览、四个步进键、跳转与取消。
 * Layout: header 0–127; current position, target value, progress bar, preview, four step keys, Jump and Cancel. */
#define BAR_Y 520
#define STEP_Y 650
#define STEP_W 143
#define STEP_GAP 16
#define CONFIRM_Y 820
#define CANCEL_Y 940
#define BUTTON_H 96
static bool live(const pn_jump_ui_t *u){return u && ((u->reader && u->reader->impl) || (u->epub && u->epub->impl));}
static pn_status_t overlay(pn_jump_ui_t *u,pn_reader_overlay_fn paint,pn_reader_present_fn present,void *ctx){
    return u->epub?pn_epub_app_overlay(u->epub,paint,u,present,ctx,PN_REFRESH_GL16):pn_reader_app_overlay(u->reader,paint,u,present,ctx,PN_REFRESH_GL16);
}
/* 值的显示：TXT“42%”，EPUB“第 12 节”。/ Value text: "42%" for TXT, "section 12" for EPUB. */
static void value_text(const pn_jump_ui_t *u,unsigned value,char *out,size_t cap){
    if(u->epub)snprintf(out,cap,"第 %u 节",value+1);else snprintf(out,cap,"%u%%",value);
}
static pn_status_t paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    (void)metadata;pn_jump_ui_t *u=ctx;pn_frame_clear(frame,PN_UI_PAPER);
    int original=font->pixels;char text[96],now[48];
    pn_status_t s=pn_font_size(font,34);
    if(s==PN_OK)s=pn_w_text(font,frame,"< 取消",PN_UI_MARGIN,78,200,PN_ALIGN_LEFT);
    if(s==PN_OK)s=pn_font_size(font,40);
    if(s==PN_OK)s=pn_w_text(font,frame,u->epub?"跳到章节":"跳到位置",0,80,PN_UI_WIDTH,PN_ALIGN_CENTER);
    pn_frame_rect(frame,PN_UI_MARGIN,124,620,2,PN_UI_RULE);
    // 当前位置与目标。/ Current position and target.
    value_text(u,u->current,now,sizeof now);
    if(s==PN_OK)s=pn_font_size(font,30);
    snprintf(text,sizeof text,"现在在 %s",now);
    if(s==PN_OK)s=pn_w_text(font,frame,text,PN_UI_MARGIN,200,620,PN_ALIGN_LEFT);
    value_text(u,u->draft,text,sizeof text);
    if(s==PN_OK)s=pn_font_size(font,88);
    if(s==PN_OK)s=pn_w_text(font,frame,text,PN_UI_MARGIN,400,620,PN_ALIGN_CENTER);
    // 进度条：粗线为目标，竖线标出现在的位置。/ Progress bar: the thick fill is the target, a tick marks where the reader is now.
    unsigned span=u->maximum?u->maximum:1;int fill=(int)(620u*u->draft/span),mark=(int)(620u*u->current/span);
    pn_frame_rect(frame,PN_UI_MARGIN,BAR_Y,620,16,12);pn_frame_rect(frame,PN_UI_MARGIN,BAR_Y,fill,16,PN_UI_INK);
    pn_frame_rect(frame,PN_UI_MARGIN+(mark>=620?618:mark),BAR_Y-12,2,40,PN_UI_INK);
    // 预览行：说明跳转后的相对位置。/ Preview line describing the relative position after the jump.
    if(s==PN_OK)s=pn_font_size(font,28);
    if(u->draft==u->current)snprintf(text,sizeof text,"与现在相同，不会移动");
    else if(u->epub)snprintf(text,sizeof text,"%s %u 节，共 %u 节",u->draft>u->current?"向后":"向前",u->draft>u->current?u->draft-u->current:u->current-u->draft,u->maximum+1);
    else snprintf(text,sizeof text,"%s %u%%，之后可“返回跳转前位置”",u->draft>u->current?"向后":"向前",u->draft>u->current?u->draft-u->current:u->current-u->draft);
    if(s==PN_OK)s=pn_w_text(font,frame,text,PN_UI_MARGIN,BAR_Y+80,620,PN_ALIGN_LEFT);
    // 四个步进键。/ Four step keys.
    char big[4][8];snprintf(big[0],sizeof big[0],"-%u",u->large);strcpy(big[1],"-1");strcpy(big[2],"+1");snprintf(big[3],sizeof big[3],"+%u",u->large);
    if(s==PN_OK)s=pn_font_size(font,36);
    for(int i=0;i<4 && s==PN_OK;i++){
        bool blocked=(i<2 && u->draft==0) || (i>=2 && u->draft>=u->maximum);
        s=pn_w_button(font,frame,big[i],PN_UI_MARGIN+i*(STEP_W+STEP_GAP),STEP_Y,STEP_W,BUTTON_H,blocked?PN_W_DISABLED:0u);
    }
    if(s==PN_OK)s=pn_font_size(font,36);
    if(s==PN_OK)s=pn_w_button(font,frame,"跳转",PN_UI_MARGIN,CONFIRM_Y,620,BUTTON_H,u->draft==u->current?PN_W_DISABLED:PN_W_SELECTED);
    if(s==PN_OK)s=pn_w_button(font,frame,"取消",PN_UI_MARGIN,CANCEL_Y,620,BUTTON_H,0u);
    if(s==PN_OK && u->notice){s=pn_font_size(font,28);if(s==PN_OK)s=pn_w_text(font,frame,u->notice,PN_UI_MARGIN,1100,620,PN_ALIGN_CENTER);}
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
pn_status_t pn_jump_ui_present(pn_jump_ui_t *u,pn_reader_present_fn present,void *ctx){
    if(!u || !u->active || !live(u))return PN_INVALID;
    u->presented=false;pn_status_t status=overlay(u,paint,present,ctx);u->presented=status==PN_OK;return status;
}
pn_status_t pn_jump_ui_open(pn_jump_ui_t *u,pn_reader_app_t *reader,pn_reader_present_fn present,void *ctx){
    if(!u || !reader || !reader->impl)return PN_INVALID;
    pn_txt_progress_t progress;pn_status_t status=pn_reader_app_progress(reader,&progress);if(status!=PN_OK)return status;
    memset(u,0,sizeof *u);u->reader=reader;
    unsigned percent=0;
    if(progress.source_size)percent=progress.source_size>=100?(unsigned)(progress.source_offset/(progress.source_size/100)):(unsigned)(progress.source_offset*100/progress.source_size);
    if(percent>100)percent=100;
    u->current=u->draft=percent;u->maximum=100;u->large=10;u->active=true;
    status=pn_jump_ui_present(u,present,ctx);if(status!=PN_OK)u->active=false;return status;
}
pn_status_t pn_jump_ui_open_epub(pn_jump_ui_t *u,pn_epub_app_t *epub,pn_reader_present_fn present,void *ctx){
    if(!u || !epub || !epub->impl)return PN_INVALID;
    size_t count=0,current=0;pn_status_t status=pn_epub_app_section_info(epub,&count,&current);if(status!=PN_OK)return status;
    if(count<2)return PN_EMPTY;
    memset(u,0,sizeof *u);u->epub=epub;
    u->current=u->draft=(unsigned)current;u->maximum=(unsigned)(count-1);u->large=count>=40?5u:count>=12?3u:2u;u->active=true;
    status=pn_jump_ui_present(u,present,ctx);if(status!=PN_OK)u->active=false;return status;
}
pn_status_t pn_jump_ui_event(pn_jump_ui_t *u,int command,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(!u || !u->active || !live(u))return PN_INVALID;
    if(!u->presented && command!=PN_JUI_RETRY)return PN_BUSY;
    u->notice=NULL;
    if(command==PN_JUI_RETRY)return pn_jump_ui_present(u,present,ctx);
    if(command==PN_JUI_CANCEL){
        // 重画当前页并关闭；重画失败时面板保留，可重试。/ Redraw the current page and close; a failed redraw keeps the panel so it can be retried.
        pn_status_t status=u->epub?pn_epub_app_step(u->epub,PN_APP_OPEN,now,present,ctx):pn_reader_app_step(u->reader,PN_APP_OPEN,now,present,ctx);
        if(status==PN_OK)u->active=false;else{u->notice="没有关闭成功，请重试";(void)pn_jump_ui_present(u,present,ctx);}
        return status;
    }
    if(command==PN_JUI_CONFIRM){
        if(u->draft==u->current){u->notice="与现在相同，不会移动";return pn_jump_ui_present(u,present,ctx);}
        pn_status_t status=u->epub?pn_epub_app_section_jump(u->epub,u->draft,now,present,ctx):pn_reader_app_jump_percent(u->reader,u->draft*100u,now,present,ctx);
        bool confirmed=u->epub?pn_epub_app_last_confirmed(u->epub):pn_reader_app_last_confirmed(u->reader);
        // 已确认显示即关闭；保存失败由阅读层另行提示。/ A confirmed display closes the panel; save failures surface through the reading layer.
        if(confirmed){u->active=false;return status;}
        u->notice="跳转没有完成，请重试";(void)pn_jump_ui_present(u,present,ctx);return status!=PN_OK?status:PN_BUSY;
    }
    if(command>=PN_JUI_STEP && command<PN_JUI_STEP+4){
        unsigned step=(command==PN_JUI_STEP || command==PN_JUI_STEP+3)?u->large:1u;
        if(command<PN_JUI_STEP+2)u->draft=u->draft>=step?u->draft-step:0u;
        else u->draft=u->draft+step>u->maximum?u->maximum:u->draft+step;
        return pn_jump_ui_present(u,present,ctx);
    }
    return PN_INVALID;
}
int pn_jump_ui_hit(const pn_jump_ui_t *u,int x,int y){
    if(!u || !u->active || x<0 || x>=684 || y<0 || y>=1216)return -1;
    if(y<112)return x<240?PN_JUI_CANCEL:-1;
    if(y>=STEP_Y && y<STEP_Y+BUTTON_H){
        int slot=(x-PN_UI_MARGIN)/(STEP_W+STEP_GAP);
        if(x>=PN_UI_MARGIN && slot<4 && (x-PN_UI_MARGIN)%(STEP_W+STEP_GAP)<STEP_W)return PN_JUI_STEP+slot;
        return -1;
    }
    if(x<PN_UI_MARGIN || x>=652)return -1;
    if(y>=CONFIRM_Y && y<CONFIRM_Y+BUTTON_H)return PN_JUI_CONFIRM;
    if(y>=CANCEL_Y && y<CANCEL_Y+BUTTON_H)return PN_JUI_CANCEL;
    return -1;
}
void pn_jump_ui_close(pn_jump_ui_t *u){if(u)memset(u,0,sizeof *u);}
