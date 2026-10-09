/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TXT文件源port，将stdio和介质租约连接到共享解码器。
 * English: TXT file-source port connecting stdio and media leases to the shared decoder.
 * 冻结：不挂载/格式化，不释放调用方租约；打开对象不可移动。
 * Frozen: no mounting/formatting or releasing caller leases; open objects never move.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_text_file.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static pn_status_t validate(void *ctx) {
    pn_text_file_t *file=ctx;
    if(!file || !file->handle)return PN_INVALID;
    return pn_media_validate(file->media,&file->lease);
}
#define BLOCK_BYTES 4096u
/* 小读取先落到对齐的4KiB块再从内存取：字体和ZIP这类随机小读很多，每次seek+read在慢介质（SD、网络盘）上代价很高。大读取直通。
 * Small reads land in an aligned 4 KiB block and are served from memory: fonts and ZIPs issue many random small reads, and each seek+read is expensive on slow media (SD, network drives). Large reads go straight through. */
static pn_status_t read_direct(pn_text_file_t *file,uint64_t offset,uint8_t *out,size_t cap,size_t *got) {
    if(fseek(file->handle,(long)offset,SEEK_SET)!=0)return PN_IO;
    *got=fread(out,1,cap,file->handle);
    return ferror(file->handle)!=0?PN_IO:PN_OK;
}
static pn_status_t read_at(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n) {
    pn_text_file_t *file=ctx;
    if(!file || !file->handle || !out || !n || offset>file->size)return PN_INVALID;
    pn_status_t status=pn_media_validate(file->media,&file->lease);if(status!=PN_OK)return status;
    uint64_t remaining=file->size-offset;if(remaining<cap)cap=(size_t)remaining;
    size_t copied=0;bool error=false;
    if(cap>=BLOCK_BYTES || !cap){
        error=read_direct(file,offset,out,cap,&copied)!=PN_OK;
    }else{
        if(!file->block)file->block=malloc(BLOCK_BYTES);
        if(!file->block){error=read_direct(file,offset,out,cap,&copied)!=PN_OK;}
        else{
            while(copied<cap && !error){
                uint64_t position=offset+copied;
                bool hit=file->block_length && position>=file->block_start && position<file->block_start+file->block_length;
                if(!hit){
                    uint64_t start=position&~(uint64_t)(BLOCK_BYTES-1);uint64_t left=file->size-start;
                    size_t want=left<BLOCK_BYTES?(size_t)left:BLOCK_BYTES,got=0;
                    file->block_length=0;
                    if(read_direct(file,start,file->block,want,&got)!=PN_OK || position>=start+got){error=true;break;}
                    file->block_start=start;file->block_length=got;
                }
                size_t inside=(size_t)(position-file->block_start),take=file->block_length-inside;
                if(take>cap-copied)take=cap-copied;
                memcpy(out+copied,file->block+inside,take);copied+=take;
            }
        }
    }
    status=pn_media_validate(file->media,&file->lease);if(status!=PN_OK)return status;
    if(error || (cap && !copied))return PN_IO;
    *n=copied;return PN_OK;
}
pn_status_t pn_text_file_open(pn_text_file_t *file,pn_media_t *media,const pn_media_lease_t *lease,
    const char *path,pn_text_source_t *source) {
    if(!file || !media || !lease || !path || !*path || !source)return PN_INVALID;
    if(file->handle)return PN_BUSY;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    FILE *handle=fopen(path,"rb");if(!handle)return PN_IO;
    struct stat info;
    if(fstat(fileno(handle),&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<0){fclose(handle);return PN_IO;}
    if((uint64_t)info.st_size>PN_TEXT_FILE_MAX_BYTES){fclose(handle);return PN_LIMIT;}
    status=pn_media_validate(media,lease);if(status!=PN_OK){fclose(handle);return status;}
    *file=(pn_text_file_t){.handle=handle,.media=media,.lease=*lease,.size=(uint64_t)info.st_size};
    *source=(pn_text_source_t){file,file->size,read_at,validate};return PN_OK;
}
pn_status_t pn_text_file_close(pn_text_file_t *file) {
    if(!file)return PN_INVALID;
    if(!file->handle)return PN_OK;
    int result=fclose(file->handle);file->handle=NULL;
    free(file->block);file->block=NULL;file->block_length=0;
    return result==0?PN_OK:PN_IO;
}
