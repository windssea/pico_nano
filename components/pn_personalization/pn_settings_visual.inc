/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：可视化设置子页：状态栏格子定制、机身线框屏幕手势与动作弹窗、阅读触控区域示意、底部三键/长按摘录/自动翻页间隔。
 * English: Visual settings sub-pages: slot-based status bar editor, wireframe screen-gesture mapper + action modal, visual tap-zone picker, and bottom keys / long-press highlight / auto-turn speed.
 */

static const char *const gact_names[PN_GACT_COUNT]={"关闭","上一页","下一页","阅读菜单","阅读目录","阅读进度","添加书签","离线查词","立即全刷","自动翻页"};
static const char *const gslot_titles[PN_GSLOT_COUNT]={
    "屏幕内 ↑","屏幕内 ↓","顶部下拉 ↓","左上边缘 →","右上边缘 ←","左边缘 ↕","右边缘 ↕","左下边缘 ↑","右下边缘 ↑"
};
static const char *const tap_zone_names[PN_TAP_ZONE_COUNT]={"左右两分","上下两分","左中右三区","单手右手","全屏下页","单手左手","关闭点按"};
static const char *const tap_zone_descs[PN_TAP_ZONE_COUNT]={
    "左上页 · 右下页","上半上页 · 下半下页","左上 · 中菜单 · 右下","左与右上上 · 右下下","轻点任意处翻下页","右与左上上 · 左下下","仅用滑动或按键翻页"
};
static const char *const swipe_turn_names[PN_SWIPE_TURN_COUNT]={"左右滑动","上下滑动","关闭滑动"};
static const char *const key_map_names[PN_KEY_MAP_COUNT]={"默认","反手","双下页","关闭"};
static const char *const key_map_notes[PN_KEY_MAP_COUNT]={"1上/3下","1下/3上","1与3皆下","禁1/3翻"};
static const char *const key2_short_names[5]={"首页","菜单","全刷","书签","自翻"};
static const char *const key2_short_notes[5]={"默认","阅读栏","清残影","加书签","自动翻"};
static const char *const lp_mode_names[PN_LP_COUNT]={"查词","摘录","关闭","双选条"};
static const char *const lp_mode_notes[PN_LP_COUNT]={"弹窗摘录","直接按句","防误触","摘录/查词"};
static const uint8_t lp_mode_order[PN_LP_COUNT]={PN_LP_MENU,PN_LP_DICT,PN_LP_HIGHLIGHT,PN_LP_OFF};
static const char *const lp_disp_names[PN_LP_COUNT]={"双选条","查词","摘录","关闭"};
static const char *const lp_disp_notes[PN_LP_COUNT]={"摘录/查词","弹窗摘录","直接按句","防误触"};
static const char *const shake_sens_names[PN_SHAKE_SENS_COUNT]={"低","中","高"};
static const char *const shake_sens_notes[PN_SHAKE_SENS_COUNT]={"防误触","默认均衡","轻抖即翻"};
static int lp_disp_index(uint8_t mode){
    for(int i=0;i<(int)PN_LP_COUNT;i++)if(lp_mode_order[i]==mode)return i;
    return 0;
}

static pn_status_t draw_picker_modal(pn_font_t *font,pn_frame_t *f,const char *title,const char *subtitle,const char *const *items,unsigned count,int current){
    return pn_status_bar_ui_picker_modal(font,f,title,subtitle,items,count,current);
}
static int hit_picker_modal(unsigned count,int x,int y,int base_cmd,int close_cmd){
    return pn_status_bar_ui_picker_hit(count,x,y,base_cmd,close_cmd);
}
static pn_status_t draw_status_bar(pn_settings_ui_t *ui,su_t *u){
    return pn_status_bar_ui_render(&u->font,&u->canvas,&ui->status_bar,u->sbar_pick_slot,PN_SBAR_SCOPE_GLOBAL,
        u->status_bar_return_screen==SCREEN_MAIN?"< 设置":"< 返回",u->message);
}

