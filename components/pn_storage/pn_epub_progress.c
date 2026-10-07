/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB语义位置载荷与A/B读写，不猜测损坏记录。
 * English: EPUB semantic payloads and A/B I/O without guessing damaged records.
 * 冻结：不读正文，不存临时事件顺序。/ Frozen: no body reads or transient event-order storage.
 */
#include "pn_epub_progress.h"
#include <string.h>
#define HEADER 68
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static unsigned get16(const uint8_t *p){return p[0]|((unsigned)p[1]<<8);}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint32_t get32(const uint8_t *p){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)p[i]<<(i*8);return v;}
static bool path_valid(const char *path,size_t n){
    if(!n || path[0]=='/' || path[n-1]=='/')return false;
    for(size_t i=0;i<n;){uint8_t a=(uint8_t)path[i];unsigned bytes;uint32_t cp;
        if(a<128){bytes=1;cp=a;}else if(a>=0xc2 && a<=0xdf){bytes=2;cp=a&31;}else if(a>=0xe0 && a<=0xef){bytes=3;cp=a&15;}else if(a>=0xf0 && a<=0xf4){bytes=4;cp=a&7;}else return false;
        if(bytes>n-i)return false;
        for(unsigned j=1;j<bytes;j++){uint8_t c=(uint8_t)path[i+j];if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63);}
        if((bytes==2 && cp<128) || (bytes==3 && cp<2048) || (bytes==4 && cp<65536) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff) || cp<32 || cp==127 || cp==':' || cp=='?' || cp=='#' || cp==92)return false;
        i+=bytes;
    }
    for(size_t a=0;a<n;){size_t b=a;while(b<n && path[b]!='/')b++;size_t size=b-a;
        if(!size || (size==1 && path[a]=='.') || (size==2 && path[a]=='.' && path[a+1]=='.'))return false;
        a=b+1;
    }
    return true;
}
pn_status_t pn_epub_progress_validate(const pn_epub_progress_t *p){
    if(!p)return PN_INVALID;
    if(p->location.version!=PN_XHTML_LOCATOR_VERSION || (p->location.position.kind!=PN_XHTML_ELEMENT && p->location.position.kind!=PN_XHTML_TEXT_POSITION))return PN_UNSUPPORTED;
    const char *end=memchr(p->location.path,0,sizeof p->location.path);
    if(!end || !path_valid(p->location.path,(size_t)(end-p->location.path)) || p->location.chapter_start || !p->location.position.element)return PN_INVALID;
    if(p->location.position.kind==PN_XHTML_ELEMENT && (p->location.position.run || p->location.position.offset))return PN_INVALID;
    return PN_OK;
}
pn_status_t pn_epub_progress_save(const pn_journal_io_t *io,pn_pool_t *pool,const pn_epub_progress_t *progress){
    if(!io || !pool)return PN_INVALID;
    pn_status_t status=pn_epub_progress_validate(progress);if(status!=PN_OK)return status;
    uint8_t bytes[HEADER+PN_EPUB_LOCATION_PATH_MAX-1]={0};size_t n=strlen(progress->location.path);
    memcpy(bytes,"PNEP",4);put16(bytes+4,1);memcpy(bytes+8,progress->book.sha256,32);
    put16(bytes+40,(uint16_t)progress->location.version);bytes[42]=(uint8_t)progress->location.position.kind;
    put64(bytes+44,progress->location.position.element);put64(bytes+52,progress->location.position.offset);put32(bytes+60,progress->location.position.run);put16(bytes+64,(uint16_t)n);memcpy(bytes+HEADER,progress->location.path,n);
    return pn_blob_save(io,pool,bytes,HEADER+n);
}
pn_status_t pn_epub_progress_load(const pn_journal_io_t *io,pn_pool_t *pool,const pn_book_id_t *book,pn_epub_progress_t *out){
    if(!io || !pool || !book || !out)return PN_INVALID;
    uint8_t *bytes=pn_alloc(pool,PN_BLOB_MAX);if(!bytes)return PN_NO_MEMORY;size_t size=0;
    pn_status_t status=pn_blob_load(io,pool,bytes,PN_BLOB_MAX,&size);pn_epub_progress_t value={0};
    if(status==PN_OK){
        if(size<HEADER || memcmp(bytes,"PNEP",4))status=PN_CORRUPT;
        else if(get16(bytes+4)!=1 || get16(bytes+6) || get16(bytes+40)!=PN_XHTML_LOCATOR_VERSION || bytes[42]>1 || bytes[43] || get16(bytes+66))status=PN_UNSUPPORTED;
        else{size_t n=get16(bytes+64);
            if(!n || n>=PN_EPUB_LOCATION_PATH_MAX || size!=HEADER+n || memchr(bytes+HEADER,0,n))status=PN_CORRUPT;
            else{memcpy(value.book.sha256,bytes+8,32);memcpy(value.location.path,bytes+HEADER,n);value.location.version=get16(bytes+40);
                value.location.position=(pn_xhtml_position_t){.element=get64(bytes+44),.offset=get64(bytes+52),.run=get32(bytes+60),.kind=(pn_xhtml_position_kind_t)bytes[42]};
                if(pn_epub_progress_validate(&value)!=PN_OK)status=PN_CORRUPT;
                else if(memcmp(value.book.sha256,book->sha256,32))status=PN_STALE_JOB;
            }
        }
    }
    if(status==PN_OK)*out=value;
    pn_free(bytes);return status;
}
