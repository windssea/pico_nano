/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：可视化设置子页：状态栏格子定制、机身线框屏幕手势与动作弹窗、阅读触控区域示意、底部三键/长按摘录/自动翻页间隔。
 * English: Visual settings sub-pages: slot-based status bar editor, wireframe screen-gesture mapper + action modal, visual tap-zone picker, and bottom keys / long-press highlight / auto-turn speed.
 */

static const char *const sbar_slot_names[PN_SBAR_SLOT_COUNT]={"空","时间","章节","书名","进度","节号","电量"};
static const char *const sbar_slot_samples[PN_SBAR_SLOT_COUNT]={"+","12:00","章节","书名","42%","3/20","76%"};
static const char *const gact_names[PN_GACT_COUNT]={"关闭","上一页","下一页","阅读菜单","阅读目录","阅读进度","添加书签","离线查词","立即全刷","自动翻页"};
static const char *const gslot_titles[PN_GSLOT_COUNT]={
    "屏幕内 ↑","屏幕内 ↓","顶部下拉 ↓","左上边缘 →","右上边缘 ←","左边缘 ↕","右边缘 ↕","左下边缘 ↑","右下边缘 ↑"
};
static const char *const tap_zone_names[PN_TAP_ZONE_COUNT]={"左右两分","上下两分","左中右三区","单手右手","全屏下一页"};
static const char *const tap_zone_descs[PN_TAP_ZONE_COUNT]={
    "左半上一页 · 右半下一页","上半上一页 · 下半下一页","左翻上页 · 中呼菜单 · 右翻下页","左与右上上页 · 右下下一页","任意轻点均翻下一页"
};
static const char *const key_map_names[PN_KEY_MAP_COUNT]={"默认","反手","双键下页"};
static const char *const key_map_notes[PN_KEY_MAP_COUNT]={"1上 / 3下","1下 / 3上","1与3皆下"};
static const char *const key2_short_names[5]={"首页","菜单","全刷","书签","自翻"};
static const char *const key2_short_notes[5]={"默认","阅读栏","清残影","加书签","自动翻"};
static const char *const lp_mode_names[PN_LP_COUNT]={"长按查词","长按摘录","关闭长按"};
static const char *const lp_mode_notes[PN_LP_COUNT]={"弹窗可摘录","直接按句","防误触"};

/// 绘制虚线矩形框（用于状态栏可点击格子占位）。/ Draw a dashed rectangle (for clickable status-bar preview slots).
static void draw_dashed_box(pn_frame_t *f,int x,int y,int w,int h,uint8_t ink){
    for(int px=x;px<x+w;px+=8){int sw=(x+w-px>4)?4:(x+w-px);pn_frame_rect(f,px,y,sw,1,ink);pn_frame_rect(f,px,y+h-1,sw,1,ink);}
    for(int py=y;py<y+h;py+=8){int sh=(y+h-py>4)?4:(y+h-py);pn_frame_rect(f,x,py,1,sh,ink);pn_frame_rect(f,x+w-1,py,1,sh,ink);}
}

