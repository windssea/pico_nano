/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：手动已读/未读标记编码：魔数PNRM、版本1、条数，随后每条“1字节状态+2字节长度+UTF-8路径”；经blob原子保存。
 * English: read-marks encoding: magic PNRM, version 1, count, then "1-byte state + 2-byte length + UTF-8 path" per entry; saved atomically via blob.
 */
#include "pn_read_marks.h"
#include <stdio.h>
#include <string.h>
#define PAYLOAD_MAX (8+PN_READ_MARKS_MAX*(3+PN_READ_MARK_PATH_MAX))
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static bool valid_path(const char *path,size_t length){
    if(!length || length>=PN_READ_MARK_PATH_MAX)return false;
    for(size_t i=0;i<length;i++){unsigned char c=(unsigned char)path[i];if(c<32 || c==127)return false;}
    return true;
}
pn_status_t pn_read_marks_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];int n=snprintf(a,sizeof a,"%s/read_marks.a",root);if(n<0 || (size_t)n>=sizeof a)return PN_LIMIT;
    n=snprintf(b,sizeof b,"%s/read_marks.b",root);if(n<0 || (size_t)n>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(files,media,lease,a,b,io);
}
pn_status_t pn_read_marks_load(const pn_journal_io_t *io,pn_pool_t *pool,pn_read_marks_t *marks){
    if(!io || !pool || !marks)return PN_INVALID;
    uint8_t *bytes=pn_alloc(pool,PAYLOAD_MAX);pn_read_marks_t *next=pn_alloc(pool,sizeof *next);
    if(!bytes || !next){pn_free(bytes);pn_free(next);return PN_NO_MEMORY;}
    size_t size=0;pn_status_t status=pn_blob_load(io,pool,bytes,PAYLOAD_MAX,&size);
    if(status==PN_OK && (size<8 || memcmp(bytes,"PNRM",4)))status=PN_CORRUPT;
    if(status==PN_OK && get(bytes+4,2)!=1)status=PN_UNSUPPORTED;
    if(status==PN_OK){
        size_t count=(size_t)get(bytes+6,2),at=8;memset(next,0,sizeof *next);
        if(count>PN_READ_MARKS_MAX)status=PN_CORRUPT;
        for(size_t i=0;i<count && status==PN_OK;i++){
            if(at+3>size){status=PN_CORRUPT;break;}
            uint8_t st=bytes[at];size_t n=(size_t)get(bytes+at+1,2);at+=3;
            if((st!=PN_READ_MARK_READ && st!=PN_READ_MARK_UNREAD) || at+n>size || !valid_path((const char *)bytes+at,n)){status=PN_CORRUPT;break;}
            next->states[i]=st;memcpy(next->paths[i],bytes+at,n);next->paths[i][n]=0;at+=n;
        }
        if(status==PN_OK && at!=size)status=PN_CORRUPT;
        if(status==PN_OK){next->count=count;*marks=*next;}
    }
    pn_free(bytes);pn_free(next);return status;
}
pn_read_mark_t pn_read_marks_get(const pn_read_marks_t *marks,const char *path){
    if(!marks || !path)return PN_READ_MARK_AUTO;
    for(size_t i=0;i<marks->count && i<PN_READ_MARKS_MAX;i++)if(!strcmp(marks->paths[i],path))return (pn_read_mark_t)marks->states[i];
    return PN_READ_MARK_AUTO;
}
bool pn_read_marks_is_read(const pn_read_marks_t *marks,const char *path,bool has_progress,uint16_t progress){
    pn_read_mark_t mark=pn_read_marks_get(marks,path);
    if(mark==PN_READ_MARK_READ)return true;
    if(mark==PN_READ_MARK_UNREAD)return false;
    return has_progress && progress==10000u;
}
pn_status_t pn_read_marks_set(const pn_journal_io_t *io,pn_pool_t *pool,const char *path,pn_read_mark_t state){
    if(!io || !pool || !path || (unsigned)state>PN_READ_MARK_UNREAD)return PN_INVALID;
    size_t length=strlen(path);if(!valid_path(path,length))return PN_INVALID;
    pn_read_marks_t *list=pn_alloc(pool,sizeof *list);if(!list)return PN_NO_MEMORY;
    pn_status_t status=pn_read_marks_load(io,pool,list);if(status==PN_EMPTY){memset(list,0,sizeof *list);status=PN_OK;}
    if(status!=PN_OK){pn_free(list);return status;}
    size_t found=0;while(found<list->count && strcmp(list->paths[found],path))found++;
    if(state==PN_READ_MARK_AUTO){
        if(found<list->count){
            memmove(list->states+found,list->states+found+1,(list->count-found-1)*sizeof list->states[0]);
            memmove(list->paths[found],list->paths[found+1],(list->count-found-1)*sizeof list->paths[0]);
            list->count--;
        }
    }else if(found<list->count){
        list->states[found]=(uint8_t)state;
    }else{
        if(list->count>=PN_READ_MARKS_MAX){pn_free(list);return PN_LIMIT;}
        memmove(list->states+1,list->states,list->count*sizeof list->states[0]);
        memmove(list->paths[1],list->paths[0],list->count*sizeof list->paths[0]);
        list->states[0]=(uint8_t)state;memcpy(list->paths[0],path,length+1);list->count++;
    }
    size_t size=8;for(size_t i=0;i<list->count;i++)size+=3+strlen(list->paths[i]);
    uint8_t *bytes=pn_alloc(pool,size);if(!bytes){pn_free(list);return PN_NO_MEMORY;}
    memcpy(bytes,"PNRM",4);put(bytes+4,1,2);put(bytes+6,list->count,2);
    size_t at=8;for(size_t i=0;i<list->count;i++){size_t n=strlen(list->paths[i]);bytes[at]=list->states[i];put(bytes+at+1,n,2);memcpy(bytes+at+3,list->paths[i],n);at+=3+n;}
    status=pn_blob_save(io,pool,bytes,size);
    pn_free(bytes);pn_free(list);return status;
}
