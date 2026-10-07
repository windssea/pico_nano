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
#include <sys/stat.h>
#include <unistd.h>
static pn_status_t validate(void *ctx) {
    pn_text_file_t *file=ctx;
    if(!file || !file->handle)return PN_INVALID;
    return pn_media_validate(file->media,&file->lease);
}
static pn_status_t read_at(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n) {
    pn_text_file_t *file=ctx;
    if(!file || !file->handle || !out || !n || offset>file->size)return PN_INVALID;
    pn_status_t status=pn_media_validate(file->media,&file->lease);if(status!=PN_OK)return status;
    if(fseek(file->handle,(long)offset,SEEK_SET)!=0)return PN_IO;
    uint64_t remaining=file->size-offset;if(remaining<cap)cap=(size_t)remaining;
    size_t got=fread(out,1,cap,file->handle);
    bool error=ferror(file->handle)!=0;
    status=pn_media_validate(file->media,&file->lease);if(status!=PN_OK)return status;
    if(error || (cap && !got))return PN_IO;
    *n=got;return PN_OK;
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
    *file=(pn_text_file_t){handle,media,*lease,(uint64_t)info.st_size};
    *source=(pn_text_source_t){file,file->size,read_at,validate};return PN_OK;
}
pn_status_t pn_text_file_close(pn_text_file_t *file) {
    if(!file)return PN_INVALID;
    if(!file->handle)return PN_OK;
    int result=fclose(file->handle);file->handle=NULL;return result==0?PN_OK:PN_IO;
}