/// 绘制居中浮层单选菜单（参考图2风格，用于状态栏格子与手势动作选择）。
/// Draw a centered floating single-select modal (Image 2 style, shared by status-bar slots and gesture actions).
static pn_status_t draw_picker_modal(pn_font_t *font,pn_frame_t *f,const char *title,const char *subtitle,const char *const *items,unsigned count,int current){
    const int row_h=64;
    const int mh=108+(int)count*row_h+12;
    const int mx=64,mw=556,my=(H-mh)/2;
    // 阴影与白卡外框 / Drop shadow and crisp white modal border.
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
static int hit_picker_modal(unsigned count,int x,int y,int base_cmd,int close_cmd){
    const int row_h=64;
    const int mh=108+(int)count*row_h+12;
    const int mx=64,mw=556,my=(H-mh)/2;
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

/// 可视化状态栏设置页（双栏上下 2×5 格 / 单栏 5 格可点预览 + 布局/字号/进度条/间距 + 恢复默认）。
/// Visual status bar settings (dual 2x5 or single 5 clickable preview slots + layout/size/progress/margin + reset).
static pn_status_t draw_status_bar(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    const pn_status_bar_prefs_t *sb=&ui->status_bar;
    uint8_t eff_pos=sb->enabled?sb->position:(uint8_t)PN_SBAR_POS_DUAL;
    pn_status_t s=pn_w_header(font,f,u->status_bar_return_screen==SCREEN_MAIN?"< 设置":"< 返回","状态栏设置",NULL);
    int orig=font->pixels;
    const char *mode_sub=!sb->enabled?"当前：默认双栏（点下方或预览格子即可自定义）"
        :eff_pos==PN_SBAR_POS_DUAL?"当前：自定义 · 上下双栏（上下均可配置）"
        :eff_pos==PN_SBAR_POS_TOP?"当前：自定义 · 顶部单栏"
        :eff_pos==PN_SBAR_POS_BOTTOM?"当前：自定义 · 底部单栏"
        :"当前：自定义 · 已关闭状态栏";
    if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,f,mode_sub,32,148,620,PN_ALIGN_CENTER,sb->enabled?PN_UI_INK:5,sb->enabled!=0);}
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
    // 模拟正文灰条 / Schematic prose lines.
    for(int li=0;li<text_lines;li++){
        int lw=(li==text_lines-1)?340:564;
        pn_frame_rect(f,px+28,text_y0+li*28,lw,10,12);
    }
    if(bar_dual){
        // 顶栏 5 格 + 底栏进度条 + 底栏 5 格 / Top 5 slots + bottom progress rail + bottom 5 slots.
        if(s==PN_OK)s=draw_sbar_preview_row(font,f,sb->slots,py+12,44,0,u->sbar_pick_slot,sb->font_size!=0);
        if(sb->progress_bar!=PN_SBAR_PROG_OFF){
            int py_line=py+ph-64;
            pn_frame_rect(f,px+20,py_line,pw-40,3,12);
            pn_frame_rect(f,px+20,py_line,(sb->progress_bar==PN_SBAR_PROG_BOOK)?320:190,4,4);
        }
        if(s==PN_OK)s=draw_sbar_preview_row(font,f,sb->bottom_slots,py+ph-56,44,5,u->sbar_pick_slot,sb->font_size!=0);
    }else if(bar_top || bar_bot){
        int bar_y=bar_top?(py+22):(py+ph-58);
        if(sb->progress_bar!=PN_SBAR_PROG_OFF){
            int py_line=bar_top?(py+12):(py+ph-66);
            pn_frame_rect(f,px+20,py_line,pw-40,3,12);
            pn_frame_rect(f,px+20,py_line,(sb->progress_bar==PN_SBAR_PROG_BOOK)?320:190,4,4);
        }
        if(s==PN_OK)s=draw_sbar_preview_row(font,f,sb->slots,bar_y,46,0,u->sbar_pick_slot,sb->font_size!=0);
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

    // 底部：恢复默认按钮（默认态与自定义态视觉区分明确）/ Footer: Restore defaults (clearly distinguishes default vs customized).
    ry=884;
    if(sb->enabled){
        pn_w_round_stroke(f,32,ry,620,76,14,1.5f,PN_UI_INK);
        if(s==PN_OK){s=pn_font_size(font,28);if(s==PN_OK)s=pn_w_text_ex(font,f,"恢复默认（经典双栏）",56,ry+48,500,PN_ALIGN_LEFT,PN_UI_INK,false);}
        pn_w_icon(f,PN_ICON_CHEVRON,606,ry+26,24,PN_UI_INK);
    }else{
        pn_w_round_fill(f,32,ry,620,76,14,PN_UI_SURFACE);
        pn_w_round_stroke(f,32,ry,620,76,14,1.5f,10);
        if(s==PN_OK){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,f,"当前为默认经典双栏 · 修改上方任一项即启用自定义",48,ry+46,588,PN_ALIGN_CENTER,5,false);}
    }
    if(s==PN_OK && *u->message)s=note(u,u->message,1012);

    // 格子内容选择浮层（支持双栏顶/底共 10 格）/ Floating slot content picker modal (supports top & bottom 10 slots in dual mode).
    if(s==PN_OK && u->sbar_pick_slot>=0 && u->sbar_pick_slot<(int)(PN_SBAR_SLOTS*2u)){
        bool is_bot=(u->sbar_pick_slot>=(int)PN_SBAR_SLOTS);
        int idx=is_bot?(u->sbar_pick_slot-(int)PN_SBAR_SLOTS):u->sbar_pick_slot;
        char stitle[48];
        if(bar_dual)snprintf(stitle,sizeof stitle,"%s第 %d 格显示内容",is_bot?"底栏":"顶栏",idx+1);
        else snprintf(stitle,sizeof stitle,"第 %d 格显示内容",idx+1);
        int cur=(int)(is_bot?sb->bottom_slots[idx]:sb->slots[idx]);
        s=draw_picker_modal(font,f,stitle,"点按选项立即保存生效",sbar_slot_names,PN_SBAR_SLOT_COUNT,cur);
    }
    pn_status_t r=pn_font_size(font,orig);return s==PN_OK?r:s;
}

