/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：七项排版草稿、正文预览与应用/取消；绘制不写配置。
 * English: seven-field typesetting drafts, body preview and apply/cancel; painting never writes configuration.
 * 冻结：变化先进入草稿；写入失败不关闭界面。
 * Frozen: changes enter drafts first; write failures never dismiss the UI.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_style_ui.h"
#include <stdio.h>
#include <string.h>
static bool live(const pn_style_ui_t *u){return u && ((u->reader && u->reader->impl) || (u->epub && u->epub->impl));}
static bool confirmed(const pn_style_ui_t *u){return u->epub?pn_epub_app_last_confirmed(u->epub):pn_reader_app_last_confirmed(u->reader);}
static pn_status_t overlay(pn_style_ui_t *u,pn_reader_overlay_fn paint,pn_reader_present_fn present,void *ctx){return u->epub?pn_epub_app_overlay(u->epub,paint,u,present,ctx,PN_REFRESH_GC16):pn_reader_app_overlay(u->reader,paint,u,present,ctx,PN_REFRESH_GC16);}
static pn_status_t preview(pn_style_ui_t *u,uint64_t now,pn_reader_present_fn present,void *ctx){return u->epub?pn_epub_app_style_preview(u->epub,&u->draft,now,present,ctx):pn_reader_app_style_preview(u->reader,&u->draft,now,present,ctx);}
static pn_status_t apply(pn_style_ui_t *u,uint64_t now,pn_reader_present_fn present,void *ctx){return u->epub?pn_epub_app_style_apply(u->epub,&u->draft,now,present,ctx):pn_reader_app_style_apply(u->reader,&u->draft,now,present,ctx);}
static pn_status_t cancel(pn_style_ui_t *u,uint64_t now,pn_reader_present_fn present,void *ctx){return u->epub?pn_epub_app_style_cancel(u->epub,now,present,ctx):pn_reader_app_style_cancel(u->reader,now,present,ctx);}
#include "pn_widgets.h"
/* 版式（docs/UI_UX.md第5节）：顶栏、段落预览、字体行、四个步进行、更多入口、三个预设、底部链接。
 * Layout (docs/UI_UX.md section 5): header, paragraph preview, font row, four steppers, a "more" entry, three presets and bottom links. */
