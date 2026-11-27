/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享可视化状态栏设置页实现（纯绘制、坐标命中与状态流转）。
 * English: Shared visual status-bar settings UI implementation (pure drawing, hit testing, and state transitions).
 */
#include "pn_status_bar_ui.h"
#include "pn_widgets.h"
#include <stdio.h>

#define SBAR_W PN_UI_WIDTH
#define SBAR_H PN_UI_HEIGHT

static const char *const sbar_slot_names[PN_SBAR_SLOT_COUNT]={"空","时间","章节","书名","进度","节号","电量"};
static const char *const sbar_slot_samples[PN_SBAR_SLOT_COUNT]={"+","12:00","章节","书名","42%","3/20","76%"};

static void draw_dashed_box(pn_frame_t *f,int x,int y,int w,int h,uint8_t ink){
    for(int px=x;px<x+w;px+=8){int sw=(x+w-px>4)?4:(x+w-px);pn_frame_rect(f,px,y,sw,1,ink);pn_frame_rect(f,px,y+h-1,sw,1,ink);}
    for(int py=y;py<y+h;py+=8){int sh=(y+h-py>4)?4:(y+h-py);pn_frame_rect(f,x,py,1,sh,ink);pn_frame_rect(f,x+w-1,py,1,sh,ink);}
}

const char *pn_status_bar_ui_summary(const pn_status_bar_prefs_t *sb,bool book_hide_bars){
    if(book_hide_bars)return "本书隐藏";
    if(!sb || !sb->enabled)return "默认双栏";
    switch(sb->position){
        case PN_SBAR_POS_DUAL:return "自定义双栏";
        case PN_SBAR_POS_TOP:return "顶部单栏";
        case PN_SBAR_POS_BOTTOM:return "底部单栏";
        case PN_SBAR_POS_OFF:return "已关闭";
        default:return "默认双栏";
    }
}

pn_status_t pn_status_bar_ui_picker_modal(pn_font_t *font,pn_frame_t *f,const char *title,const char *subtitle,
    const char *const *items,unsigned count,int current){
    if(!font || !font->impl || !f || !f->pixels || !title || !items || !count)return PN_INVALID;
    const int row_h=64;
    const int mh=108+(int)count*row_h+12;
    const int mx=64,mw=556,my=(SBAR_H-mh)/2;
    pn_w_round_fill(f,mx+5,my+6,mw,mh,14,12);
    pn_w_round_fill(f,mx,my,mw,mh,14,PN_UI_PAPER);
    pn_w_round_stroke(f,mx,my,mw,mh,14,2.0f,PN_UI_INK);
    int orig=font->pixels;
    pn_status_t s=pn_font_size(font,30);
    if(s==PN_OK)s=pn_w_text_ex(font,f,title,mx+28,my+46,mw-56,PN_ALIGN_LEFT,PN_UI_INK,true);
    if(s==PN_OK && subtitle && *subtitle){
        s=pn_font_size(font,22);
        if(s==PN_OK)s=pn_w_text_ex(font,f,subtitle,mx+28,my+82,mw-56,PN_ALIGN_LEFT,5,false);
    }
    pn_frame_rect(f,mx+20,my+98,mw-40,2,8);
    if(s==PN_OK)s=pn_font_size(font,28);
    for(unsigned i=0;i<count && s==PN_OK;i++){
        int ry=my+102+(int)i*row_h;
        s=pn_w_text_ex(font,f,items[i],mx+28,ry+42,mw-96,PN_ALIGN_LEFT,PN_UI_INK,(int)i==current);
        if((int)i==current)pn_w_icon(f,PN_ICON_CHECK,mx+mw-54,ry+18,28,PN_UI_INK);
        if(i+1<count)pn_frame_rect(f,mx+24,ry+row_h-1,mw-48,1,12);
    }
    pn_status_t r=pn_font_size(font,orig);return s==PN_OK?r:s;
}

int pn_status_bar_ui_picker_hit(unsigned count,int x,int y,int base_cmd,int close_cmd){
    const int row_h=64;
    const int mh=108+(int)count*row_h+12;
    const int mx=64,mw=556,my=(SBAR_H-mh)/2;
    if(x<mx || x>=mx+mw || y<my || y>=my+mh)return close_cmd;
    if(y>=my+102 && y<my+102+(int)count*row_h)return base_cmd+(y-(my+102))/row_h;
    return -1;
}

