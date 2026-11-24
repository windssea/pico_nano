/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：排版与阅读线草稿、正文预览与应用/取消；绘制不写配置。
 * English: typesetting and reading-guide drafts, body preview and apply/cancel; painting never writes configuration.
 * 冻结：变化先进入草稿；写入失败不关闭界面。
 * Frozen: changes enter drafts first; write failures never dismiss the UI.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_brand.h"
#include "pn_style_ui.h"
#include "pn_builtin_art.h"
#include "pn_reader_chrome.h"
#include <stdio.h>
#include <string.h>
static bool live(const pn_style_ui_t *u){return u && ((u->reader && u->reader->impl) || (u->epub && u->epub->impl));}
static bool confirmed(const pn_style_ui_t *u){return u->epub?pn_epub_app_last_confirmed(u->epub):pn_reader_app_last_confirmed(u->reader);}
static pn_status_t overlay(pn_style_ui_t *u,pn_reader_overlay_fn paint,pn_reader_present_fn present,void *ctx){return u->epub?pn_epub_app_overlay(u->epub,paint,u,present,ctx,PN_REFRESH_GL16):pn_reader_app_overlay(u->reader,paint,u,present,ctx,PN_REFRESH_GL16);}
static pn_status_t preview(pn_style_ui_t *u,uint64_t now,pn_reader_present_fn present,void *ctx){return u->epub?pn_epub_app_style_preview(u->epub,&u->draft,now,present,ctx):pn_reader_app_style_preview(u->reader,&u->draft,now,present,ctx);}
static pn_status_t apply(pn_style_ui_t *u,uint64_t now,pn_reader_present_fn present,void *ctx){return u->epub?pn_epub_app_style_apply(u->epub,&u->draft,now,present,ctx):pn_reader_app_style_apply(u->reader,&u->draft,now,present,ctx);}
static pn_status_t cancel(pn_style_ui_t *u,uint64_t now,pn_reader_present_fn present,void *ctx){return u->epub?pn_epub_app_style_cancel(u->epub,now,present,ctx):pn_reader_app_style_cancel(u->reader,now,present,ctx);}
#include "pn_widgets.h"
/* 双列草稿面板：全部十二项（含背景）六行两列保留在一页；点上方段落预览查看整页。
 * Two-column draft panel: all twelve fields including backgrounds stay on one page in six rows; tap the paragraph preview for the full page. */
#define PREVIEW_Y 136
#define PREVIEW_H 160
#define MODE_BASE 330
#define PRESET_Y 342
#define PRESET_H 60
#define FONT_ROW_Y 410
#define STEP_Y0 498
#define STEP_PITCH 88
#define CELL_H 80 ///< 单格高度 / Cell height
#define CELL_W 300
#define CELL_PITCH 320
#define APPLY_Y 1112
#define APPLY_H 88
// 按用途分组：间距两行、字形外观一行（字重/清晰度）、阅读线两行。/ Grouped by purpose: two spacing rows, one glyph-appearance row (weight/sharpness), two reading-guide rows.
static const unsigned fields[PN_SUI_FIELDS]={0,1,2,5,3,4,9,10,6,8,7,11,12};
static const char *const names[PN_SUI_FIELDS]={"字号","行距","段距","首行缩进","字间距","边距","阅读线","上下位置","阅读线深浅","字重","清晰度","背景","书内图片"};
typedef struct {unsigned pixels,line,gap,indent,margin;} preset_t;
static const preset_t presets[3]={{PN_STYLE_DEFAULT_PIXELS,145,40,2,32},{36,125,20,2,24},{56,160,40,2,32}};
static const char *const preset_names[3]={"舒适","紧凑","大字"};
static unsigned field_value(const pn_style_t *s,unsigned i){switch(i){case 0:return s->pixels;case 1:return s->line_percent;case 2:return s->gap_percent;case 3:return s->indent_em;case 4:return s->tracking_percent;case 5:return s->margin;case 6:return s->reading_line;case 8:return s->reading_line_shade;case 9:return (unsigned)(s->weight+1);case 10:return s->clarity;case 11:return s->background;case 12:return s->hide_images;default:return (unsigned)(s->reading_line_offset+8);}}
static void field_set(pn_style_t *s,unsigned i,unsigned v){switch(i){case 0:s->pixels=(uint16_t)v;break;case 1:s->line_percent=(uint16_t)v;break;case 2:s->gap_percent=(uint16_t)v;break;case 3:s->indent_em=(uint16_t)v;break;case 4:s->tracking_percent=(uint16_t)v;break;case 5:s->margin=(uint16_t)v;break;case 6:s->reading_line=(uint16_t)v;break;case 8:s->reading_line_shade=(uint16_t)v;break;case 9:s->weight=(int16_t)((int)v-1);break;case 10:s->clarity=(uint16_t)v;break;case 11:s->background=(uint16_t)v;memset(s->background_name,0,sizeof s->background_name);memset(&s->background_identity,0,sizeof s->background_identity);break;case 12:s->hide_images=(uint16_t)v;break;default:s->reading_line_offset=(int16_t)((int)v-8);break;}}
static void field_text(const pn_style_t *s,unsigned i,char *out,size_t cap){
    unsigned v=field_value(s,i);
    if(i==1 || i==2)snprintf(out,cap,"%u.%02u",v/100,v%100);
    else if(i==3)snprintf(out,cap,"%u 字",v);
    else if(i==4)snprintf(out,cap,"%u%%",v);
    else if(i==5){
        if(s->margin_top || s->margin_bottom || s->margin_right_delta || s->margin<16 || s->margin>80)snprintf(out,cap,"自定义");
        else snprintf(out,cap,v<=20?"窄":v<=32?"中":"宽");
    }
    else if(i==6)snprintf(out,cap,"%s",v==1?"虚线":v==2?"点线":"关闭");
    else if(i==7)snprintf(out,cap,"%+d px",s->reading_line_offset);
    else if(i==8){static const char *const shades[4]={"浅","中","深","黑"};snprintf(out,cap,"%s",shades[v<4?v:0]);}
    else if(i==9){static const char *const weights[5]={"细","标准","稍粗","粗","特粗"};snprintf(out,cap,"%s",weights[v<5?v:1]);}
    else if(i==10){static const char *const levels[4]={"标准","清晰","锐利","黑白"};snprintf(out,cap,"%s",levels[v<4?v:0]);}
    else if(i==11)snprintf(out,cap,"%s",v==PN_READING_BACKGROUND_EXTERNAL?"自定义":pn_reading_background_name(v));
    else snprintf(out,cap,"%u",v);
}
static int selected_preset(const pn_style_t *s){
    for(int i=0;i<3;i++)if(s->pixels==presets[i].pixels && s->line_percent==presets[i].line && s->gap_percent==presets[i].gap && s->indent_em==presets[i].indent && s->margin==presets[i].margin)return i;
    return -1;
}
static pn_font_t *body_font(const pn_style_ui_t *u){
    pn_font_t *font=u->epub?pn_epub_app_body_font(u->epub):u->reader?pn_reader_app_body_font(u->reader):NULL;
    return font && font->impl?font:NULL;
}
/* 当前字体名：文件取不含扩展名的文件名，否则“默认字体”。/ Current font name: a file's base name without extension, otherwise "default font". */
static void font_name(pn_style_ui_t *u,char *out,size_t cap){
    snprintf(out,cap,"系统字体");
    pn_font_preferences_t prefs;char directory[300];
    pn_status_t status=u->epub?pn_epub_app_fonts_get(u->epub,&prefs,directory,sizeof directory):u->reader?pn_reader_app_fonts_get(u->reader,&prefs,directory,sizeof directory):PN_INVALID;
    if(status==PN_OK && prefs.primary.kind==PN_FONT_FILE && cap>1){
        const char *slash=strrchr(prefs.primary.path,'/'),*base=slash?slash+1:prefs.primary.path;
        // 按UTF-8字符边界截短到缓冲区，再去掉扩展名。/ Clip to the buffer at a UTF-8 character boundary, then drop the extension.
        size_t n=strnlen(base,cap-1);
        if(n==cap-1)while(n>0 && ((unsigned char)base[n]&0xc0)==0x80)n--;
        memcpy(out,base,n);out[n]=0;
        char *dot=strrchr(out,'.');if(dot && dot!=out)*dot=0;
    }
    bool inherited=u->epub?pn_epub_app_font_is_inherited(u->epub):pn_reader_app_font_is_inherited(u->reader);
    if(inherited && cap){
        const char *prefix="全局 · ";size_t lead=strlen(prefix);
        if(cap<=lead){out[0]=0;return;}
        size_t n=strlen(out),room=cap-lead-1;
        if(n>room){n=room;while(n && ((unsigned char)out[n]&0xc0)==0x80)n--;}
        memmove(out+lead,out,n);memcpy(out,prefix,lead);out[lead+n]=0;
    }
}
/* 每项占半行，标签在上、浅雾左右步进按键与居中数值在下，触控边界清晰不重叠。
 * Each field uses half a row with label on top and fog left/right stepper pads flanking the value for clear touch boundaries. */
