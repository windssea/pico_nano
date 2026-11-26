/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：每书按句高亮摘录 A/B 日志实现（单条 256B PNHL v1 记录，目录可重建）。
 * English: per-book sentence highlight A/B journal implementation (256-byte PNHL v1 records).
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_highlights.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)(v&0xffu);p[1]=(uint8_t)((v>>8)&0xffu);}
static uint16_t get16(const uint8_t *p){return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(i*8);return v;}

uint64_t pn_highlight_text_hash(const char *utf8){
    uint64_t h=14695981039346656037ULL;
    if(!utf8)return h;
    for(const unsigned char *p=(const unsigned char *)utf8;*p;p++){
        if(*p==' ' || *p=='\t' || *p=='\r' || *p=='\n')continue;
        h^=(uint64_t)*p;h*=1099511628211ULL;
    }
    return h;
}
static pn_status_t allowed(pn_highlights_t *hl,bool write){
    if(!hl || !hl->media)return PN_INVALID;
    pn_status_t s=pn_media_validate(hl->media,&hl->lease);if(s!=PN_OK)return s;
    return hl->lease.access==PN_MEDIA_USB || (write && hl->lease.access!=PN_MEDIA_WRITE)?PN_INVALID:PN_OK;
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
static pn_status_t bind(pn_highlights_t *hl,uint64_t id,pn_journal_files_t *files,pn_journal_io_t *io){
    if(!id)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];
    int n=snprintf(a,sizeof a,"%s/%016llx.a",hl->directory,(unsigned long long)id);
    if(n<0 || (size_t)n>=sizeof a)return PN_LIMIT;
    n=snprintf(b,sizeof b,"%s/%016llx.b",hl->directory,(unsigned long long)id);
    if(n<0 || (size_t)n>=sizeof b)return PN_LIMIT;
    pn_status_t s=regular(a);if(s!=PN_OK && s!=PN_EMPTY)return s;
    s=regular(b);if(s!=PN_OK && s!=PN_EMPTY)return s;
    return pn_journal_files_init(files,hl->media,&hl->lease,a,b,io);
}
static pn_status_t save_record(pn_highlights_t *hl,const pn_highlight_t *item){
    pn_journal_files_t files;pn_journal_io_t io;
    pn_status_t s=bind(hl,item->id,&files,&io);if(s!=PN_OK)return s;
    uint8_t bytes[PN_JOURNAL_PAYLOAD_MAX];memset(bytes,0,sizeof bytes);
    memcpy(bytes,"PNHL",4);
    // v2 为 EPUB 保留 start_run (bytes[6..7]) 与 end_run (bytes[254..255])，TXT 保持 v1。
    // v2 persists EPUB start_run (bytes[6..7]) and end_run (bytes[254..255]); TXT stays v1.
    bool v2=item->is_epub;
    bytes[4]=v2?2u:1u;
    bytes[5]=(uint8_t)((item->deleted?1u:0u)|(item->is_epub?2u:0u));
    if(v2){
        uint16_t srun=item->start_run>0xffffu?0xffffu:(uint16_t)item->start_run;
        uint16_t erun=item->end_run>0xffffu?0xffffu:(uint16_t)item->end_run;
        put16(bytes+6,srun);
        put16(bytes+254,erun);
    }
    memcpy(bytes+8,item->book.sha256,32);
    put64(bytes+40,item->id);
    put64(bytes+48,item->start);
    put64(bytes+56,item->end);
    put64(bytes+64,item->text_hash);
    size_t plen=strnlen(item->path,PN_HIGHLIGHT_PATH_MAX-1);
    memcpy(bytes+72,item->path,plen);
    size_t max_tlen=v2?116u:(PN_HIGHLIGHT_TEXT_MAX-1u);
    size_t tlen=strnlen(item->text,max_tlen);
    // 截断时保证不落在 UTF-8 多字节中间。/ Keep UTF-8 boundary intact when truncating.
    while(tlen>0 && ((unsigned char)item->text[tlen]&0xc0)==0x80)tlen--;
    bytes[136]=(uint8_t)tlen;
    memcpy(bytes+137,item->text,tlen);
    return pn_journal_save(&io,bytes,sizeof bytes);
}
static pn_status_t load_record(pn_highlights_t *hl,uint64_t id,pn_highlight_t *out){
    pn_status_t checked=directory(hl->directory);if(checked!=PN_OK)return checked;
    pn_journal_files_t files;pn_journal_io_t io;
    pn_status_t s=bind(hl,id,&files,&io);if(s!=PN_OK)return s;
    pn_record_t record;s=pn_journal_load(&io,&record);if(s!=PN_OK)return s;
    if(record.size!=PN_JOURNAL_PAYLOAD_MAX || memcmp(record.payload,"PNHL",4))return PN_CORRUPT;
    const uint8_t *p=record.payload;
    uint8_t ver=p[4];
    if((ver!=1u && ver!=2u) || (p[5]&~3u))return PN_UNSUPPORTED;
    if(ver==1u && (p[6] || p[7]))return PN_UNSUPPORTED;
    if(memcmp(p+8,hl->book.sha256,32))return PN_STALE_JOB;
    uint64_t rid=get64(p+40);if(rid!=id)return PN_CORRUPT;
    uint8_t tlen=p[136];
    if(ver==2u?tlen>116u:tlen>=PN_HIGHLIGHT_TEXT_MAX)return PN_CORRUPT;
    pn_highlight_t item={0};
    item.id=rid;item.book=hl->book;item.deleted=(p[5]&1u)!=0;item.is_epub=(p[5]&2u)!=0;
    item.start=get64(p+48);item.end=get64(p+56);item.text_hash=get64(p+64);
    if(ver==2u){item.start_run=get16(p+6);item.end_run=get16(p+254);}
    memcpy(item.path,p+72,PN_HIGHLIGHT_PATH_MAX-1);item.path[PN_HIGHLIGHT_PATH_MAX-1]=0;
    memcpy(item.text,p+137,tlen);item.text[tlen]=0;
    *out=item;return PN_OK;
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
static bool matches_candidate(const pn_highlight_t *existing,const pn_highlight_t *cand){
    if(!cand || existing->is_epub!=cand->is_epub)return false;
    if(existing->is_epub && strcmp(existing->path,cand->path)!=0)return false;
    // 区间重叠或同句哈希一致即视作同一句摘录（便于再次长按取消）。
    // Overlapping span or identical sentence hash counts as the same highlight so repeating toggles it off.
    if(cand->text_hash && existing->text_hash==cand->text_hash)return true;
    uint64_t a0=existing->start,a1=existing->end>a0?existing->end:a0+1;
    uint64_t b0=cand->start,b1=cand->end>b0?cand->end:b0+1;
    return a0<b1 && b0<a1;
}
typedef struct {size_t active,matching;uint64_t highest;pn_highlight_t dup;bool has_dup;} summary_t;
static pn_status_t scan(pn_highlights_t *hl,uint64_t after,pn_highlight_t *items,size_t capacity,
    size_t *count,summary_t *summary,const pn_highlight_t *match){
    *count=0;memset(summary,0,sizeof *summary);
    pn_status_t s=allowed(hl,false);if(s!=PN_OK)return s;
    s=directory(hl->directory);if(s==PN_EMPTY)return allowed(hl,false);if(s!=PN_OK)return s;
    DIR *dir=opendir(hl->directory);if(!dir)return errno==ENOENT?allowed(hl,false):PN_IO;
    for(;;){
        s=allowed(hl,false);if(s!=PN_OK)break;
        errno=0;struct dirent *entry=readdir(dir);if(!entry){if(errno)s=PN_IO;break;}
        uint64_t id;if(!record_name(entry->d_name,&id))continue;
        if(entry->d_name[17]=='b'){
            char path[PN_JOURNAL_PATH_MAX];int n=snprintf(path,sizeof path,"%s/%016llx.a",hl->directory,(unsigned long long)id);
            if(n<0 || (size_t)n>=sizeof path){s=PN_LIMIT;break;}
            s=regular(path);if(s==PN_OK)continue;if(s!=PN_EMPTY)break;
        }
        if(id>summary->highest)summary->highest=id;
        pn_highlight_t item;s=load_record(hl,id,&item);
        if(s!=PN_OK){s=PN_OK;continue;} // 单条损坏不阻塞整书高亮加载 / Corrupt entry does not block other highlights
        if(item.deleted)continue;
        if(++summary->active>PN_HIGHLIGHTS_MAX){summary->active=PN_HIGHLIGHTS_MAX;continue;}
        if(match && !summary->has_dup && matches_candidate(&item,match)){summary->dup=item;summary->has_dup=true;}
        if(id<=after)continue;
        summary->matching++;
        if(!items || !capacity)continue;
        size_t at=0;while(at<*count && items[at].id<id)at++;
        if(at>=capacity)continue;
        size_t next=*count<capacity?*count+1:capacity;
        for(size_t i=next-1;i>at;i--)items[i]=items[i-1];
        items[at]=item;*count=next;
    }
    if(closedir(dir)!=0 && s==PN_OK)s=PN_IO;
    pn_status_t current=allowed(hl,false);if(current!=PN_OK)s=current;
    if(s!=PN_OK)*count=0;
    return s;
}
pn_status_t pn_highlights_init(pn_highlights_t *hl,pn_media_t *media,const pn_media_lease_t *lease,
    const char *root,const pn_book_id_t *book){
    if(!hl || !media || !lease || !root || !*root || !book)return PN_INVALID;
    pn_status_t s=pn_media_validate(media,lease);if(s!=PN_OK)return s;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    struct stat info;if(stat(root,&info)!=0 || !S_ISDIR(info.st_mode))return PN_IO;
    char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",book->sha256[i]);
    pn_highlights_t next={.media=media,.lease=*lease,.book=*book};
    int n=snprintf(next.directory,sizeof next.directory,"%s/%s.highlights",root,hash);
    if(n<0 || (size_t)n+19>=sizeof next.directory)return PN_LIMIT;
    *hl=next;return PN_OK;
}
pn_status_t pn_highlights_list(pn_highlights_t *hl,uint64_t after,pn_highlight_t *items,
    size_t capacity,size_t *count,bool *more){
    if(!items || !capacity || capacity>PN_HIGHLIGHTS_MAX || !count || !more)return PN_INVALID;
    *count=0;*more=false;summary_t summary;
    pn_status_t s=scan(hl,after,items,capacity,count,&summary,NULL);
    if(s==PN_OK)*more=summary.matching>*count;
    return s;
}
pn_status_t pn_highlights_load_all(pn_highlights_t *hl,pn_highlight_t *items,size_t capacity,size_t *count){
    if(!items || !capacity || !count)return PN_INVALID;
    *count=0;summary_t summary;
    return scan(hl,0,items,capacity>PN_HIGHLIGHTS_MAX?PN_HIGHLIGHTS_MAX:capacity,count,&summary,NULL);
}
pn_status_t pn_highlights_toggle(pn_highlights_t *hl,const pn_highlight_t *candidate,bool *added,uint64_t *id_out){
    if(!candidate)return PN_INVALID;
    if(added)*added=false;
    if(id_out)*id_out=0;
    pn_status_t s=allowed(hl,true);if(s!=PN_OK)return s;
    size_t count=0;summary_t summary;
    s=scan(hl,0,NULL,0,&count,&summary,candidate);if(s!=PN_OK)return s;
    if(summary.has_dup){
        pn_highlight_t gone=summary.dup;gone.deleted=true;
        s=save_record(hl,&gone);
        if(s==PN_OK){if(added)*added=false;if(id_out)*id_out=gone.id;}
        return s;
    }
    if(summary.active>=PN_HIGHLIGHTS_MAX)return PN_LIMIT;
    if(summary.highest==UINT64_MAX)return PN_LIMIT;
    s=directory(hl->directory);
    if(s==PN_EMPTY && mkdir(hl->directory,0755)!=0 && errno!=EEXIST)return PN_IO;
    else if(s!=PN_OK && s!=PN_EMPTY)return s;
    pn_highlight_t item=*candidate;
    item.id=summary.highest+1u;item.book=hl->book;item.deleted=false;
    if(!item.text_hash && item.text[0])item.text_hash=pn_highlight_text_hash(item.text);
    s=save_record(hl,&item);
    if(s==PN_OK){if(added)*added=true;if(id_out)*id_out=item.id;}
    return s;
}
pn_status_t pn_highlights_delete(pn_highlights_t *hl,uint64_t id){
    pn_status_t s=allowed(hl,true);if(s!=PN_OK)return s;
    pn_highlight_t item;s=load_record(hl,id,&item);if(s!=PN_OK)return s;
    if(item.deleted)return PN_EMPTY;
    item.deleted=true;return save_record(hl,&item);
}
pn_status_t pn_highlights_get(pn_highlights_t *hl,uint64_t id,pn_highlight_t *out){
    if(!out)return PN_INVALID;
    pn_status_t s=allowed(hl,false);if(s!=PN_OK)return s;
    pn_highlight_t item;s=load_record(hl,id,&item);if(s!=PN_OK)return s;
    if(item.deleted)return PN_EMPTY;
    *out=item;return PN_OK;
}
static bool parse_hl_dir_name(const char *name,pn_book_id_t *out){
    static const char suffix[]=".highlights";
    size_t len=strlen(name);
    if(len!=64u+sizeof(suffix)-1u || memcmp(name+64u,suffix,sizeof(suffix)-1u)!=0)return false;
    for(unsigned i=0;i<32u;i++){
        unsigned char c0=(unsigned char)name[i*2u],c1=(unsigned char)name[i*2u+1u];
        unsigned hi,lo;
        if(c0>='0' && c0<='9')hi=(unsigned)(c0-'0');else if(c0>='a' && c0<='f')hi=(unsigned)(c0-'a'+10);else return false;
        if(c1>='0' && c1<='9')lo=(unsigned)(c1-'0');else if(c1>='a' && c1<='f')lo=(unsigned)(c1-'a'+10);else return false;
        out->sha256[i]=(uint8_t)((hi<<4)|lo);
    }
    return true;
}
pn_status_t pn_highlights_scan_all(pn_media_t *media,const char *state_dir,pn_highlight_t *out,size_t capacity,size_t *count_out){
    if(!media || !state_dir || !*state_dir || !out || !capacity || !count_out)return PN_INVALID;
    *count_out=0;
    pn_media_lease_t lease={0};
    pn_status_t s=pn_media_acquire(media,PN_MEDIA_READ,&lease);
    if(s!=PN_OK)return s;
    DIR *dir=opendir(state_dir);
    if(dir){
        struct dirent *ent;
        while((ent=readdir(dir))!=NULL && *count_out<capacity){
            pn_book_id_t book={0};
            if(!parse_hl_dir_name(ent->d_name,&book))continue;
            pn_highlights_t store={0};
            if(pn_highlights_init(&store,media,&lease,state_dir,&book)!=PN_OK)continue;
            uint64_t cursor=0;bool more=true;
            while(more && *count_out<capacity){
                size_t room=capacity-*count_out;
                if(room>8u)room=8u;
                size_t got=0;
                if(pn_highlights_list(&store,cursor,out+*count_out,room,&got,&more)!=PN_OK || !got)break;
                cursor=out[*count_out+got-1u].id;
                *count_out+=got;
            }
        }
        closedir(dir);
    }
    (void)pn_media_release(media,&lease);
    return PN_OK;
}