static pn_status_t draw_sbar_preview_row(pn_font_t *font,pn_frame_t *f,const uint8_t *slots,int bar_y,int bar_h,int pick_offset,int active_pick,bool font_med){
    const int sx0=32+18,sw=112,sgap=6;
    pn_status_t s=pn_font_size(font,font_med?22:20);
    for(unsigned i=0;i<PN_SBAR_SLOTS && s==PN_OK;i++){
        int bx=sx0+(int)i*(sw+sgap);
        bool picked=(active_pick==pick_offset+(int)i);
        if(picked)pn_frame_rect(f,bx,bar_y,sw,bar_h,PN_UI_SURFACE);
        draw_dashed_box(f,bx,bar_y,sw,bar_h,picked?PN_UI_INK:6);
        uint8_t kind=slots[i]<PN_SBAR_SLOT_COUNT?slots[i]:0;
        if(kind==PN_SBAR_SLOT_BATTERY){
            pn_w_battery_status(f,bx+38,bar_y+(bar_h-18)/2,34,18,76,false,PN_UI_INK);
        }else{
            s=pn_w_text_ex(font,f,sbar_slot_samples[kind],bx+4,bar_y+bar_h/2+8,sw-8,PN_ALIGN_CENTER,kind?PN_UI_INK:6,kind!=0);
        }
    }
    return s;
}

