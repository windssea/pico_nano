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
#define PREVIEW_H 260
#define FONT_ROW_Y 416
#define STEP_Y0 512
#define STEP_PITCH 88
#define MORE_Y 872
#define PRESET_Y 968
#define LINKS_Y 1084
#define BOX_X 288
#define BOX_W 364
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
    if(s==PN_OK)s=pn_w_text(font,frame,names[field],PN_UI_MARGIN,y+54,230,PN_ALIGN_LEFT);
    pn_w_round_outline(frame,BOX_X,y,BOX_W,80,PN_UI_RADIUS,selected?4:2,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,40);
    if(s==PN_OK)s=pn_w_text(font,frame,"-",BOX_X,y+56,88,PN_ALIGN_CENTER);
    if(s==PN_OK)s=pn_w_text(font,frame,"+",BOX_X+BOX_W-88,y+56,88,PN_ALIGN_CENTER);
    if(s==PN_OK)s=pn_font_size(font,34);
    if(s==PN_OK)s=pn_w_text(font,frame,value,BOX_X+88,y+54,BOX_W-176,PN_ALIGN_CENTER);
    return s;
}
/* 用草稿的字号/行距/缩进/字距试排一段，让数值变化立刻可见；正文字体不可用时用UI字体。
 * Typeset a sample paragraph with the draft's size, spacing, indent and tracking so changes are visible at once; the UI font is used when the body font is unavailable. */