/// 图1 & 图2：可视化屏幕手势线框图 + 动作选择弹窗。
/// Images 1 & 2: Visual wireframe screen-gesture mapper + action picker modal.
static void gesture_pill_rect(int slot,int *rx,int *ry,int *rw,int *rh){
    *rh=50;
    switch(slot){
        case PN_GSLOT_TOP_DOWN:       *rx=244;*ry=242;*rw=196;break;
        case PN_GSLOT_LEFT_EDGE_IN:   *rx=136;*ry=326;*rw=160;break;
        case PN_GSLOT_RIGHT_EDGE_IN:  *rx=388;*ry=326;*rw=160;break;
        case PN_GSLOT_SWIPE_UP:       *rx=232;*ry=464;*rw=220;break;
        case PN_GSLOT_SWIPE_DOWN:     *rx=232;*ry=664;*rw=220;break;
        case PN_GSLOT_LEFT_EDGE_VERT: *rx=136;*ry=822;*rw=160;break;
        case PN_GSLOT_RIGHT_EDGE_VERT:*rx=388;*ry=822;*rw=160;break;
        case PN_GSLOT_BOT_LEFT_UP:    *rx=136;*ry=1004;*rw=160;break;
        case PN_GSLOT_BOT_RIGHT_UP:   *rx=388;*ry=1004;*rw=160;break;
        default:*rx=0;*ry=0;*rw=0;*rh=0;break;
    }
}
static pn_status_t draw_gesture_pill(pn_font_t *font,pn_frame_t *f,int slot,uint8_t act){
    int x,y,w,h;gesture_pill_rect(slot,&x,&y,&w,&h);
    bool on_act=(act!=PN_GACT_NONE && act<PN_GACT_COUNT);
    char lbl[32];snprintf(lbl,sizeof lbl,"%s >",act<PN_GACT_COUNT?gact_names[act]:"关闭");
    pn_w_round_fill(f,x,y,w,h,10,on_act?PN_UI_INK:PN_UI_PAPER);
    pn_w_round_stroke(f,x,y,w,h,10,1.5f,on_act?PN_UI_INK:7);
    return pn_w_text_ex(font,f,lbl,x+6,y+33,w-12,PN_ALIGN_CENTER,on_act?PN_UI_PAPER:5,on_act);
}
static pn_status_t draw_gestures(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    const pn_gesture_prefs_t *g=&ui->gestures;
    pn_status_t s=pn_w_header(font,f,"< 返回","屏幕手势",NULL);
    int orig=font->pixels;
    if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,f,"点按示意框中的选项修改（仅阅读界面生效）",32,156,620,PN_ALIGN_CENTER,5,false);}
    // 机身屏幕线框（x=88..596, y=176..1130）/ Device screen wireframe box.
    const int fx=88,fy=176,fw=508,fh=954;
    pn_w_round_fill(f,fx,fy,fw,fh,20,PN_UI_PAPER);
    pn_w_round_stroke(f,fx,fy,fw,fh,20,2.0f,6);
    // 顶部下拉把手示意（向下 v 指示）/ Top pull tab indicator (downward chevron).
    pn_w_round_fill(f,292,fy+2,100,26,8,6);
    for(int d=0;d<7;d++){pn_frame_rect(f,335+d,fy+11+d,2,2,PN_UI_PAPER);pn_frame_rect(f,348-d,fy+11+d,2,2,PN_UI_PAPER);}
    // 左/右上边沿内滑耳朵 / Left & right upper inward tabs.
    pn_w_round_stroke(f,fx-2,300,34,104,14,1.5f,7);
    pn_w_round_stroke(f,fx+fw-32,300,34,104,14,1.5f,7);
    // 左右垂直滑动长箭头 / Left & right vertical edge drag arrows.
    pn_frame_rect(f,118,450,2,468,7);pn_frame_rect(f,564,450,2,468,7);
    pn_w_dot(f,119.0f,450.0f,4.0f,6);pn_w_dot(f,119.0f,918.0f,4.0f,6);
    pn_w_dot(f,565.0f,450.0f,4.0f,6);pn_w_dot(f,565.0f,918.0f,4.0f,6);
    // 中心圆点与上下双向实心箭头（参考图1核心视觉）/ Center dot with up/down arrowheads (signature Image 1 visual).
    pn_frame_rect(f,341,528,2,122,PN_UI_INK);
    pn_w_dot(f,342.0f,589.0f,8.0f,PN_UI_INK);
    for(int d=0;d<8;d++){
        pn_frame_rect(f,341-d,526+d,2*d+2,1,PN_UI_INK);
        pn_frame_rect(f,341-d,652-d,2*d+2,1,PN_UI_INK);
    }
    // 底部左右上滑耳朵 / Bottom-left & bottom-right swipe-up tabs.
    pn_w_round_stroke(f,160,fy+fh-28,112,30,12,1.5f,7);
    pn_w_round_stroke(f,412,fy+fh-28,112,30,12,1.5f,7);

    if(s==PN_OK)s=pn_font_size(font,22);
    for(int slot=0;slot<PN_GSLOT_COUNT && s==PN_OK;slot++){
        s=draw_gesture_pill(font,f,slot,g->slots[slot]);
    }
    if(s==PN_OK && ui->vertical_turn){
        s=pn_font_size(font,20);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"提示：已开上下翻页，屏幕内上下滑优先翻页",32,1166,620,PN_ALIGN_CENTER,5,false);
    }else if(s==PN_OK && *u->message){
        s=note(u,u->message,1168);
    }
    if(s==PN_OK && u->gesture_pick_slot>=0 && u->gesture_pick_slot<(int)PN_GSLOT_COUNT){
        s=draw_picker_modal(font,f,gslot_titles[u->gesture_pick_slot],"仅阅读界面生效",gact_names,PN_GACT_COUNT,(int)g->slots[u->gesture_pick_slot]);
    }
    pn_status_t r=pn_font_size(font,orig);return s==PN_OK?r:s;
}