pn_status_t pn_status_bar_ui_render(pn_font_t *font,pn_frame_t *f,const pn_status_bar_prefs_t *sb,
    int pick_slot,pn_sbar_ui_scope_t scope,const char *back_label,const char *notice){
    if(!font || !font->impl || !f || !f->pixels || !sb)return PN_INVALID;
    uint8_t eff_pos=sb->enabled?sb->position:(uint8_t)PN_SBAR_POS_DUAL;
    pn_status_t s=pn_w_header(font,f,(back_label && *back_label)?back_label:"< 返回","状态栏设置",NULL);
    int orig=font->pixels;
    if(scope==PN_SBAR_SCOPE_GLOBAL){
        const char *mode_sub=!sb->enabled?"全局默认 · 默认双栏（未独立设置的书籍自动生效）"
            :eff_pos==PN_SBAR_POS_DUAL?"全局默认 · 自定义上下双栏（未独立设置的书籍生效）"
            :eff_pos==PN_SBAR_POS_TOP?"全局默认 · 顶部单栏（未独立设置的书籍生效）"
            :eff_pos==PN_SBAR_POS_BOTTOM?"全局默认 · 底部单栏（未独立设置的书籍生效）"
            :"全局默认 · 关闭状态栏（未独立设置的书籍生效）";
        if(s==PN_OK){s=pn_font_size(font,21);if(s==PN_OK)s=pn_w_text_ex(font,f,mode_sub,32,148,620,PN_ALIGN_CENTER,sb->enabled?PN_UI_INK:5,sb->enabled!=0);}
    }else{
        bool indep=(scope==PN_SBAR_SCOPE_BOOK_CUSTOM);
        pn_w_round_fill(f,32,124,118,32,8,indep?PN_UI_INK:PN_UI_SURFACE);
        pn_w_round_stroke(f,32,124,118,32,8,1.5f,indep?PN_UI_INK:7);
        if(s==PN_OK){s=pn_font_size(font,20);if(s==PN_OK)s=pn_w_text_ex(font,f,indep?"本书独立":"跟随全局",32,147,118,PN_ALIGN_CENTER,indep?PN_UI_PAPER:PN_UI_INK,true);}
        const char *scope_note=indep?"仅对本书生效 · 不影响全局与其他书籍":"使用全局设置 · 修改任一项转为本书独立";
        if(s==PN_OK){s=pn_font_size(font,21);if(s==PN_OK)s=pn_w_text_ex(font,f,scope_note,162,148,490,PN_ALIGN_LEFT,indep?PN_UI_INK:5,indep);}
    }
    // 顶部可视化阅读页预览框（x=32..652, y=166..412）/ Top visual reading page preview card.
    const int px=32,py=166,pw=620,ph=246;
    pn_w_round_fill(f,px,py,pw,ph,14,PN_UI_PAPER);
    pn_w_round_stroke(f,px,py,pw,ph,14,2.0f,PN_UI_INK);
    bool bar_dual=(eff_pos==PN_SBAR_POS_DUAL);
    bool bar_top=(eff_pos==PN_SBAR_POS_TOP);
    bool bar_bot=(eff_pos==PN_SBAR_POS_BOTTOM);
    bool bar_off=(eff_pos==PN_SBAR_POS_OFF);
    int text_y0=bar_dual?(py+74):bar_top?(py+88):(py+24);
    int text_lines=bar_off?7:bar_dual?3:5;
    for(int li=0;li<text_lines;li++){
        int lw=(li==text_lines-1)?340:564;
        pn_frame_rect(f,px+28,text_y0+li*28,lw,10,12);
    }
    if(bar_dual){
        if(s==PN_OK)s=draw_sbar_preview_row(font,f,sb->slots,py+12,44,0,pick_slot,sb->font_size!=0);
        if(sb->progress_bar!=PN_SBAR_PROG_OFF){
            int py_line=py+ph-64;
            pn_frame_rect(f,px+20,py_line,pw-40,3,12);
            pn_frame_rect(f,px+20,py_line,(sb->progress_bar==PN_SBAR_PROG_BOOK)?320:190,4,4);
        }
        if(s==PN_OK)s=draw_sbar_preview_row(font,f,sb->bottom_slots,py+ph-56,44,5,pick_slot,sb->font_size!=0);
    }else if(bar_top || bar_bot){
        int bar_y=bar_top?(py+22):(py+ph-58);
        if(sb->progress_bar!=PN_SBAR_PROG_OFF){
            int py_line=bar_top?(py+12):(py+ph-66);
            pn_frame_rect(f,px+20,py_line,pw-40,3,12);
            pn_frame_rect(f,px+20,py_line,(sb->progress_bar==PN_SBAR_PROG_BOOK)?320:190,4,4);
        }
        if(s==PN_OK)s=draw_sbar_preview_row(font,f,sb->slots,bar_y,46,0,pick_slot,sb->font_size!=0);
    }else if(s==PN_OK){
        s=pn_font_size(font,22);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"状态栏已关闭（纯全屏正文）",px+20,py+ph-24,pw-40,PN_ALIGN_CENTER,5,false);
    }
    const char *hint=bar_dual?"点上方或下方虚线格子，分别配置上下栏内容":bar_off?"可点下方「布局」重新开启双栏或单栏":"点状态栏上的虚线格子，换显示内容";
    if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,f,hint,32,444,620,PN_ALIGN_CENTER,5,false);}

    // 行1：布局 [双栏 | 上方 | 下方 | 关闭] / Row 1: Layout mode (4-way explicit).
    int ry=476;
    if(s==PN_OK){s=pn_font_size(font,28);if(s==PN_OK)s=pn_w_text_ex(font,f,"布局",32,ry+42,160,PN_ALIGN_LEFT,PN_UI_INK,false);}
    {
        static const char *const pos_l[4]={"双栏","上方","下方","关闭"};
        static const uint8_t pos_v[4]={PN_SBAR_POS_DUAL,PN_SBAR_POS_TOP,PN_SBAR_POS_BOTTOM,PN_SBAR_POS_OFF};
        pn_w_round_stroke(f,212,ry,440,64,12,1.5f,PN_UI_INK);
        if(s==PN_OK)s=pn_font_size(font,24);
        for(int i=0;i<4 && s==PN_OK;i++){
            int cx=212+i*110;bool sel=(eff_pos==pos_v[i]);
            if(sel)pn_w_round_fill(f,cx+3,ry+3,104,58,9,PN_UI_INK);
            s=pn_w_text_ex(font,f,pos_l[i],cx+4,ry+41,102,PN_ALIGN_CENTER,sel?PN_UI_PAPER:PN_UI_INK,sel);
            if(i<3 && !sel && eff_pos!=pos_v[i+1])pn_frame_rect(f,cx+110,ry+14,1,36,11);
        }
    }
    pn_frame_rect(f,32,ry+80,620,1,12);

    // 行2：字号 [小 | 中] / Row 2: Font size.
    ry=572;
    if(s==PN_OK){s=pn_font_size(font,28);if(s==PN_OK)s=pn_w_text_ex(font,f,"字号",32,ry+42,200,PN_ALIGN_LEFT,PN_UI_INK,false);}
    {
        static const char *const sz_l[2]={"小","中"};
        pn_w_round_stroke(f,452,ry,200,64,12,1.5f,PN_UI_INK);
        if(s==PN_OK)s=pn_font_size(font,24);
        for(int i=0;i<2 && s==PN_OK;i++){
            int cx=452+i*100;bool sel=(sb->font_size==(uint8_t)i);
            if(sel)pn_w_round_fill(f,cx+3,ry+3,94,58,9,PN_UI_INK);
            s=pn_w_text_ex(font,f,sz_l[i],cx+4,ry+41,92,PN_ALIGN_CENTER,sel?PN_UI_PAPER:PN_UI_INK,sel);
        }
    }
    pn_frame_rect(f,32,ry+80,620,1,12);

    // 行3：进度条 [全书 | 本章 | 关闭] / Row 3: Progress bar.
    ry=668;
    if(s==PN_OK){s=pn_font_size(font,28);if(s==PN_OK)s=pn_w_text_ex(font,f,"进度条",32,ry+42,200,PN_ALIGN_LEFT,PN_UI_INK,false);}
    {
        static const char *const pr_l[3]={"全书","本章","关闭"};
        pn_w_round_stroke(f,292,ry,360,64,12,1.5f,PN_UI_INK);
        if(s==PN_OK)s=pn_font_size(font,24);
        for(int i=0;i<3 && s==PN_OK;i++){
            int cx=292+i*120;bool sel=(sb->progress_bar==(uint8_t)i);
            if(sel)pn_w_round_fill(f,cx+3,ry+3,114,58,9,PN_UI_INK);
            s=pn_w_text_ex(font,f,pr_l[i],cx+4,ry+41,112,PN_ALIGN_CENTER,sel?PN_UI_PAPER:PN_UI_INK,sel);
            if(i<2 && !sel && sb->progress_bar!=(uint8_t)(i+1))pn_frame_rect(f,cx+120,ry+14,1,36,11);
        }
    }
    pn_frame_rect(f,32,ry+80,620,1,12);

    // 行4：距离屏幕间距 [-  8 px  +] / Row 4: Edge spacing.
    ry=764;
    if(s==PN_OK){s=pn_font_size(font,28);if(s==PN_OK)s=pn_w_text_ex(font,f,"距离屏幕间距",32,ry+42,260,PN_ALIGN_LEFT,PN_UI_INK,false);}
    pn_w_round_stroke(f,352,ry,300,64,12,1.5f,PN_UI_INK);
    pn_frame_rect(f,442,ry+12,1,40,11);pn_frame_rect(f,562,ry+12,1,40,11);
    pn_w_icon(f,PN_ICON_MINUS,385,ry+20,24,sb->margin_px>0?PN_UI_INK:PN_UI_DISABLED);
    pn_w_icon(f,PN_ICON_PLUS,595,ry+20,24,sb->margin_px<PN_SBAR_MARGIN_MAX?PN_UI_INK:PN_UI_DISABLED);
    if(s==PN_OK){
        char mv[24];snprintf(mv,sizeof mv,"%u px",(unsigned)sb->margin_px);
        s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_ex(font,f,mv,444,ry+41,116,PN_ALIGN_CENTER,PN_UI_INK,true);
    }

    // 底部：按作用域显示“恢复全局状态栏 / 恢复默认”操作区 / Footer: scope-aware restore-global / reset-defaults action bar.
    ry=884;
    if(scope==PN_SBAR_SCOPE_BOOK_CUSTOM){
        pn_w_round_fill(f,32,ry,344,76,14,PN_UI_INK);
        if(s==PN_OK){s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text_ex(font,f,"恢复全局状态栏",32,ry+47,344,PN_ALIGN_CENTER,PN_UI_PAPER,true);}
        pn_w_round_stroke(f,392,ry,260,76,14,1.5f,PN_UI_INK);
        if(s==PN_OK){s=pn_font_size(font,25);if(s==PN_OK)s=pn_w_text_ex(font,f,"本书恢复默认",392,ry+47,260,PN_ALIGN_CENTER,PN_UI_INK,false);}
    }else if(scope==PN_SBAR_SCOPE_BOOK_GLOBAL){
        pn_w_round_fill(f,32,ry,620,76,14,PN_UI_SURFACE);
        pn_w_round_stroke(f,32,ry,620,76,14,1.5f,10);
        if(s==PN_OK){s=pn_font_size(font,23);if(s==PN_OK)s=pn_w_text_ex(font,f,"当前已跟随全局状态栏 · 调整上方任意项将仅对本书生效",44,ry+46,596,PN_ALIGN_CENTER,5,false);}
    }else if(sb->enabled){
        pn_w_round_stroke(f,32,ry,620,76,14,1.5f,PN_UI_INK);
        if(s==PN_OK){s=pn_font_size(font,28);if(s==PN_OK)s=pn_w_text_ex(font,f,"恢复默认（经典双栏）",56,ry+48,500,PN_ALIGN_LEFT,PN_UI_INK,false);}
        pn_w_icon(f,PN_ICON_CHEVRON,606,ry+26,24,PN_UI_INK);
    }else{
        pn_w_round_fill(f,32,ry,620,76,14,PN_UI_SURFACE);
        pn_w_round_stroke(f,32,ry,620,76,14,1.5f,10);
        if(s==PN_OK){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,f,"当前为默认经典双栏 · 修改上方任一项即启用自定义",48,ry+46,588,PN_ALIGN_CENTER,5,false);}
    }
    if(s==PN_OK && notice && *notice){
        s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_lines_ex(font,f,notice,PN_UI_MARGIN+8,1012,604,3,34,PN_UI_MUTED,false);
    }

    // 格子内容选择浮层（支持双栏顶/底共 10 格）/ Floating slot content picker modal (10 slots in dual mode).
    if(s==PN_OK && pick_slot>=0 && pick_slot<(int)(PN_SBAR_SLOTS*2u)){
        bool is_bot=(pick_slot>=(int)PN_SBAR_SLOTS);
        int idx=is_bot?(pick_slot-(int)PN_SBAR_SLOTS):pick_slot;
        char stitle[48];
        if(bar_dual)snprintf(stitle,sizeof stitle,"%s第 %d 格显示内容",is_bot?"底栏":"顶栏",idx+1);
        else snprintf(stitle,sizeof stitle,"第 %d 格显示内容",idx+1);
        int cur=(int)(is_bot?sb->bottom_slots[idx]:sb->slots[idx]);
        const char *modal_sub=(scope==PN_SBAR_SCOPE_GLOBAL)?"点按选项立即保存为全局默认":"点按选项立即保存为本书独立设置";
        s=pn_status_bar_ui_picker_modal(font,f,stitle,modal_sub,sbar_slot_names,PN_SBAR_SLOT_COUNT,cur);
    }
    pn_status_t r=pn_font_size(font,orig);return s==PN_OK?r:s;
}