static pn_status_t stepper(pn_font_t *font,pn_frame_t *frame,const pn_style_t *draft,unsigned field,int x,int y,bool selected){
    char value[40];field_text(draft,field,value,sizeof value);
    pn_w_round_stroke(frame,x,y,CELL_W,CELL_H,12,selected?2.0f:1.5f,selected?PN_UI_INK:9);
    pn_status_t s=pn_font_size(font,21);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,names[field],x+12,y+24,CELL_W-24,PN_ALIGN_LEFT,5,false);
    if(s==PN_OK && field==5)s=pn_w_text_ex(font,frame,"详调 >",x+12,y+24,CELL_W-24,PN_ALIGN_RIGHT,4,true);
    bool choice=field==5 || field==6 || field==8 || field==9 || field==10;
    if(field==11){pn_w_icon(frame,PN_ICON_CHEVRON,x+CELL_W-38,y+44,24,6);if(s==PN_OK)s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text_ex(font,frame,value,x+12,y+67,CELL_W-60,PN_ALIGN_LEFT,PN_UI_INK,true);return s;}
    pn_w_round_fill(frame,x+8,y+32,64,40,8,PN_UI_SURFACE);pn_w_round_stroke(frame,x+8,y+32,64,40,8,1.0f,11);
    pn_w_round_fill(frame,x+CELL_W-72,y+32,64,40,8,PN_UI_SURFACE);pn_w_round_stroke(frame,x+CELL_W-72,y+32,64,40,8,1.0f,11);
    pn_w_icon(frame,choice?PN_ICON_BACK:PN_ICON_MINUS,x+29,y+41,22,PN_UI_INK);
    pn_w_icon(frame,choice?PN_ICON_CHEVRON:PN_ICON_PLUS,x+CELL_W-51,y+41,22,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,value,x+74,y+62,CELL_W-148,PN_ALIGN_CENTER,PN_UI_INK,true);
    return s;
}
/* 用草稿的字号/行距/缩进/字距试排一段，让数值变化立刻可见；正文字体不可用时用UI字体。
 * Typeset a sample paragraph with the draft's size, spacing, indent and tracking so changes are visible at once; the UI font is used when the body font is unavailable. */
