/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：每书书签目录是可重建索引，独立记录保持A/B恢复边界。
 * English: per-book directory is a reconstructible index, with independent A/B record recovery.
 * 冻结：不擦坏记录；保留删除墓碑与已观察的ID；owner串行操作。
 * Frozen: preserve corrupt records, deletion tombstones and observed IDs; serialize owner operations.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_bookmarks.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
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
static pn_status_t record_save(pn_epub_bookmarks_t *m,const pn_journal_io_t *io,const pn_epub_bookmark_t *mark){
    size_t label;pn_status_t status=label_size(mark->label,&label);if(status!=PN_OK)return status;
    status=pn_epub_progress_validate(&mark->position);if(status!=PN_OK)return status;
    uint8_t bytes[80+PN_EPUB_LOCATION_PATH_MAX-1+PN_BOOKMARK_LABEL_MAX]={0};size_t path=strlen(mark->position.location.path);
    memcpy(bytes,"PNEM",4);put(bytes+4,1,2);put(bytes+6,mark->deleted?1:0,2);put(bytes+8,mark->id,8);memcpy(bytes+16,mark->position.book.sha256,32);
    put(bytes+48,mark->position.location.version,2);bytes[50]=(uint8_t)mark->position.location.position.kind;
    put(bytes+52,mark->position.location.position.element,8);put(bytes+60,mark->position.location.position.offset,8);put(bytes+68,mark->position.location.position.run,4);
    put(bytes+72,path,2);put(bytes+74,label,2);memcpy(bytes+80,mark->position.location.path,path);memcpy(bytes+80+path,mark->label,label);
    return pn_blob_save(io,m->pool,bytes,80+path+label);
}
static pn_status_t record_load(pn_epub_bookmarks_t *m,const pn_journal_io_t *io,uint64_t id,pn_epub_bookmark_t *out){
    uint8_t bytes[80+PN_EPUB_LOCATION_PATH_MAX-1+PN_BOOKMARK_LABEL_MAX];size_t n=0;pn_status_t status=pn_blob_load(io,m->pool,bytes,sizeof bytes,&n);if(status!=PN_OK)return status;
    if(n<80 || memcmp(bytes,"PNEM",4))return PN_CORRUPT;
    if(get(bytes+4,2)!=1 || get(bytes+6,2)>1 || get(bytes+48,2)!=1 || bytes[50]>1 || bytes[51] || get(bytes+76,4))return PN_UNSUPPORTED;
    size_t path=(size_t)get(bytes+72,2),label=(size_t)get(bytes+74,2);
    if(!path || path>=PN_EPUB_LOCATION_PATH_MAX || label>PN_BOOKMARK_LABEL_MAX || n!=80+path+label || memchr(bytes+80,0,path+label))return PN_CORRUPT;
    pn_epub_bookmark_t mark={.id=get(bytes+8,8),.deleted=get(bytes+6,2)==1};memcpy(mark.position.book.sha256,bytes+16,32);memcpy(mark.position.location.path,bytes+80,path);memcpy(mark.label,bytes+80+path,label);
    mark.position.location.version=(unsigned)get(bytes+48,2);mark.position.location.position=(pn_xhtml_position_t){.kind=(pn_xhtml_position_kind_t)bytes[50],.element=get(bytes+52,8),.offset=get(bytes+60,8),.run=(uint32_t)get(bytes+68,4)};
    size_t actual;if(!mark.id || pn_epub_progress_validate(&mark.position)!=PN_OK || label_size(mark.label,&actual)!=PN_OK || actual!=label)return PN_CORRUPT;
    if(mark.id!=id || memcmp(mark.position.book.sha256,m->book.sha256,32))return PN_STALE_JOB;
    *out=mark;return PN_OK;
}

