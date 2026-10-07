/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：字体预览不读取目录、不写记录，仅按已验证模型画字。
 * English: font previews neither read directories nor write records; paint validated models only.
 * 冻结：缺字画方框，不用备用字体伪造候选覆盖。
 * Frozen: missing glyphs use boxes, never a fallback that disguises candidate coverage.
 */
#include "pn_font_preview.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
static const char sample[]="字体预览，阅读从容。\n春风又绿江南岸，明月何时照我还。\nABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz\n0123456789，。！？：；（）【】\n篇 旐 旓 䌽";
const char *pn_font_preview_text(void){return sample;}
static pn_status_t read_string(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t length=strlen(s);if(at>length)return PN_INVALID;size_t count=length-(size_t)at;if(count>cap)count=cap;memcpy(out,s+at,count);*n=count;return PN_OK;}
static pn_status_t draw_string(pn_font_t *font,pn_font_t *fallback,pn_frame_t *frame,const char *value,int x,int y,int width,int height){
    pn_frame_t part={frame->pixels+(size_t)y*frame->stride+(size_t)x/2,width,height,frame->stride};pn_text_source_t source={(void *)value,strlen(value),read_string,NULL};pn_text_reader_t reader;pn_status_t status=pn_text_open(&reader,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    int ascent,descent;status=pn_font_vertical(font,&ascent,&descent);if(status!=PN_OK)return status;int line=(font->pixels*145+99)/100;if(line<ascent+descent)line=ascent+descent;int baseline=ascent;int32_t cursor=0;pn_text_char_t c;
    while((status=pn_text_next(&reader,&c))==PN_OK){if(c.codepoint==10){cursor=0;baseline+=line;continue;}int32_t advance;pn_font_t *used=font;pn_status_t measured=pn_font_advance(used,c.codepoint,&advance);if(measured==PN_EMPTY && fallback){used=fallback;measured=pn_font_advance(used,c.codepoint,&advance);}if(measured==PN_EMPTY)advance=font->pixels*64;else if(measured!=PN_OK)return measured;
        if(advance<0 || advance>width*64)return PN_LIMIT;
        if(cursor>width*64-advance){cursor=0;baseline+=line;}if(baseline+descent>height)return baseline==ascent?PN_LIMIT:PN_OK;
        if(measured==PN_EMPTY){int size=font->pixels-4,px=cursor/64,py=baseline-font->pixels;pn_frame_rect(&part,px,py,size,1,0);pn_frame_rect(&part,px,py+size,size,1,0);pn_frame_rect(&part,px,py,1,size,0);pn_frame_rect(&part,px+size,py,1,size,0);}
        else{status=pn_font_draw(used,&part,c.codepoint,cursor,baseline,PN_FONT_GRAY);if(status!=PN_OK)return status;}cursor+=advance;
    }
    return status==PN_EMPTY?PN_OK:status;
}
static const char *style_name(const char *value){
    if(!*value)return "未知";
    if(!strcmp(value,"Regular") || !strcmp(value,"Normal"))return "常规";
    if(!strcmp(value,"Bold"))return "粗体";
    if(!strcmp(value,"Italic"))return "斜体";
    if(!strcmp(value,"Bold Italic"))return "粗斜体";
    if(!strcmp(value,"Light"))return "细体";
    if(!strcmp(value,"Thin"))return "纤细";
    if(!strcmp(value,"Medium"))return "中等";
    return value;
}
pn_status_t pn_font_preview_draw(pn_font_t *ui,pn_font_t *body,const pn_font_info_t *info,uint64_t bytes,unsigned checked,unsigned missing,pn_frame_t *frame){
    if(!ui || !ui->impl || !body || !body->impl || !info || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342 || checked>128 || missing>checked)return PN_INVALID;
    if(!memchr(info->family,0,sizeof info->family) || !memchr(info->style,0,sizeof info->style))return PN_INVALID;
    pn_frame_clear(frame,15);pn_status_t status=draw_string(ui,NULL,frame,"字体预览",32,24,620,72);if(status!=PN_OK)return status;pn_frame_rect(frame,32,110,620,1,8);
    int original=body->pixels;status=pn_font_size(body,ui->pixels);
    if(status==PN_OK)status=draw_string(ui,body,frame,*info->family?info->family:"未提供名称",32,136,620,96);
    pn_status_t restored=pn_font_size(body,original);if(status==PN_OK)status=restored;if(status!=PN_OK)return status;
    char label[256];snprintf(label,sizeof label,"样式：%s",style_name(info->style));status=draw_string(ui,NULL,frame,label,32,236,620,60);if(status!=PN_OK)return status;
    if(info->weight)snprintf(label,sizeof label,"%s  字重 %u  字形 %" PRIu32,info->variable?"可变字体":"静态字体",info->weight,info->glyphs);else snprintf(label,sizeof label,"%s  字重未知  字形 %" PRIu32,info->variable?"可变字体":"静态字体",info->glyphs);
    status=draw_string(ui,NULL,frame,label,32,296,620,60);if(status!=PN_OK)return status;
    snprintf(label,sizeof label,"文件 %llu 字节",(unsigned long long)bytes);status=draw_string(ui,NULL,frame,label,32,356,620,60);if(status!=PN_OK)return status;
    snprintf(label,sizeof label,"样例检测：缺字 %u / %u",missing,checked);status=draw_string(ui,NULL,frame,label,32,416,620,60);if(status!=PN_OK)return status;
    status=draw_string(body,NULL,frame,sample,32,480,620,588);if(status!=PN_OK)return status;pn_frame_rect(frame,32,1080,620,1,8);
    status=draw_string(ui,NULL,frame,"样例检查不代表全部字符",32,1110,620,70);return status;
}