static pn_status_t sample(pn_style_ui_t *u,pn_font_t *ui,pn_frame_t *frame){
    pn_w_round_stroke(frame,32,PREVIEW_Y,620,PREVIEW_H,PN_UI_RADIUS,2.0f,8);
    pn_status_t s=pn_font_size(ui,26);
    if(s==PN_OK)s=pn_w_text(ui,frame,"当前段落预览",52,PREVIEW_Y+40,560,PN_ALIGN_LEFT);
    if(s==PN_OK)s=pn_w_text(ui,frame,"轻点查看整页",52,PREVIEW_Y+40,560,PN_ALIGN_RIGHT);
    pn_font_t *body=body_font(u);pn_font_t *face=body?body:ui;int saved=face->pixels,saved_weight=pn_font_weight_level(face),saved_clarity=pn_font_clarity_level(face);
    int pixels=(int)u->draft.pixels;if(pixels>56)pixels=56;
    if(s==PN_OK)s=pn_font_size(face,pixels);
    // 预览按草稿字重画，结束后恢复正文原字重。/ Preview with the draft weight, then restore the body's original weight.
    if(s==PN_OK && face==body)s=pn_font_weight(face,u->draft.weight);
    if(s==PN_OK && face==body)s=pn_font_clarity(face,u->draft.clarity);
    int pitch=pixels*(int)u->draft.line_percent/100;
    unsigned room=(unsigned)((PREVIEW_H-60)/pitch);if(room<1)room=1;if(room>4)room=4;
    unsigned lines=0;
    if(s==PN_OK)s=pn_w_text_flow(face,frame,"字与字之间，留出恰好的距离。让阅读回到舒适，把时间留给文字。",52,PREVIEW_Y+60+pixels,556,room,pitch,pixels*(int)u->draft.indent_em,pixels*64*(int)u->draft.tracking_percent/100,&lines);
    if(s==PN_OK && u->draft.reading_line){int ascent,descent;s=pn_font_vertical(face,&ascent,&descent);
        if(s==PN_OK){pn_frame_t preview={frame->pixels+(PREVIEW_Y+60)*frame->stride+26,556,PREVIEW_H-72,frame->stride};int gap=pitch-ascent-descent;if(gap<0)gap=0;
            for(unsigned i=0;i<lines;i++)pn_reader_guide_row(&preview,u->draft.reading_line,u->draft.reading_line_shade,pixels+(int)i*pitch+descent+gap/2+u->draft.reading_line_offset);}}
    pn_status_t restored=pn_font_size(face,saved);
    if(restored==PN_OK)restored=pn_font_weight(face,saved_weight);
    if(restored==PN_OK)restored=pn_font_clarity(face,saved_clarity);
    return s==PN_OK?restored:s;
}
static pn_status_t paint_main(pn_style_ui_t *u,pn_font_t *font,pn_frame_t *frame){
    pn_status_t s=sample(u,font,frame);int chosen=selected_preset(&u->draft);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,"排版模式",32,MODE_BASE,300,PN_ALIGN_LEFT,PN_UI_MUTED,true);
    if(s==PN_OK && chosen<0)s=pn_w_text_ex(font,frame,"自定义",352,MODE_BASE,300,PN_ALIGN_RIGHT,PN_UI_INK,true);
    if(s==PN_OK)s=pn_w_segments(font,frame,preset_names,NULL,3,chosen,PRESET_Y,PRESET_H);
    char name[PN_FONT_NAME_MAX+8];font_name(u,name,sizeof name);
    if(s==PN_OK)s=pn_font_size(font,28);
    if(s==PN_OK)s=pn_w_text(font,frame,"字体",32,FONT_ROW_Y+44,140,PN_ALIGN_LEFT);
    pn_w_set_fallback(body_font(u));
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,pn_brand_label(name),176,FONT_ROW_Y+44,420,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
    pn_w_set_fallback(NULL);pn_w_icon(frame,PN_ICON_CHEVRON,624,FONT_ROW_Y+21,28,PN_UI_INK);
    pn_frame_rect(frame,32,FONT_ROW_Y+72,620,1,PN_UI_SELECT);
    for(unsigned i=0;i<PN_SUI_FIELDS-1 && s==PN_OK;i++)s=stepper(font,frame,&u->draft,fields[i],32+(int)(i%2)*CELL_PITCH,STEP_Y0+(int)(i/2)*STEP_PITCH,u->selected==fields[i]);
    // 图片与去上下栏开关并排置于底行，均只改草稿；与应用/取消及键盘字段共用同一状态。
    // Image and bar-hiding toggles share the bottom row, modifying only the draft alongside apply/cancel and keyboard state.
    pn_w_round_stroke(frame,32,1028,CELL_W,72,10,u->selected==12?2:1,u->selected==12?PN_UI_INK:8);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text(font,frame,"书内图片",44,1057,170,PN_ALIGN_LEFT);
    if(s==PN_OK)s=pn_w_text(font,frame,u->draft.hide_images?"不显示":"显示",44,1070,CELL_W-24,PN_ALIGN_RIGHT);
    if(s==PN_OK)s=pn_font_size(font,18);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,"关闭后保留占位",44,1084,180,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    pn_w_round_stroke(frame,352,1028,CELL_W,72,10,1,8);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text(font,frame,"上下栏",364,1057,170,PN_ALIGN_LEFT);
    if(s==PN_OK)s=pn_w_text(font,frame,u->draft.hide_bars?"隐藏":"显示",364,1070,CELL_W-24,PN_ALIGN_RIGHT);
    if(s==PN_OK)s=pn_font_size(font,18);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,"状态栏与页脚",364,1084,180,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    return s;
}
#define MCELL_H 108
#define MBTN_W 96
#define MBTN_H 64
#define MHIT_W 108
/* 边距详调步进格：放大左右减/加实体按键与触控热区，便于单手点准。
 * Enlarged margin stepper cell: bigger visible minus/plus buttons and touch hit zones for reliable tapping. */
