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
#include <strings.h>
#define TOOL_COUNT 8
#define SHEET_PADDING 25
#define BUTTON_W 148
#define BUTTON_H 96
#define COLUMN_PITCH 162
#define ROW_Y0 936
#define ROW_PITCH 116
static const char *const labels[TOOL_COUNT]={"目录","书签/摘录","跳转","排版","阅读线","自动翻页","刷新","返回"};
static const pn_icon_t icons[TOOL_COUNT]={PN_ICON_TOC,PN_ICON_BOOKMARK,PN_ICON_PROGRESS,PN_ICON_TYPESET,PN_ICON_LIST,PN_ICON_ARROW,PN_ICON_REFRESH,PN_ICON_BACK};
static const pn_reader_tool_t tool_ids[TOOL_COUNT]={PN_TOOL_TOC,PN_TOOL_BOOKMARKS,PN_TOOL_JUMP,PN_TOOL_TYPESET,PN_TOOL_GUIDE,PN_TOOL_AUTO_TURN,PN_TOOL_REFRESH,PN_TOOL_SHELF};
static const uint8_t tool_bits[TOOL_COUNT]={0,1,2,3,6,7,4,5};
pn_status_t pn_reader_status_text(pn_font_t *ui,pn_frame_t *frame){
    if(!ui || !ui->impl || !frame || !frame->pixels)return PN_INVALID;
    pn_w_status_snapshot_t state=pn_w_status_snapshot();char text[32];
    if(state.battery>=0)snprintf(text,sizeof text,"%s   %d%%",*state.clock?state.clock:"--:--",state.battery);
    else snprintf(text,sizeof text,"%s   --%%",*state.clock?state.clock:"--:--");
    int original=ui->pixels;
    pn_status_t s=pn_font_size(ui,20);
    if(s==PN_OK)pn_w_battery_status(frame,620,6,32,18,state.battery,state.charging,PN_UI_MUTED);
    if(s==PN_OK)s=pn_w_text_ex(ui,frame,text,410,24,202,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
    pn_status_t restored=pn_font_size(ui,original);return s==PN_OK?restored:s;
}
pn_status_t pn_reader_status_render(pn_font_t *ui,pn_frame_t *frame,unsigned background){
    if(!ui || !ui->impl)return PN_INVALID;
    pn_status_t s=pn_reading_background_region(background,frame,0,PN_READER_TOP);
    return s==PN_OK?pn_reader_status_text(ui,frame):s;
}
pn_status_t pn_reader_footer_progress_ex(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const char *left,const char *right,int basis,unsigned background,int font_pixels){
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
    int fpx=(font_pixels>=20 && font_pixels<=30)?font_pixels:26;
    pn_status_t status=pn_font_size(ui,fpx);
    int right_width=0;
    if(status==PN_OK)status=pn_w_text_width(ui,right,&right_width);
    int left_room=PN_UI_WIDTH-2*PN_UI_MARGIN-(right_width?right_width+24:0);
    int baseline=PN_READER_FOOTER_Y+52+(fpx-26)/3;
    if(status==PN_OK && *left)status=pn_w_text(ui,frame,left,PN_UI_MARGIN,baseline,left_room,PN_ALIGN_LEFT);
    if(status==PN_OK && *right)status=pn_w_text(ui,frame,right,PN_UI_WIDTH-PN_UI_MARGIN-right_width,baseline,right_width,PN_ALIGN_RIGHT);
    pn_w_set_fallback(NULL);
    pn_status_t restored=pn_font_size(ui,original);return status==PN_OK?restored:status;
}
pn_status_t pn_reader_footer_progress(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const char *left,const char *right,int basis,unsigned background){
    return pn_reader_footer_progress_ex(ui,fallback,frame,left,right,basis,background,26);
}
pn_status_t pn_reader_footer_render(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const char *left,const char *right){
    return pn_reader_footer_progress(ui,fallback,frame,left,right,-1,0);
}
static pn_status_t draw_sbar_slot(pn_font_t *ui,pn_frame_t *frame,uint8_t slot_kind,const pn_reader_bar_info_t *info,const pn_w_status_snapshot_t *st,int x,int w,int baseline,int bar_y,int bar_h,pn_align_t align){
    if(slot_kind==PN_SBAR_SLOT_NONE || w<=4)return PN_OK;
    if(slot_kind==PN_SBAR_SLOT_BATTERY){
        int bw=32,bh=18,by=bar_y+(bar_h-bh)/2;
        int bx=(align==PN_ALIGN_RIGHT)?(x+w-bw):(align==PN_ALIGN_CENTER)?(x+(w-bw)/2):x;
        pn_w_battery_status(frame,bx,by,bw,bh,st->battery,st->charging,PN_UI_MUTED);
        if(w>=bw+44){
            char bp[12];if(st->battery>=0)snprintf(bp,sizeof bp,"%d%%",st->battery);else snprintf(bp,sizeof bp,"--%%");
            if(align==PN_ALIGN_RIGHT)return pn_w_text_ex(ui,frame,bp,x,baseline,bx-x-6,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
            if(align==PN_ALIGN_LEFT)return pn_w_text_ex(ui,frame,bp,bx+bw+6,baseline,x+w-(bx+bw+6),PN_ALIGN_LEFT,PN_UI_MUTED,false);
        }
        return PN_OK;
    }
    char buf[96]="";
    switch(slot_kind){
        case PN_SBAR_SLOT_TIME:snprintf(buf,sizeof buf,"%s",*st->clock?st->clock:"--:--");break;
        case PN_SBAR_SLOT_CHAPTER:snprintf(buf,sizeof buf,"%s",(info->chapter && *info->chapter)?info->chapter:(info->book && *info->book)?info->book:"");break;
        case PN_SBAR_SLOT_BOOK:snprintf(buf,sizeof buf,"%s",(info->book && *info->book)?info->book:"");break;
        case PN_SBAR_SLOT_PROGRESS:if(info->book_basis>=0 && info->book_basis<=10000)snprintf(buf,sizeof buf,"%d%%",info->book_basis/100);break;
        case PN_SBAR_SLOT_SECTION:if(info->section && *info->section)snprintf(buf,sizeof buf,"%s",info->section);break;
        default:return PN_OK;
    }
    if(!buf[0])return PN_OK;
    return pn_w_text_ex(ui,frame,buf,x,baseline,w,align,PN_UI_MUTED,false);
}
static pn_status_t draw_sbar_slots_row(pn_font_t *ui,pn_frame_t *frame,const uint8_t *slots,const pn_reader_bar_info_t *info,const pn_w_status_snapshot_t *st,int baseline,int text_y,int text_h){
    // 5格宽：相邻槽位为空时，中间章节/书名槽自动向两侧扩展，左右槽也可向内侧空槽扩展，避免不必要截断。
    // 5-slot spans: adjacent empty slots let middle/edge text expand cleanly without unnecessary truncation.
    int sx[5]={24,134,234,450,540};
    int sw[5]={104,96,216,86,120};
    if(slots[0]!=PN_SBAR_SLOT_NONE && slots[1]==PN_SBAR_SLOT_NONE && slots[2]==PN_SBAR_SLOT_NONE){sw[0]=360;}
    else if(slots[0]!=PN_SBAR_SLOT_NONE && slots[1]==PN_SBAR_SLOT_NONE){sw[0]=200;}
    if(slots[1]==PN_SBAR_SLOT_NONE && slots[3]==PN_SBAR_SLOT_NONE){sx[2]=140;sw[2]=392;}
    else if(slots[1]==PN_SBAR_SLOT_NONE){sx[2]=140;sw[2]=304;}
    else if(slots[3]==PN_SBAR_SLOT_NONE){sw[2]=304;}
    static const pn_align_t aligns[5]={PN_ALIGN_LEFT,PN_ALIGN_LEFT,PN_ALIGN_CENTER,PN_ALIGN_RIGHT,PN_ALIGN_RIGHT};
    pn_status_t s=PN_OK;
    for(unsigned i=0;i<5 && s==PN_OK;i++){
        if(slots[i]==PN_SBAR_SLOT_NONE)continue;
        s=draw_sbar_slot(ui,frame,slots[i],info,st,sx[i],sw[i],baseline,text_y,text_h,aligns[i]);
    }
    return s;
}
pn_status_t pn_reader_return_bar_render(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const pn_reader_bar_info_t *info,unsigned background){
    if(!ui || !ui->impl || !frame || !frame->pixels || !info)return PN_INVALID;
    int y0=PN_UI_HEIGHT-PN_RETURN_BAR_HEIGHT;
    if(background!=PN_READING_BACKGROUND_EXTERNAL){
        pn_status_t bg=pn_reading_background_region(background,frame,y0,PN_RETURN_BAR_HEIGHT);
        if(bg!=PN_OK)return bg;
    }
    pn_frame_rect(frame,24,y0+2,PN_UI_WIDTH-48,1,11);
    int original=ui->pixels;
    pn_status_t s=pn_font_size(ui,22);
    pn_w_set_fallback(fallback);
    int baseline=y0+38;
    if(s==PN_OK){
        const char *lbl=(info->draft_hint && *info->draft_hint)?info->draft_hint:"< 返回跳转前位置";
        s=pn_w_text_ex(ui,frame,lbl,24,baseline,430,PN_ALIGN_LEFT,PN_UI_INK,true);
    }
    if(s==PN_OK){
        if(info->draft_hint && *info->draft_hint){
            s=pn_w_text_ex(ui,frame,"点击返回设置",460,baseline,200,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
        }else if(info->book_basis>=0 && info->book_basis<=10000){
            char pct[16];snprintf(pct,sizeof pct,"%d%%",info->book_basis/100);
            s=pn_w_text_ex(ui,frame,pct,500,baseline,160,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
        }
    }
    pn_w_set_fallback(NULL);
    pn_status_t r=pn_font_size(ui,original);return s==PN_OK?r:s;
}
pn_status_t pn_reader_custom_bar_render(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const pn_reader_bar_info_t *info,unsigned background){
    if(!ui || !ui->impl || !frame || !frame->pixels || !info)return PN_INVALID;
    pn_status_bar_prefs_t cfg=pn_status_bar_active();
    if(!cfg.enabled)return PN_EMPTY;
    if(cfg.position==PN_SBAR_POS_OFF){
        if(info->show_return || (info->draft_hint && *info->draft_hint)){
            return pn_reader_return_bar_render(ui,fallback,frame,info,background);
        }
        return PN_OK;
    }
    int fpx=cfg.font_size?22:18;
    int text_h=cfg.font_size?36:30;
    int prog_h=(cfg.progress_bar!=PN_SBAR_PROG_OFF)?6:0;
    int original=ui->pixels;
    pn_status_t s=PN_OK;
    pn_w_status_snapshot_t st=pn_w_status_snapshot();
    pn_w_set_fallback(fallback);

    if(cfg.position==PN_SBAR_POS_DUAL){
        // 顶栏（仅 5 格内容，绝不被返回条覆盖）/ Top bar (5 configurable slots, never clobbered by return bar).
        int top_y=(int)cfg.margin_px;
        if(background!=PN_READING_BACKGROUND_EXTERNAL){
            s=pn_reading_background_region(background,frame,0,top_y+text_h+2);
        }
        if(s==PN_OK)s=pn_font_size(ui,fpx);
        int top_base=top_y+text_h-(text_h-fpx)/2-2;
        if(s==PN_OK)s=draw_sbar_slots_row(ui,frame,cfg.slots,info,&st,top_base,top_y,text_h);

        // 底栏（进度条 + 5 格内容，跳转/摘录返回条始终在此处显示）/ Bottom bar (progress + 5 slots; return hint always lives here).
        int bot_total=text_h+prog_h;
        int bot_y0=PN_UI_HEIGHT-(int)cfg.margin_px-bot_total;
        if(s==PN_OK && background!=PN_READING_BACKGROUND_EXTERNAL){
            s=pn_reading_background_region(background,frame,bot_y0,PN_UI_HEIGHT-bot_y0);
        }
        int bot_text_y=prog_h?(bot_y0+prog_h):bot_y0;
        if(s==PN_OK && prog_h && !(info->draft_hint && *info->draft_hint)){
            int b=(cfg.progress_bar==PN_SBAR_PROG_CHAPTER && info->chapter_basis>=0)?info->chapter_basis:info->book_basis;
            int tx=24,tw=PN_UI_WIDTH-48;
            pn_frame_rect(frame,tx,bot_y0+1,tw,2,12);
            if(b>=0 && b<=10000){int fill=tw*b/10000;if(fill>0)pn_frame_rect(frame,tx,bot_y0+1,fill,3,PN_UI_INK);}
        }
        int bot_base=bot_text_y+text_h-(text_h-fpx)/2-2;
        if(s==PN_OK && (info->draft_hint && *info->draft_hint)){
            s=pn_w_text_ex(ui,frame,info->draft_hint,24,bot_base,420,PN_ALIGN_LEFT,PN_UI_INK,false);
            if(s==PN_OK)s=pn_w_text_ex(ui,frame,"点击返回设置",456,bot_base,204,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
        }else if(s==PN_OK && info->show_return){
            s=pn_w_text_ex(ui,frame,"< 返回跳转前位置",24,bot_base,400,PN_ALIGN_LEFT,PN_UI_INK,true);
            uint8_t rslot=cfg.bottom_slots[4]?cfg.bottom_slots[4]:PN_SBAR_SLOT_PROGRESS;
            if(s==PN_OK)s=draw_sbar_slot(ui,frame,rslot,info,&st,480,180,bot_base,bot_text_y,text_h,PN_ALIGN_RIGHT);
        }else if(s==PN_OK){
            s=draw_sbar_slots_row(ui,frame,cfg.bottom_slots,info,&st,bot_base,bot_text_y,text_h);
        }
        pn_w_set_fallback(NULL);
        pn_status_t r=pn_font_size(ui,original);return s==PN_OK?r:s;
    }

    int total_h=text_h+prog_h;
    int y0=(cfg.position==PN_SBAR_POS_TOP)?(int)cfg.margin_px:(PN_UI_HEIGHT-(int)cfg.margin_px-total_h);
    if(background!=PN_READING_BACKGROUND_EXTERNAL){
        pn_status_t bg=pn_reading_background_region(background,frame,cfg.position==PN_SBAR_POS_TOP?0:y0,cfg.position==PN_SBAR_POS_TOP?(y0+total_h+2):(PN_UI_HEIGHT-y0));
        if(bg!=PN_OK){pn_w_set_fallback(NULL);return bg;}
    }
    int text_y=prog_h?(y0+prog_h):y0,prog_y=y0+1;
    // 细进度条（全书/本章）/ Thin progress bar line (whole book or current section).
    if(prog_h && !(info->draft_hint && *info->draft_hint)){
        int b=(cfg.progress_bar==PN_SBAR_PROG_CHAPTER && info->chapter_basis>=0)?info->chapter_basis:info->book_basis;
        int tx=24,tw=PN_UI_WIDTH-48;
        pn_frame_rect(frame,tx,prog_y,tw,2,12);
        if(b>=0 && b<=10000){int fill=tw*b/10000;if(fill>0)pn_frame_rect(frame,tx,prog_y,fill,3,PN_UI_INK);}
    }
    s=pn_font_size(ui,fpx);
    int baseline=text_y+text_h-(text_h-fpx)/2-2;
    if(s==PN_OK && (info->draft_hint && *info->draft_hint)){
        s=pn_w_text_ex(ui,frame,info->draft_hint,24,baseline,420,PN_ALIGN_LEFT,PN_UI_INK,false);
        if(s==PN_OK)s=pn_w_text_ex(ui,frame,"点击返回设置",456,baseline,204,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
    }else if(s==PN_OK && cfg.position==PN_SBAR_POS_BOTTOM && info->show_return){
        s=pn_w_text_ex(ui,frame,"< 返回跳转前位置",24,baseline,400,PN_ALIGN_LEFT,PN_UI_INK,true);
        if(s==PN_OK)s=draw_sbar_slot(ui,frame,cfg.slots[4]?cfg.slots[4]:PN_SBAR_SLOT_BATTERY,info,&st,480,180,baseline,text_y,text_h,PN_ALIGN_RIGHT);
    }else if(s==PN_OK){
        s=draw_sbar_slots_row(ui,frame,cfg.slots,info,&st,baseline,text_y,text_h);
    }
    pn_w_set_fallback(NULL);
    pn_status_t r=pn_font_size(ui,original);
    if(s!=PN_OK)return s;
    if(r!=PN_OK)return r;
    // 顶部单栏模式下，返回跳转前位置仍放在屏幕下侧预留带，顺手且绝不覆盖正文。
    // In top-only bar mode, render "< 返回跳转前位置" in the reserved bottom strip for thumb reach without overlapping prose.
    if(cfg.position==PN_SBAR_POS_TOP && info->show_return && !(info->draft_hint && *info->draft_hint)){
        return pn_reader_return_bar_render(ui,fallback,frame,info,background);
    }
    return PN_OK;
}
pn_status_t pn_reader_custom_bar_refresh(pn_font_t *ui,pn_font_t *fallback,pn_frame_t *frame,const pn_reader_bar_info_t *info,unsigned background){
    return pn_reader_custom_bar_render(ui,fallback,frame,info,background);
}
int pn_reader_bar_hit(int x,int y,bool hide_bars,bool can_return){
    if(x<0 || x>=PN_UI_WIDTH || y<0 || y>=PN_UI_HEIGHT)return -1;
    int top_in=0,bot_in=0;pn_status_bar_insets(hide_bars,&top_in,&bot_in);
    // 所有布局模式下，「< 返回跳转前位置」均固定位于屏幕底部左侧。
    // Across every status-bar mode, "< 返回跳转前位置" is anchored at bottom-left for comfortable thumb reach.
    if(can_return && bot_in>0 && y>=PN_UI_HEIGHT-bot_in-8 && x<440)return 11;
    if(hide_bars)return -1;
    pn_status_bar_prefs_t cfg=pn_status_bar_active();
    if(!cfg.enabled){
        return y>=PN_READER_FOOTER_Y?14:-1;
    }
    if(cfg.position==PN_SBAR_POS_OFF)return -1;
    if(cfg.position==PN_SBAR_POS_TOP && y<top_in+8)return 14;
    if((cfg.position==PN_SBAR_POS_BOTTOM || cfg.position==PN_SBAR_POS_DUAL) && y>=PN_UI_HEIGHT-bot_in-8)return 14;
    return -1;
}
pn_status_t pn_reader_toolbar_top_render(pn_font_t *ui,pn_frame_t *frame,const pn_toolbar_top_t *top){
    if(!ui || !ui->impl || !frame || !frame->pixels || !top || frame->width!=PN_UI_WIDTH || frame->height!=PN_UI_HEIGHT)return PN_INVALID;
    int original=ui->pixels;
    // 顶部信息条：白底＋底边轻影细线，即使在“去上下栏”模式下也能查看时间、电量、书名与进度，并一键加书签。
    // Top info strip: white sheet with subtle bottom hairline so clock, battery, title, progress and quick bookmark stay accessible even in bar-less mode.
    pn_frame_rect(frame,0,0,PN_UI_WIDTH,PN_TOOLBAR_TOP_H,PN_UI_PAPER);
    pn_frame_rect(frame,0,PN_TOOLBAR_TOP_H-2,PN_UI_WIDTH,1,9);
    pn_frame_rect(frame,0,PN_TOOLBAR_TOP_H-1,PN_UI_WIDTH,3,PN_UI_SELECT);
    pn_status_t s=pn_w_status(ui,frame);
    int right_limit=418;
    if(s==PN_OK && top->basis>=0 && top->basis<=10000){
        char pct[16];snprintf(pct,sizeof pct,"%d%%",top->basis/100);
        s=pn_font_size(ui,22);
        if(s==PN_OK)s=pn_w_text_ex(ui,frame,pct,right_limit-64,84,64,PN_ALIGN_RIGHT,5,true);
        right_limit-=72;
    }
    if(s==PN_OK){
        const char *title=(top->title && *top->title)?top->title:"正在阅读";
        s=pn_font_size(ui,26);
        if(s==PN_OK)s=pn_w_text_ex(ui,frame,title,PN_UI_MARGIN,86,right_limit-PN_UI_MARGIN,PN_ALIGN_LEFT,PN_UI_INK,true);
    }
    if(s==PN_OK){
        // 顶栏离线查词入口，紧邻一键书签按钮。/ Top-bar offline dictionary lookup pill beside the quick bookmark button.
        int dx=430,dy=48,dw=100,dh=50;
        pn_w_round_fill(frame,dx,dy,dw,dh,14,PN_UI_SURFACE);
        pn_w_round_stroke(frame,dx,dy,dw,dh,14,1.5f,9);
        pn_w_icon(frame,PN_ICON_SEARCH,dx+12,dy+12,24,PN_UI_INK);
        s=pn_font_size(ui,22);
        if(s==PN_OK)s=pn_w_text_ex(ui,frame,"查词",dx+36,dy+34,58,PN_ALIGN_CENTER,PN_UI_INK,false);
    }
    if(s==PN_OK && top->quick_bookmark){
        int bx=540,by=48,bw=112,bh=50;
        if(top->bookmarked){
            pn_w_round_fill(frame,bx,by,bw,bh,14,PN_UI_INK);
            pn_w_icon(frame,PN_ICON_BOOKMARK,bx+14,by+12,24,PN_UI_PAPER);
            s=pn_font_size(ui,22);
            if(s==PN_OK)s=pn_w_text_ex(ui,frame,"已签",bx+38,by+34,64,PN_ALIGN_CENTER,PN_UI_PAPER,true);
        }else{
            pn_w_round_fill(frame,bx,by,bw,bh,14,PN_UI_SURFACE);
            pn_w_round_stroke(frame,bx,by,bw,bh,14,1.5f,9);
            pn_w_icon(frame,PN_ICON_BOOKMARK,bx+14,by+12,24,PN_UI_INK);
            s=pn_font_size(ui,22);
            if(s==PN_OK)s=pn_w_text_ex(ui,frame,"书签",bx+38,by+34,64,PN_ALIGN_CENTER,PN_UI_INK,false);
        }
    }
    pn_status_t restored=pn_font_size(ui,original);return s==PN_OK?restored:s;
}
pn_status_t pn_reader_toolbar_render(pn_font_t *ui,pn_frame_t *frame,unsigned unavailable){
    if(!ui || !ui->impl || !frame || !frame->pixels || frame->width!=PN_UI_WIDTH || frame->height!=PN_UI_HEIGHT)return PN_INVALID;
    int original=ui->pixels;
    // 底部面板：柔和圆角顶边、细描边与抓手；每个入口采用柔和浅雾底块，减少墨水屏残影与厚重边框感。
    // Bottom sheet: soft rounded top edge, hairline and grab handle; soft fog tiles reduce ghosting and visual heaviness.
    pn_w_round_fill(frame,-2,PN_TOOLBAR_Y-4,PN_UI_WIDTH+4,PN_UI_HEIGHT-PN_TOOLBAR_Y+44,PN_UI_CHIP_RADIUS,PN_UI_SELECT);
    pn_w_round_fill(frame,0,PN_TOOLBAR_Y,PN_UI_WIDTH,PN_UI_HEIGHT-PN_TOOLBAR_Y+40,PN_UI_CHIP_RADIUS,PN_UI_PAPER);
    pn_w_round_stroke(frame,-2,PN_TOOLBAR_Y,PN_UI_WIDTH+4,PN_UI_HEIGHT-PN_TOOLBAR_Y+40,PN_UI_CHIP_RADIUS,1.5f,9);
    pn_w_round_fill(frame,(PN_UI_WIDTH-56)/2,PN_TOOLBAR_Y+14,56,5,3,10);
    pn_status_t status=pn_font_size(ui,23);
    for(unsigned i=0;i<TOOL_COUNT && status==PN_OK;i++){
        int x=SHEET_PADDING+(int)(i%4)*COLUMN_PITCH,y=ROW_Y0+(int)(i/4)*ROW_PITCH;bool off=(unavailable&(1u<<tool_bits[i]))!=0;
        pn_w_round_fill(frame,x,y,BUTTON_W,BUTTON_H,14,off?PN_UI_PAPER:PN_UI_SURFACE);
        pn_w_round_stroke(frame,x,y,BUTTON_W,BUTTON_H,14,1.5f,off?PN_UI_SELECT:10);
        pn_w_icon(frame,icons[i],x+(BUTTON_W-32)/2,y+12,32,off?PN_UI_DISABLED:PN_UI_INK);
        status=pn_w_text_ex(ui,frame,labels[i],x+4,y+BUTTON_H-13,BUTTON_W-8,PN_ALIGN_CENTER,off?PN_UI_DISABLED:PN_UI_INK,false);
    }
    if(status==PN_OK){status=pn_font_size(ui,22);if(status==PN_OK)status=pn_w_text_ex(ui,frame,"点击正文空白处或按中键关闭",PN_UI_MARGIN,PN_UI_HEIGHT-16,PN_UI_WIDTH-2*PN_UI_MARGIN,PN_ALIGN_CENTER,5,false);}
    pn_status_t restored=pn_font_size(ui,original);return status==PN_OK?restored:status;
}
int pn_reader_toolbar_hit(int x,int y,unsigned unavailable){
    if(x<0 || x>=PN_UI_WIDTH || y<0 || y>=PN_UI_HEIGHT)return -1;
    if(y>=40 && y<PN_TOOLBAR_TOP_H && x>=532 && x<664)return PN_TOOL_QUICK_BOOKMARK;
    if(y>=40 && y<PN_TOOLBAR_TOP_H && x>=424 && x<532)return PN_TOOL_DICT;
    if(y<PN_TOOLBAR_Y)return PN_TOOL_CLOSE;
    for(unsigned i=0;i<TOOL_COUNT;i++){
        int bx=SHEET_PADDING+(int)(i%4)*COLUMN_PITCH,by=ROW_Y0+(int)(i/4)*ROW_PITCH;
        if(x>=bx && x<bx+BUTTON_W && y>=by && y<by+BUTTON_H)return (unavailable&(1u<<tool_bits[i]))?-1:(int)tool_ids[i];
    }
    return -1;
}
bool pn_reader_loading_needed(bool text_format,uint64_t source_bytes){return !text_format || source_bytes>65536u;}
pn_status_t pn_reader_jump_loading_render(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    (void)ctx;(void)metadata;pn_frame_clear(frame,PN_UI_PAPER);
    pn_status_t s=pn_w_header(font,frame,"","阅读",NULL);
    return s==PN_OK?pn_w_loading(font,frame,"正在跳转书页","正在准备目标页面，请稍候",-1,-1):s;
}
void pn_reader_guide_row_custom(pn_frame_t *frame,unsigned style,unsigned shade,int x0,int x1,int y,int thickness,int dash_len,int dash_gap,int font_pixels,bool follow_font){
    if(!frame || !frame->pixels || style<1 || style>3 || frame->width<1 || frame->height<1)return;
    if(thickness<1)thickness=1;
    if(thickness>3)thickness=3;
    if(x0<0)x0=0;
    if(x1>frame->width)x1=frame->width;
    if(x1<=x0)return;
    int y0=y,h=thickness;
    if(y0<0){h+=y0;y0=0;}
    if(y0+h>frame->height)h=frame->height-y0;
    if(h<=0)return;
    // 浅档保留原灰阶10；越界深浅按浅处理。/ The light level keeps the original gray 10; out-of-range shades fall back to light.
    static const uint8_t inks[4]={10,7,4,0};uint8_t ink=inks[shade<4?shade:0];
    if(style==3u){pn_frame_rect(frame,x0,y0,x1-x0,h,ink);return;}
    int dash=style==1?19:2,period=style==1?31:13;
    if(follow_font){
        int px=(font_pixels>=20 && font_pixels<=96)?font_pixels:44;
        if(style==1){
            dash=px*19/44;if(dash<10)dash=10;if(dash>32)dash=32;
            period=px*31/44;if(period<16)period=16;if(period>52)period=52;
        }else{
            dash=thickness+1;if(dash>4)dash=4;
            period=px*13/44;if(period<8)period=8;if(period>22)period=22;
        }
    }
    if(style==1u && dash_len>=4 && dash_len<=40){
        int cur_gap=period>dash?period-dash:8;
        dash=dash_len;period=dash+cur_gap;
    }
    if(dash_gap>=4 && dash_gap<=40){
        period=dash+dash_gap;
    }
    if(period<=dash)period=dash+4;
    for(int x=x0;x<x1;x+=period){int width=x1-x;if(width>dash)width=dash;pn_frame_rect(frame,x,y0,width,h,ink);}
}
void pn_reader_guide_row_ex(pn_frame_t *frame,unsigned style,unsigned shade,int x0,int x1,int y,int thickness,int font_pixels,bool follow_font){
    pn_reader_guide_row_custom(frame,style,shade,x0,x1,y,thickness,0,0,font_pixels,follow_font);
}
void pn_reader_guide_row(pn_frame_t *frame,unsigned style,unsigned shade,int y){
    if(!frame)return;
    pn_reader_guide_row_ex(frame,style,shade,0,frame->width,y,1,44,false);
}
void pn_reader_highlight_span(pn_frame_t *viewport,int x0,int x1,int baseline,int pixels,bool has_bg){
    if(!viewport || !viewport->pixels || pixels<=0)return;
    if(x0<0)x0=0;
    if(x1>viewport->width)x1=viewport->width;
    int w=x1-x0;
    if(w<=0)return;
    int top=baseline-pixels*88/100;
    int h=pixels+pixels/8;
    if(has_bg){
        // 有背景图时相对压暗现有像素，保留纸纹/竹影纹理的同时清晰呈现高亮底色。
        // Darken existing pixels relative to the background so paper/bamboo texture stays visible inside the highlight band.
        int y0=top<0?0:top;
        int y1=top+h>viewport->height?viewport->height:top+h;
        for(int y=y0;y<y1;y++){
            for(int x=x0;x<x1;x++){
                uint8_t px=pn_frame_get(viewport,x,y);
                uint8_t next=px>7u?(uint8_t)(px-3u):(px>4u?4u:px);
                if(next!=px)pn_frame_pixel(viewport,x,y,next);
            }
        }
        pn_frame_rect(viewport,x0,baseline+pixels/6,w,2,4);
    }else{
        pn_frame_rect(viewport,x0,top,w,h,13);
        pn_frame_rect(viewport,x0,baseline+pixels/6,w,2,5);
    }
}
static void lp_menu_box(const pn_lp_menu_t *m,int *bx,int *by,int *bw,int *bh){
    int w=m && m->highlighted?344:312,h=76;
    int x=(m?m->anchor_x:342)-w/2;
    if(x<24)x=24;
    if(x+w>684-24)x=684-24-w;
    int ay=m?m->anchor_y:608;
    // 默认弹在手指上方避让点中句子；靠近顶部时自动弹在下方。/ Pop above the finger by default; flip below near the top edge.
    int y=ay-116;
    if(y<64)y=ay+44;
    if(y+h>1216-64)y=1216-64-h;
    *bx=x;*by=y;*bw=w;*bh=h;
}
pn_status_t pn_lp_menu_render(const pn_lp_menu_t *menu,pn_font_t *ui,pn_frame_t *frame){
    if(!menu || !menu->active || !ui || !ui->impl || !frame || !frame->pixels)return PN_INVALID;
    int x,y,w,h;lp_menu_box(menu,&x,&y,&w,&h);
    int left_w=menu->highlighted?192:156,right_w=w-left_w;
    int original=ui->pixels;
    // 柔和外阴影 + 白底深描边圆角胶囊，低面积刷新几乎无残影。/ Soft drop shadow + crisp white capsule for low-ghosting local refresh.
    pn_w_round_fill(frame,x+4,y+4,w,h,18,11);
    pn_w_round_fill(frame,x,y,w,h,18,PN_UI_PAPER);
    pn_w_round_stroke(frame,x,y,w,h,18,2.0f,PN_UI_INK);
    pn_frame_rect(frame,x+left_w,y+16,1,h-32,10);
    pn_w_icon(frame,PN_ICON_LIST,x+20,y+25,26,PN_UI_INK);
    pn_status_t s=pn_font_size(ui,26);
    if(s==PN_OK)s=pn_w_text_ex(ui,frame,menu->highlighted?"取消摘录":"摘录",x+50,y+48,left_w-58,PN_ALIGN_CENTER,PN_UI_INK,true);
    pn_w_icon(frame,PN_ICON_SEARCH,x+left_w+20,y+24,28,PN_UI_INK);
    if(s==PN_OK)s=pn_w_text_ex(ui,frame,"查词",x+left_w+52,y+48,right_w-60,PN_ALIGN_CENTER,PN_UI_INK,true);
    pn_status_t restored=pn_font_size(ui,original);
    return s==PN_OK?restored:s;
}
int pn_lp_menu_hit(const pn_lp_menu_t *menu,int x,int y){
    if(!menu || !menu->active || x<0 || x>=684 || y<0 || y>=1216)return -1;
    int bx,by,bw,bh;lp_menu_box(menu,&bx,&by,&bw,&bh);
    int left_w=menu->highlighted?192:156;
    if(x>=bx && x<bx+bw && y>=by && y<by+bh){
        return x<bx+left_w?PN_LPM_EXCERPT:PN_LPM_DICT;
    }
    return PN_LPM_DISMISS;
}
pn_status_t pn_reader_loading_render(pn_font_t *ui,pn_frame_t *frame,const char *title){
    return pn_reader_loading_render_phase(ui,frame,title,0);
}
pn_status_t pn_reader_loading_render_phase(pn_font_t *ui,pn_frame_t *frame,const char *title,unsigned phase){
    (void)phase;return pn_reader_opening_render(ui,frame,title,NULL,NULL);
}
/// 仅裁剪显示副本，UTF8字符不截半；原文件名/书籍元数据不变。/ Trim display copies only, preserving UTF-8 boundaries and original names/metadata.
static void opening_title(const char *source,char out[512],bool filename){
    size_t n=strlen(source);if(filename){const char *dot=strrchr(source,'.');if(dot && (!strcasecmp(dot,".epub") || !strcasecmp(dot,".txt")))n=(size_t)(dot-source);}
    bool cut=n>=512;if(cut){n=508;while(n && ((unsigned char)source[n]&0xc0)==0x80)n--;}
    if(n){size_t start=n-1;while(start && ((unsigned char)source[start]&0xc0)==0x80)start--;
        unsigned lead=(unsigned char)source[start],bytes=lead>=0xf0?4:lead>=0xe0?3:lead>=0xc0?2:1;
        if(start+bytes>n){n=start;cut=true;}}
    if(cut && n>508){n=508;while(n && ((unsigned char)source[n]&0xc0)==0x80)n--;}
    if(!n){snprintf(out,512,"未命名书籍");return;}memcpy(out,source,n);if(cut){memcpy(out+n,"...",3);n+=3;}out[n]=0;
}
pn_status_t pn_reader_opening_render(pn_font_t *ui,pn_frame_t *frame,const char *title,const pn_book_preview_t *preview,const char *note){
    if(!ui || !ui->impl || !frame || !frame->pixels || !title)return PN_INVALID;
    int original=ui->pixels;pn_frame_clear(frame,PN_UI_PAPER);
    char name[512];bool metadata=preview && *preview->display.title;opening_title(metadata?preview->display.title:title,name,!metadata);
    const char *author=preview && *preview->display.author?preview->display.author:NULL;
    pn_status_t status=pn_font_size(ui,36);
#define OPEN_DRAW(expr) do{if(status==PN_OK)status=(expr);}while(0)
    int title_width=0;OPEN_DRAW(pn_w_text_width_ex(ui,name,true,&title_width));
    if(status==PN_OK && ((title_width>556 && title_width<=620) || title_width>1112))OPEN_DRAW(pn_font_size(ui,32));
    unsigned lines=1;OPEN_DRAW(pn_w_text_lines_measure_ex(ui,name,556,3,true,&lines));if(!lines)lines=1;
    int cover_y=(frame->height-(PN_COVER_HEIGHT+12+56+(int)(lines-1)*48+(author?98:54)))/2;
    int cover_x=(frame->width-PN_COVER_WIDTH)/2;pn_frame_t cover={0};
    if(preview && preview->ready)(void)pn_frame_bind(&cover,(uint8_t *)preview->pixels,PN_COVER_BYTES,PN_COVER_WIDTH,PN_COVER_HEIGHT);
    else OPEN_DRAW(pn_builtin_cover_frame(name,&cover));
    // 拟物厚度只画在封面外，真实缩略图像素保持；不用贴图或额外画布。/ Draw book depth outside the cover, preserving thumbnail pixels without textures or another canvas.
    pn_w_round_fill(frame,cover_x-2,cover_y+7,PN_COVER_WIDTH+12,PN_COVER_HEIGHT+5,3,12);
    pn_frame_rect(frame,cover_x+2,cover_y+PN_COVER_HEIGHT,PN_COVER_WIDTH+4,6,14);
    pn_frame_rect(frame,cover_x+PN_COVER_WIDTH,cover_y+3,6,PN_COVER_HEIGHT-1,14);
    pn_frame_rect(frame,cover_x+PN_COVER_WIDTH+3,cover_y+6,1,PN_COVER_HEIGHT-6,10);
    pn_frame_rect(frame,cover_x+5,cover_y+PN_COVER_HEIGHT+3,PN_COVER_WIDTH-2,1,10);
    pn_frame_rect(frame,cover_x-4,cover_y,3,PN_COVER_HEIGHT,6);
    pn_w_outline(frame,cover_x-1,cover_y-1,PN_COVER_WIDTH+2,PN_COVER_HEIGHT+2,1,9);
    // 缺图沿用书库内置封面；真实缩略图原样拷贝，不新增解码或图片。/ Missing art uses library built-ins; real thumbnails copy exactly, without another decoder or image.
    if(status==PN_OK)for(int y=0;y<PN_COVER_HEIGHT;y++)for(int x=0;x<PN_COVER_WIDTH;x++)pn_frame_pixel(frame,cover_x+x,cover_y+y,pn_frame_get(&cover,x*cover.width/PN_COVER_WIDTH,y*cover.height/PN_COVER_HEIGHT));
    int title_y=cover_y+PN_COVER_HEIGHT+12+56;unsigned used=0;
    OPEN_DRAW(pn_w_text_lines_centered_ex(ui,frame,name,64,title_y,556,3,48,0,true,&used));
    int last_title=title_y+(int)(used?used-1:0)*48;
    if(author){OPEN_DRAW(pn_font_size(ui,24));OPEN_DRAW(pn_w_text_ex(ui,frame,author,64,last_title+44,556,PN_ALIGN_CENTER,5,false));}
    OPEN_DRAW(pn_font_size(ui,26));OPEN_DRAW(pn_w_text_ex(ui,frame,note?note:"正在打开书籍",64,last_title+(author?98:54),556,PN_ALIGN_CENTER,4,false));
#undef OPEN_DRAW
    pn_status_t restored=pn_font_size(ui,original);return status==PN_OK?restored:status;
}
