/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：严格流式TXT解码，保留每个字符的原始字节范围。
 * English: strict streamed TXT decoding preserving original byte ranges per character.
 * 冻结：不静默替换、不整书分配；source不可并发变更，owner串行调用。
 * Frozen: no silent replacement or whole-book allocation; immutable source and serialized owner calls.
 */
#include "pn_text.h"
#include "pn_gbk.generated.h"
#include <string.h>
static pn_status_t byte_at(pn_text_reader_t *r,uint64_t offset,uint8_t *out) {
    if(offset>=r->source.size)return PN_EMPTY;
    if(!r->cache_size || offset<r->cache_begin || offset-r->cache_begin>=r->cache_size) {
        uint64_t remaining=r->source.size-offset;size_t request=sizeof r->cache;
        if(remaining<request)request=(size_t)remaining;
        size_t n=0;pn_status_t status=r->source.read_at(r->source.ctx,offset,r->cache,request,&n);
        r->cache_size=0;
        if(status!=PN_OK)return status;
        if(!n || n>request)return PN_IO;
        r->cache_begin=offset;r->cache_size=n;
    }
    *out=r->cache[offset-r->cache_begin];return PN_OK;
}
static pn_status_t required(pn_text_reader_t *r,uint64_t offset,uint8_t *out) {
    pn_status_t s=byte_at(r,offset,out);return s==PN_EMPTY?PN_CORRUPT:s;
}
static pn_status_t unit(pn_text_reader_t *r,uint64_t offset,uint16_t *out) {
    uint8_t a,b;pn_status_t s=required(r,offset,&a);if(s!=PN_OK)return s;
    if(offset==UINT64_MAX)return PN_CORRUPT;
    s=required(r,offset+1,&b);if(s!=PN_OK)return s;
    *out=r->encoding==PN_TEXT_UTF16_LE ? (uint16_t)(a|(b<<8)):(uint16_t)((a<<8)|b);return PN_OK;
}
static pn_status_t scalar(pn_text_reader_t *r,uint64_t offset,uint32_t *cp,uint64_t *end) {
    uint8_t first;pn_status_t status=byte_at(r,offset,&first);if(status!=PN_OK)return status;
    if(r->encoding==PN_TEXT_UTF16_LE || r->encoding==PN_TEXT_UTF16_BE) {
        if((offset-r->content_begin)&1)return PN_CORRUPT;
        uint16_t a;status=unit(r,offset,&a);if(status!=PN_OK)return status;
        if(a>=0xdc00 && a<=0xdfff)return PN_CORRUPT;
        if(a>=0xd800 && a<=0xdbff) {
            if(r->source.size-offset<4)return PN_CORRUPT;
            uint16_t b;status=unit(r,offset+2,&b);if(status!=PN_OK)return status;
            if(b<0xdc00 || b>0xdfff)return PN_CORRUPT;
            *cp=0x10000+(((uint32_t)a-0xd800)<<10)+(b-0xdc00);*end=offset+4;
        } else {*cp=a;*end=offset+2;}
        return PN_OK;
    }
    if(first<0x80){*cp=first;*end=offset+1;return PN_OK;}
    if(r->encoding==PN_TEXT_GBK) {
        if(first<0x81 || first>0xfe || r->source.size-offset<2)return PN_CORRUPT;
        uint8_t trail;status=required(r,offset+1,&trail);if(status!=PN_OK)return status;
        if(trail<0x40 || trail>0xfe || trail==0x7f)return PN_CORRUPT;
        uint16_t value=pn_gbk_table[(first-0x81)*191+(trail-0x40)];if(!value)return PN_CORRUPT;
        *cp=value;*end=offset+2;return PN_OK;
    }
    unsigned count;uint32_t value,minimum;
    if(first>=0xc2 && first<=0xdf){count=2;value=first&0x1f;minimum=0x80;}
    else if(first>=0xe0 && first<=0xef){count=3;value=first&0x0f;minimum=0x800;}
    else if(first>=0xf0 && first<=0xf4){count=4;value=first&7;minimum=0x10000;}
    else return PN_CORRUPT;
    if(r->source.size-offset<count)return PN_CORRUPT;
    for(unsigned i=1;i<count;i++) {
        uint8_t b;status=required(r,offset+i,&b);if(status!=PN_OK)return status;
        if((b&0xc0)!=0x80)return PN_CORRUPT;
        value=(value<<6)|(b&0x3f);
    }
    if(value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff))return PN_CORRUPT;
    *cp=value;*end=offset+count;return PN_OK;
}
pn_status_t pn_text_open(pn_text_reader_t *reader,const pn_text_source_t *source,pn_text_encoding_t encoding) {
    if(!reader || !source || !source->read_at)return PN_INVALID;
    if(encoding!=PN_TEXT_AUTO && (encoding<PN_TEXT_UTF8 || encoding>PN_TEXT_GBK))return PN_UNSUPPORTED;
    if(source->validate){pn_status_t status=source->validate(source->ctx);if(status!=PN_OK)return status;}
    pn_text_reader_t r={.source=*source,.encoding=encoding==PN_TEXT_AUTO?PN_TEXT_UTF8:encoding};
    uint8_t prefix[3]={0};size_t count=source->size<3?(size_t)source->size:3;
    for(size_t i=0;i<count;i++){pn_status_t s=byte_at(&r,i,&prefix[i]);if(s!=PN_OK)return s;}
    pn_text_encoding_t bom=PN_TEXT_AUTO;uint64_t skip=0;
    if(count>=3 && prefix[0]==0xef && prefix[1]==0xbb && prefix[2]==0xbf){bom=PN_TEXT_UTF8;skip=3;}
    else if(count>=2 && prefix[0]==0xff && prefix[1]==0xfe){bom=PN_TEXT_UTF16_LE;skip=2;}
    else if(count>=2 && prefix[0]==0xfe && prefix[1]==0xff){bom=PN_TEXT_UTF16_BE;skip=2;}
    if(bom!=PN_TEXT_AUTO){if(encoding!=PN_TEXT_AUTO && encoding!=bom)return PN_CORRUPT;r.encoding=bom;r.content_begin=skip;}
    if((r.encoding==PN_TEXT_UTF16_LE || r.encoding==PN_TEXT_UTF16_BE) && ((source->size-r.content_begin)&1))return PN_CORRUPT;
    r.cursor=r.content_begin;*reader=r;return PN_OK;
}
pn_status_t pn_text_next(pn_text_reader_t *r,pn_text_char_t *out) {
    if(!r || !out || !r->source.read_at || r->encoding<PN_TEXT_UTF8 || r->encoding>PN_TEXT_GBK || r->cursor>r->source.size)return PN_INVALID;
    if(r->source.validate){pn_status_t status=r->source.validate(r->source.ctx);if(status!=PN_OK)return status;}
    uint32_t cp;uint64_t end;pn_status_t status=scalar(r,r->cursor,&cp,&end);if(status!=PN_OK)return status;
    if(cp==13) {
        uint32_t next;uint64_t after;status=scalar(r,end,&next,&after);
        if(status!=PN_OK && status!=PN_EMPTY && status!=PN_CORRUPT)return status;
        if(status==PN_OK && next==10)end=after;
        cp=10;
    }
    *out=(pn_text_char_t){cp,r->cursor,end};r->cursor=end;return PN_OK;
}
pn_status_t pn_text_seek(pn_text_reader_t *r,uint64_t offset) {
    if(!r || !r->source.read_at || offset<r->content_begin || offset>r->source.size)return PN_INVALID;
    if(r->source.validate){pn_status_t status=r->source.validate(r->source.ctx);if(status!=PN_OK)return status;}
    pn_text_reader_t probe=*r;probe.cursor=probe.content_begin;
    while(probe.cursor<offset) {
        pn_text_char_t c;pn_status_t s=pn_text_next(&probe,&c);if(s!=PN_OK)return s;
        if(probe.cursor>offset)return PN_INVALID;
    }
    *r=probe;return PN_OK;
}

pn_status_t pn_text_probe(const pn_text_source_t *source,uint64_t limit,pn_text_encoding_t *encoding) {
    if(!source || !source->read_at || !encoding)return PN_INVALID;
    if(source->size>limit)return PN_LIMIT;
    pn_text_reader_t reader;pn_status_t status=pn_text_open(&reader,source,PN_TEXT_AUTO);
    if(status!=PN_OK)return status;
    pn_text_encoding_t selected=reader.encoding;bool has_bom=reader.content_begin!=0;
    pn_text_char_t character;
    while((status=pn_text_next(&reader,&character))==PN_OK){}
    if(status==PN_CORRUPT && !has_bom) {
        selected=PN_TEXT_GBK;status=pn_text_open(&reader,source,selected);if(status!=PN_OK)return status;
        while((status=pn_text_next(&reader,&character))==PN_OK){}
    }
    if(status!=PN_EMPTY)return status;
    *encoding=selected;return PN_OK;
}