/// 单张阅读触控区示意卡（供整页与局部切换复用）。/ Single reading tap-zone card (reused by full and partial redraws).
static pn_status_t draw_tap_zone_card(pn_font_t *font,pn_frame_t *f,const pn_settings_ui_t *ui,unsigned i){
    int orig=font->pixels;
    int col=(int)(i%2),row=(int)(i/2);
    int cx=(i==4)?32:(32+col*318),cy=182+row*294;
    int cw=(i==4)?620:302,ch=(i==4)?236:276;
    bool sel=(ui->gestures.tap_zone==(uint8_t)i);
    pn_frame_rect(f,cx-2,cy-2,cw+4,ch+4,PN_UI_PAPER);
    pn_w_round_fill(f,cx,cy,cw,ch,16,sel?PN_UI_SURFACE:PN_UI_PAPER);
    pn_w_round_stroke(f,cx,cy,cw,ch,16,sel?3.0f:1.5f,sel?PN_UI_INK:9);
    // 缩略屏区域示意 / Miniature screen zone schematic.
    int sx=cx+(i==4?24:64),sy=cy+18,sw=(i==4)?176:174,sh=160;
    pn_w_round_fill(f,sx,sy,sw,sh,8,PN_UI_PAPER);
    pn_w_round_stroke(f,sx,sy,sw,sh,8,1.5f,PN_UI_INK);
    pn_status_t s=pn_font_size(font,20);
    if(i==PN_TAP_ZONE_LR){
        pn_frame_rect(f,sx+sw/2,sy+2,1,sh-4,7);
        pn_frame_rect(f,sx+sw/2+2,sy+2,sw/2-4,sh-4,14);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"上页",sx+2,sy+88,sw/2-4,PN_ALIGN_CENTER,5,false);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"下页",sx+sw/2+2,sy+88,sw/2-4,PN_ALIGN_CENTER,PN_UI_INK,true);
    }else if(i==PN_TAP_ZONE_TB){
        pn_frame_rect(f,sx+2,sy+sh/2,sw-4,1,7);
        pn_frame_rect(f,sx+2,sy+sh/2+2,sw-4,sh/2-4,14);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"上页",sx+4,sy+48,sw-8,PN_ALIGN_CENTER,5,false);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"下页",sx+4,sy+128,sw-8,PN_ALIGN_CENTER,PN_UI_INK,true);
    }else if(i==PN_TAP_ZONE_THREE_COL){
        int w3=sw/3;
        pn_frame_rect(f,sx+w3,sy+2,1,sh-4,7);pn_frame_rect(f,sx+2*w3,sy+2,1,sh-4,7);
        pn_frame_rect(f,sx+w3+2,sy+2,w3-3,sh-4,13);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"上",sx+2,sy+88,w3-4,PN_ALIGN_CENTER,5,false);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"菜单",sx+w3+2,sy+88,w3-4,PN_ALIGN_CENTER,PN_UI_INK,true);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"下",sx+2*w3+2,sy+88,w3-4,PN_ALIGN_CENTER,PN_UI_INK,false);
    }else if(i==PN_TAP_ZONE_ONE_HAND_R){
        int w3=sw/3;
        pn_frame_rect(f,sx+w3,sy+2,1,sh-4,7);
        pn_frame_rect(f,sx+w3+1,sy+sh/2,sw-w3-3,1,7);
        pn_frame_rect(f,sx+w3+2,sy+sh/2+2,sw-w3-4,sh/2-4,14);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"上",sx+2,sy+88,w3-4,PN_ALIGN_CENTER,5,false);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"上页",sx+w3+2,sy+48,sw-w3-4,PN_ALIGN_CENTER,5,false);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"下页",sx+w3+2,sy+128,sw-w3-4,PN_ALIGN_CENTER,PN_UI_INK,true);
    }else{
        pn_frame_rect(f,sx+2,sy+2,sw-4,sh-4,14);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"全屏下页",sx+4,sy+88,sw-8,PN_ALIGN_CENTER,PN_UI_INK,true);
    }
    if(s==PN_OK){
        int tx=(i==4)?(sx+sw+24):(cx+14),tw=(i==4)?(cw-sw-64):(cw-28),ty=(i==4)?(cy+86):(cy+218);
        s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_ex(font,f,tap_zone_names[i],tx,ty,tw-(sel?34:0),i==4?PN_ALIGN_LEFT:PN_ALIGN_CENTER,PN_UI_INK,sel);
        if(sel)pn_w_icon(f,PN_ICON_CHECK,cx+cw-42,i==4?(cy+62):(cy+194),26,PN_UI_INK);
        if(s==PN_OK){s=pn_font_size(font,20);if(s==PN_OK)s=pn_w_text_ex(font,f,tap_zone_descs[i],tx,ty+32,tw,i==4?PN_ALIGN_LEFT:PN_ALIGN_CENTER,5,false);}
    }
    pn_status_t r=pn_font_size(font,orig);return s==PN_OK?r:s;
}
/// 可视化阅读触控区域选择页（5种方案直观示意）。/ Visual reading tap-zone layout picker (5 schematic cards).
static pn_status_t draw_tap_zones(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_header(font,f,"< 返回","阅读触控区域",NULL);
    int orig=font->pixels;
    if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,f,"轻点阅读正文时的翻页与菜单热区（左右滑始终有效）",32,152,620,PN_ALIGN_CENTER,5,false);}
    for(unsigned i=0;i<PN_TAP_ZONE_COUNT && s==PN_OK;i++)s=draw_tap_zone_card(font,f,ui,i);
    if(s==PN_OK && *u->message)s=note(u,u->message,1050);
    pn_status_t r=pn_font_size(font,orig);return s==PN_OK?r:s;
}