#define HEADER_H 128
#define PREVIEW_Y 140
#define PREVIEW_H 228
#define MODE_BASE 404
#define PRESET_Y 416
#define PRESET_H 64
#define FONT_ROW_Y 494
#define STEP_Y0 576
#define STEP_PITCH 84
#define MARGIN_ROW_Y (STEP_Y0+3*STEP_PITCH)
#define INDENT_ROW_Y (STEP_Y0+4*STEP_PITCH)
#define MORE_Y (STEP_Y0+5*STEP_PITCH)
#define APPLY_Y 1112
#define APPLY_H 88
#define LINKS_Y 1084
#define BOX_X 300
#define BOX_W 352
#define MORE_STEP_Y0 156
static const char *const names[PN_SUI_FIELDS]={"字号","行距","段距","首行缩进","字间距","边距","清残影"};
typedef struct {unsigned pixels,line,gap,indent,margin;} preset_t;
static const preset_t presets[3]={{44,145,40,2,32},{36,125,20,2,24},{56,160,40,2,32}};
static const char *const preset_names[3]={"舒适","紧凑","大字"};
static unsigned field_value(const pn_style_t *s,unsigned i){switch(i){case 0:return s->pixels;case 1:return s->line_percent;case 2:return s->gap_percent;case 3:return s->indent_em;case 4:return s->tracking_percent;case 5:return s->margin;default:return s->gl_before_clear;}}
static void field_set(pn_style_t *s,unsigned i,unsigned v){switch(i){case 0:s->pixels=(uint16_t)v;break;case 1:s->line_percent=(uint16_t)v;break;case 2:s->gap_percent=(uint16_t)v;break;case 3:s->indent_em=(uint16_t)v;break;case 4:s->tracking_percent=(uint16_t)v;break;case 5:s->margin=(uint16_t)v;break;default:s->gl_before_clear=(uint16_t)v;}}
static void field_text(const pn_style_t *s,unsigned i,char *out,size_t cap){
    unsigned v=field_value(s,i);
    if(i==6)snprintf(out,cap,v?"每 %u 页":"每页",v+1);
    else if(i==1 || i==2)snprintf(out,cap,"%u.%02u",v/100,v%100);
    else if(i==3)snprintf(out,cap,"%u 字",v);
    else if(i==4)snprintf(out,cap,"%u%%",v);
    else if(i==5)snprintf(out,cap,v<=20?"窄":v<=32?"中":"宽");
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
    snprintf(out,cap,"默认字体");
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
}
/* 步进行：左侧名称，右侧圆角框内“- 值 +”。/ Stepper row: name on the left, "- value +" inside a rounded box on the right. */
static pn_status_t stepper(pn_font_t *font,pn_frame_t *frame,const pn_style_t *draft,unsigned field,int y,bool selected){
    char value[40];field_text(draft,field,value,sizeof value);
    pn_status_t s=pn_font_size(font,34);
    if(s==PN_OK)s=pn_w_text(font,frame,names[field],PN_UI_MARGIN,y+52,230,PN_ALIGN_LEFT);
    pn_w_round_outline(frame,BOX_X,y+8,BOX_W,64,PN_UI_RADIUS,selected?3:2,PN_UI_INK);
    // 减/加用线条图标，中间值两侧各一条细分隔。/ Minus and plus are line icons with a hairline divider on each side of the value.
    pn_w_icon(frame,PN_ICON_MINUS,BOX_X+30,y+26,28,PN_UI_INK);pn_w_icon(frame,PN_ICON_PLUS,BOX_X+BOX_W-58,y+26,28,PN_UI_INK);
    pn_frame_rect(frame,BOX_X+88,y+20,1,40,10);pn_frame_rect(frame,BOX_X+BOX_W-88,y+20,1,40,10);
    if(s==PN_OK)s=pn_font_size(font,32);
    if(s==PN_OK)s=pn_w_text(font,frame,value,BOX_X+88,y+52,BOX_W-176,PN_ALIGN_CENTER);
    return s;
}
/* 用草稿的字号/行距/缩进/字距试排一段，让数值变化立刻可见；正文字体不可用时用UI字体。
 * Typeset a sample paragraph with the draft's size, spacing, indent and tracking so changes are visible at once; the UI font is used when the body font is unavailable. */
