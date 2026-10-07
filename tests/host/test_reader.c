/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享TXT阅读会话，区分准备页和实际显示位置。
 * English: shared TXT reading session separating prepared pages from visible positions.
 * 冻结：owner串行调用；仅成功显示提交进度；源和glyph缓冲保持有效。
 * Frozen: serialized owner calls; commit progress only after successful presentation; source and glyph buffers remain valid.
 */
#include "pn_reader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint8_t data[1200];bool available;} memory_t;
static pn_status_t read_at(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){memory_t *m=ctx;if(!m->available)return PN_STALE_MEDIA;size_t left=sizeof m->data-(size_t)off;if(cap>left)cap=left;memcpy(out,m->data+off,cap);*n=cap;return PN_OK;}
static pn_status_t validate(void *ctx){return ((memory_t *)ctx)->available?PN_OK:PN_STALE_MEDIA;}
static pn_status_t advance(void *ctx,uint32_t cp,int32_t *out){(void)ctx;(void)cp;*out=5*64;return PN_OK;}
int main(void){
    memory_t m={.available=true};memset(m.data,'a',sizeof m.data);
    pn_text_source_t source={&m,sizeof m.data,read_at,validate};pn_book_id_t book={{42}};
    pn_layout_t layout={25,20,10,8,0,0,0};pn_font_metrics_t metrics={NULL,advance};pn_page_glyph_t glyphs[100];pn_reader_t reader;
    assert(pn_reader_init(&reader,&source,PN_TEXT_UTF8,&book,&layout,&metrics,glyphs,100,(pn_job_token_t){1,1},NULL)==PN_OK);
    pn_txt_progress_t position={0};assert(pn_reader_progress(&reader,&position)==PN_EMPTY);
    pn_reader_receipt_t page,other;assert(pn_reader_prepare(&reader,PN_READ_FIRST,0,&page)==PN_OK && page.anchor.begin==0 && page.anchor.end==10);
    assert(pn_reader_prepare(&reader,PN_READ_NEXT,0,&other)==PN_BUSY);assert(pn_reader_progress(&reader,&position)==PN_EMPTY);
    assert(pn_reader_complete(&reader,&page,true,0)==PN_OK);
    assert(pn_reader_complete(&reader,&page,true,0)==PN_INVALID);
    assert(pn_reader_prepare(&reader,PN_READ_NEXT,0,&page)==PN_OK && page.anchor.begin==10);
    assert(pn_reader_complete(&reader,&page,false,1)==PN_IO);
    assert(pn_reader_progress(&reader,&position)==PN_OK && position.source_offset==0);
    assert(pn_reader_prepare(&reader,PN_READ_NEXT,0,&page)==PN_OK && page.anchor.begin==10);
    other=page;other.anchor.begin++;assert(pn_reader_complete(&reader,&other,true,1)==PN_INVALID && reader.preparing);
    assert(pn_reader_complete(&reader,&page,true,1)==PN_OK);
    assert(pn_reader_prepare(&reader,PN_READ_PREVIOUS,0,&page)==PN_OK && page.anchor.begin==0);
    assert(pn_reader_complete(&reader,&page,true,2)==PN_OK);
    assert(pn_reader_prepare(&reader,PN_READ_PREVIOUS,0,&page)==PN_EMPTY);
    for(unsigned i=0;i<45;i++){assert(pn_reader_prepare(&reader,PN_READ_NEXT,0,&page)==PN_OK);assert(pn_reader_complete(&reader,&page,true,3+i)==PN_OK);}
    assert(reader.visible.begin==450);
    for(unsigned i=0;i<45;i++){assert(pn_reader_prepare(&reader,PN_READ_PREVIOUS,0,&page)==PN_OK);assert(pn_reader_complete(&reader,&page,true,50+i)==PN_OK);}
    assert(reader.visible.begin==0);
    assert(pn_reader_prepare(&reader,PN_READ_JUMP,303,&page)==PN_OK);assert(pn_reader_complete(&reader,&page,true,100)==PN_OK);
    assert(pn_reader_prepare(&reader,PN_READ_NEXT,0,&page)==PN_OK);
    layout.width=40;assert(pn_reader_reflow(&reader,&layout)==PN_OK);
    assert(pn_reader_complete(&reader,&page,true,101)==PN_STALE_JOB);
    assert(pn_reader_prepare(&reader,PN_READ_CURRENT,0,&page)==PN_OK && page.anchor.begin==303);
    assert(pn_reader_complete(&reader,&page,true,102)==PN_OK);
    assert(pn_reader_progress(&reader,&position)==PN_OK && position.source_offset==303);
    assert(pn_reader_prepare(&reader,PN_READ_NEXT,0,&page)==PN_OK);m.available=false;
    assert(pn_reader_complete(&reader,&page,true,103)==PN_STALE_MEDIA);
    assert(pn_reader_progress(&reader,&position)==PN_OK && position.source_offset==303);
    puts("reader: prepare/confirm, failure, forged receipt, previous history, jump and reflow passed");return 0;
}