static pn_status_t draw_auto_turn_box(pn_font_t *font,pn_frame_t *f,const pn_gesture_prefs_t *g){
    int ry=772,orig=font->pixels;
    pn_frame_rect(f,28,ry-2,628,88,PN_UI_PAPER);
    pn_w_round_fill(f,32,ry,620,84,14,PN_UI_SURFACE);
    pn_w_round_stroke(f,32,ry,620,84,14,1.5f,9);
    pn_w_round_fill(f,44,ry+12,120,60,10,PN_UI_PAPER);pn_w_round_stroke(f,44,ry+12,120,60,10,1.5f,PN_UI_INK);
    pn_w_round_fill(f,520,ry+12,120,60,10,PN_UI_PAPER);pn_w_round_stroke(f,520,ry+12,120,60,10,1.5f,PN_UI_INK);
    pn_w_icon(f,PN_ICON_MINUS,90,ry+28,28,g->auto_turn_sec>PN_AUTO_TURN_MIN_SEC?PN_UI_INK:PN_UI_DISABLED);
    pn_w_icon(f,PN_ICON_PLUS,566,ry+28,28,g->auto_turn_sec<PN_AUTO_TURN_MAX_SEC?PN_UI_INK:PN_UI_DISABLED);
    char tv[32];snprintf(tv,sizeof tv,"每 %u 秒翻页",(unsigned)g->auto_turn_sec);
    pn_status_t s=pn_font_size(font,30);
    if(s==PN_OK)s=pn_w_text_ex(font,f,tv,176,ry+54,332,PN_ALIGN_CENTER,PN_UI_INK,true);
    pn_status_t r=pn_font_size(font,orig);return s==PN_OK?r:s;
}
/// 图3 & 图5：底部三键映射、高亮摘录长按方式与自动翻页间隔设置页。
/// Images 3 & 5: Bottom 3-key map, sentence highlight long-press behavior, and auto page-turn speed.
static pn_status_t draw_key_highlight(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    const pn_gesture_prefs_t *g=&ui->gestures;
    pn_status_t s=pn_w_header(font,f,"< 返回","三键与摘录",NULL);
    int orig=font->pixels;
    if(s==PN_OK)s=pn_w_group(font,f,"底部三键（阅读时 KEY1 / KEY3）",176);
    if(s==PN_OK)s=pn_w_segments(font,f,key_map_names,key_map_notes,PN_KEY_MAP_COUNT,(int)g->key_map,194,88);
    if(s==PN_OK)s=pn_w_group(font,f,"底部中键（KEY2 短按，默认回首页）",332);
    if(s==PN_OK)s=pn_w_segments(font,f,key2_short_names,key2_short_notes,5,(int)g->key2_short_action,350,78);
    if(s==PN_OK)s=pn_w_group(font,f,"高亮摘录与正文长按",474);
    if(s==PN_OK)s=pn_w_segments(font,f,lp_mode_names,lp_mode_notes,PN_LP_COUNT,(int)g->long_press_mode,492,88);
    if(s==PN_OK)s=note(u,"按句自动高亮摘录，无需拖选；长按已摘录句子可取消。选「长按查词」时也可在词典弹窗顶部一键摘录当前句。",618);
    if(s==PN_OK)s=pn_w_group(font,f,"自动翻页默认间隔",754);
    if(s==PN_OK)s=draw_auto_turn_box(font,f,g);
    if(s==PN_OK)s=note(u,"阅读中可从底部工具栏或自定义手势启动自动翻页；轻点正文或按键随时暂停，到达书尾自动停止。",900);
    if(s==PN_OK && *u->message)s=note(u,u->message,1024);
    pn_status_t r=pn_font_size(font,orig);return s==PN_OK?r:s;
}

