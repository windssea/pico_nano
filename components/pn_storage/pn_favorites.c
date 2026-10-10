/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：收藏夹编码：魔数PNFV、版本1、条数，随后每条“长度＋UTF-8路径”；整份经blob原子保存。
 * English: favorites encoding: magic PNFV, version 1, count, then "length + UTF-8 path" per entry; the whole payload is saved atomically through the blob layer.
 */
#include "pn_favorites.h"
#include <stdio.h>
#include <string.h>
#define PAYLOAD_MAX (8+PN_FAVORITES_MAX*(2+PN_FAVORITES_PATH_MAX))
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
/* 路径：非空、无控制字符、长度受限。/ Path: non-empty, no control characters, bounded length. */
static bool valid_path(const char *path,size_t length){
    if(!length || length>=PN_FAVORITES_PATH_MAX)return false;
    for(size_t i=0;i<length;i++){unsigned char c=(unsigned char)path[i];if(c<32 || c==127)return false;}
    return true;
}
pn_status_t pn_favorites_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];int n=snprintf(a,sizeof a,"%s/favorites.a",root);if(n<0 || (size_t)n>=sizeof a)return PN_LIMIT;
    n=snprintf(b,sizeof b,"%s/favorites.b",root);if(n<0 || (size_t)n>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(files,media,lease,a,b,io);
}
pn_status_t pn_favorites_load(const pn_journal_io_t *io,pn_pool_t *pool,pn_favorites_t *favorites){
    if(!io || !pool || !favorites)return PN_INVALID;
    uint8_t *bytes=pn_alloc(pool,PAYLOAD_MAX);pn_favorites_t *next=pn_alloc(pool,sizeof *next);
    if(!bytes || !next){pn_free(bytes);pn_free(next);return PN_NO_MEMORY;}
    size_t size=0;pn_status_t status=pn_blob_load(io,pool,bytes,PAYLOAD_MAX,&size);
    if(status==PN_OK && (size<8 || memcmp(bytes,"PNFV",4)))status=PN_CORRUPT;
    if(status==PN_OK && get(bytes+4,2)!=1)status=PN_UNSUPPORTED;
    if(status==PN_OK){
        size_t count=(size_t)get(bytes+6,2),at=8;memset(next,0,sizeof *next);
        if(count>PN_FAVORITES_MAX)status=PN_CORRUPT;
        for(size_t i=0;i<count && status==PN_OK;i++){
            if(at+2>size){status=PN_CORRUPT;break;}
            size_t n=(size_t)get(bytes+at,2);at+=2;
            if(at+n>size || !valid_path((const char *)bytes+at,n)){status=PN_CORRUPT;break;}
            memcpy(next->paths[i],bytes+at,n);next->paths[i][n]=0;at+=n;
        }
        if(status==PN_OK && at!=size)status=PN_CORRUPT;
        if(status==PN_OK){next->count=count;*favorites=*next;}
    }
    pn_free(bytes);pn_free(next);return status;
}
bool pn_favorites_contains(const pn_favorites_t *favorites,const char *path){
    if(!favorites || !path)return false;
    for(size_t i=0;i<favorites->count && i<PN_FAVORITES_MAX;i++)if(!strcmp(favorites->paths[i],path))return true;
    return false;
}
pn_status_t pn_favorites_toggle(const pn_journal_io_t *io,pn_pool_t *pool,const char *path,bool *favorite){
    if(!io || !pool || !path || !favorite)return PN_INVALID;
    size_t length=strlen(path);if(!valid_path(path,length))return PN_INVALID;
    pn_favorites_t *list=pn_alloc(pool,sizeof *list);if(!list)return PN_NO_MEMORY;
    pn_status_t status=pn_favorites_load(io,pool,list);if(status==PN_EMPTY){memset(list,0,sizeof *list);status=PN_OK;}
    if(status!=PN_OK){pn_free(list);return status;}
    size_t found=0;while(found<list->count && strcmp(list->paths[found],path))found++;
    bool added=found==list->count;
    if(added){
        if(list->count>=PN_FAVORITES_MAX){pn_free(list);return PN_LIMIT;}
        memmove(list->paths[1],list->paths[0],list->count*sizeof list->paths[0]);memcpy(list->paths[0],path,length+1);list->count++;
    }else{memmove(list->paths[found],list->paths[found+1],(list->count-found-1)*sizeof list->paths[0]);list->count--;}
    size_t size=8;for(size_t i=0;i<list->count;i++)size+=2+strlen(list->paths[i]);
    uint8_t *bytes=pn_alloc(pool,size);if(!bytes){pn_free(list);return PN_NO_MEMORY;}
    memcpy(bytes,"PNFV",4);put(bytes+4,1,2);put(bytes+6,list->count,2);
    size_t at=8;for(size_t i=0;i<list->count;i++){size_t n=strlen(list->paths[i]);put(bytes+at,n,2);memcpy(bytes+at+2,list->paths[i],n);at+=2+n;}
    status=pn_blob_save(io,pool,bytes,size);
    if(status==PN_OK)*favorite=added;
    pn_free(bytes);pn_free(list);return status;
}
