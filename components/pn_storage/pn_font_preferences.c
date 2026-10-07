/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：字体选择双槽载荷与全局/逐书继承解析。
 * English: dual-slot font selections and global/per-book inheritance resolution.
 * 冻结：损坏/未知记录不回退默认，不写字体或阅读进度。
 * Frozen: damaged/unknown records never fall back to defaults; no font/progress writes.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_font_preferences.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#define HEADER 136u
#define MAX_PAYLOAD (HEADER+2u*(PN_FONT_REFERENCE_PATH_MAX-1u))
static void put(uint8_t *p,uint64_t value,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(value>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t value=0;for(unsigned i=0;i<n;i++)value|=(uint64_t)p[i]<<(8*i);return value;}
static bool zero(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return false;return true;}
static bool path_valid(const char *p,size_t n){
    size_t begin=0;if(!n)return false;
    if(p[0]=='/')begin=1;else if(n>=3 && ((p[0]>='A' && p[0]<='Z') || (p[0]>='a' && p[0]<='z')) && p[1]==':' && p[2]=='/')begin=3;else return false;
    if(begin==n || p[n-1]=='/')return false;
    for(size_t i=0;i<n;){uint8_t a=(uint8_t)p[i];unsigned bytes;uint32_t cp;
        if(a<128){bytes=1;cp=a;}else if(a>=0xc2 && a<=0xdf){bytes=2;cp=a&31;}else if(a>=0xe0 && a<=0xef){bytes=3;cp=a&15;}else if(a>=0xf0 && a<=0xf4){bytes=4;cp=a&7;}else return false;
        if(bytes>n-i)return false;
        for(unsigned j=1;j<bytes;j++){uint8_t c=(uint8_t)p[i+j];if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63);}
        if((bytes==2 && cp<128) || (bytes==3 && cp<2048) || (bytes==4 && cp<65536) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff) || cp<32 || cp==127 || cp==92 || (cp==':' && !(begin==3 && i==1)))return false;
        i+=bytes;
    }
    for(size_t a=begin;a<n;){size_t b=a;while(b<n && p[b]!='/')b++;size_t length=b-a;if(!length || (length==1 && p[a]=='.') || (length==2 && p[a]=='.' && p[a+1]=='.'))return false;a=b+1;}
    return true;
}
static pn_status_t reference_valid(const pn_font_reference_t *r){
    if(!r)return PN_INVALID;
    if((unsigned)r->kind>PN_FONT_FILE)return PN_UNSUPPORTED;
    const char *end=memchr(r->path,0,sizeof r->path);if(!end)return PN_LIMIT;size_t n=(size_t)(end-r->path);
    if(r->kind==PN_FONT_RESIDENT)return !n && !r->size && zero(r->identity.sha256,32)?PN_OK:PN_INVALID;
    return r->size>=12 && r->size<=PN_FONT_REFERENCE_MAX_BYTES && path_valid(r->path,n)?PN_OK:PN_INVALID;
}
pn_status_t pn_font_preferences_validate(const pn_font_preferences_t *p,bool global){
    if(!p || (global && p->inherit))return PN_INVALID;
    pn_status_t status=reference_valid(&p->primary);if(status==PN_OK)status=reference_valid(&p->fallback);if(status!=PN_OK)return status;
    if(p->inherit && (p->primary.kind!=PN_FONT_RESIDENT || p->fallback.kind!=PN_FONT_RESIDENT))return PN_INVALID;
    return PN_OK;
}
pn_status_t pn_font_preferences_save(const pn_journal_io_t *io,pn_pool_t *pool,const pn_book_id_t *book,const pn_font_preferences_t *p){
    if(!io || !pool)return PN_INVALID;
    pn_status_t status=pn_font_preferences_validate(p,!book);if(status!=PN_OK)return status;
    pn_font_preferences_t existing;status=pn_font_preferences_load(io,pool,book,&existing);if(status!=PN_OK && status!=PN_EMPTY)return status;
    uint8_t bytes[MAX_PAYLOAD]={0};memcpy(bytes,"PNFP",4);put(bytes+4,1,2);put(bytes+6,(book?1u:0u)|(p->inherit?2u:0u),2);if(book)memcpy(bytes+8,book->sha256,32);
    const pn_font_reference_t *sources[]={&p->primary,&p->fallback};size_t at=HEADER;
    for(unsigned i=0;i<2;i++){const pn_font_reference_t *r=sources[i];uint8_t *b=bytes+40+i*48;size_t n=strlen(r->path);put(b,r->kind,2);put(b+2,n,2);put(b+4,r->size,8);memcpy(b+12,r->identity.sha256,32);memcpy(bytes+at,r->path,n);at+=n;}
    return pn_blob_save(io,pool,bytes,at);
}
pn_status_t pn_font_preferences_load(const pn_journal_io_t *io,pn_pool_t *pool,const pn_book_id_t *book,pn_font_preferences_t *out){
    if(!io || !pool || !out)return PN_INVALID;
    uint8_t bytes[MAX_PAYLOAD];size_t n;pn_status_t status=pn_blob_load(io,pool,bytes,sizeof bytes,&n);if(status!=PN_OK)return status;
    if(n<HEADER || memcmp(bytes,"PNFP",4))return PN_CORRUPT;
    unsigned flags=(unsigned)get(bytes+6,2);if(get(bytes+4,2)!=1 || flags>3)return PN_UNSUPPORTED;
    if((flags&1)!=(book?1u:0u) || (book?memcmp(bytes+8,book->sha256,32)!=0:!zero(bytes+8,32)))return PN_STALE_JOB;
    pn_font_preferences_t p={.inherit=(flags&2)!=0};pn_font_reference_t *sources[]={&p.primary,&p.fallback};size_t at=HEADER;
    for(unsigned i=0;i<2;i++){const uint8_t *b=bytes+40+i*48;size_t length=(size_t)get(b+2,2);if(get(b,2)>1 || get(b+44,4))return PN_UNSUPPORTED;
        if(length>=PN_FONT_REFERENCE_PATH_MAX || length>n-at || memchr(bytes+at,0,length))return PN_CORRUPT;
        pn_font_reference_t *r=sources[i];r->kind=(pn_font_reference_kind_t)get(b,2);r->size=get(b+4,8);memcpy(r->identity.sha256,b+12,32);memcpy(r->path,bytes+at,length);at+=length;
    }
    if(at!=n || pn_font_preferences_validate(&p,!book)!=PN_OK)return PN_CORRUPT;
    *out=p;return PN_OK;
}
pn_status_t pn_font_preferences_resolve(const pn_journal_io_t *global,const pn_journal_io_t *per_book,pn_pool_t *pool,const pn_book_id_t *book,pn_font_preferences_t *out,bool *from_book){
    if(!global || !per_book || !pool || !book || !out || !from_book)return PN_INVALID;
    pn_font_preferences_t p;pn_status_t status=pn_font_preferences_load(per_book,pool,book,&p);
    if(status==PN_OK && !p.inherit){*out=p;*from_book=true;return PN_OK;}
    if(status!=PN_EMPTY && status!=PN_OK)return status;
    status=pn_font_preferences_load(global,pool,NULL,&p);if(status==PN_OK){*out=p;*from_book=false;}return status;
}
pn_status_t pn_font_preferences_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,const pn_book_id_t *book,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char name[80]="fonts",a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];if(book){for(unsigned i=0;i<32;i++)snprintf(name+i*2,3,"%02x",book->sha256[i]);memcpy(name+64,".fonts",7);}
    int n=snprintf(a,sizeof a,"%s/%s.a",root,name);if(n<0 || (size_t)n>=sizeof a)return PN_LIMIT;
    n=snprintf(b,sizeof b,"%s/%s.b",root,name);if(n<0 || (size_t)n>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(files,media,lease,a,b,io);
}
pn_status_t pn_font_reference_verify(const pn_font_reference_t *r,pn_media_t *media,const pn_media_lease_t *lease){
    pn_status_t status=reference_valid(r);if(status!=PN_OK)return status;
    status=pn_media_validate(media,lease);if(status!=PN_OK)return status;if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    if(r->kind==PN_FONT_RESIDENT)return PN_OK;
    struct stat info;
#ifdef ESP_PLATFORM
    int result=stat(r->path,&info);
#else
    int result=lstat(r->path,&info);
#endif
    if(result || !S_ISREG(info.st_mode) || info.st_size<0)return PN_IO;
    if((uint64_t)info.st_size!=r->size)return PN_STALE_JOB;
    pn_book_id_t id;status=pn_identity_file(media,lease,r->path,PN_FONT_REFERENCE_MAX_BYTES,&id);
    if(status==PN_OK && memcmp(id.sha256,r->identity.sha256,32))status=PN_STALE_JOB;
    return status;
}