static unsigned gestures_active_count(const pn_gesture_prefs_t *g){
    unsigned n=0;for(unsigned i=0;i<PN_GSLOT_COUNT;i++)if(g->slots[i]!=PN_GACT_NONE)n++;return n;
}
static int visual_settings_hit(const su_t *u,const pn_settings_ui_t *ui,int x,int y){
    if(u->screen==SCREEN_STATUS_BAR){
        if(u->sbar_pick_slot>=0)return hit_picker_modal(PN_SBAR_SLOT_COUNT,x,y,PN_SETUI_SBAR_PICK,PN_SETUI_SBAR_MODAL_CLOSE);
        const pn_status_bar_prefs_t *sb=&ui->status_bar;
        uint8_t eff_pos=sb->enabled?sb->position:(uint8_t)PN_SBAR_POS_DUAL;
        const int sx0=32+18,sw=112,sgap=6;
        if(eff_pos==PN_SBAR_POS_DUAL){
            int top_y=166+12,bot_y=166+246-56;
            if(y>=top_y-6 && y<top_y+50){
                for(int i=0;i<(int)PN_SBAR_SLOTS;i++){int bx=sx0+i*(sw+sgap);if(x>=bx && x<bx+sw)return PN_SETUI_SBAR_SLOT+i;}
            }
            if(y>=bot_y-6 && y<bot_y+50){
                for(int i=0;i<(int)PN_SBAR_SLOTS;i++){int bx=sx0+i*(sw+sgap);if(x>=bx && x<bx+sw)return PN_SETUI_SBAR_BSLOT+i;}
            }
        }else if(eff_pos==PN_SBAR_POS_TOP || eff_pos==PN_SBAR_POS_BOTTOM){
            int bar_y=(eff_pos==PN_SBAR_POS_TOP)?(166+22):(166+246-58);
            if(y>=bar_y-6 && y<bar_y+52){
                for(int i=0;i<(int)PN_SBAR_SLOTS;i++){int bx=sx0+i*(sw+sgap);if(x>=bx && x<bx+sw)return PN_SETUI_SBAR_SLOT+i;}
            }
        }
        if(y>=476 && y<540 && x>=212 && x<652){
            static const int pos_cmds[4]={PN_SETUI_SBAR_POS_DUAL,PN_SETUI_SBAR_POS+PN_SBAR_POS_TOP,PN_SETUI_SBAR_POS+PN_SBAR_POS_BOTTOM,PN_SETUI_SBAR_POS+PN_SBAR_POS_OFF};
            return pos_cmds[(x-212)/110];
        }
        if(y>=572 && y<636 && x>=452 && x<652)return PN_SETUI_SBAR_FONT+(x-452)/100;
        if(y>=668 && y<732 && x>=292 && x<652)return PN_SETUI_SBAR_PROG+(x-292)/120;
        if(y>=764 && y<828){if(x>=352 && x<442)return PN_SETUI_SBAR_MARGIN_DEC;if(x>=562 && x<652)return PN_SETUI_SBAR_MARGIN_INC;}
        if(sb->enabled && y>=884 && y<960 && x>=32 && x<652)return PN_SETUI_SBAR_RESET;
        return -1;
    }
    if(u->screen==SCREEN_GESTURES){
        if(u->gesture_pick_slot>=0)return hit_picker_modal(PN_GACT_COUNT,x,y,PN_SETUI_GACT_PICK,PN_SETUI_GMODAL_CLOSE);
        for(int slot=0;slot<(int)PN_GSLOT_COUNT;slot++){
            int rx,ry,rw,rh;gesture_pill_rect(slot,&rx,&ry,&rw,&rh);
            if(x>=rx-6 && x<rx+rw+6 && y>=ry-6 && y<ry+rh+6)return PN_SETUI_GSLOT+slot;
        }
        return -1;
    }
    if(u->screen==SCREEN_TAP_ZONES){
        for(unsigned i=0;i<PN_TAP_ZONE_COUNT;i++){
            int col=(int)(i%2),row=(int)(i/2);
            int cx=(i==4)?32:(32+col*318),cy=182+row*294;
            int cw=(i==4)?620:302,ch=(i==4)?236:276;
            if(x>=cx && x<cx+cw && y>=cy && y<cy+ch)return PN_SETUI_TAP_ZONE_PICK+(int)i;
        }
        return -1;
    }
    if(u->screen==SCREEN_KEY_HIGHLIGHT){
        int c=pn_w_segments_hit(PN_KEY_MAP_COUNT,194,88,x,y);if(c>=0)return PN_SETUI_KEY_MAP+c;
        c=pn_w_segments_hit(5,350,78,x,y);if(c>=0)return PN_SETUI_KEY2_SHORT+c;
        c=pn_w_segments_hit(PN_LP_COUNT,492,88,x,y);if(c>=0)return PN_SETUI_LP_MODE+c;
        if(y>=772 && y<856){if(x>=44 && x<180)return PN_SETUI_AUTO_TURN_DEC;if(x>=504 && x<640)return PN_SETUI_AUTO_TURN_INC;}
        return -1;
    }
    return -1;
}