typedef struct {size_t active,matching;uint64_t highest,duplicate;} summary_t;
static pn_status_t allowed(pn_epub_bookmarks_t *m,bool write){
    if(!m || !m->media)return PN_INVALID;
    pn_status_t status=pn_media_validate(m->media,&m->lease);if(status!=PN_OK)return status;
    return m->lease.access==PN_MEDIA_USB || (write && m->lease.access!=PN_MEDIA_WRITE)?PN_INVALID:PN_OK;
}
static bool same_location(const pn_epub_progress_t *a,const pn_epub_progress_t *b){return !strcmp(a->location.path,b->location.path) && a->location.version==b->location.version && a->location.position.kind==b->location.position.kind && a->location.position.element==b->location.position.element && a->location.position.run==b->location.position.run && a->location.position.offset==b->location.position.offset;}
static pn_status_t regular(const char *path){
    struct stat info;
#ifdef ESP_PLATFORM
    int result=stat(path,&info);
#else
    int result=lstat(path,&info);
#endif
    if(result!=0)return errno==ENOENT?PN_EMPTY:PN_IO;
    return S_ISREG(info.st_mode)?PN_OK:PN_CORRUPT;
}
static pn_status_t directory(const char *path){
    struct stat info;
#ifdef ESP_PLATFORM
    int result=stat(path,&info);
#else
    int result=lstat(path,&info);
#endif
    if(result!=0)return errno==ENOENT?PN_EMPTY:PN_IO;
    return S_ISDIR(info.st_mode)?PN_OK:PN_CORRUPT;
}
static pn_status_t bind(pn_epub_bookmarks_t *m,uint64_t id,pn_journal_files_t *files,pn_journal_io_t *io){
    if(!id)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];
    int n=snprintf(a,sizeof a,"%s/%016llx.a",m->directory,(unsigned long long)id);
    if(n<0 || (size_t)n>=sizeof a)return PN_LIMIT;
    n=snprintf(b,sizeof b,"%s/%016llx.b",m->directory,(unsigned long long)id);
    if(n<0 || (size_t)n>=sizeof b)return PN_LIMIT;
    pn_status_t status=regular(a);if(status!=PN_OK && status!=PN_EMPTY)return status;
    status=regular(b);if(status!=PN_OK && status!=PN_EMPTY)return status;
    return pn_journal_files_init(files,m->media,&m->lease,a,b,io);
}
static pn_status_t load(pn_epub_bookmarks_t *m,uint64_t id,pn_epub_bookmark_t *mark){
    pn_status_t checked=directory(m->directory);if(checked!=PN_OK)return checked;
    pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=bind(m,id,&files,&io);
    if(status==PN_OK)status=record_load(m,&io,id,mark);
    
    return status;
}
static bool record_name(const char *name,uint64_t *id){
    if(strlen(name)!=18 || name[16]!='.' || (name[17]!='a' && name[17]!='b'))return false;
    uint64_t value=0;for(unsigned i=0;i<16;i++){
        unsigned char c=(unsigned char)name[i];unsigned digit;
        if(c>='0' && c<='9')digit=c-'0';else if(c>='a' && c<='f')digit=c-'a'+10;else return false;
        value=(value<<4)|digit;
    }
    if(!value)return false;
    *id=value;return true;
}
static pn_status_t scan(pn_epub_bookmarks_t *m,uint64_t after,pn_epub_bookmark_t *items,size_t capacity,
    size_t *count,summary_t *summary,const pn_epub_progress_t *match){
    *count=0;memset(summary,0,sizeof *summary);pn_status_t status=allowed(m,false);if(status!=PN_OK)return status;
    status=directory(m->directory);if(status==PN_EMPTY)return allowed(m,false);if(status!=PN_OK)return status;
    DIR *dir=opendir(m->directory);
    if(!dir)return errno==ENOENT?allowed(m,false):PN_IO;
    for(;;){
        status=allowed(m,false);if(status!=PN_OK)break;
        errno=0;struct dirent *entry=readdir(dir);if(!entry){if(errno)status=PN_IO;break;}
        uint64_t id;if(!record_name(entry->d_name,&id))continue;
        if(entry->d_name[17]=='b'){
            char path[PN_JOURNAL_PATH_MAX];int n=snprintf(path,sizeof path,"%s/%016llx.a",m->directory,(unsigned long long)id);
            if(n<0 || (size_t)n>=sizeof path){status=PN_LIMIT;break;}
            status=regular(path);if(status==PN_OK)continue;if(status!=PN_EMPTY)break;
        }
        if(id>summary->highest)summary->highest=id;
        pn_epub_bookmark_t mark;status=load(m,id,&mark);if(status!=PN_OK)break;
        if(mark.deleted)continue;
        if(++summary->active>PN_EPUB_BOOKMARKS_MAX){status=PN_LIMIT;break;}
        if(match && same_location(&mark.position,match))summary->duplicate=id;
        if(id<=after)continue;
        summary->matching++;
        if(!items || !capacity)continue;
        size_t at=0;while(at<*count && items[at].id<id)at++;
        if(at>=capacity)continue;
        size_t next=*count<capacity?*count+1:capacity;
        for(size_t i=next-1;i>at;i--)items[i]=items[i-1];
        items[at]=mark;*count=next;
    }
    if(closedir(dir)!=0 && status==PN_OK)status=PN_IO;
    pn_status_t current=allowed(m,false);if(current!=PN_OK)status=current;
    if(status!=PN_OK)*count=0;
    return status;
}
pn_status_t pn_epub_bookmarks_init(pn_epub_bookmarks_t *m,pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *root,const pn_book_id_t *book){
    if(!m || !pool || !media || !lease || !root || !*root || !book)return PN_INVALID;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    struct stat info;if(stat(root,&info)!=0 || !S_ISDIR(info.st_mode))return PN_IO;
    char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",book->sha256[i]);
    pn_epub_bookmarks_t next={.pool=pool,.media=media,.lease=*lease,.book=*book};
    int n=snprintf(next.directory,sizeof next.directory,"%s/%s.emarks",root,hash);
    if(n<0 || (size_t)n+19>=sizeof next.directory)return PN_LIMIT;
    *m=next;return PN_OK;
}
pn_status_t pn_epub_bookmarks_list(pn_epub_bookmarks_t *m,uint64_t after,pn_epub_bookmark_t *items,
    size_t capacity,size_t *count,bool *more){
    if(!count || !more)return PN_INVALID;
    *count=0;*more=false;if(!items || !capacity || capacity>PN_EPUB_BOOKMARKS_MAX)return PN_INVALID;
    summary_t summary;pn_status_t status=scan(m,after,items,capacity,count,&summary,NULL);
    if(status==PN_OK)*more=summary.matching>*count;
    return status;
}
pn_status_t pn_epub_bookmarks_add(pn_epub_bookmarks_t *m,const pn_epub_progress_t *p,const char *label,uint64_t *id){
    if(!p || !label || !id)return PN_INVALID;
    pn_status_t status=allowed(m,true);if(status!=PN_OK)return status;
    if(memcmp(p->book.sha256,m->book.sha256,32))return PN_STALE_JOB;
    status=pn_epub_progress_validate(p);if(status!=PN_OK)return status;
    size_t label_length;status=label_size(label,&label_length);if(status!=PN_OK)return status;
    summary_t summary;size_t count;status=scan(m,0,NULL,0,&count,&summary,p);if(status!=PN_OK)return status;
    if(summary.duplicate){*id=summary.duplicate;return PN_OK;}
    if(summary.active>=PN_EPUB_BOOKMARKS_MAX || summary.highest==UINT64_MAX)return PN_LIMIT;
    if(mkdir(m->directory,0700)!=0 && errno!=EEXIST)return PN_IO;
    status=directory(m->directory);if(status!=PN_OK)return status;
    uint64_t next=summary.highest+1;pn_journal_files_t files;pn_journal_io_t io;
    status=bind(m,next,&files,&io);if(status==PN_OK){pn_epub_bookmark_t mark={.position=*p,.id=next};memcpy(mark.label,label,label_length);status=record_save(m,&io,&mark);}
    if(status==PN_OK)*id=next;
    return status;
}
pn_status_t pn_epub_bookmarks_rename(pn_epub_bookmarks_t *m,uint64_t id,const char *label){
    pn_status_t status=allowed(m,true);if(status!=PN_OK)return status;
    pn_epub_bookmark_t mark;status=load(m,id,&mark);if(status!=PN_OK)return status;
    pn_journal_files_t files;pn_journal_io_t io;status=bind(m,id,&files,&io);
    if(status!=PN_OK)return status;
    if(mark.deleted)return PN_EMPTY;
    size_t length;status=label_size(label,&length);if(status!=PN_OK)return status;
    memset(mark.label,0,sizeof mark.label);memcpy(mark.label,label,length);return record_save(m,&io,&mark);
}
pn_status_t pn_epub_bookmarks_delete(pn_epub_bookmarks_t *m,uint64_t id){
    pn_status_t status=allowed(m,true);if(status!=PN_OK)return status;
    pn_epub_bookmark_t mark;status=load(m,id,&mark);if(status!=PN_OK)return status;
    pn_journal_files_t files;pn_journal_io_t io;status=bind(m,id,&files,&io);
    if(status!=PN_OK)return status;
    if(mark.deleted)return PN_OK;
    mark.deleted=true;return record_save(m,&io,&mark);
}
pn_status_t pn_epub_bookmarks_position(pn_epub_bookmarks_t *m,uint64_t id,pn_epub_progress_t *p){
    if(!p)return PN_INVALID;
    pn_status_t status=allowed(m,false);if(status!=PN_OK)return status;
    pn_epub_bookmark_t mark;status=load(m,id,&mark);if(status!=PN_OK)return status;
    if(mark.deleted)return PN_EMPTY;
    *p=mark.position;return PN_OK;
}
