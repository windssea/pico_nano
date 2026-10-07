/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：七项排版草稿、正文预览与应用/取消；绘制不写配置。
 * English: seven-field typesetting drafts, body preview and apply/cancel; painting never writes configuration.
 * 冻结：变化先进入草稿；写入失败不关闭界面。
 * Frozen: changes enter drafts first; write failures never dismiss the UI.
 */
#include "pn_style_ui.h"
#include <stdio.h>
#include <string.h>
static bool live(const pn_style_ui_t *u){return u && ((u->reader && u->reader->impl) || (u->epub && u->epub->impl));}
static bool confirmed(const pn_style_ui_t *u){return u->epub?pn_epub_app_last_confirmed(u->epub):pn_reader_app_last_confirmed(u->reader);}
static pn_status_t overlay(pn_style_ui_t *u,pn_reader_overlay_fn paint,pn_reader_present_fn present,void *ctx){return u->epub?pn_epub_app_overlay(u->epub,paint,u,present,ctx,PN_REFRESH_GC16):pn_reader_app_overlay(u->reader,paint,u,present,ctx,PN_REFRESH_GC16);}
static pn_status_t preview(pn_style_ui_t *u,uint64_t now,pn_reader_present_fn present,void *ctx){return u->epub?pn_epub_app_style_preview(u->epub,&u->draft,now,present,ctx):pn_reader_app_style_preview(u->reader,&u->draft,now,present,ctx);}
static pn_status_t apply(pn_style_ui_t *u,uint64_t now,pn_reader_present_fn present,void *ctx){return u->epub?pn_epub_app_style_apply(u->epub,&u->draft,now,present,ctx):pn_reader_app_style_apply(u->reader,&u->draft,now,present,ctx);}
static pn_status_t cancel(pn_style_ui_t *u,uint64_t now,pn_reader_present_fn present,void *ctx){return u->epub?pn_epub_app_style_cancel(u->epub,now,present,ctx):pn_reader_app_style_cancel(u->reader,now,present,ctx);}
static pn_status_t read_string(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t size=strlen(s);if(off>size)return PN_INVALID;size_t count=size-(size_t)off;if(count>cap)count=cap;memcpy(out,s+off,count);*n=count;return PN_OK;}
static pn_status_t label(pn_font_t *font,pn_frame_t *frame,const char *value,int x,int baseline,int width){
    pn_frame_t part={frame->pixels+(size_t)(baseline-60)*frame->stride+(size_t)x/2,width,80,frame->stride};
    pn_text_source_t source={(void *)value,strlen(value),read_string,NULL};pn_text_reader_t decoder;pn_status_t status=pn_text_open(&decoder,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    int32_t at=0;pn_text_char_t c;
    while((status=pn_text_next(&decoder,&c))==PN_OK){int32_t advance;pn_status_t measured=pn_font_advance(font,c.codepoint,&advance);if(measured==PN_EMPTY)advance=font->pixels*64;else if(measured!=PN_OK)return measured;
        if(advance<0 || at>width*64-advance)return PN_LIMIT;
        if(measured==PN_EMPTY){pn_frame_rect(&part,at/64,60-font->pixels,font->pixels-4,1,0);pn_frame_rect(&part,at/64,56,font->pixels-4,1,0);pn_frame_rect(&part,at/64,60-font->pixels,1,font->pixels-4,0);pn_frame_rect(&part,at/64+font->pixels-5,60-font->pixels,1,font->pixels-4,0);}
        else{status=pn_font_draw(font,&part,c.codepoint,at,60,PN_FONT_GRAY);if(status!=PN_OK)return status;}
        at+=advance;
    }
    return status==PN_EMPTY?PN_OK:status;
}
static pn_status_t button(pn_font_t *font,pn_frame_t *frame,const char *value,int x,int y,int width){
    pn_frame_rect(frame,x,y,width,1,5);pn_frame_rect(frame,x,y+79,width,1,5);pn_frame_rect(frame,x,y,1,80,5);pn_frame_rect(frame,x+width-1,y,1,80,5);
    return label(font,frame,value,x+16,y+52,width-32);
}
static unsigned field_value(const pn_style_t *s,unsigned i){switch(i){case 0:return s->pixels;case 1:return s->line_percent;case 2:return s->gap_percent;case 3:return s->indent_em;case 4:return s->tracking_percent;case 5:return s->margin;default:return s->gl_before_clear;}}
static void field_set(pn_style_t *s,unsigned i,unsigned v){switch(i){case 0:s->pixels=(uint16_t)v;break;case 1:s->line_percent=(uint16_t)v;break;case 2:s->gap_percent=(uint16_t)v;break;case 3:s->indent_em=(uint16_t)v;break;case 4:s->tracking_percent=(uint16_t)v;break;case 5:s->margin=(uint16_t)v;break;default:s->gl_before_clear=(uint16_t)v;break;}}
static pn_status_t paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    (void)metadata;pn_style_ui_t *u=ctx;pn_frame_clear(frame,15);pn_status_t status=label(font,frame,"排版设置",32,84,400);if(status!=PN_OK)return status;
    status=button(font,frame,"字体",456,32,196);if(status!=PN_OK)return status;
    pn_frame_rect(frame,32,120,620,1,8);const char *names[]={"字号","行间距","段落间距","首行缩进","字间距","边距","清残影"};
    for(unsigned i=0;i<PN_SUI_FIELDS;i++){int y=160+(int)i*120;char value[40];unsigned v=field_value(&u->draft,i);
        if(i==6)snprintf(value,sizeof value,v?"每 %u 页":"每页",v+1);
        else if(i==1)snprintf(value,sizeof value,"%u.%02u 倍",v/100,v%100);
        else snprintf(value,sizeof value,i==2 || i==4?"%u%%":i==3?"%u 字":"%u 像素",v);
        status=label(font,frame,names[i],48,y+52,170);if(status!=PN_OK)return status;status=label(font,frame,value,232,y+52,200);if(status!=PN_OK)return status;
        status=button(font,frame,"减",448,y,88);if(status!=PN_OK)return status;status=button(font,frame,"加",560,y,88);if(status!=PN_OK)return status;
        if(i==u->selected)pn_frame_rect(frame,32,y,2,100,0);
        pn_frame_rect(frame,48,y+108,584,1,10);
    }
    const char *labels[]={"预览","取消","应用"};for(int i=0;i<3;i++){status=button(font,frame,labels[i],32+i*212,1130,196);if(status!=PN_OK)return status;}
    status=label(font,frame,u->notice?u->notice:"段距与字距按字号比例计算",48,1106,584);
    return status;
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
    if(command==PN_SUI_RETRY || command==PN_SUI_FORM)return pn_style_ui_present(u,present,ctx);
    pn_status_t status=PN_OK;u->notice=NULL;
    if(command==PN_SUI_FONTS){if(u->application_failed){u->notice="请先应用或取消排版";(void)pn_style_ui_present(u,present,ctx);return PN_BUSY;}if(u->did_preview){status=cancel(u,now,present,ctx);if(status!=PN_OK)return status;u->did_preview=false;}u->request_fonts=true;return PN_OK;}
    if(command>=PN_SUI_FIELD && command<PN_SUI_FIELD+PN_SUI_FIELDS*2){
        static const unsigned steps[]={2,5,5,1,5,2,1},minimum[]={28,100,0,0,0,16,0},maximum[]={72,220,100,2,50,80,30};
        unsigned field=(unsigned)(command-PN_SUI_FIELD)/2;bool up=(command-PN_SUI_FIELD)&1;unsigned v=field_value(&u->draft,field);u->selected=field;
        if(up && v+steps[field]<=maximum[field])v+=steps[field];else if(!up && v>=minimum[field]+steps[field])v-=steps[field];else status=PN_EMPTY;
        field_set(&u->draft,field,v);
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
    if(x>=456 && x<652 && y>=32 && y<112)return PN_SUI_FONTS;
    if(y>=160 && y<1000){unsigned row=(unsigned)(y-160)/120;if(row<PN_SUI_FIELDS && (y-160)%120<80){if(x>=448 && x<536)return PN_SUI_FIELD+(int)row*2;if(x>=560 && x<648)return PN_SUI_FIELD+(int)row*2+1;}}
    if(y>=1130 && y<1210){if(x>=32 && x<228)return PN_SUI_PREVIEW;if(x>=244 && x<440)return PN_SUI_CANCEL;if(x>=456 && x<652)return PN_SUI_APPLY;}return -1;
}
void pn_style_ui_close(pn_style_ui_t *u){if(u)memset(u,0,sizeof *u);}

pn_status_t pn_style_ui_open_epub(pn_style_ui_t *u,pn_epub_app_t *reader,pn_reader_present_fn present,void *ctx){
    if(!u || !reader || !reader->impl)return PN_INVALID;
    memset(u,0,sizeof *u);u->epub=reader;pn_status_t status=pn_epub_app_style_get(reader,&u->draft);if(status!=PN_OK)return status;u->active=true;return pn_style_ui_present(u,present,ctx);
}