/// 图1 & 图2：可视化屏幕手势线框图 + 动作选择弹窗。
/// Images 1 & 2: Visual wireframe screen-gesture mapper + action picker modal.
static void gesture_pill_rect(int slot,int *rx,int *ry,int *rw,int *rh){
    *rh=48;
    switch(slot){
        case PN_GSLOT_TOP_DOWN:       *rx=244;*ry=266;*rw=196;break;
        case PN_GSLOT_LEFT_EDGE_IN:   *rx=136;*ry=346;*rw=160;break;
        case PN_GSLOT_RIGHT_EDGE_IN:  *rx=388;*ry=346;*rw=160;break;
        case PN_GSLOT_SWIPE_UP:       *rx=232;*ry=480;*rw=220;break;
        case PN_GSLOT_SWIPE_DOWN:     *rx=232;*ry=676;*rw=220;break;
        case PN_GSLOT_LEFT_EDGE_VERT: *rx=136;*ry=832;*rw=160;break;
        case PN_GSLOT_RIGHT_EDGE_VERT:*rx=388;*ry=832;*rw=160;break;
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
    return pn_w_text_ex(font,f,lbl,x+6,y+32,w-12,PN_ALIGN_CENTER,on_act?PN_UI_PAPER:5,on_act);
}
static pn_status_t draw_gestures(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    const pn_gesture_prefs_t *g=&ui->gestures;
    pn_status_t s=pn_w_header(font,f,"< 返回","屏幕手势与滑动",NULL);
    int orig=font->pixels;
    // 顶部翻页滑动方向分段栏（整合原「滑动翻页」与「上下翻页」开关，避免冲突）。
    // Top swipe-turn selector unifying legacy swipe-on/off and horizontal/vertical switches.
    if(s==PN_OK)s=pn_w_segments(font,f,swipe_turn_names,NULL,PN_SWIPE_TURN_COUNT,(int)(g->swipe_turn<PN_SWIPE_TURN_COUNT?g->swipe_turn:0u),140,56);
    // 机身屏幕线框（x=88..596, y=214..1130）/ Device screen wireframe box.
    const int fx=88,fy=214,fw=508,fh=916;
    pn_w_round_fill(f,fx,fy,fw,fh,20,PN_UI_PAPER);
    pn_w_round_stroke(f,fx,fy,fw,fh,20,2.0f,6);
    // 顶部下拉把手示意（向下 v 指示）/ Top pull tab indicator (downward chevron).
    pn_w_round_fill(f,292,fy+2,100,26,8,6);
    for(int d=0;d<7;d++){pn_frame_rect(f,335+d,fy+11+d,2,2,PN_UI_PAPER);pn_frame_rect(f,348-d,fy+11+d,2,2,PN_UI_PAPER);}
    // 左/右上边沿内滑耳朵 / Left & right upper inward tabs.
    pn_w_round_stroke(f,fx-2,320,34,100,14,1.5f,7);
    pn_w_round_stroke(f,fx+fw-32,320,34,100,14,1.5f,7);
    // 左右垂直滑动长箭头 / Left & right vertical edge drag arrows.
    pn_frame_rect(f,118,466,2,452,7);pn_frame_rect(f,564,466,2,452,7);
    pn_w_dot(f,119.0f,466.0f,4.0f,6);pn_w_dot(f,119.0f,918.0f,4.0f,6);
    pn_w_dot(f,565.0f,466.0f,4.0f,6);pn_w_dot(f,565.0f,918.0f,4.0f,6);
    // 中心圆点与上下双向实心箭头 / Center dot with up/down arrowheads.
    pn_frame_rect(f,341,542,2,120,PN_UI_INK);
    pn_w_dot(f,342.0f,602.0f,8.0f,PN_UI_INK);
    for(int d=0;d<8;d++){
        pn_frame_rect(f,341-d,540+d,2*d+2,1,PN_UI_INK);
        pn_frame_rect(f,341-d,664-d,2*d+2,1,PN_UI_INK);
    }
    // 底部左右上滑耳朵 / Bottom-left & bottom-right swipe-up tabs.
    pn_w_round_stroke(f,160,fy+fh-28,112,30,12,1.5f,7);
    pn_w_round_stroke(f,412,fy+fh-28,112,30,12,1.5f,7);

    if(s==PN_OK)s=pn_font_size(font,22);
    for(int slot=0;slot<PN_GSLOT_COUNT && s==PN_OK;slot++){
        s=draw_gesture_pill(font,f,slot,g->slots[slot]);
    }
    if(s==PN_OK && g->swipe_turn==PN_SWIPE_TURN_TB){
        s=pn_font_size(font,20);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"提示：已选上下滑动翻页，屏幕内上下滑优先翻页",32,1166,620,PN_ALIGN_CENTER,5,false);
    }else if(s==PN_OK && *u->message){
        s=note(u,u->message,1168);
    }
    if(s==PN_OK && u->gesture_pick_slot>=0 && u->gesture_pick_slot<(int)PN_GSLOT_COUNT){
        s=draw_picker_modal(font,f,gslot_titles[u->gesture_pick_slot],"仅阅读界面生效",gact_names,PN_GACT_COUNT,(int)g->slots[u->gesture_pick_slot]);
    }
    pn_status_t r=pn_font_size(font,orig);return s==PN_OK?r:s;
}

