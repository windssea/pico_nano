/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：实际TXT页捕获，组合共享解码/分页/字体，不替代交互session。
 * English: real TXT page capture combining shared decoding/layout/font, not an interactive session.
 */
#include "reader_capture.h"
#include "pn_font.h"
#include "pn_text_file.h"
#include <stdio.h>
#include <limits.h>
#include <string.h>
static pn_status_t advance(void *ctx,uint32_t cp,int32_t *value){
    pn_font_t *font=ctx;pn_status_t status=pn_font_advance(font,cp,value);
    if(status==PN_EMPTY){*value=font->pixels*64;return PN_OK;}return status;
}
static pn_status_t label_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n){
    const char *s=ctx;size_t size=strlen(s);if(offset>size)return PN_INVALID;
    size_t count=size-(size_t)offset;if(count>cap)count=cap;memcpy(out,s+offset,count);*n=count;return PN_OK;
}
static pn_status_t ui_text(pn_font_t *font,pn_frame_t *frame,const char *value,int x,int y){
    pn_text_source_t source={(void *)value,strlen(value),label_read,NULL};pn_text_reader_t reader;
    pn_status_t status=pn_text_open(&reader,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    pn_text_char_t c;int32_t at=x*64;
    while((status=pn_text_next(&reader,&c))==PN_OK){int32_t advance;status=pn_font_advance(font,c.codepoint,&advance);if(status!=PN_OK)return status;
        status=pn_font_draw(font,frame,c.codepoint,at,y,PN_FONT_GRAY);if(status!=PN_OK)return status;at+=advance;}
    return status==PN_EMPTY?PN_OK:status;
}
pn_status_t pn_sim_reader_prepare(pn_display_t *display,pn_job_token_t token,pn_pool_t *pool,
    const char *book,const char *font_path,unsigned requested,int pixels){
    pn_media_t media;pn_media_init(&media);pn_status_t status=pn_media_attach(&media,1);
    pn_media_lease_t book_lease={0},font_lease={0};pn_text_file_t book_file={0},font_file={0};
    pn_text_source_t source={0},font_source=pn_font_builtin_source();pn_font_t font={0},ui={0};
    pn_page_glyph_t *glyphs=NULL;pn_draw_lease_t draw={0};pn_frame_t *frame=NULL;
    if(status==PN_OK)status=pn_media_acquire(&media,PN_MEDIA_READ,&book_lease);
    if(status==PN_OK)status=pn_text_file_open(&book_file,&media,&book_lease,book,&source);
    if(status==PN_OK && font_path){status=pn_media_acquire(&media,PN_MEDIA_READ,&font_lease);if(status==PN_OK)status=pn_text_file_open(&font_file,&media,&font_lease,font_path,&font_source);}
    if(status==PN_OK)status=pn_font_open(&font,pool,&font_source,pixels);
    if(status==PN_OK){pn_text_source_t built_in=pn_font_builtin_source();status=pn_font_open(&ui,pool,&built_in,24);}
    if(status==PN_OK){glyphs=pn_alloc(pool,PN_PAGE_GLYPHS_MAX*sizeof *glyphs);if(!glyphs)status=PN_NO_MEMORY;}
    pn_text_reader_t reader;pn_text_encoding_t encoding=PN_TEXT_UTF8;
    if(status==PN_OK)status=pn_text_probe(&source,PN_TEXT_FILE_MAX_BYTES,&encoding);
    if(status==PN_OK)status=pn_text_open(&reader,&source,encoding);
    int ascent=0,descent=0;
    if(status==PN_OK)status=pn_font_vertical(&font,&ascent,&descent);
    int line=(pixels*145+99)/100;if(line<ascent+descent)line=ascent+descent;
    pn_layout_t layout={620,1020,line,ascent,0,pixels/4,0};pn_font_metrics_t metrics={&font,advance};
    pn_text_page_t page={.glyphs=glyphs,.capacity=PN_PAGE_GLYPHS_MAX};bool paragraph=true;
    for(unsigned n=1;status==PN_OK && n<=requested;n++){status=pn_text_paginate(&reader,&layout,&metrics,paragraph,&page);paragraph=page.next_paragraph_start;}
    if(status==PN_OK)status=pn_display_begin_draw(display,token,&draw,&frame);
    unsigned missing=0;
    if(status==PN_OK){pn_frame_clear(frame,15);
        for(size_t i=0;i<page.count;i++){pn_page_glyph_t *g=&glyphs[i];if(g->source.codepoint==10 || g->source.codepoint==9)continue;
            status=pn_font_draw(&font,frame,g->source.codepoint,g->x_64+32*64,g->baseline+100,PN_FONT_GRAY);
            if(status==PN_EMPTY){int x=g->x_64/64+32,y=g->baseline+100-pixels;int size=pixels-4;pn_frame_rect(frame,x,y,size,1,0);pn_frame_rect(frame,x,y+size,size,1,0);pn_frame_rect(frame,x,y,1,size,0);pn_frame_rect(frame,x+size,y,1,size,0);missing++;status=PN_OK;}
            if(status!=PN_OK)break;
        }
    }
    if(status==PN_OK)status=ui_text(&ui,frame,"小纸 Pico",32,48);
    if(status==PN_OK){pn_frame_rect(frame,32,70,620,1,7);pn_frame_rect(frame,32,1150,620,1,7);char footer[96];
        snprintf(footer,sizeof footer,"第 %u 页  |  %llu%%  |  缺字 %u",requested,(unsigned long long)(source.size?page.begin*100/source.size:0),missing);
        status=ui_text(&ui,frame,footer,32,1190);
    }
    if(status==PN_OK){printf("reader: page=%u begin=%llu end=%llu size=%d missing=%u\n",requested,(unsigned long long)page.begin,(unsigned long long)page.end,pixels,missing);status=pn_display_publish(display,&draw,requested,PN_REFRESH_GL16);}
    if(status!=PN_OK && draw.ticket)(void)pn_display_discard(display,&draw);
    pn_free(glyphs);pn_font_close(&ui);pn_font_close(&font);
    pn_status_t closed=pn_text_file_close(&book_file);if(status==PN_OK)status=closed;
    closed=pn_text_file_close(&font_file);if(status==PN_OK)status=closed;
    if(book_lease.ticket)(void)pn_media_release(&media,&book_lease);
    if(font_lease.ticket)(void)pn_media_release(&media,&font_lease);
    return status;
}
