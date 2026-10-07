/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：严格流式TXT解码，保留每个字符的原始字节范围。
 * English: strict streamed TXT decoding preserving original byte ranges per character.
 * 冻结：不静默替换、不整书分配；source不可并发变更，owner串行调用。
 * Frozen: no silent replacement or whole-book allocation; immutable source and serialized owner calls.
 */
#include "pn_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {const uint8_t *data;size_t size;size_t chunk;bool fail;} memory_t;
static pn_status_t read_at(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n){
    memory_t *m=ctx;if(m->fail)return PN_STALE_MEDIA;
    assert(offset<=m->size);size_t count=m->size-(size_t)offset;
    if(count>cap)count=cap;
    if(count>m->chunk)count=m->chunk;
    memcpy(out,m->data+offset,count);*n=count;return PN_OK;
}
static void expect(pn_text_reader_t *r,uint32_t cp,uint64_t begin,uint64_t end){pn_text_char_t c;assert(pn_text_next(r,&c)==PN_OK);assert(c.codepoint==cp && c.begin==begin && c.end==end);}
int main(void){
    const uint8_t utf8[]={0xef,0xbb,0xbf,'A',0xe4,0xb8,0xad,0xf0,0x9f,0x98,0x80,'\r','\n','B','\r','C'};
    memory_t m={utf8,sizeof utf8,1,false};pn_text_source_t s={&m,m.size,read_at,NULL};pn_text_reader_t r;
    assert(pn_text_open(&r,&s,PN_TEXT_AUTO)==PN_OK && r.content_begin==3 && r.encoding==PN_TEXT_UTF8);
    expect(&r,'A',3,4);expect(&r,0x4e2d,4,7);expect(&r,0x1f600,7,11);expect(&r,'\n',11,13);
    expect(&r,'B',13,14);expect(&r,'\n',14,15);expect(&r,'C',15,16);
    pn_text_char_t c={.codepoint=42};assert(pn_text_next(&r,&c)==PN_EMPTY && c.codepoint==42);
    assert(pn_text_seek(&r,5)==PN_INVALID && r.cursor==16);
    assert(pn_text_seek(&r,12)==PN_INVALID);assert(pn_text_seek(&r,4)==PN_OK);expect(&r,0x4e2d,4,7);
    const uint8_t le[]={0xff,0xfe,0x2d,0x4e,0x3d,0xd8,0x00,0xde,0x0d,0x00,0x0a,0x00};
    m=(memory_t){le,sizeof le,1,false};s.size=m.size;
    assert(pn_text_open(&r,&s,PN_TEXT_AUTO)==PN_OK && r.encoding==PN_TEXT_UTF16_LE);
    expect(&r,0x4e2d,2,4);expect(&r,0x1f600,4,8);expect(&r,'\n',8,12);
    assert(pn_text_seek(&r,6)==PN_INVALID && r.cursor==12);assert(pn_text_seek(&r,3)==PN_INVALID);
    const uint8_t gbk[]={0xd6,0xd0,0xce,0xc4,'\r','\n'};m=(memory_t){gbk,sizeof gbk,1,false};s.size=m.size;
    assert(pn_text_open(&r,&s,PN_TEXT_GBK)==PN_OK);expect(&r,0x4e2d,0,2);expect(&r,0x6587,2,4);expect(&r,'\n',4,6);
    assert(pn_text_seek(&r,1)==PN_INVALID);
    const uint8_t bad[][4]={{0xc0,0xaf,0,0},{0xed,0xa0,0x80,0},{0xf4,0x90,0x80,0x80},{0xe4,0xb8,0,0}};
    const size_t sizes[]={2,3,4,2};
    for(size_t i=0;i<4;i++){m=(memory_t){bad[i],sizes[i],1,false};s.size=m.size;assert(pn_text_open(&r,&s,PN_TEXT_UTF8)==PN_OK);c.codepoint=42;assert(pn_text_next(&r,&c)==PN_CORRUPT && r.cursor==0 && c.codepoint==42);}
    m.fail=true;r.cache_size=0;assert(pn_text_next(&r,&c)==PN_STALE_MEDIA && r.cursor==0);
    m=(memory_t){gbk,sizeof gbk,1,false};s.size=m.size;pn_text_encoding_t detected=PN_TEXT_UTF8;
    assert(pn_text_probe(&s,m.size,&detected)==PN_OK && detected==PN_TEXT_GBK);
    assert(pn_text_probe(&s,m.size-1,&detected)==PN_LIMIT && detected==PN_TEXT_GBK);
    const uint8_t malformed_le[]={0xff,0xfe,0x00,0xd8};m=(memory_t){malformed_le,sizeof malformed_le,1,false};s.size=m.size;
    assert(pn_text_probe(&s,m.size,&detected)==PN_CORRUPT && detected==PN_TEXT_GBK);
    puts("text: encodings, short reads, source spans, boundaries and strict failures passed");return 0;
}