static pn_status_t sample(pn_style_ui_t *u,pn_font_t *ui,pn_frame_t *frame){
    pn_w_round_stroke(frame,32,PREVIEW_Y,620,PREVIEW_H,PN_UI_RADIUS,2.0f,8);
    pn_status_t s=pn_font_size(ui,26);
    if(s==PN_OK)s=pn_w_text(ui,frame,"当前段落预览",52,PREVIEW_Y+40,560,PN_ALIGN_LEFT);
    if(s==PN_OK)s=pn_w_text(ui,frame,"轻点查看整页",52,PREVIEW_Y+40,560,PN_ALIGN_RIGHT);
    pn_font_t *body=body_font(u);pn_font_t *face=body?body:ui;int saved=face->pixels;
    int pixels=(int)u->draft.pixels;if(pixels>56)pixels=56;
    if(s==PN_OK)s=pn_font_size(face,pixels);
    int pitch=pixels*(int)u->draft.line_percent/100;
    unsigned room=(unsigned)((PREVIEW_H-60)/pitch);if(room<1)room=1;if(room>4)room=4;
    if(s==PN_OK)s=pn_w_text_flow(face,frame,"字与字之间，留出恰好的距离。让阅读回到舒适，把时间留给文字。",52,PREVIEW_Y+60+pixels,556,room,pitch,pixels*(int)u->draft.indent_em,pixels*64*(int)u->draft.tracking_percent/100,NULL);
    pn_status_t restored=pn_font_size(face,saved);
    return s==PN_OK?restored:s;
}
static pn_status_t paint_main(pn_style_ui_t *u,pn_font_t *font,pn_frame_t *frame){
    pn_status_t s=sample(u,font,frame);
    // 排版模式：三段分段控件；手动调过的组合标“自定义”。/ Typesetting modes as a three-way segmented control; hand-tuned combinations are marked "custom".
    int chosen=selected_preset(&u->draft);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,"排版模式",PN_UI_MARGIN+8,MODE_BASE,300,PN_ALIGN_LEFT,PN_UI_MUTED,true);
    if(s==PN_OK && chosen<0)s=pn_w_text_ex(font,frame,"自定义",352,MODE_BASE,300,PN_ALIGN_RIGHT,PN_UI_INK,true);
    if(s==PN_OK)s=pn_w_segments(font,frame,preset_names,NULL,3,chosen,PRESET_Y,PRESET_H);
    // 字体行。/ Font row.
    char name[PN_FONT_NAME_MAX+8];font_name(u,name,sizeof name);
    if(s==PN_OK)s=pn_font_size(font,32);
    if(s==PN_OK)s=pn_w_text(font,frame,"字体",PN_UI_MARGIN,FONT_ROW_Y+52,200,PN_ALIGN_LEFT);
    pn_font_t *body=body_font(u);
    pn_w_set_fallback(body);
    if(s==PN_OK)s=pn_font_size(font,28);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,name,200,FONT_ROW_Y+52,390,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
    pn_w_set_fallback(NULL);
    pn_w_icon(frame,PN_ICON_CHEVRON,PN_UI_WIDTH-PN_UI_MARGIN-28,FONT_ROW_Y+28,28,PN_UI_INK);
    pn_frame_rect(frame,PN_UI_MARGIN,FONT_ROW_Y+81,620,1,PN_UI_SELECT);
    // 字号、行距、段距、页边距四个步进行，首行缩进为开关。/ Size, line, gap and margin steppers; the first-line indent is a toggle.
    static const unsigned rows[4]={0,1,2,5};
    for(unsigned i=0;i<4 && s==PN_OK;i++){s=stepper(font,frame,&u->draft,rows[i],STEP_Y0+(int)i*STEP_PITCH,u->selected==rows[i]);pn_frame_rect(frame,PN_UI_MARGIN,STEP_Y0+(int)i*STEP_PITCH+81,620,1,PN_UI_SELECT);}
    if(s==PN_OK)s=pn_font_size(font,32);
    if(s==PN_OK)s=pn_w_text(font,frame,"首行缩进",PN_UI_MARGIN,INDENT_ROW_Y+52,300,PN_ALIGN_LEFT);
    pn_w_toggle(frame,PN_UI_WIDTH-PN_UI_MARGIN-64,INDENT_ROW_Y+24,u->draft.indent_em>0);
    pn_frame_rect(frame,PN_UI_MARGIN,INDENT_ROW_Y+81,620,1,PN_UI_SELECT);
    // 更多入口与固定“应用”。/ The "more" entry and the fixed Apply button.
    if(s==PN_OK)s=pn_font_size(font,32);
    if(s==PN_OK)s=pn_w_text(font,frame,"字距与清残影",PN_UI_MARGIN,MORE_Y+52,400,PN_ALIGN_LEFT);
    pn_w_icon(frame,PN_ICON_CHEVRON,PN_UI_WIDTH-PN_UI_MARGIN-28,MORE_Y+28,28,PN_UI_INK);
    return s;
}
static pn_status_t paint_more(pn_style_ui_t *u,pn_font_t *font,pn_frame_t *frame){
    pn_status_t s=PN_OK;static const unsigned rows[2]={4,6};
    for(unsigned i=0;i<2 && s==PN_OK;i++)s=stepper(font,frame,&u->draft,rows[i],MORE_STEP_Y0+(int)i*STEP_PITCH,u->selected==rows[i]);
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_text_lines_ex(font,frame,"字距按字号的百分比计算。清残影是每隔几页做一次整屏刷新，数字越小越干净、闪屏越多。",PN_UI_MARGIN,MORE_STEP_Y0+2*STEP_PITCH+40,620,4,38,PN_UI_MUTED,false);
    return s;
}
static pn_status_t paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    (void)metadata;pn_style_ui_t *u=ctx;pn_frame_clear(frame,PN_UI_PAPER);
    int original=font->pixels;
    pn_status_t s=pn_w_header(font,frame,"< 返回",u->more?"字距与清残影":"排版设置",NULL);
    if(s==PN_OK && !u->more){s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text_ex(font,frame,"恢复默认",480,100,172,PN_ALIGN_RIGHT,PN_UI_MUTED,false);}
    if(s==PN_OK)s=u->more?paint_more(u,font,frame):paint_main(u,font,frame);
    // 底部固定的深色“应用”；取消即左上返回，“恢复默认”在右上。/ A fixed dark Apply at the bottom; cancelling is the Back at the top left and Restore defaults sits at the top right.
    if(s==PN_OK)s=pn_font_size(font,32);
    if(s==PN_OK)s=pn_w_button(font,frame,"应用",PN_UI_MARGIN,APPLY_Y,620,APPLY_H,PN_W_SELECTED);
    if(s==PN_OK && u->notice){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,frame,u->notice,PN_UI_MARGIN,APPLY_Y-14,620,PN_ALIGN_CENTER,PN_UI_INK,true);}
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
pn_status_t pn_style_ui_present(pn_style_ui_t *u,pn_reader_present_fn present,void *ctx){
    if(!u || !u->active || !live(u))return PN_INVALID;
    u->preview=false;u->presented=false;pn_status_t status=overlay(u,paint,present,ctx);u->presented=status==PN_OK;return status;
}
pn_status_t pn_style_ui_open(pn_style_ui_t *u,pn_reader_app_t *reader,pn_reader_present_fn present,void *ctx){
    if(!u || !reader || !reader->impl)return PN_INVALID;
    memset(u,0,sizeof *u);u->reader=reader;pn_status_t status=pn_reader_app_style_get(reader,&u->draft);if(status!=PN_OK)return status;u->active=true;return pn_style_ui_present(u,present,ctx);
}
pn_status_t pn_style_ui_event(pn_style_ui_t *u,int command,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(!u || !u->active || !live(u))return PN_INVALID;
    if(!u->presented && command!=PN_SUI_RETRY)return PN_BUSY;
    if(command==PN_SUI_FORM)u->more=u->selected==4 || u->selected==6;
    if(command==PN_SUI_RETRY || command==PN_SUI_FORM)return pn_style_ui_present(u,present,ctx);
    pn_status_t status=PN_OK;u->notice=NULL;
    if(command==PN_SUI_FONTS){if(u->application_failed){u->notice="请先应用或取消排版";(void)pn_style_ui_present(u,present,ctx);return PN_BUSY;}if(u->did_preview){status=cancel(u,now,present,ctx);if(status!=PN_OK)return status;u->did_preview=false;}u->request_fonts=true;return PN_OK;}
    if(command>=PN_SUI_FIELD && command<PN_SUI_FIELD+PN_SUI_FIELDS*2){
        static const unsigned steps[]={2,5,5,1,5,2,1},minimum[]={28,100,0,0,0,16,0},maximum[]={72,220,100,2,50,80,30};
        unsigned field=(unsigned)(command-PN_SUI_FIELD)/2;bool up=(command-PN_SUI_FIELD)&1;unsigned v=field_value(&u->draft,field);u->selected=field;u->more=field==4 || field==6;
        // 页边距只有窄/中/宽三档。/ Margins have just three levels: narrow, medium and wide.
        if(field==5){unsigned level=v<=20?0u:v<=32?1u:2u;static const unsigned margins[3]={20,32,48};if(up && level<2)level++;else if(!up && level>0)level--;else status=PN_EMPTY;field_set(&u->draft,5,margins[level]);}
        else{
        if(up && v+steps[field]<=maximum[field])v+=steps[field];else if(!up && v>=minimum[field]+steps[field])v-=steps[field];else status=PN_EMPTY;
        field_set(&u->draft,field,v);}
    }else if(command==PN_SUI_INDENT){u->draft.indent_em=u->draft.indent_em?0:2;u->selected=3;
    }else if(command==PN_SUI_MORE){u->more=true;u->selected=4;
    }else if(command==PN_SUI_BACK_MAIN){u->more=false;u->selected=0;
    }else if(command==PN_SUI_RESET){u->draft=pn_style_default(44);
    }else if(command>=PN_SUI_PRESET && command<PN_SUI_PRESET+3){
        const preset_t *chosen=&presets[command-PN_SUI_PRESET];
        u->draft.pixels=(uint16_t)chosen->pixels;u->draft.line_percent=(uint16_t)chosen->line;u->draft.gap_percent=(uint16_t)chosen->gap;u->draft.indent_em=(uint16_t)chosen->indent;u->draft.margin=(uint16_t)chosen->margin;u->draft.tracking_percent=0;
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
    if(y<PN_W_HEADER_H){if(pn_w_header_hit(x,y,false)==1)return u->more?PN_SUI_BACK_MAIN:PN_SUI_CANCEL;return !u->more && x>=480 && y>=44?PN_SUI_RESET:-1;}
    if(y>=APPLY_Y && y<APPLY_Y+APPLY_H)return x>=PN_UI_MARGIN && x<652?PN_SUI_APPLY:-1;
    if(!u->more && y>=PREVIEW_Y && y<PREVIEW_Y+PREVIEW_H && x>=32 && x<652)return PN_SUI_PREVIEW;
    static const unsigned main_rows[4]={0,1,2,5},more_rows[2]={4,6};
    const unsigned *rows=u->more?more_rows:main_rows;unsigned count=u->more?2:4;int top=u->more?MORE_STEP_Y0:STEP_Y0;
    for(unsigned i=0;i<count;i++){
        int row=top+(int)i*STEP_PITCH;
        if(y>=row && y<row+STEP_PITCH){if(x>=BOX_X && x<BOX_X+110)return PN_SUI_FIELD+(int)rows[i]*2;if(x>=BOX_X+BOX_W-110 && x<BOX_X+BOX_W)return PN_SUI_FIELD+(int)rows[i]*2+1;return -1;}
    }
    if(!u->more){
        if(y>=FONT_ROW_Y && y<FONT_ROW_Y+82)return PN_SUI_FONTS;
        if(y>=INDENT_ROW_Y && y<INDENT_ROW_Y+STEP_PITCH)return PN_SUI_INDENT;
        if(y>=MORE_Y && y<MORE_Y+STEP_PITCH)return PN_SUI_MORE;
        {int preset=pn_w_segments_hit(3,PRESET_Y-10,PRESET_H+20,x,y);if(preset>=0)return PN_SUI_PRESET+preset;}
    }
    return -1;
}
void pn_style_ui_close(pn_style_ui_t *u){if(u)memset(u,0,sizeof *u);}

pn_status_t pn_style_ui_open_epub(pn_style_ui_t *u,pn_epub_app_t *reader,pn_reader_present_fn present,void *ctx){
    if(!u || !reader || !reader->impl)return PN_INVALID;
    memset(u,0,sizeof *u);u->epub=reader;pn_status_t status=pn_epub_app_style_get(reader,&u->draft);if(status!=PN_OK)return status;u->active=true;return pn_style_ui_present(u,present,ctx);
}
