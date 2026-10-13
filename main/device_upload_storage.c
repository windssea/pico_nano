/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：已挂载FAT卷的正向同步屏障与实时空间查询。
 * English: positive synchronization barrier and live space queries on an already mounted FAT volume.
 * 冻结：仅串行owner调用；不以卸载代替同步，不修改用户文件。
 * Frozen: serialized owner calls only; never substitute unmounting for sync or modify user files.
 */
#define _POSIX_C_SOURCE 200809L
#include "device_upload_storage.h"
#include "device_card_presence.h"
#include "pn_sd_layout.h"
#include "read_pico_sd.h"
#include "esp_vfs_fat.h"
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static pn_status_t ready(pn_device_upload_storage_t *s){
    if(!s || !s->media)return PN_INVALID;
    if(!s->media->available || s->epoch!=s->media->epoch)return PN_STALE_MEDIA;
    return pn_device_card_presence();
}
static pn_status_t guard(pn_device_upload_storage_t *s){
    pn_status_t status=ready(s);
    if(status!=PN_OK)return status;
    if(pn_media_active(s->media)!=1)return PN_BUSY;
    for(size_t i=0;i<PN_MEDIA_MAX_LEASES;i++){
        const pn_media_lease_t *lease=&s->media->slots[i];
        if(lease->ticket && lease->access==PN_MEDIA_WRITE)return pn_media_validate(s->media,lease);
    }
    return PN_BUSY;
}
static bool inside(const pn_device_upload_storage_t *s,const char *path){
    if(!path)return false;
    size_t n=strlen(s->root);
    if(strncmp(path,s->root,n) || (path[n] && path[n]!='/'))return false;
    for(const char *at=path+n;*at;){
        if(*at++!='/')return false;
        const char *end=strchr(at,'/');size_t length=end?(size_t)(end-at):strlen(at);
        if(!length || (length==1 && at[0]=='.') || (length==2 && at[0]=='.' && at[1]=='.'))return false;
        at+=length;
    }
    return true;
}
static int inspect(const char *path,struct stat *info){
#ifdef ESP_PLATFORM
    return stat(path,info);
#else
    return lstat(path,info);
#endif
}
static pn_status_t sync_directory(void *ctx,const char *path){
    pn_device_upload_storage_t *s=ctx;
    pn_status_t status=guard(s);
    if(status!=PN_OK)return status;
    if(!inside(s,path))return PN_INVALID;
    struct stat info;
    if(inspect(path,&info) || !S_ISDIR(info.st_mode))return PN_INVALID;
    char folder[PN_UPLOAD_FILES_ROOT_MAX+64],marker[PN_UPLOAD_FILES_ROOT_MAX+96];
    // 首次同步也可用于创建应用根；标记必须位于同一卷的固定私有目录，拒绝符号链接。
    // The first sync can create the application root; the marker stays in its fixed private directory on the same volume, rejecting symlinks.
    snprintf(folder,sizeof folder,"%s/" PN_SD_APP,s->root);
    if(inspect(folder,&info)){if(errno!=ENOENT || mkdir(folder,0700))return PN_IO;}else if(!S_ISDIR(info.st_mode))return PN_INVALID;
    snprintf(folder,sizeof folder,"%s/" PN_SD_PRIVATE,s->root);
    if(inspect(folder,&info)){if(errno!=ENOENT || mkdir(folder,0700))return PN_IO;}else if(!S_ISDIR(info.st_mode))return PN_INVALID;
    snprintf(marker,sizeof marker,"%s/volume.sync",folder);
    if(inspect(marker,&info)==0){
        if(!S_ISREG(info.st_mode) || info.st_size<0 || info.st_size>1)return PN_CORRUPT;
    }else if(errno!=ENOENT)return PN_IO;
    int flags=O_WRONLY|O_CREAT;
#ifdef O_NOFOLLOW
    flags|=O_NOFOLLOW;
#endif
    int fd=open(marker,flags,0600);
    if(fd<0)return PN_IO;
    // 必须实际写入，FatFs未修改文件的f_sync可能直接返回而不执行sync_fs。
    // Always write: FatFs f_sync can skip sync_fs for an unmodified file.
    uint8_t byte=++s->serial;
    ssize_t count;
    do{count=write(fd,&byte,1);}while(count<0 && errno==EINTR);
    bool failed=count!=1;
    if(!failed && fsync(fd))failed=true;
    if(close(fd))failed=true;
    status=guard(s);
    return status!=PN_OK?status:failed?PN_IO:PN_OK;
}
static pn_status_t space_free(void *ctx,const char *root,uint64_t *out){
    pn_device_upload_storage_t *s=ctx;
    if(!out || !root || !s || strcmp(root,s->root))return PN_INVALID;
    pn_status_t status=guard(s);
    if(status!=PN_OK)return status;
    uint64_t total=0,available=0;
    esp_err_t result=esp_vfs_fat_info(s->root,&total,&available);
    status=guard(s);
    if(status!=PN_OK)return status;
    if(result!=ESP_OK || !total || available>total)return PN_IO;
    *out=available;
    return PN_OK;
}
pn_status_t pn_device_upload_storage_options(pn_device_upload_storage_t *s,pn_media_t *media,const char *root,pn_upload_files_options_t *options){
    if(!s || !media || !root || !options)return PN_INVALID;
    size_t n=strlen(root);
    if(n<2 || n>=sizeof s->root || root[0]!='/' || root[n-1]=='/')return PN_INVALID;
#ifdef ESP_PLATFORM
    if(strcmp(root,"/sdcard"))return PN_INVALID;
#endif
    pn_device_upload_storage_t candidate={.media=media,.epoch=media->epoch};
    memcpy(candidate.root,root,n+1);
    pn_status_t status=ready(&candidate);
    if(status!=PN_OK)return status;
    *s=candidate;
    *options=(pn_upload_files_options_t){.root=s->root,.ctx=s,.sync_directory=sync_directory,.space_free=space_free};
    return PN_OK;
}
