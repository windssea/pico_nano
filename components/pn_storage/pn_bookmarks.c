/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：每书书签目录是可重建索引，独立记录保持A/B恢复边界。
 * English: per-book directory is a reconstructible index, with independent A/B record recovery.
 * 冻结：不擦坏记录；保留删除墓碑与已观察的ID；owner串行操作。
 * Frozen: preserve corrupt records, deletion tombstones and observed IDs; serialize owner operations.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_bookmarks.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {size_t active,matching;uint64_t highest,duplicate;} summary_t;
static pn_status_t allowed(pn_bookmarks_t *m,bool write){
    if(!m || !m->media)return PN_INVALID;
    pn_status_t status=pn_media_validate(m->media,&m->lease);if(status!=PN_OK)return status;
    return m->lease.access==PN_MEDIA_USB || (write && m->lease.access!=PN_MEDIA_WRITE)?PN_INVALID:PN_OK;
}
static bool same_source(const pn_txt_progress_t *a,const pn_txt_progress_t *b){
    return !memcmp(a->book.sha256,b->book.sha256,32) && a->source_size==b->source_size &&
        a->encoding==b->encoding && a->paragraph_version==b->paragraph_version;
}
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
static pn_status_t bind(pn_bookmarks_t *m,uint64_t id,pn_journal_files_t *files,pn_journal_io_t *io){
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
static pn_status_t load(pn_bookmarks_t *m,uint64_t id,pn_txt_bookmark_t *mark){
    pn_status_t checked=directory(m->directory);if(checked!=PN_OK)return checked;
    pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=bind(m,id,&files,&io);
    if(status==PN_OK)status=pn_txt_bookmark_load(&io,&m->expected.book,id,mark);
    if(status==PN_OK && !same_source(&mark->position,&m->expected))status=PN_CORRUPT;
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
static pn_status_t scan(pn_bookmarks_t *m,uint64_t after,pn_txt_bookmark_t *items,size_t capacity,
    size_t *count,summary_t *summary,const pn_txt_progress_t *match){
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
        pn_txt_bookmark_t mark;status=load(m,id,&mark);if(status!=PN_OK)break;
        if(mark.deleted)continue;
        if(++summary->active>PN_BOOKMARKS_MAX){status=PN_LIMIT;break;}
        if(match && mark.position.source_offset==match->source_offset)summary->duplicate=id;
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
pn_status_t pn_bookmarks_init(pn_bookmarks_t *m,pn_media_t *media,const pn_media_lease_t *lease,
    const char *root,const pn_txt_progress_t *p){
    if(!m || !media || !lease || !root || !*root || !p || p->source_offset>p->source_size ||
       !p->paragraph_version || p->encoding<PN_TEXT_UTF8 || p->encoding>PN_TEXT_GBK)return PN_INVALID;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    struct stat info;if(stat(root,&info)!=0 || !S_ISDIR(info.st_mode))return PN_IO;
    char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",p->book.sha256[i]);
    pn_bookmarks_t next={.media=media,.lease=*lease,.expected=*p};
    int n=snprintf(next.directory,sizeof next.directory,"%s/%s.marks",root,hash);
    if(n<0 || (size_t)n+19>=sizeof next.directory)return PN_LIMIT;
    *m=next;return PN_OK;
}
pn_status_t pn_bookmarks_list(pn_bookmarks_t *m,uint64_t after,pn_txt_bookmark_t *items,
    size_t capacity,size_t *count,bool *more){
    if(!count || !more)return PN_INVALID;
    *count=0;*more=false;if(!items || !capacity || capacity>PN_BOOKMARKS_MAX)return PN_INVALID;
    summary_t summary;pn_status_t status=scan(m,after,items,capacity,count,&summary,NULL);
    if(status==PN_OK)*more=summary.matching>*count;
    return status;
}
pn_status_t pn_bookmarks_add(pn_bookmarks_t *m,const pn_txt_progress_t *p,const char *label,uint64_t *id){
    if(!p || !label || !id)return PN_INVALID;
    pn_status_t status=allowed(m,true);if(status!=PN_OK)return status;
    if(p->source_offset>p->source_size || !same_source(p,&m->expected))return PN_STALE_JOB;
    summary_t summary;size_t count;status=scan(m,0,NULL,0,&count,&summary,p);if(status!=PN_OK)return status;
    if(summary.duplicate){*id=summary.duplicate;return PN_OK;}
    if(summary.active>=PN_BOOKMARKS_MAX || summary.highest==UINT64_MAX)return PN_LIMIT;
    if(mkdir(m->directory,0700)!=0 && errno!=EEXIST)return PN_IO;
    status=directory(m->directory);if(status!=PN_OK)return status;
    uint64_t next=summary.highest+1;pn_journal_files_t files;pn_journal_io_t io;
    status=bind(m,next,&files,&io);if(status==PN_OK)status=pn_txt_bookmark_create(&io,p,next,label);
    if(status==PN_OK)*id=next;
    return status;
}
pn_status_t pn_bookmarks_rename(pn_bookmarks_t *m,uint64_t id,const char *label){
    pn_status_t status=allowed(m,true);if(status!=PN_OK)return status;
    pn_txt_bookmark_t mark;status=load(m,id,&mark);if(status!=PN_OK)return status;
    pn_journal_files_t files;pn_journal_io_t io;status=bind(m,id,&files,&io);
    return status==PN_OK?pn_txt_bookmark_rename(&io,&m->expected.book,id,label):status;
}
pn_status_t pn_bookmarks_delete(pn_bookmarks_t *m,uint64_t id){
    pn_status_t status=allowed(m,true);if(status!=PN_OK)return status;
    pn_txt_bookmark_t mark;status=load(m,id,&mark);if(status!=PN_OK)return status;
    pn_journal_files_t files;pn_journal_io_t io;status=bind(m,id,&files,&io);
    return status==PN_OK?pn_txt_bookmark_delete(&io,&m->expected.book,id):status;
}
pn_status_t pn_bookmarks_position(pn_bookmarks_t *m,uint64_t id,pn_txt_progress_t *p){
    if(!p)return PN_INVALID;
    pn_status_t status=allowed(m,false);if(status!=PN_OK)return status;
    pn_txt_bookmark_t mark;status=load(m,id,&mark);if(status!=PN_OK)return status;
    if(mark.deleted)return PN_EMPTY;
    *p=mark.position;return PN_OK;
}
