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
/* 双列草稿面板：全部九项保留在一页，字体与预览独立入口。/ Two-column draft panel: all nine fields on one page, with separate font and preview entries. */
#define PREVIEW_Y 140
#define PREVIEW_H 180
#define MODE_BASE 360
#define PRESET_Y 372
#define PRESET_H 64
#define FONT_ROW_Y 448
#define STEP_Y0 548
#define STEP_PITCH 100
#define CELL_W 300
#define CELL_PITCH 320
#define APPLY_Y 1112
#define APPLY_H 88
static const unsigned fields[PN_SUI_FIELDS]={0,1,2,5,3,4,6,8,7};
static const char *const names[PN_SUI_FIELDS]={"字号","行距","段距","首行缩进","字间距","边距","阅读线","上下位置","阅读线深浅"};
typedef struct {unsigned pixels,line,gap,indent,margin;} preset_t;
static const preset_t presets[3]={{PN_STYLE_DEFAULT_PIXELS,145,40,2,32},{36,125,20,2,24},{56,160,40,2,32}};
static const char *const preset_names[3]={"舒适","紧凑","大字"};
static unsigned field_value(const pn_style_t *s,unsigned i){switch(i){case 0:return s->pixels;case 1:return s->line_percent;case 2:return s->gap_percent;case 3:return s->indent_em;case 4:return s->tracking_percent;case 5:return s->margin;case 6:return s->reading_line;case 8:return s->reading_line_shade;default:return (unsigned)(s->reading_line_offset+8);}}
static void field_set(pn_style_t *s,unsigned i,unsigned v){switch(i){case 0:s->pixels=(uint16_t)v;break;case 1:s->line_percent=(uint16_t)v;break;case 2:s->gap_percent=(uint16_t)v;break;case 3:s->indent_em=(uint16_t)v;break;case 4:s->tracking_percent=(uint16_t)v;break;case 5:s->margin=(uint16_t)v;break;case 6:s->reading_line=(uint16_t)v;break;case 8:s->reading_line_shade=(uint16_t)v;break;default:s->reading_line_offset=(int16_t)((int)v-8);break;}}
static void field_text(const pn_style_t *s,unsigned i,char *out,size_t cap){
    unsigned v=field_value(s,i);
    if(i==1 || i==2)snprintf(out,cap,"%u.%02u",v/100,v%100);
    else if(i==3)snprintf(out,cap,"%u 字",v);
    else if(i==4)snprintf(out,cap,"%u%%",v);
    else if(i==5)snprintf(out,cap,v<=20?"窄":v<=32?"中":"宽");
    else if(i==6)snprintf(out,cap,"%s",v==1?"虚线":v==2?"点线":"关闭");
    else if(i==7)snprintf(out,cap,"%+d px",s->reading_line_offset);
    else if(i==8){static const char *const shades[4]={"浅","中","深","黑"};snprintf(out,cap,"%s",shades[v<4?v:0]);}
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
/* 每项占半行，标签在上、减值加在下，触控区不互相重叠。/ Each field uses half a row, with its label above minus/value/plus and disjoint touch targets. */
static pn_status_t stepper(pn_font_t *font,pn_frame_t *frame,const pn_style_t *draft,unsigned field,int x,int y,bool selected){
    char value[40];field_text(draft,field,value,sizeof value);
    pn_w_round_stroke(frame,x,y,CELL_W,92,10,selected?2:1,selected?PN_UI_INK:8);
    pn_status_t s=pn_font_size(font,22);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,names[field],x+12,y+28,CELL_W-24,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    pn_w_icon(frame,PN_ICON_MINUS,x+22,y+54,24,PN_UI_INK);pn_w_icon(frame,PN_ICON_PLUS,x+CELL_W-46,y+54,24,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,28);
    if(s==PN_OK)s=pn_w_text(font,frame,value,x+68,y+77,CELL_W-136,PN_ALIGN_CENTER);
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
    unsigned lines=0;
    if(s==PN_OK)s=pn_w_text_flow(face,frame,"字与字之间，留出恰好的距离。让阅读回到舒适，把时间留给文字。",52,PREVIEW_Y+60+pixels,556,room,pitch,pixels*(int)u->draft.indent_em,pixels*64*(int)u->draft.tracking_percent/100,&lines);
    if(s==PN_OK && u->draft.reading_line){int ascent,descent;s=pn_font_vertical(face,&ascent,&descent);
        if(s==PN_OK){pn_frame_t preview={frame->pixels+(PREVIEW_Y+60)*frame->stride+26,556,PREVIEW_H-72,frame->stride};int gap=pitch-ascent-descent;if(gap<0)gap=0;
            for(unsigned i=0;i<lines;i++)pn_reader_guide_row(&preview,u->draft.reading_line,u->draft.reading_line_shade,pixels+(int)i*pitch+descent+gap/2+u->draft.reading_line_offset);}}
    pn_status_t restored=pn_font_size(face,saved);
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
    if(s==PN_OK)s=pn_w_text(font,frame,"字体",32,FONT_ROW_Y+48,140,PN_ALIGN_LEFT);
    pn_w_set_fallback(body_font(u));
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,pn_brand_label(name),176,FONT_ROW_Y+48,420,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
    pn_w_set_fallback(NULL);pn_w_icon(frame,PN_ICON_CHEVRON,624,FONT_ROW_Y+25,28,PN_UI_INK);
    pn_frame_rect(frame,32,FONT_ROW_Y+78,620,1,PN_UI_SELECT);
    for(unsigned i=0;i<PN_SUI_FIELDS && s==PN_OK;i++)s=stepper(font,frame,&u->draft,fields[i],32+(int)(i%2)*CELL_PITCH,STEP_Y0+(int)(i/2)*STEP_PITCH,u->selected==fields[i]);
    if(s==PN_OK){s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_button(font,frame,"整页预览",352,STEP_Y0+4*STEP_PITCH,CELL_W,92,0);}
    return s;
}
pn_status_t pn_style_ui_render(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    (void)metadata;pn_style_ui_t *u=ctx;if(!live(u) || !u->active || !font || !font->impl || !frame || !frame->pixels)return PN_INVALID;pn_frame_clear(frame,PN_UI_PAPER);
    int original=font->pixels;
    pn_status_t s=pn_w_header(font,frame,"< 返回","排版设置",NULL);
    if(s==PN_OK && !u->more){s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text_ex(font,frame,"恢复默认",480,100,172,PN_ALIGN_RIGHT,PN_UI_MUTED,false);}
    if(s==PN_OK)s=paint_main(u,font,frame);
    // 底部固定的深色“应用”；取消即左上返回，“恢复默认”在右上。/ A fixed dark Apply at the bottom; cancelling is the Back at the top left and Restore defaults sits at the top right.
    if(s==PN_OK)s=pn_font_size(font,32);
    if(s==PN_OK)s=pn_w_button(font,frame,"应用",PN_UI_MARGIN,APPLY_Y,620,APPLY_H,PN_W_SELECTED);
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
pn_status_t pn_style_ui_event(pn_style_ui_t *u,int command,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(!u || !u->active || !live(u))return PN_INVALID;
    if(!u->presented && command!=PN_SUI_RETRY)return PN_BUSY;
    if(command==PN_SUI_FORM)u->more=false;
    if(command==PN_SUI_RETRY || command==PN_SUI_FORM)return pn_style_ui_present(u,present,ctx);
    pn_status_t status=PN_OK;u->notice=NULL;
    if(command==PN_SUI_FONTS){if(u->application_failed){u->notice="请先应用或取消排版";(void)pn_style_ui_present(u,present,ctx);return PN_BUSY;}if(u->did_preview){status=cancel(u,now,present,ctx);if(status!=PN_OK)return status;u->did_preview=false;}u->request_fonts=true;return PN_OK;}
    if(command>=PN_SUI_FIELD && command<PN_SUI_FIELD+PN_SUI_FIELDS*2){
        static const unsigned steps[]={2,5,5,1,5,2,1,2,1},minimum[]={28,100,0,0,0,16,0,0,0},maximum[]={72,220,100,2,50,80,2,16,3};
        unsigned field=(unsigned)(command-PN_SUI_FIELD)/2;bool up=(command-PN_SUI_FIELD)&1;unsigned v=field_value(&u->draft,field);u->selected=field;u->more=false;
        // 页边距只有窄/中/宽三档。/ Margins have just three levels: narrow, medium and wide.
        if(field==5){unsigned level=v<=20?0u:v<=32?1u:2u;static const unsigned margins[3]={20,32,48};if(up && level<2)level++;else if(!up && level>0)level--;else status=PN_EMPTY;field_set(&u->draft,5,margins[level]);}
        else{
        if(up && v+steps[field]<=maximum[field])v+=steps[field];else if(!up && v>=minimum[field]+steps[field])v-=steps[field];else status=PN_EMPTY;
        field_set(&u->draft,field,v);}
    }else if(command==PN_SUI_INDENT){u->draft.indent_em=u->draft.indent_em?0:2;u->selected=3;
    }else if(command==PN_SUI_MORE){u->more=false;u->selected=4;
    }else if(command==PN_SUI_BACK_MAIN){u->more=false;u->selected=0;
    }else if(command==PN_SUI_RESET){u->draft=pn_style_default(PN_STYLE_DEFAULT_PIXELS);
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
    if(y<PN_W_HEADER_H){if(pn_w_header_hit(x,y,false)==1)return PN_SUI_CANCEL;return x>=480 && y>=44?PN_SUI_RESET:-1;}
    if(y>=APPLY_Y && y<APPLY_Y+APPLY_H)return x>=32 && x<652?PN_SUI_APPLY:-1;
    if(y>=PREVIEW_Y && y<PREVIEW_Y+PREVIEW_H && x>=32 && x<652)return PN_SUI_PREVIEW;
    if(y>=FONT_ROW_Y && y<FONT_ROW_Y+80 && x>=32 && x<652)return PN_SUI_FONTS;
    for(unsigned i=0;i<PN_SUI_FIELDS;i++){
        int left=32+(int)(i%2)*CELL_PITCH,top=STEP_Y0+(int)(i/2)*STEP_PITCH;
        if(y>=top+38 && y<top+92){if(x>=left && x<left+68)return PN_SUI_FIELD+(int)fields[i]*2;if(x>=left+CELL_W-68 && x<left+CELL_W)return PN_SUI_FIELD+(int)fields[i]*2+1;}
    }
    if(x>=352 && x<652 && y>=STEP_Y0+4*STEP_PITCH && y<STEP_Y0+4*STEP_PITCH+92)return PN_SUI_PREVIEW;
    int preset=pn_w_segments_hit(3,PRESET_Y,PRESET_H,x,y);return preset>=0?PN_SUI_PRESET+preset:-1;
}
void pn_style_ui_close(pn_style_ui_t *u){if(u)memset(u,0,sizeof *u);}

pn_status_t pn_style_ui_open_epub(pn_style_ui_t *u,pn_epub_app_t *reader,pn_reader_present_fn present,void *ctx){
    if(!u || !reader || !reader->impl)return PN_INVALID;
    memset(u,0,sizeof *u);u->epub=reader;pn_status_t status=pn_epub_app_style_get(reader,&u->draft);if(status!=PN_OK)return status;u->active=true;return pn_style_ui_present(u,present,ctx);
}
