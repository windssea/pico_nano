/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界文本分页节点；字体度量由调用方提供。
 * English: bounded text page nodes using caller-provided font metrics.
 * 冻结：不栅格化、不分配、不保存永久页码；失败不消费reader。
 * Frozen: no rasterization, allocation or permanent page numbers; failures do not consume the reader.
 */
#include "pn_layout.h"
static pn_status_t failure(pn_text_page_t *page,pn_status_t status){page->valid=false;page->count=0;return status;}
static bool opening(uint32_t c){return c=='(' || c=='[' || c=='{' || c==0x2018 || c==0x201c || c==0x3008 || c==0x300a || c==0x300c || c==0x300e || c==0xff08;}
static bool closing(uint32_t c){return c==')' || c==']' || c=='}' || c==',' || c=='.' || c=='!' || c=='?' || c==':' || c==';' || c==0x2019 || c==0x201d || c==0x3001 || c==0x3002 || c==0x3009 || c==0x300b || c==0x300d || c==0x300f || c==0xff09 || c==0xff0c || c==0xff01 || c==0xff1f;}
pn_status_t pn_text_paginate(pn_text_reader_t *reader,const pn_layout_t *layout,
    const pn_font_metrics_t *metrics,bool paragraph,pn_text_page_t *page) {
    if(!page)return PN_INVALID;
    page->valid=false;page->count=0;
    if(!reader || !reader->source.read_at || !layout || !metrics || !metrics->advance || !page->glyphs ||
        !page->capacity || page->capacity>PN_PAGE_GLYPHS_MAX || layout->width<1 || layout->width>4096 ||
        layout->height<1 || layout->height>4096 || layout->line_height<1 || layout->line_height>layout->height ||
        layout->ascent<0 || layout->ascent>layout->line_height || layout->indent<0 || layout->indent>=layout->width ||
        layout->paragraph_gap<0 || layout->paragraph_gap>4096 || layout->letter_spacing_64<0 || layout->letter_spacing_64>4096)return PN_INVALID;
    pn_text_reader_t work=*reader;page->begin=work.cursor;
    int y=0;int32_t x=paragraph?layout->indent*64:0;
    size_t line_start=0,last_space=SIZE_MAX;bool next_paragraph=paragraph;
    while(y+layout->line_height<=layout->height) {
        pn_text_char_t character;pn_status_t status=pn_text_next(&work,&character);
        if(status==PN_EMPTY)break;
        if(status!=PN_OK)return failure(page,status);
        int32_t advance=0;
        if(character.codepoint!=10){status=metrics->advance(metrics->ctx,character.codepoint,&advance);if(status!=PN_OK)return failure(page,status);}
        if(advance<0)return failure(page,PN_INVALID);
        if(advance>layout->width*64)return failure(page,PN_LIMIT);
        if(character.codepoint!=10 && x>layout->width*64-advance) {
            if(page->count==line_start)return failure(page,PN_LIMIT);
            size_t cut=page->count;
            if(last_space!=SIZE_MAX && last_space+1<page->count)cut=last_space+1;
            else if(page->count-line_start>=2 && (opening(page->glyphs[page->count-1].source.codepoint) || closing(character.codepoint)))cut=page->count-1;
            work.cursor=cut<page->count?page->glyphs[cut].source.begin:character.begin;
            page->count=cut;y+=layout->line_height;x=0;line_start=page->count;last_space=SIZE_MAX;next_paragraph=false;
            continue;
        }
        if(page->count==page->capacity)return failure(page,PN_LIMIT);
        page->glyphs[page->count]=(pn_page_glyph_t){character,x,y+layout->ascent,advance};
        if(character.codepoint==' ' || character.codepoint==9)last_space=page->count;
        page->count++;x+=advance;if(character.codepoint!=10)x+=layout->letter_spacing_64;next_paragraph=false;
        if(character.codepoint==10){y+=layout->line_height+layout->paragraph_gap;x=layout->indent*64;line_start=page->count;last_space=SIZE_MAX;next_paragraph=true;}
    }
    if(!page->count)return PN_EMPTY;
    page->end=work.cursor;page->next_paragraph_start=next_paragraph;page->valid=true;*reader=work;return PN_OK;
}
