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
    pn_font_metrics_t metrics={.ctx=NULL,.advance=advance};pn_page_glyph_t glyphs[100];pn_text_page_t page={.glyphs=glyphs,.capacity=100};
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
    const uint8_t compact[]="AB\n\n\nC";m=(memory_t){compact,sizeof compact-1};source.size=m.size;assert(pn_text_open(&reader,&source,PN_TEXT_UTF8)==PN_OK);
    layout=(pn_layout_t){.width=100,.height=100,.line_height=10,.ascent=8,.paragraph_gap=3,.compact_paragraphs=true};page.capacity=100;
    assert(pn_text_paginate(&reader,&layout,&metrics,true,&page)==PN_OK && page.count==sizeof compact-1 && page.end==sizeof compact-1 && glyphs[5].baseline==21);
    uint8_t blank[5000];memset(blank,10,sizeof blank);m=(memory_t){blank,sizeof blank};source.size=m.size;assert(pn_text_open(&reader,&source,PN_TEXT_UTF8)==PN_OK);size_t consumed=0;
    for(;;){pn_status_t s=pn_text_paginate(&reader,&layout,&metrics,true,&page);if(s==PN_EMPTY)break;assert(s==PN_OK && page.count && page.end>page.begin);consumed+=page.count;}assert(consumed==sizeof blank);
    // 两端对齐：自动折行的中文/混排/多词英文行贴齐右边距，段末与硬换行不拉伸，英文单词不被拆散。
    // Full justification: wrapped CJK/mixed/multi-word Latin lines flush right, paragraph/hard-break lines stay left-aligned, Latin words stay intact.
    {const uint8_t j_cjk[]="\xe4\xb8\xad\xe6\x96\x87\xe6\x8e\x92\xe7\x89\x88"; // 4 CJK chars = 40px, width 34px -> 3 on line 1 (30px -> +4px), 1 on line 2
     m=(memory_t){j_cjk,sizeof j_cjk-1};source.size=m.size;assert(pn_text_open(&reader,&source,PN_TEXT_UTF8)==PN_OK);
     layout=(pn_layout_t){.width=34,.height=30,.line_height=10,.ascent=8};
     assert(pn_text_paginate(&reader,&layout,&metrics,true,&page)==PN_OK && page.count==4);
     assert(glyphs[0].x_64==0 && glyphs[2].x_64+10*64==34*64 && glyphs[3].x_64==0 && glyphs[3].baseline==18);
     const uint8_t j_mix[]="\xe4\xb8\xad" "ab" "\xe6\x96\x87\xe6\x8e\x92"; // 中(10) a(5) b(5) 文(10) = 30px; width 33px -> 中 a b 文 on line 1, 排 on line 2; "ab" stays contiguous (b.x - a.x == 5*64)
     m=(memory_t){j_mix,sizeof j_mix-1};source.size=m.size;assert(pn_text_open(&reader,&source,PN_TEXT_UTF8)==PN_OK);
     layout=(pn_layout_t){.width=33,.height=30,.line_height=10,.ascent=8};
     assert(pn_text_paginate(&reader,&layout,&metrics,true,&page)==PN_OK && page.count==5);
     assert(glyphs[2].x_64-glyphs[1].x_64==5*64 && glyphs[3].x_64+10*64==33*64 && glyphs[4].x_64==0);
     const uint8_t j_en[]="aa bb cc dd"; // width 35 -> "aa bb " or "aa bb" wraps before cc/dd; "aa bb" (25px) justifies space to flush 35px
     m=(memory_t){j_en,sizeof j_en-1};source.size=m.size;assert(pn_text_open(&reader,&source,PN_TEXT_UTF8)==PN_OK);
     layout=(pn_layout_t){.width=35,.height=30,.line_height=10,.ascent=8};
     assert(pn_text_paginate(&reader,&layout,&metrics,true,&page)==PN_OK && page.count==sizeof j_en-1);
     assert(glyphs[1].x_64-glyphs[0].x_64==5*64 && glyphs[4].x_64+5*64==35*64);}
    // 负字间距防重叠夹紧 + TXT 章节行识别与居中排版。/ Negative tracking floor clamping + TXT chapter heading detection and centered layout.
    {const uint8_t tight[]="ABCDEF";m=(memory_t){tight,6};source.size=m.size;assert(pn_text_open(&reader,&source,PN_TEXT_UTF8)==PN_OK);
     layout=(pn_layout_t){.width=100,.height=30,.line_height=10,.ascent=8,.letter_spacing_64=-4*64};
     assert(pn_text_paginate(&reader,&layout,&metrics,true,&page)==PN_OK && page.count==6);
     // advance=5px, -4px clamped to >=2.5px (160/64), ensuring strictly increasing x_64
     assert(glyphs[1].x_64>glyphs[0].x_64 && glyphs[1].x_64==160);
     const uint32_t zh_ch[]={0x7b2c,0x4e00,0x7ae0,0x20,0x5f00,0x7bc7}; // 第一章 开篇
     const uint32_t zh_prose[]={0x7b2c,0x4e00,0x7ae0,0x5f88,0x597d,0x770b,0x3002}; // 第一章很好看。
     assert(pn_text_is_chapter_line(zh_ch,6) && !pn_text_is_chapter_line(zh_prose,7));
     const uint8_t txt_ch[]="\xe7\xac\xac\xe4\xb8\x80\xe7\xab\xa0\n" "AB";
     m=(memory_t){txt_ch,sizeof txt_ch-1};source.size=m.size;assert(pn_text_open(&reader,&source,PN_TEXT_UTF8)==PN_OK);
     layout=(pn_layout_t){.width=100,.height=60,.line_height=10,.ascent=8,.indent=10,.paragraph_gap=4,.chapter_align=2,.chapter_gap=2,.chapter_style_txt=true};
     assert(pn_text_paginate(&reader,&layout,&metrics,true,&page)==PN_OK && page.count==6);
     assert(glyphs[0].heading==1 && glyphs[0].x_64==(100-30)*64/2 && glyphs[4].heading==0 && glyphs[4].x_64==10*64 && glyphs[4].baseline==8+10+6);}
    puts("layout: page source continuity, word wrap, justification, negative tracking, chapter lines, compact blank paragraphs, capacity and oversized glyph passed");return 0;
}