static pn_status_t margin_stepper(pn_font_t *font,pn_frame_t *frame,const char *label,int px,int x,int y){
    char value[24];snprintf(value,sizeof value,"%d px",px);
    pn_w_round_stroke(frame,x,y,CELL_W,MCELL_H,12,1,8);
    pn_status_t s=pn_font_size(font,22);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,label,x+14,y+28,CELL_W-28,PN_ALIGN_CENTER,PN_UI_MUTED,false);
    int by=y+36,lx=x+8,rx=x+CELL_W-8-MBTN_W;
    pn_w_round_fill(frame,lx,by,MBTN_W,MBTN_H,10,PN_UI_SELECT);
    pn_w_round_stroke(frame,lx,by,MBTN_W,MBTN_H,10,1.5f,PN_UI_INK);
    pn_w_icon(frame,PN_ICON_MINUS,lx+(MBTN_W-28)/2,by+(MBTN_H-28)/2,28,PN_UI_INK);
    pn_w_round_fill(frame,rx,by,MBTN_W,MBTN_H,10,PN_UI_SELECT);
    pn_w_round_stroke(frame,rx,by,MBTN_W,MBTN_H,10,1.5f,PN_UI_INK);
    pn_w_icon(frame,PN_ICON_PLUS,rx+(MBTN_W-28)/2,by+(MBTN_H-28)/2,28,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,value,lx+MBTN_W,by+42,rx-(lx+MBTN_W),PN_ALIGN_CENTER,PN_UI_INK,true);
    return s;
}
static pn_status_t paint_margins(pn_style_ui_t *u,pn_font_t *font,pn_frame_t *frame){
    pn_status_t s=pn_w_header(font,frame,"< 返回","边距与栏位",NULL);
    if(s==PN_OK){s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text_ex(font,frame,"重置边距",480,100,172,PN_ALIGN_RIGHT,PN_UI_MUTED,false);}
    // 顶部示意框：紧凑高度给下方步进大按钮留足空间。/ Compact top diagram leaves generous space for larger stepper buttons below.
    const int px=44,py=134,pw=132,ph=226;
    pn_w_round_fill(frame,px,py,pw,ph,8,PN_UI_PAPER);
    pn_w_round_stroke(frame,px,py,pw,ph,8,2.0f,PN_UI_INK);
    if(u->draft.background){
        int bx=px+u->draft.bg_margin_left*pw/684,by=py+u->draft.bg_margin_top*ph/1216;
        int bw=(684-(int)u->draft.bg_margin_left-(int)u->draft.bg_margin_right)*pw/684;
        int bh=(1216-(int)u->draft.bg_margin_top-(int)u->draft.bg_margin_bottom)*ph/1216;
        if(bw>2 && bh>2){pn_frame_rect(frame,bx,by,bw,bh,13);pn_w_outline(frame,bx,by,bw,bh,1,8);}
    }
    if(!u->draft.hide_bars){
        pn_frame_rect(frame,px+4,py+4,pw-8,5,9);
        pn_frame_rect(frame,px+4,py+ph-14,pw-8,10,9);
    }
    int tx=px+pn_style_margin_left(&u->draft)*pw/684,ty=py+pn_style_body_top(&u->draft)*ph/1216;
    int tw=pn_style_body_width(&u->draft)*pw/684,th=pn_style_body_height(&u->draft)*ph/1216;
    if(tw>4 && th>4){
        pn_w_outline(frame,tx,ty,tw,th,1,PN_UI_INK);
        for(int r=0;r<6;r++){int ly=ty+8+r*(th-16)/6;if(ly<ty+th-5)pn_frame_rect(frame,tx+5,ly,tw>10?tw-10:2,2,4);}
    }
    char info[64];
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK){snprintf(info,sizeof info,"正文宽 %d × 高 %d px",pn_style_body_width(&u->draft),pn_style_body_height(&u->draft));s=pn_w_text_ex(font,frame,info,204,174,448,PN_ALIGN_LEFT,PN_UI_INK,true);}
    if(s==PN_OK)s=pn_font_size(font,22);
    if(s==PN_OK){snprintf(info,sizeof info,"上下栏：%s",u->draft.hide_bars?"已隐藏（全屏正文）":"显示状态栏与页脚");s=pn_w_text_ex(font,frame,info,204,216,448,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    if(s==PN_OK){snprintf(info,sizeof info,"正文边距：上%d 下%d 左%d 右%d",pn_style_margin_top(&u->draft),pn_style_margin_bottom(&u->draft),pn_style_margin_left(&u->draft),pn_style_margin_right(&u->draft));s=pn_w_text_ex(font,frame,info,204,254,448,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    if(s==PN_OK){snprintf(info,sizeof info,"阅读背景：%s",u->draft.background==PN_READING_BACKGROUND_EXTERNAL?"自定义":pn_reading_background_name(u->draft.background));s=pn_w_text_ex(font,frame,info,204,292,448,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    if(s==PN_OK){snprintf(info,sizeof info,"背景边距：上%u 下%u 左%u 右%u",u->draft.bg_margin_top,u->draft.bg_margin_bottom,u->draft.bg_margin_left,u->draft.bg_margin_right);s=pn_w_text_ex(font,frame,info,204,330,448,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    if(s==PN_OK)s=pn_w_group(font,frame,"正文与上下栏",392);
    pn_w_round_stroke(frame,32,404,620,72,10,1,8);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text(font,frame,"去上下栏（隐藏状态栏与页脚）",48,449,430,PN_ALIGN_LEFT);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,u->draft.hide_bars?"已开启":"已关闭",48,449,580,PN_ALIGN_RIGHT,PN_UI_INK,u->draft.hide_bars!=0);
    if(s==PN_OK)s=margin_stepper(font,frame,"正文上边距",pn_style_margin_top(&u->draft),32,488);
    if(s==PN_OK)s=margin_stepper(font,frame,"正文下边距",pn_style_margin_bottom(&u->draft),352,488);
    if(s==PN_OK)s=margin_stepper(font,frame,"正文左边距",pn_style_margin_left(&u->draft),32,606);
    if(s==PN_OK)s=margin_stepper(font,frame,"正文右边距",pn_style_margin_right(&u->draft),352,606);
    if(s==PN_OK)s=pn_w_group(font,frame,"阅读背景边距",750);
    if(s==PN_OK)s=margin_stepper(font,frame,"背景上边距",u->draft.bg_margin_top,32,762);
    if(s==PN_OK)s=margin_stepper(font,frame,"背景下边距",u->draft.bg_margin_bottom,352,762);
    if(s==PN_OK)s=margin_stepper(font,frame,"背景左边距",u->draft.bg_margin_left,32,880);
    if(s==PN_OK)s=margin_stepper(font,frame,"背景右边距",u->draft.bg_margin_right,352,880);
    if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,frame,"步进 4px · 长按连调 · 可先点整页预览查看效果",32,1034,620,PN_ALIGN_CENTER,PN_UI_MUTED,false);}
    if(s==PN_OK && u->notice){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,frame,u->notice,32,APPLY_Y-14,620,PN_ALIGN_CENTER,PN_UI_INK,true);}
    if(s==PN_OK)s=pn_font_size(font,30);
    if(s==PN_OK)s=pn_w_button(font,frame,"整页预览",32,APPLY_Y,300,APPLY_H,0u);
    if(s==PN_OK)s=pn_w_button(font,frame,"应用",352,APPLY_Y,300,APPLY_H,PN_W_SELECTED);
    return s;
}
static pn_status_t paint_backgrounds(pn_style_ui_t *u,pn_font_t *font,pn_frame_t *frame){
    pn_status_t s=pn_w_header(font,frame,"< 返回","阅读背景",NULL);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,"仅修改草稿，应用后保存",32,164,620,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    for(unsigned i=0;i<=PN_READING_BACKGROUND_COUNT && s==PN_OK;i++){
        unsigned id=pn_reading_background_id(i);
        int x=32+(int)(i%3)*212,y=190+(int)(i/3)*286;bool chosen=u->draft.background!=PN_READING_BACKGROUND_EXTERNAL && pn_reading_background_resolve(u->draft.background)==id;
        s=pn_reading_background_rect(id,frame,x+36,y+12,124,220);
        pn_w_round_stroke(frame,x,y,196,266,10,chosen?3:1,chosen?PN_UI_INK:PN_UI_RULE);
        if(s==PN_OK)s=pn_font_size(font,22);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,pn_reading_background_name(id),x+6,y+252,184,PN_ALIGN_CENTER,PN_UI_INK,chosen);
    }
    pn_background_gallery_paint(&u->gallery,&u->draft,frame);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK && !pn_background_gallery_count(&u->gallery))s=pn_w_text_ex(font,frame,"上传的阅读背景会显示在这里",32,556,620,PN_ALIGN_CENTER,PN_UI_MUTED,false);
    if(s==PN_OK)s=pn_w_button(font,frame,"上一页",32,1104,196,80,pn_background_gallery_can_page(&u->gallery,true)?0:PN_W_DISABLED);
    if(s==PN_OK)s=pn_w_button(font,frame,"背景边距",244,1104,196,80,0u);
    if(s==PN_OK)s=pn_w_button(font,frame,"下一页",456,1104,196,80,pn_background_gallery_can_page(&u->gallery,false)?0:PN_W_DISABLED);
    return s;
}
pn_status_t pn_style_ui_render(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    (void)metadata;pn_style_ui_t *u=ctx;if(!live(u) || !u->active || !font || !font->impl || !frame || !frame->pixels)return PN_INVALID;pn_frame_clear(frame,PN_UI_PAPER);
    int original=font->pixels;
    if(u->margins){pn_status_t s=paint_margins(u,font,frame);pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;}
    if(u->backgrounds){pn_status_t s=paint_backgrounds(u,font,frame);pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;}
    pn_status_t s=pn_w_header(font,frame,"< 返回","排版设置",NULL);
    if(s==PN_OK && !u->more){s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text_ex(font,frame,"恢复默认",480,100,172,PN_ALIGN_RIGHT,PN_UI_MUTED,false);}
    if(s==PN_OK)s=paint_main(u,font,frame);
    // 底部：左侧“使用全局”（清除本书特配）、右侧深色主按钮“应用到本书”（x=300仍命中应用以兼容既有测试）。
    // Bottom bar: left "Use Global" (clear book override), right primary "Apply to Book" (x=300 stays inside Apply).
    if(s==PN_OK)s=pn_font_size(font,30);
    if(s==PN_OK)s=pn_w_button(font,frame,"使用全局",PN_UI_MARGIN,APPLY_Y,244,APPLY_H,0u);
    if(s==PN_OK)s=pn_w_button(font,frame,"应用到本书",292,APPLY_Y,360,APPLY_H,PN_W_SELECTED);
    if(s==PN_OK && u->notice){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,frame,u->notice,PN_UI_MARGIN,APPLY_Y-14,620,PN_ALIGN_CENTER,PN_UI_INK,true);}
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
pn_status_t pn_style_ui_present(pn_style_ui_t *u,pn_reader_present_fn present,void *ctx){
    if(!u || !u->active || !live(u))return PN_INVALID;
    u->preview=false;u->presented=false;pn_status_t status=overlay(u,pn_style_ui_render,present,ctx);u->presented=status==PN_OK;return status;
}
pn_status_t pn_style_ui_open(pn_style_ui_t *u,pn_reader_app_t *reader,pn_reader_present_fn present,void *ctx){
    if(!u || !reader || !reader->impl)return PN_INVALID;
    memset(u,0,sizeof *u);u->reader=reader;pn_status_t status=pn_reader_app_style_get(reader,&u->draft);if(status!=PN_OK)return status;u->active=true;return pn_style_ui_present(u,present,ctx);
}
// 与字体和壁纸共用静态等待视觉；不修改阅读像素或校验流程。/ Share static busy visuals with fonts and wallpaper without changing reading pixels or validation.
static pn_status_t busy_paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    (void)ctx;(void)metadata;pn_frame_clear(frame,PN_UI_PAPER);
    pn_status_t s=pn_w_header(font,frame,"","排版设置",NULL);
    return s==PN_OK?pn_w_loading(font,frame,"正在重新排版","正在准备阅读页面，请稍候",-1,-1):s;
}
pn_status_t pn_style_ui_event(pn_style_ui_t *u,int command,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(!u || !u->active || !live(u))return PN_INVALID;
    if(!u->presented && command!=PN_SUI_RETRY)return PN_BUSY;
    if(command==PN_SUI_FORM)u->more=false;
    if(command==PN_SUI_RETRY || command==PN_SUI_FORM)return pn_style_ui_present(u,present,ctx);
    pn_status_t status=PN_OK;u->notice=NULL;
    if(!u->backgrounds && !u->margins && (command==PN_SUI_PREVIEW || command==PN_SUI_APPLY || command==PN_SUI_USE_GLOBAL || (command==PN_SUI_CANCEL && u->did_preview))){
        u->presented=false;status=overlay(u,busy_paint,present,ctx);
        if(status!=PN_OK){(void)pn_style_ui_present(u,present,ctx);return status;}
    }
    if(u->margins){
        if(command==PN_SUI_MARGINS_BACK || command==PN_SUI_CANCEL){
            u->margins=false;
            if(!u->margins_from_bg){u->backgrounds=false;pn_background_gallery_close(&u->gallery);}
            u->margins_from_bg=false;
            return pn_style_ui_present(u,present,ctx);
        }
        if(command==PN_SUI_MARGINS_RESET){
            u->draft.hide_bars=0;u->draft.margin_top=0;u->draft.margin_bottom=0;pn_style_set_margins_lr(&u->draft,32);
            u->draft.bg_margin_top=0;u->draft.bg_margin_bottom=0;u->draft.bg_margin_left=0;u->draft.bg_margin_right=0;
            return pn_style_ui_present(u,present,ctx);
        }
        if(command==PN_SUI_TOGGLE_BARS){
            pn_style_t next=u->draft;next.hide_bars=next.hide_bars?0:1;
            if(pn_style_validate(&next)==PN_OK)u->draft=next;else status=PN_EMPTY;
            return pn_style_ui_present(u,present,ctx);
        }
        if(command>=PN_SUI_MARGIN_ADJ && command<PN_SUI_MARGIN_ADJ+16){
            unsigned item=(unsigned)(command-PN_SUI_MARGIN_ADJ)/2;bool up=((command-PN_SUI_MARGIN_ADJ)&1)!=0;
            int delta=up?4:-4;pn_style_t next=u->draft;
            int val=0;
            switch(item){
                case 0:val=pn_style_margin_top(&next)+delta;if(val<0 || val>200)status=PN_EMPTY;else next.margin_top=(uint16_t)val;break;
                case 1:val=pn_style_margin_bottom(&next)+delta;if(val<0 || val>200)status=PN_EMPTY;else next.margin_bottom=(uint16_t)val;break;
                case 2:val=pn_style_margin_left(&next)+delta;if(val<0 || val>160)status=PN_EMPTY;else pn_style_set_margin_left(&next,val);break;
                case 3:val=pn_style_margin_right(&next)+delta;if(val<0 || val>160)status=PN_EMPTY;else pn_style_set_margin_right(&next,val);break;
                case 4:val=(int)next.bg_margin_top+delta;if(val<0 || val>200)status=PN_EMPTY;else next.bg_margin_top=(uint16_t)val;break;
                case 5:val=(int)next.bg_margin_bottom+delta;if(val<0 || val>200)status=PN_EMPTY;else next.bg_margin_bottom=(uint16_t)val;break;
                case 6:val=(int)next.bg_margin_left+delta;if(val<0 || val>160)status=PN_EMPTY;else next.bg_margin_left=(uint16_t)val;break;
                default:val=(int)next.bg_margin_right+delta;if(val<0 || val>160)status=PN_EMPTY;else next.bg_margin_right=(uint16_t)val;break;
            }
            if(status==PN_OK && pn_style_validate(&next)==PN_OK)u->draft=next;else status=PN_EMPTY;
            pn_status_t drawn=pn_style_ui_present(u,present,ctx);return drawn!=PN_OK?drawn:status;
        }
        if(command==PN_SUI_PREVIEW || command==PN_SUI_APPLY){
            u->presented=false;status=overlay(u,busy_paint,present,ctx);
            if(status!=PN_OK){(void)pn_style_ui_present(u,present,ctx);return status;}
            if(command==PN_SUI_PREVIEW){status=preview(u,now,present,ctx);if(confirmed(u)){u->preview=true;u->did_preview=true;u->presented=true;return status;}}
            else{status=apply(u,now,present,ctx);u->application_failed=status!=PN_OK && confirmed(u);if(status==PN_OK){pn_background_gallery_close(&u->gallery);u->active=false;return status;}}
            if(status!=PN_OK && status!=PN_EMPTY)u->notice="操作失败，草稿保留";
            pn_status_t drawn=pn_style_ui_present(u,present,ctx);return drawn!=PN_OK?drawn:status;
        }
        return PN_EMPTY;
    }
    if(u->backgrounds){
        if(command==PN_SUI_MARGINS){u->margins=true;u->margins_from_bg=true;return pn_style_ui_present(u,present,ctx);}
        if(command==PN_SUI_BACKGROUND_BACK || command==PN_SUI_CANCEL)u->backgrounds=false;
        else if(command>=PN_SUI_BACKGROUND_ITEM && command<=PN_SUI_BACKGROUND_ITEM+PN_READING_BACKGROUND_LEGACY_MAX && pn_reading_background_resolve((unsigned)(command-PN_SUI_BACKGROUND_ITEM))==(unsigned)(command-PN_SUI_BACKGROUND_ITEM)){field_set(&u->draft,11,(unsigned)(command-PN_SUI_BACKGROUND_ITEM));u->backgrounds=false;u->selected=11;}
        else if(command==PN_SUI_BACKGROUND_PREVIOUS || command==PN_SUI_BACKGROUND_NEXT){bool previous=command==PN_SUI_BACKGROUND_PREVIOUS;if(!pn_background_gallery_can_page(&u->gallery,previous))return PN_EMPTY;status=pn_background_gallery_page(&u->gallery,previous);}
        else if(command>=PN_SUI_BACKGROUND_EXTERNAL && command<PN_SUI_BACKGROUND_EXTERNAL+6){
            u->presented=false;status=overlay(u,busy_paint,present,ctx);
            if(status==PN_OK)status=pn_background_gallery_choose(&u->gallery,(unsigned)(command-PN_SUI_BACKGROUND_EXTERNAL),&u->draft);
            if(status==PN_OK){u->backgrounds=false;u->selected=11;}else u->notice="背景未能读取，请重试";
        }else return PN_EMPTY;
        if(!u->backgrounds)pn_background_gallery_close(&u->gallery);
        return pn_style_ui_present(u,present,ctx);
    }
    if(command==PN_SUI_MARGINS){u->margins=true;u->margins_from_bg=false;return pn_style_ui_present(u,present,ctx);}
    if(command==PN_SUI_TOGGLE_BARS){
        pn_style_t next=u->draft;next.hide_bars=next.hide_bars?0:1;
        if(pn_style_validate(&next)==PN_OK)u->draft=next;else status=PN_EMPTY;
        pn_status_t drawn=pn_style_ui_present(u,present,ctx);return drawn!=PN_OK?drawn:status;
    }
    if(command==PN_SUI_BACKGROUND){
        pn_pool_t *pool=NULL;pn_media_t *media=NULL;status=u->epub?pn_epub_app_paper_context(u->epub,&pool,&media):pn_reader_app_paper_context(u->reader,&pool,&media);
        if(status==PN_OK)status=pn_background_gallery_open(&u->gallery,pool,media);
        if(status!=PN_OK){u->notice="图库暂不可用，请重试";return pn_style_ui_present(u,present,ctx);}
        u->backgrounds=true;return pn_style_ui_present(u,present,ctx);
    }
    if(command==PN_SUI_FONTS){if(u->application_failed){u->notice="请先应用或取消排版";(void)pn_style_ui_present(u,present,ctx);return PN_BUSY;}if(u->did_preview){status=cancel(u,now,present,ctx);if(status!=PN_OK)return status;u->did_preview=false;}u->request_fonts=true;return PN_OK;}
    if(command>=PN_SUI_FIELD && command<PN_SUI_FIELD+PN_SUI_FIELDS*2){
        static const unsigned steps[]={2,5,5,1,5,2,1,2,1,1,1,1,1},minimum[]={28,100,0,0,0,16,0,0,0,0,0,0,0},maximum[]={72,220,100,2,50,80,2,16,3,4,3,8,1};
        unsigned field=(unsigned)(command-PN_SUI_FIELD)/2;bool up=(command-PN_SUI_FIELD)&1;unsigned v=field_value(&u->draft,field);u->selected=field;u->more=false;
        // 页边距只有窄/中/宽三档。/ Margins have just three levels: narrow, medium and wide.
        // 左右切换的档位项（边距、阅读线、深浅、字重、清晰度）首尾循环，不在两端卡住；数值项仍在上下限停住。
        // Option fields switched with left/right (margin, reading line, shade, weight, sharpness) wrap around instead of sticking at either end; numeric fields still stop at their limits.
        if(field==5){unsigned level=v<=20?0u:v<=32?1u:2u;static const unsigned margins[3]={20,32,48};level=up?(level+1u)%3u:(level+2u)%3u;pn_style_set_margins_lr(&u->draft,(int)margins[level]);}
        else if(field==11){unsigned slot=0;while(slot<PN_READING_BACKGROUND_COUNT && pn_reading_background_id(slot)!=pn_reading_background_resolve(v))slot++;slot=up?(slot+1u)%(PN_READING_BACKGROUND_COUNT+1u):(slot+PN_READING_BACKGROUND_COUNT)%(PN_READING_BACKGROUND_COUNT+1u);field_set(&u->draft,field,pn_reading_background_id(slot));}
        else if(field==6 || field==8 || field==9 || field==10 || field==12){unsigned span=maximum[field]-minimum[field]+1u,offset=v-minimum[field];offset=up?(offset+1u)%span:(offset+span-1u)%span;field_set(&u->draft,field,minimum[field]+offset);}
        else{
        if(up && v+steps[field]<=maximum[field])v+=steps[field];else if(!up && v>=minimum[field]+steps[field])v-=steps[field];else status=PN_EMPTY;
        field_set(&u->draft,field,v);}
    }else if(command==PN_SUI_INDENT){u->draft.indent_em=u->draft.indent_em?0:2;u->selected=3;
    }else if(command==PN_SUI_MORE){u->more=false;u->selected=4;
    }else if(command==PN_SUI_BACK_MAIN){u->more=false;u->selected=0;
    }else if(command==PN_SUI_RESET){u->draft=pn_style_default(PN_STYLE_DEFAULT_PIXELS);
    }else if(command==PN_SUI_USE_GLOBAL){
        status=u->epub?pn_epub_app_style_use_global(u->epub,now,present,ctx):pn_reader_app_style_use_global(u->reader,now,present,ctx);
        if(status==PN_OK){u->active=false;return status;}
        u->application_failed=confirmed(u);
    }else if(command==PN_SUI_SAVE_GLOBAL){
        status=u->epub?pn_epub_app_style_save_global(u->epub,&u->draft):pn_reader_app_style_save_global(u->reader,&u->draft);
        u->notice=status==PN_OK?"已设为全局默认（新书自动生效）":status==PN_UNSUPPORTED?"无状态目录，无法保存全局默认":"保存全局默认失败，请重试";
        pn_status_t drawn=pn_style_ui_present(u,present,ctx);return drawn!=PN_OK?drawn:status;
    }else if(command>=PN_SUI_PRESET && command<PN_SUI_PRESET+3){
        const preset_t *chosen=&presets[command-PN_SUI_PRESET];
        u->draft.pixels=(uint16_t)chosen->pixels;u->draft.line_percent=(uint16_t)chosen->line;u->draft.gap_percent=(uint16_t)chosen->gap;u->draft.indent_em=(uint16_t)chosen->indent;pn_style_set_margins_lr(&u->draft,(int)chosen->margin);u->draft.tracking_percent=0;
    }else if(command==PN_SUI_PREVIEW){status=preview(u,now,present,ctx);if(confirmed(u)){u->preview=true;u->did_preview=true;u->presented=true;return status;}}
    else if(command==PN_SUI_APPLY){status=apply(u,now,present,ctx);u->application_failed=status!=PN_OK && confirmed(u);if(status==PN_OK){u->active=false;return status;}}
    else if(command==PN_SUI_CANCEL){status=cancel(u,now,present,ctx);if(status==PN_OK){u->active=false;return status;}}
    else status=PN_INVALID;
    if(status!=PN_OK && status!=PN_EMPTY)u->notice="操作失败，草稿保留";
    pn_status_t drawn=pn_style_ui_present(u,present,ctx);return drawn!=PN_OK?drawn:status;
}
int pn_style_ui_hit(const pn_style_ui_t *u,int x,int y){
    if(!u || !u->active || x<0 || x>=684 || y<0 || y>=1216)return -1;
    if(u->preview)return PN_SUI_FORM;
    if(u->margins){
        if(y<PN_W_HEADER_H){if(pn_w_header_hit(x,y,false)==1)return PN_SUI_MARGINS_BACK;return x>=480 && y>=44?PN_SUI_MARGINS_RESET:-1;}
        if(y>=404 && y<476 && x>=32 && x<652)return PN_SUI_TOGGLE_BARS;
        if(y>=APPLY_Y && y<APPLY_Y+APPLY_H){if(x>=32 && x<332)return PN_SUI_PREVIEW;if(x>=352 && x<652)return PN_SUI_APPLY;return -1;}
        static const int tops[4]={488,606,762,880};
        for(unsigned row=0;row<4;row++){
            int top=tops[row];
            if(y>=top && y<top+MCELL_H){
                for(unsigned col=0;col<2;col++){
                    int left=32+(int)col*CELL_PITCH;unsigned item=row*2+col;
                    if(x>=left && x<left+MHIT_W)return PN_SUI_MARGIN_ADJ+(int)item*2;
                    if(x>=left+CELL_W-MHIT_W && x<left+CELL_W)return PN_SUI_MARGIN_ADJ+(int)item*2+1;
                }
            }
        }
        return -1;
    }
    if(u->backgrounds){
        if(pn_w_header_hit(x,y,false)==1)return PN_SUI_BACKGROUND_BACK;
        if(x>=32 && x<652 && y>=1104 && y<1184){
            if(x<228)return pn_background_gallery_can_page(&u->gallery,true)?PN_SUI_BACKGROUND_PREVIOUS:-1;
            if(x>=244 && x<440)return PN_SUI_MARGINS;
            if(x>=456)return pn_background_gallery_can_page(&u->gallery,false)?PN_SUI_BACKGROUND_NEXT:-1;
            return -1;
        }
        if(x<32 || x>=652 || y<190 || y>=190+3*286)return -1;
        int col=(x-32)/212,row=(y-190)/286;
        if((x-32)%212>=196 || (y-190)%286>=266)return -1;
        unsigned slot=(unsigned)(row*3+col);
        if(slot<=PN_READING_BACKGROUND_COUNT)return PN_SUI_BACKGROUND_ITEM+(int)pn_reading_background_id(slot);
        return slot-3<pn_background_gallery_count(&u->gallery)?PN_SUI_BACKGROUND_EXTERNAL+(int)slot-3:-1;
    }
    if(y<PN_W_HEADER_H){if(pn_w_header_hit(x,y,false)==1)return PN_SUI_CANCEL;return x>=480 && y>=44?PN_SUI_RESET:-1;}
    if(y>=APPLY_Y && y<APPLY_Y+APPLY_H){if(x>=32 && x<280)return PN_SUI_USE_GLOBAL;if(x>=288 && x<652)return PN_SUI_APPLY;return -1;}
    if(y>=PREVIEW_Y && y<PREVIEW_Y+PREVIEW_H && x>=32 && x<652)return PN_SUI_PREVIEW;
    if(y>=FONT_ROW_Y && y<FONT_ROW_Y+74 && x>=32 && x<652)return PN_SUI_FONTS;
    if(y>=1028 && y<1100){if(x>=32 && x<332)return PN_SUI_FIELD+24;if(x>=352 && x<652)return PN_SUI_TOGGLE_BARS;return -1;}
    for(unsigned i=0;i<PN_SUI_FIELDS-1;i++){
        int left=32+(int)(i%2)*CELL_PITCH,top=STEP_Y0+(int)(i/2)*STEP_PITCH;
        if(fields[i]==11 && x>=left && x<left+CELL_W && y>=top && y<top+CELL_H)return PN_SUI_BACKGROUND;
        if(y>=top+26 && y<top+CELL_H){if(x>=left && x<left+68)return PN_SUI_FIELD+(int)fields[i]*2;if(x>=left+CELL_W-68 && x<left+CELL_W)return PN_SUI_FIELD+(int)fields[i]*2+1;}
        if(fields[i]==5 && x>=left && x<left+CELL_W && y>=top && y<top+CELL_H)return PN_SUI_MARGINS;
    }
    int preset=pn_w_segments_hit(3,PRESET_Y,PRESET_H,x,y);return preset>=0?PN_SUI_PRESET+preset:-1;
}
void pn_style_ui_close(pn_style_ui_t *u){if(u){pn_background_gallery_close(&u->gallery);memset(u,0,sizeof *u);}}
pn_status_t pn_style_ui_tick(pn_style_ui_t *u,pn_reader_present_fn present,void *ctx){if(!u || !u->active || !u->backgrounds || !u->presented)return PN_EMPTY;pn_status_t s=pn_background_gallery_tick(&u->gallery);return s==PN_OK?pn_style_ui_present(u,present,ctx):s;}

pn_status_t pn_style_ui_open_epub(pn_style_ui_t *u,pn_epub_app_t *reader,pn_reader_present_fn present,void *ctx){
    if(!u || !reader || !reader->impl)return PN_INVALID;
    memset(u,0,sizeof *u);u->epub=reader;pn_status_t status=pn_epub_app_style_get(reader,&u->draft);if(status!=PN_OK)return status;u->active=true;return pn_style_ui_present(u,present,ctx);
}
