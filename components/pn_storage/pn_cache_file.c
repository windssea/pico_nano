/* SPDX-License-Identifier: Apache-2.0
 * 中文：单owner的缓存协议，失败不删除原件或过期介质上的文件。
 * English: single-owner cache protocol, never deleting originals or files on expired media. */
#define _POSIX_C_SOURCE 200809L
#include "pn_cache_file.h"
#include "pn_cancel.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifndef ESP_PLATFORM
#define cache_stat lstat
#else
#define cache_stat stat
#endif
#define CACHE_PATH_MAX 1024u

bool pn_cache_file_miss(pn_status_t s){return s==PN_EMPTY || s==PN_IO || s==PN_CORRUPT || s==PN_LIMIT || s==PN_NO_MEMORY || s==PN_UNSUPPORTED;}
static pn_status_t checked(pn_media_t *m,const pn_media_lease_t *l){pn_status_t s=pn_cancel_checkpoint();return s==PN_OK?pn_media_validate(m,l):s;}
static pn_status_t writable(pn_media_t *m,const pn_media_lease_t *l){
    if(!l || (l->access!=PN_MEDIA_WRITE && l->access!=PN_MEDIA_CACHE_WRITE))return PN_INVALID;
    return checked(m,l);
}
pn_status_t pn_cache_file_load(pn_media_t *m,const pn_media_lease_t *l,const char *path,pn_cache_file_read_fn read,void *ctx){
    if(!path || !*path || !read || !l || l->access==PN_MEDIA_USB)return PN_INVALID;
    pn_status_t s=checked(m,l);if(s!=PN_OK)return s;
    s=read(ctx,path);if(s==PN_OK)return checked(m,l);if(!pn_cache_file_miss(s))return s;pn_status_t primary=s;
    char backup[CACHE_PATH_MAX];int n=snprintf(backup,sizeof backup,"%s.bak",path);if(n<0 || (size_t)n>=sizeof backup)return PN_EMPTY;
    s=checked(m,l);if(s!=PN_OK)return s;s=read(ctx,backup);
    if(s==PN_OK)return checked(m,l);
    return pn_cache_file_miss(s) && primary!=PN_EMPTY?primary:s;
}
pn_status_t pn_cache_file_directory(pn_media_t *m,const pn_media_lease_t *l,const char *path){
    if(!path || !*path || strlen(path)>=CACHE_PATH_MAX)return PN_INVALID;
    pn_status_t s=writable(m,l);if(s!=PN_OK)return s;
    char part[CACHE_PATH_MAX];strcpy(part,path);size_t start=part[0]=='/'?1:0;
    for(size_t i=start;;i++)if(part[i]=='/' || !part[i]){
        char end=part[i];size_t length=i-start;
        if(!length || (length==1 && part[start]=='.') || (length==2 && part[start]=='.' && part[start+1]=='.'))return PN_INVALID;
        part[i]=0;s=writable(m,l);if(s!=PN_OK)return s;struct stat info;
        if(!cache_stat(part,&info)){if(!S_ISDIR(info.st_mode))return PN_IO;}
        else if(errno!=ENOENT || mkdir(part,0700))return PN_IO;
        part[i]=end;if(!end)break;start=i+1;
    }
    return checked(m,l);
}
pn_status_t pn_cache_commit_begin(pn_cache_commit_t *c,pn_media_t *m,const pn_media_lease_t *l,const char *target,const char *tmp,const char *bak){
    if(!c || c->active || !target || !*target || !tmp || !bak)return PN_INVALID;
    size_t n=strlen(target);if(strncmp(tmp,target,n) || strcmp(tmp+n,".tmp") || strncmp(bak,target,n) || strcmp(bak+n,".bak"))return PN_INVALID;
    pn_status_t s=writable(m,l);if(s!=PN_OK)return s;
    *c=(pn_cache_commit_t){.media=m,.lease=*l,.target=target,.temporary=tmp,.backup=bak,.active=true};return PN_OK;
}
void pn_cache_commit_abort(pn_cache_commit_t *c){
    if(!c || !c->active)return;
    if(pn_media_validate(c->media,&c->lease)==PN_OK){
        struct stat info;if(c->moved && !c->committed && cache_stat(c->target,&info) && errno==ENOENT)(void)rename(c->backup,c->target);
        if(!c->committed)(void)remove(c->temporary);
    }
    c->active=false;
}
pn_status_t pn_cache_commit_step(pn_cache_commit_t *c){
    if(!c || !c->active)return PN_EMPTY;
    pn_status_t s=writable(c->media,&c->lease);if(s==PN_CANCELLED)return s;if(s!=PN_OK){pn_cache_commit_abort(c);return s;}
    struct stat info;
    switch(c->phase){
    case 0:
        if(!cache_stat(c->target,&info)){if(!S_ISREG(info.st_mode))s=PN_IO;else c->phase=c->preserve_backup?5:1;}
        else if(errno==ENOENT)c->phase=3;else s=PN_IO;
        break;
    case 1:
        if(!cache_stat(c->backup,&info) && !S_ISREG(info.st_mode)){s=PN_IO;break;}
        if(remove(c->backup) && errno!=ENOENT)s=PN_IO;else c->phase=2;break;
    case 2:if(rename(c->target,c->backup))s=PN_IO;else{c->moved=true;c->phase=3;}break;
    case 3:if(rename(c->temporary,c->target))s=PN_IO;else{c->committed=true;c->phase=4;}break;
    case 4:
        if(c->moved || c->preserve_backup)(void)remove(c->backup);
        c->active=false;return PN_OK;
    case 5:
        // 备份已经由调用方完整核验；不能以坏主包覆盖最后有效代。/ The caller fully verified the backup; never overwrite the last valid generation with a bad primary.
        if(cache_stat(c->backup,&info) || !S_ISREG(info.st_mode))s=PN_IO;
        else if(remove(c->target) && errno!=ENOENT)s=PN_IO;else c->phase=3;
        break;
    default:s=PN_INVALID;break;
    }
    if(s!=PN_OK)pn_cache_commit_abort(c);
    return s==PN_OK?PN_BUSY:s;
}