int pn_status_bar_ui_hit(const pn_status_bar_prefs_t *sb,int pick_slot,pn_sbar_ui_scope_t scope,int x,int y){
    if(!sb || x<0 || x>=SBAR_W || y<0 || y>=SBAR_H)return -1;
    if(pick_slot>=0)return pn_status_bar_ui_picker_hit(PN_SBAR_SLOT_COUNT,x,y,PN_SBAR_UI_PICK_BASE,PN_SBAR_UI_MODAL_CLOSE);
    if(pn_w_header_hit(x,y,false)==1)return PN_SBAR_UI_BACK;
    uint8_t eff_pos=sb->enabled?sb->position:(uint8_t)PN_SBAR_POS_DUAL;
    const int sx0=32+18,sw=112,sgap=6;
    if(eff_pos==PN_SBAR_POS_DUAL){
        int top_y=166+12,bot_y=166+246-56;
        if(y>=top_y-6 && y<top_y+50){
            for(int i=0;i<(int)PN_SBAR_SLOTS;i++){int bx=sx0+i*(sw+sgap);if(x>=bx && x<bx+sw)return PN_SBAR_UI_SLOT_BASE+i;}
        }
        if(y>=bot_y-6 && y<bot_y+50){
            for(int i=0;i<(int)PN_SBAR_SLOTS;i++){int bx=sx0+i*(sw+sgap);if(x>=bx && x<bx+sw)return PN_SBAR_UI_BSLOT_BASE+i;}
        }
    }else if(eff_pos==PN_SBAR_POS_TOP || eff_pos==PN_SBAR_POS_BOTTOM){
        int bar_y=(eff_pos==PN_SBAR_POS_TOP)?(166+22):(166+246-58);
        if(y>=bar_y-6 && y<bar_y+52){
            for(int i=0;i<(int)PN_SBAR_SLOTS;i++){int bx=sx0+i*(sw+sgap);if(x>=bx && x<bx+sw)return PN_SBAR_UI_SLOT_BASE+i;}
        }
    }
    if(y>=476 && y<540 && x>=212 && x<652){
        static const int pos_cmds[4]={PN_SBAR_UI_POS_DUAL,PN_SBAR_UI_POS_TOP,PN_SBAR_UI_POS_BOTTOM,PN_SBAR_UI_POS_OFF};
        return pos_cmds[(x-212)/110];
    }
    if(y>=572 && y<636 && x>=452 && x<652)return PN_SBAR_UI_FONT_BASE+(x-452)/100;
    if(y>=668 && y<732 && x>=292 && x<652)return PN_SBAR_UI_PROG_BASE+(x-292)/120;
    if(y>=764 && y<828){if(x>=352 && x<442)return PN_SBAR_UI_MARGIN_DEC;if(x>=562 && x<652)return PN_SBAR_UI_MARGIN_INC;}
    if(y>=884 && y<960){
        if(scope==PN_SBAR_SCOPE_BOOK_CUSTOM){
            if(x>=32 && x<376)return PN_SBAR_UI_USE_GLOBAL;
            if(x>=392 && x<652)return PN_SBAR_UI_RESET;
        }else if(scope==PN_SBAR_SCOPE_GLOBAL && sb->enabled && x>=32 && x<652){
            return PN_SBAR_UI_RESET;
        }
    }
    return -1;
}