static pn_status_t sample(pn_style_ui_t *u,pn_font_t *ui,pn_frame_t *frame){
    pn_w_round_outline(frame,32,PREVIEW_Y,620,PREVIEW_H,PN_UI_RADIUS,2,PN_UI_INK);
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
    // 字体行。/ Font row.
    char name[PN_FONT_NAME_MAX+8];font_name(u,name,sizeof name);
    if(s==PN_OK)s=pn_font_size(font,34);
    if(s==PN_OK)s=pn_w_text(font,frame,"字体",PN_UI_MARGIN,FONT_ROW_Y+54,200,PN_ALIGN_LEFT);
    pn_font_t *body=body_font(u);
    pn_w_set_fallback(body);
    if(s==PN_OK)s=pn_w_text(font,frame,name,200,FONT_ROW_Y+54,410,PN_ALIGN_RIGHT);
    pn_w_set_fallback(NULL);
    if(s==PN_OK)s=pn_w_text(font,frame,">",PN_UI_MARGIN,FONT_ROW_Y+54,PN_UI_WIDTH-2*PN_UI_MARGIN,PN_ALIGN_RIGHT);
    pn_frame_rect(frame,PN_UI_MARGIN,FONT_ROW_Y+82,620,1,PN_UI_RULE);
    for(unsigned i=0;i<4 && s==PN_OK;i++)s=stepper(font,frame,&u->draft,i,STEP_Y0+(int)i*STEP_PITCH,u->selected==i);
    // 更多入口。/ The "more" entry.
    if(s==PN_OK)s=pn_w_text(font,frame,"边距与更多选项",PN_UI_MARGIN,MORE_Y+54,400,PN_ALIGN_LEFT);
    if(s==PN_OK)s=pn_w_text(font,frame,">",PN_UI_MARGIN,MORE_Y+54,PN_UI_WIDTH-2*PN_UI_MARGIN,PN_ALIGN_RIGHT);
    pn_frame_rect(frame,PN_UI_MARGIN,MORE_Y+82,620,1,PN_UI_RULE);
    // 三个预设。/ Three presets.
    int chosen=selected_preset(&u->draft);
    for(int i=0;i<3 && s==PN_OK;i++)s=pn_w_button(font,frame,preset_names[i],32+i*212,PRESET_Y,196,96,i==chosen?PN_W_SELECTED:0u);
    return s;
}
static pn_status_t paint_more(pn_style_ui_t *u,pn_font_t *font,pn_frame_t *frame){
    pn_status_t s=PN_OK;
    for(unsigned i=4;i<PN_SUI_FIELDS && s==PN_OK;i++)s=stepper(font,frame,&u->draft,i,MORE_STEP_Y0+(int)(i-4)*STEP_PITCH,u->selected==i);
    if(s==PN_OK)s=pn_font_size(font,28);
    if(s==PN_OK)s=pn_w_text_lines(font,frame,"字距按字号的百分比计算。清残影是每隔几页做一次整屏刷新，数字越小越干净、闪屏越多。",PN_UI_MARGIN,MORE_STEP_Y0+3*STEP_PITCH+24,620,4,40,NULL);
    return s;
}
static pn_status_t paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    (void)metadata;pn_style_ui_t *u=ctx;pn_frame_clear(frame,PN_UI_PAPER);
    int original=font->pixels;
    pn_status_t s=pn_font_size(font,34);
    if(s==PN_OK)s=pn_w_text(font,frame,"< 返回",PN_UI_MARGIN,78,200,PN_ALIGN_LEFT);
    if(s==PN_OK)s=pn_font_size(font,40);
    if(s==PN_OK)s=pn_w_text(font,frame,u->more?"更多排版":"排版",0,80,PN_UI_WIDTH,PN_ALIGN_CENTER);
    if(s==PN_OK)s=pn_font_size(font,34);
    if(s==PN_OK)s=pn_w_text(font,frame,"应用",500,78,152,PN_ALIGN_RIGHT);
    pn_frame_rect(frame,PN_UI_MARGIN,HEADER_H-4,620,2,PN_UI_RULE);
    if(s==PN_OK)s=u->more?paint_more(u,font,frame):paint_main(u,font,frame);
    // 底部：两个小链接“取消返回 · 恢复默认”，整页预览改为轻点段落预览框。/ Bottom: two small links "cancel · restore defaults"; the full-page preview moved to tapping the paragraph box.
    if(s==PN_OK)s=pn_font_size(font,30);
    if(s==PN_OK)s=pn_w_text(font,frame,"取消返回",32,LINKS_Y+52,300,PN_ALIGN_RIGHT);
    if(s==PN_OK)s=pn_w_text(font,frame,"·",332,LINKS_Y+52,20,PN_ALIGN_CENTER);
    if(s==PN_OK)s=pn_w_text(font,frame,"恢复默认",352,LINKS_Y+52,300,PN_ALIGN_LEFT);
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_text(font,frame,u->notice?u->notice:"点“应用”才会保存；取消返回不改变当前排版",PN_UI_MARGIN,1200,620,PN_ALIGN_CENTER);
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
    if(command==PN_SUI_FORM)u->more=u->selected>=4;
    if(command==PN_SUI_RETRY || command==PN_SUI_FORM)return pn_style_ui_present(u,present,ctx);
    pn_status_t status=PN_OK;u->notice=NULL;
    if(command==PN_SUI_FONTS){if(u->application_failed){u->notice="请先应用或取消排版";(void)pn_style_ui_present(u,present,ctx);return PN_BUSY;}if(u->did_preview){status=cancel(u,now,present,ctx);if(status!=PN_OK)return status;u->did_preview=false;}u->request_fonts=true;return PN_OK;}
    if(command>=PN_SUI_FIELD && command<PN_SUI_FIELD+PN_SUI_FIELDS*2){
        static const unsigned steps[]={2,5,5,1,5,2,1},minimum[]={28,100,0,0,0,16,0},maximum[]={72,220,100,2,50,80,30};
        unsigned field=(unsigned)(command-PN_SUI_FIELD)/2;bool up=(command-PN_SUI_FIELD)&1;unsigned v=field_value(&u->draft,field);u->selected=field;u->more=field>=4;
        if(up && v+steps[field]<=maximum[field])v+=steps[field];else if(!up && v>=minimum[field]+steps[field])v-=steps[field];else status=PN_EMPTY;
        field_set(&u->draft,field,v);
    }else if(command==PN_SUI_MORE){u->more=true;u->selected=4;
    }else if(command==PN_SUI_BACK_MAIN){u->more=false;u->selected=3;
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
    if(y<112){if(x<240)return u->more?PN_SUI_BACK_MAIN:PN_SUI_CANCEL;if(x>=500 && x<652 && y>=24)return PN_SUI_APPLY;return -1;}
    if(y>=LINKS_Y && y<LINKS_Y+80){if(x>=32 && x<332)return PN_SUI_CANCEL;if(x>=352 && x<652)return PN_SUI_RESET;return -1;}
    if(!u->more && y>=PREVIEW_Y && y<PREVIEW_Y+PREVIEW_H && x>=32 && x<652)return PN_SUI_PREVIEW;
    unsigned first=u->more?4:0,count=u->more?3:4;int top=u->more?MORE_STEP_Y0:STEP_Y0;
    for(unsigned i=0;i<count;i++){
        int row=top+(int)i*STEP_PITCH;
        if(y>=row && y<row+80){if(x>=BOX_X && x<BOX_X+88)return PN_SUI_FIELD+(int)(first+i)*2;if(x>=BOX_X+BOX_W-88 && x<BOX_X+BOX_W)return PN_SUI_FIELD+(int)(first+i)*2+1;return -1;}
    }
    if(!u->more){
        if(y>=FONT_ROW_Y && y<FONT_ROW_Y+80)return PN_SUI_FONTS;
        if(y>=MORE_Y && y<MORE_Y+80)return PN_SUI_MORE;
        if(y>=PRESET_Y && y<PRESET_Y+96){for(int i=0;i<3;i++)if(x>=32+i*212 && x<228+i*212)return PN_SUI_PRESET+i;}
    }
    return -1;
}
void pn_style_ui_close(pn_style_ui_t *u){if(u)memset(u,0,sizeof *u);}

pn_status_t pn_style_ui_open_epub(pn_style_ui_t *u,pn_epub_app_t *reader,pn_reader_present_fn present,void *ctx){
    if(!u || !reader || !reader->impl)return PN_INVALID;
    memset(u,0,sizeof *u);u->epub=reader;pn_status_t status=pn_epub_app_style_get(reader,&u->draft);if(status!=PN_OK)return status;u->active=true;return pn_style_ui_present(u,present,ctx);
}
