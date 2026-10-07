/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：单个TXT书签的持久记录和操作，不负责书签列表索引。
 * English: persistent single-TXT-bookmark records and operations, excluding list indexing.
 * 冻结：每个书签独立A/B路径；删除留墓碑，不影响阅读进度。
 * Frozen: distinct A/B paths per bookmark; deletion uses tombstones and never changes reading progress.
 */
#include "pn_bookmark.h"
#include <string.h>
static void put(uint8_t *out,uint64_t v,unsigned n) {for(unsigned i=0;i<n;i++)out[i]=(uint8_t)(v>>(i*8));}
static uint64_t get(const uint8_t *in,unsigned n) {uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)in[i]<<(i*8);return v;}
static bool valid_position(const pn_txt_progress_t *p) {
    return p && p->source_offset<=p->source_size && p->paragraph_version &&
        p->encoding>=PN_TEXT_UTF8 && p->encoding<=PN_TEXT_GBK;
}
static pn_status_t label_size(const char *label,size_t *length) {
    if (!label) return PN_INVALID;
    size_t n=0;while(n<=PN_BOOKMARK_LABEL_MAX && label[n])n++;
    if(n>PN_BOOKMARK_LABEL_MAX)return PN_LIMIT;
    for(size_t i=0;i<n;) {
        uint8_t c=(uint8_t)label[i++];uint32_t cp;unsigned extra;
        if(c<0x80) {if(c<0x20 || c==0x7f)return PN_INVALID;continue;}
        if(c>=0xc2 && c<=0xdf){cp=c&0x1f;extra=1;}
        else if(c>=0xe0 && c<=0xef){cp=c&0x0f;extra=2;}
        else if(c>=0xf0 && c<=0xf4){cp=c&7;extra=3;}
        else return PN_INVALID;
        if(n-i<extra)return PN_INVALID;
        for(unsigned j=0;j<extra;j++) {uint8_t t=(uint8_t)label[i++];if((t&0xc0)!=0x80)return PN_INVALID;cp=(cp<<6)|(t&0x3f);}
        if((extra==1 && cp<0x80)||(extra==2 && cp<0x800)||(extra==3 && cp<0x10000)||
            (cp>=0xd800 && cp<=0xdfff)||cp>0x10ffff)return PN_INVALID;
    }
    *length=n;return PN_OK;
}
static pn_status_t save(const pn_journal_io_t *io,const pn_txt_bookmark_t *mark) {
    size_t n=0;pn_status_t status=label_size(mark->label,&n);if(status!=PN_OK)return status;
    uint8_t bytes[120]={0};memcpy(bytes,"PNBM",4);put(bytes+4,1,2);put(bytes+6,mark->deleted?1:0,2);
    memcpy(bytes+8,mark->position.book.sha256,32);put(bytes+40,mark->position.source_offset,8);
    put(bytes+48,mark->position.source_size,8);put(bytes+56,mark->position.paragraph_version,4);
    put(bytes+60,mark->position.encoding,2);put(bytes+62,n,2);put(bytes+64,mark->id,8);
    memcpy(bytes+72,mark->label,n);return pn_journal_save(io,bytes,sizeof bytes);
}
pn_status_t pn_txt_bookmark_create(const pn_journal_io_t *io,const pn_txt_progress_t *position,uint64_t id,const char *label) {
    if(!valid_position(position) || !id)return PN_INVALID;
    size_t n=0;pn_status_t status=label_size(label,&n);if(status!=PN_OK)return status;
    pn_record_t record;status=pn_journal_load(io,&record);
    if(status==PN_OK)return PN_BUSY;
    if(status!=PN_EMPTY)return status;
    pn_txt_bookmark_t mark={.position=*position,.id=id};memcpy(mark.label,label,n);
    return save(io,&mark);
}
pn_status_t pn_txt_bookmark_load(const pn_journal_io_t *io,const pn_book_id_t *book,uint64_t id,pn_txt_bookmark_t *mark) {
    if(!book || !id || !mark)return PN_INVALID;
    pn_record_t record;pn_status_t status=pn_journal_load(io,&record);if(status!=PN_OK)return status;
    const uint8_t *b=record.payload;
    if(record.size!=120 || memcmp(b,"PNBM",4)!=0)return PN_CORRUPT;
    if(get(b+4,2)!=1 || get(b+6,2)>1)return PN_UNSUPPORTED;
    size_t n=(size_t)get(b+62,2);if(n>PN_BOOKMARK_LABEL_MAX)return PN_CORRUPT;
    pn_txt_bookmark_t result={.id=get(b+64,8),.deleted=get(b+6,2)==1};
    memcpy(result.position.book.sha256,b+8,32);result.position.source_offset=get(b+40,8);
    result.position.source_size=get(b+48,8);result.position.paragraph_version=(uint32_t)get(b+56,4);
    result.position.encoding=(pn_text_encoding_t)get(b+60,2);memcpy(result.label,b+72,n);
    size_t actual=0;
    if(!valid_position(&result.position) || !result.id || label_size(result.label,&actual)!=PN_OK || actual!=n)return PN_CORRUPT;
    for(size_t i=n;i<PN_BOOKMARK_LABEL_MAX;i++)if(b[72+i])return PN_CORRUPT;
    if(result.id!=id || memcmp(result.position.book.sha256,book->sha256,32)!=0)return PN_STALE_JOB;
    *mark=result;return PN_OK;
}
pn_status_t pn_txt_bookmark_rename(const pn_journal_io_t *io,const pn_book_id_t *book,uint64_t id,const char *label) {
    size_t n=0;pn_status_t status=label_size(label,&n);if(status!=PN_OK)return status;
    pn_txt_bookmark_t mark;status=pn_txt_bookmark_load(io,book,id,&mark);if(status!=PN_OK)return status;
    if(mark.deleted)return PN_EMPTY;
    memset(mark.label,0,sizeof mark.label);memcpy(mark.label,label,n);return save(io,&mark);
}
pn_status_t pn_txt_bookmark_delete(const pn_journal_io_t *io,const pn_book_id_t *book,uint64_t id) {
    pn_txt_bookmark_t mark;pn_status_t status=pn_txt_bookmark_load(io,book,id,&mark);if(status!=PN_OK)return status;
    if(mark.deleted)return PN_OK;
    mark.deleted=true;return save(io,&mark);
}
pn_status_t pn_txt_bookmark_position(const pn_journal_io_t *io,const pn_book_id_t *book,uint64_t id,pn_txt_progress_t *position) {
    if(!position)return PN_INVALID;
    pn_txt_bookmark_t mark;pn_status_t status=pn_txt_bookmark_load(io,book,id,&mark);if(status!=PN_OK)return status;
    if(mark.deleted)return PN_EMPTY;
    *position=mark.position;return PN_OK;
}
