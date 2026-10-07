/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界文本分页节点；字体度量由调用方提供。
 * English: bounded text page nodes using caller-provided font metrics.
 * 冻结：不栅格化、不分配、不保存永久页码；失败不消费reader。
 * Frozen: no rasterization, allocation or permanent page numbers; failures do not consume the reader.
 */
#include "pn_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {const uint8_t *data;size_t size;} memory_t;
static pn_status_t read_at(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n){memory_t *m=ctx;size_t take=m->size-(size_t)offset;if(take>cap)take=cap;memcpy(out,m->data+offset,take);*n=take;return PN_OK;}
static pn_status_t advance(void *ctx,uint32_t cp,int32_t *out){(void)ctx;*out=cp<128?5*64:10*64;return PN_OK;}
int main(void){
    const uint8_t bytes[]="one two three four five\nabcdef\nXYZ";
    memory_t m={bytes,sizeof bytes-1};pn_text_source_t source={&m,m.size,read_at,NULL};pn_text_reader_t reader;
    assert(pn_text_open(&reader,&source,PN_TEXT_UTF8)==PN_OK);
    pn_layout_t layout={.width=25,.height=24,.line_height=10,.ascent=8,.indent=0,.paragraph_gap=0};
    pn_font_metrics_t metrics={NULL,advance};pn_page_glyph_t glyphs[100];pn_text_page_t page={.glyphs=glyphs,.capacity=100};
    uint32_t gathered[100];size_t total=0;bool paragraph=true;unsigned pages=0;
    for(;;){uint64_t begin=reader.cursor;pn_status_t status=pn_text_paginate(&reader,&layout,&metrics,paragraph,&page);if(status==PN_EMPTY)break;assert(status==PN_OK && page.valid && page.begin==begin && page.end>begin && page.end==reader.cursor);pages++;
        for(size_t i=0;i<page.count;i++){assert(total<100);gathered[total++]=page.glyphs[i].source.codepoint;assert(page.glyphs[i].x_64>=0 && page.glyphs[i].baseline<=24);}
        paragraph=page.next_paragraph_start;
    }
    assert(pages>=3 && total==m.size);for(size_t i=0;i<total;i++)assert(gathered[i]==bytes[i]);
    assert(pn_text_seek(&reader,0)==PN_OK);page.capacity=1;
    assert(pn_text_paginate(&reader,&layout,&metrics,true,&page)==PN_LIMIT && !page.valid && page.count==0 && reader.cursor==0);
    layout.width=4;page.capacity=100;
    assert(pn_text_paginate(&reader,&layout,&metrics,true,&page)==PN_LIMIT && reader.cursor==0);
    const uint8_t cjk[]={0xe4,0xb8,0xad,0xe4,0xb8,0xad,0xef,0xbc,0x8c,0xe4,0xb8,0xad};
    m=(memory_t){cjk,sizeof cjk};source.size=m.size;assert(pn_text_open(&reader,&source,PN_TEXT_UTF8)==PN_OK);
    layout=(pn_layout_t){.width=20,.height=30,.line_height=10,.ascent=8};
    assert(pn_text_paginate(&reader,&layout,&metrics,true,&page)==PN_OK && page.count==4);
    assert(page.glyphs[0].baseline==8 && page.glyphs[1].baseline==18 && page.glyphs[2].baseline==18);
    assert(page.glyphs[2].source.codepoint==0xff0c && page.glyphs[2].x_64==640);
    // 字间距只改变字符之间的位置，不改变源位置。/ Tracking changes inter-glyph positions without changing source locations.
    const uint8_t tracked[]="ABCD";m=(memory_t){tracked,4};source.size=4;assert(pn_text_open(&reader,&source,PN_TEXT_UTF8)==PN_OK);
    layout=(pn_layout_t){.width=20,.height=20,.line_height=10,.ascent=8,.letter_spacing_64=2*64};
    assert(pn_text_paginate(&reader,&layout,&metrics,true,&page)==PN_OK && page.count==4);
    assert(glyphs[1].x_64==7*64 && glyphs[2].x_64==14*64 && glyphs[3].x_64==0 && glyphs[3].baseline==18);
    for(unsigned round=0;round<200;round++) {
        uint8_t input[600];uint32_t state=round+1;
        for(size_t i=0;i<sizeof input;i++){state=state*1664525u+1013904223u;input[i]=(state%9==0)?10:(state%5==0)?32:(uint8_t)('a'+state%26);}
        m=(memory_t){input,sizeof input};source.size=m.size;assert(pn_text_open(&reader,&source,PN_TEXT_UTF8)==PN_OK);
        layout=(pn_layout_t){.width=20+(int)(round%10)*5,.height=20+(int)(round%4)*10,.line_height=10,.ascent=8,.indent=round%3};
        bool para=true;size_t seen=0;
        for(;;){pn_status_t status=pn_text_paginate(&reader,&layout,&metrics,para,&page);if(status==PN_EMPTY)break;assert(status==PN_OK && page.count && page.valid);for(size_t i=0;i<page.count;i++){assert(seen<sizeof input);assert(page.glyphs[i].source.codepoint==input[seen]);assert(page.glyphs[i].source.begin==seen && page.glyphs[i].source.end==seen+1);seen++;}para=page.next_paragraph_start;}
        assert(seen==sizeof input);
    }
    puts("layout: page source continuity, word wrap, capacity and oversized glyph passed");return 0;
}