/// 7 套触控区示意卡（2×3 网格 + 底部宽卡，单页完整呈现）。/ 7 visual tap-zone cards (2x3 grid + bottom wide card on one screen).
static void tap_zone_card_rect(unsigned i,int *cx,int *cy,int *cw,int *ch){
    if(i==6u){*cx=32;*cy=962;*cw=620;*ch=152;return;}
    int col=(int)(i%2u),row=(int)(i/2u);
    *cx=32+col*318;*cy=176+row*262;*cw=302;*ch=246;
}
static pn_status_t draw_tap_zone_card(pn_font_t *font,pn_frame_t *f,const pn_settings_ui_t *ui,unsigned i){
    int orig=font->pixels;
    int cx,cy,cw,ch;tap_zone_card_rect(i,&cx,&cy,&cw,&ch);
    bool sel=(ui->gestures.tap_zone==(uint8_t)i);
    pn_frame_rect(f,cx-2,cy-2,cw+4,ch+4,PN_UI_PAPER);
    pn_w_round_fill(f,cx,cy,cw,ch,16,sel?PN_UI_SURFACE:PN_UI_PAPER);
    pn_w_round_stroke(f,cx,cy,cw,ch,16,sel?3.0f:1.5f,sel?PN_UI_INK:9);
    // 缩略屏区域示意 / Miniature screen zone schematic.
    int sx=cx+(i==6u?24:75),sy=cy+(i==6u?18:14),sw=152,sh=(i==6u?116:136);
    pn_w_round_fill(f,sx,sy,sw,sh,8,PN_UI_PAPER);
    pn_w_round_stroke(f,sx,sy,sw,sh,8,1.5f,PN_UI_INK);
    pn_status_t s=pn_font_size(font,19);
    if(i==PN_TAP_ZONE_LR){
        pn_frame_rect(f,sx+sw/2,sy+2,1,sh-4,7);
        pn_frame_rect(f,sx+sw/2+2,sy+2,sw/2-4,sh-4,14);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"上页",sx+2,sy+76,sw/2-4,PN_ALIGN_CENTER,5,false);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"下页",sx+sw/2+2,sy+76,sw/2-4,PN_ALIGN_CENTER,PN_UI_INK,true);
    }else if(i==PN_TAP_ZONE_TB){
        pn_frame_rect(f,sx+2,sy+sh/2,sw-4,1,7);
        pn_frame_rect(f,sx+2,sy+sh/2+2,sw-4,sh/2-4,14);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"上页",sx+4,sy+42,sw-8,PN_ALIGN_CENTER,5,false);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"下页",sx+4,sy+108,sw-8,PN_ALIGN_CENTER,PN_UI_INK,true);
    }else if(i==PN_TAP_ZONE_THREE_COL){
        int w3=sw/3;
        pn_frame_rect(f,sx+w3,sy+2,1,sh-4,7);pn_frame_rect(f,sx+2*w3,sy+2,1,sh-4,7);
        pn_frame_rect(f,sx+w3+2,sy+2,w3-3,sh-4,13);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"上",sx+2,sy+76,w3-4,PN_ALIGN_CENTER,5,false);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"菜单",sx+w3+2,sy+76,w3-4,PN_ALIGN_CENTER,PN_UI_INK,true);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"下",sx+2*w3+2,sy+76,w3-4,PN_ALIGN_CENTER,PN_UI_INK,false);
    }else if(i==PN_TAP_ZONE_ONE_HAND_R){
        int w3=sw/3;
        pn_frame_rect(f,sx+w3,sy+2,1,sh-4,7);
        pn_frame_rect(f,sx+w3+1,sy+sh/2,sw-w3-3,1,7);
        pn_frame_rect(f,sx+w3+2,sy+sh/2+2,sw-w3-4,sh/2-4,14);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"上",sx+2,sy+76,w3-4,PN_ALIGN_CENTER,5,false);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"上页",sx+w3+2,sy+42,sw-w3-4,PN_ALIGN_CENTER,5,false);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"下页",sx+w3+2,sy+108,sw-w3-4,PN_ALIGN_CENTER,PN_UI_INK,true);
    }else if(i==PN_TAP_ZONE_ONE_HAND_L){
        int w23=sw*2/3;
        pn_frame_rect(f,sx+w23,sy+2,1,sh-4,7);
        pn_frame_rect(f,sx+2,sy+sh/2,w23-2,1,7);
        pn_frame_rect(f,sx+2,sy+sh/2+2,w23-2,sh/2-4,14);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"上页",sx+2,sy+42,w23-4,PN_ALIGN_CENTER,5,false);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"下页",sx+2,sy+108,w23-4,PN_ALIGN_CENTER,PN_UI_INK,true);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"上",sx+w23+2,sy+76,sw-w23-4,PN_ALIGN_CENTER,5,false);
    }else if(i==PN_TAP_ZONE_ALL_NEXT){
        pn_frame_rect(f,sx+2,sy+2,sw-4,sh-4,14);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"全屏下页",sx+4,sy+76,sw-8,PN_ALIGN_CENTER,PN_UI_INK,true);
    }else{
        pn_frame_rect(f,sx+2,sy+2,sw-4,sh-4,13);
        if(s==PN_OK)s=pn_w_text_ex(font,f,"禁用点按",sx+4,sy+66,sw-8,PN_ALIGN_CENTER,5,false);
    }
    if(s==PN_OK){
        int tx=(i==6u)?(sx+sw+24):(cx+12),tw=(i==6u)?(cw-sw-64):(cw-24),ty=(i==6u)?(cy+64):(cy+186);
        s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_ex(font,f,tap_zone_names[i],tx,ty,tw-(sel?30:0),i==6u?PN_ALIGN_LEFT:PN_ALIGN_CENTER,PN_UI_INK,sel);
        if(sel)pn_w_icon(f,PN_ICON_CHECK,cx+cw-40,i==6u?(cy+42):(cy+164),24,PN_UI_INK);
        if(s==PN_OK){s=pn_font_size(font,19);if(s==PN_OK)s=pn_w_text_ex(font,f,tap_zone_descs[i],tx,ty+32,tw,i==6u?PN_ALIGN_LEFT:PN_ALIGN_CENTER,5,false);}
    }
    pn_status_t r=pn_font_size(font,orig);return s==PN_OK?r:s;
}
/// 可视化阅读触控区域选择页（7种方案直观示意，直接生效无旧开关冲突）。/ Visual reading tap-zone layout picker (7 schematic cards, directly authoritative).
static pn_status_t draw_tap_zones(pn_settings_ui_t *ui,su_t *u){
    pn_font_t *font=&u->font;pn_frame_t *f=&u->canvas;
    pn_status_t s=pn_w_header(font,f,"< 返回","阅读触控区域",NULL);
    int orig=font->pixels;
    if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,f,"轻点阅读正文时的翻页与菜单热区（立即生效）",32,150,620,PN_ALIGN_CENTER,5,false);}
    for(unsigned i=0;i<PN_TAP_ZONE_COUNT && s==PN_OK;i++)s=draw_tap_zone_card(font,f,ui,i);
    if(s==PN_OK && *u->message)s=note(u,u->message,1152);
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
/// 底部三键映射、高亮摘录与正文长按行为、自动翻页间隔设置页。
/// Bottom 3-key map, body long-press dual-pill / highlight / dict mode, and auto page-turn speed.
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
    if(s==PN_OK)s=pn_w_segments(font,f,lp_disp_names,lp_disp_notes,PN_LP_COUNT,lp_disp_index(g->long_press_mode),492,88);
    if(s==PN_OK)s=note(u,"默认「双选条」长按弹出 [摘录 | 查词]，零翻页延迟；也可固定为直接查词、直接摘录或关闭长按。",618);
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
        int act=pn_status_bar_ui_hit(&ui->status_bar,u->sbar_pick_slot,PN_SBAR_SCOPE_GLOBAL,x,y);
        if(act<0)return -1;
        if(act==PN_SBAR_UI_BACK)return PN_SETUI_BACK;
        if(act==PN_SBAR_UI_MODAL_CLOSE)return PN_SETUI_SBAR_MODAL_CLOSE;
        if(act>=PN_SBAR_UI_SLOT_BASE && act<PN_SBAR_UI_SLOT_BASE+(int)PN_SBAR_SLOTS)return PN_SETUI_SBAR_SLOT+(act-PN_SBAR_UI_SLOT_BASE);
        if(act>=PN_SBAR_UI_BSLOT_BASE && act<PN_SBAR_UI_BSLOT_BASE+(int)PN_SBAR_SLOTS)return PN_SETUI_SBAR_BSLOT+(act-PN_SBAR_UI_BSLOT_BASE);
        if(act>=PN_SBAR_UI_PICK_BASE && act<PN_SBAR_UI_PICK_BASE+(int)PN_SBAR_SLOT_COUNT)return PN_SETUI_SBAR_PICK+(act-PN_SBAR_UI_PICK_BASE);
        if(act==PN_SBAR_UI_POS_DUAL)return PN_SETUI_SBAR_POS_DUAL;
        if(act==PN_SBAR_UI_POS_TOP)return PN_SETUI_SBAR_POS+PN_SBAR_POS_TOP;
        if(act==PN_SBAR_UI_POS_BOTTOM)return PN_SETUI_SBAR_POS+PN_SBAR_POS_BOTTOM;
        if(act==PN_SBAR_UI_POS_OFF)return PN_SETUI_SBAR_POS+PN_SBAR_POS_OFF;
        if(act>=PN_SBAR_UI_FONT_BASE && act<PN_SBAR_UI_FONT_BASE+2)return PN_SETUI_SBAR_FONT+(act-PN_SBAR_UI_FONT_BASE);
        if(act>=PN_SBAR_UI_PROG_BASE && act<PN_SBAR_UI_PROG_BASE+3)return PN_SETUI_SBAR_PROG+(act-PN_SBAR_UI_PROG_BASE);
        if(act==PN_SBAR_UI_MARGIN_DEC)return PN_SETUI_SBAR_MARGIN_DEC;
        if(act==PN_SBAR_UI_MARGIN_INC)return PN_SETUI_SBAR_MARGIN_INC;
        if(act==PN_SBAR_UI_RESET)return PN_SETUI_SBAR_RESET;
        return -1;
    }
    if(u->screen==SCREEN_GESTURES){
        if(u->gesture_pick_slot>=0)return hit_picker_modal(PN_GACT_COUNT,x,y,PN_SETUI_GACT_PICK,PN_SETUI_GMODAL_CLOSE);
        int sc=pn_w_segments_hit(PN_SWIPE_TURN_COUNT,140,56,x,y);
        if(sc>=0)return PN_SETUI_SWIPE_TURN+sc;
        for(int slot=0;slot<(int)PN_GSLOT_COUNT;slot++){
            int rx,ry,rw,rh;gesture_pill_rect(slot,&rx,&ry,&rw,&rh);
            if(x>=rx-6 && x<rx+rw+6 && y>=ry-6 && y<ry+rh+6)return PN_SETUI_GSLOT+slot;
        }
        return -1;
    }
    if(u->screen==SCREEN_TAP_ZONES){
        for(unsigned i=0;i<PN_TAP_ZONE_COUNT;i++){
            int cx,cy,cw,ch;tap_zone_card_rect(i,&cx,&cy,&cw,&ch);
            if(x>=cx && x<cx+cw && y>=cy && y<cy+ch)return PN_SETUI_TAP_ZONE_PICK+(int)i;
        }
        return -1;
    }
    if(u->screen==SCREEN_KEY_HIGHLIGHT){
        int c=pn_w_segments_hit(PN_KEY_MAP_COUNT,194,88,x,y);if(c>=0)return PN_SETUI_KEY_MAP+c;
        c=pn_w_segments_hit(5,350,78,x,y);if(c>=0)return PN_SETUI_KEY2_SHORT+c;
        c=pn_w_segments_hit(PN_LP_COUNT,492,88,x,y);if(c>=0)return PN_SETUI_LP_MODE+(int)lp_mode_order[c];
        if(y>=772 && y<856){if(x>=44 && x<180)return PN_SETUI_AUTO_TURN_DEC;if(x>=504 && x<640)return PN_SETUI_AUTO_TURN_INC;}
        return -1;
    }
    return -1;
}