bool pn_status_bar_ui_apply(pn_status_bar_prefs_t *prefs,int *pick_slot,int action,bool *changed){
    if(changed)*changed=false;
    if(!prefs || !pick_slot)return false;
    if(action==PN_SBAR_UI_MODAL_CLOSE){*pick_slot=-1;return true;}
    if(action>=PN_SBAR_UI_SLOT_BASE && action<PN_SBAR_UI_SLOT_BASE+(int)PN_SBAR_SLOTS){*pick_slot=action-PN_SBAR_UI_SLOT_BASE;return true;}
    if(action>=PN_SBAR_UI_BSLOT_BASE && action<PN_SBAR_UI_BSLOT_BASE+(int)PN_SBAR_SLOTS){*pick_slot=(int)PN_SBAR_SLOTS+(action-PN_SBAR_UI_BSLOT_BASE);return true;}
    pn_status_bar_prefs_t next=*prefs;
    if(!next.enabled)next.position=PN_SBAR_POS_DUAL;
    if(action>=PN_SBAR_UI_PICK_BASE && action<PN_SBAR_UI_PICK_BASE+(int)PN_SBAR_SLOT_COUNT && *pick_slot>=0 && *pick_slot<(int)(PN_SBAR_SLOTS*2u)){
        next.enabled=1;
        if(*pick_slot<(int)PN_SBAR_SLOTS)next.slots[*pick_slot]=(uint8_t)(action-PN_SBAR_UI_PICK_BASE);
        else next.bottom_slots[*pick_slot-(int)PN_SBAR_SLOTS]=(uint8_t)(action-PN_SBAR_UI_PICK_BASE);
        *pick_slot=-1;
    }else if(action==PN_SBAR_UI_POS_DUAL){next.enabled=1;next.position=PN_SBAR_POS_DUAL;}
    else if(action==PN_SBAR_UI_POS_TOP){next.enabled=1;next.position=PN_SBAR_POS_TOP;}
    else if(action==PN_SBAR_UI_POS_BOTTOM){next.enabled=1;next.position=PN_SBAR_POS_BOTTOM;}
    else if(action==PN_SBAR_UI_POS_OFF){next.enabled=1;next.position=PN_SBAR_POS_OFF;}
    else if(action>=PN_SBAR_UI_FONT_BASE && action<PN_SBAR_UI_FONT_BASE+2){next.enabled=1;next.font_size=(uint8_t)(action-PN_SBAR_UI_FONT_BASE);}
    else if(action>=PN_SBAR_UI_PROG_BASE && action<PN_SBAR_UI_PROG_BASE+3){next.enabled=1;next.progress_bar=(uint8_t)(action-PN_SBAR_UI_PROG_BASE);}
    else if(action==PN_SBAR_UI_MARGIN_DEC){next.enabled=1;if(next.margin_px>=2)next.margin_px=(uint8_t)(next.margin_px-2);}
    else if(action==PN_SBAR_UI_MARGIN_INC){next.enabled=1;if((unsigned)next.margin_px+2u<=PN_SBAR_MARGIN_MAX)next.margin_px=(uint8_t)(next.margin_px+2u);}
    else if(action==PN_SBAR_UI_RESET){next=pn_status_bar_prefs_default();}
    else return false;
    *prefs=next;
    if(changed)*changed=true;
    return true;
}
