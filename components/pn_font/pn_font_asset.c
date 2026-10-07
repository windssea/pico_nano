/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：稳定私有源地址，字体身份/元数据和样例只读校验。
 * English: stable private source addresses and read-only identity/metadata/sample checks.
 * 冻结：不移动活动文件，不写字体、选择或进度。
 * Frozen: never move live files or write fonts, selections or progress.
 */
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "pn_font_asset.h"
#include "pn_text_file.h"
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
typedef struct {pn_pool_t *pool;pn_media_t *media;pn_media_lease_t lease;pn_text_file_t file;pn_text_source_t source;pn_font_t font;pn_font_reference_t reference;pn_font_info_t info;} asset_t;
static pn_status_t release(asset_t *a){pn_font_close(&a->font);pn_status_t status=pn_text_file_close(&a->file);if(a->lease.ticket){pn_status_t s=pn_media_release(a->media,&a->lease);if(status==PN_OK)status=s;}pn_free(a);return status;}
pn_status_t pn_font_asset_open(pn_font_asset_t *asset,pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *guard,const char *path,uint64_t expected_size,int pixels){
    if(!asset || !pool || !media || !guard || !path || !*path || pixels<8 || pixels>128)return PN_INVALID;
    if(asset->impl)return PN_BUSY;
    if(expected_size>PN_FONT_REFERENCE_MAX_BYTES || strlen(path)>=PN_FONT_REFERENCE_PATH_MAX)return PN_LIMIT;
    pn_font_preferences_t check={0};check.primary.kind=PN_FONT_FILE;check.primary.size=12;strcpy(check.primary.path,path);pn_status_t status=pn_font_preferences_validate(&check,false);if(status!=PN_OK)return status;
    status=pn_media_validate(media,guard);if(status!=PN_OK)return status;if(guard->access==PN_MEDIA_USB)return PN_INVALID;
    struct stat st;
#ifdef ESP_PLATFORM
    int result=stat(path,&st);
#else
    int result=lstat(path,&st);
#endif
    if(result || !S_ISREG(st.st_mode) || st.st_size<0)return PN_IO;
    uint64_t size=(uint64_t)st.st_size;if(expected_size && expected_size!=size)return PN_STALE_JOB;if(size>PN_FONT_REFERENCE_MAX_BYTES)return PN_LIMIT;if(size<12)return PN_CORRUPT;
    asset_t *a=pn_alloc(pool,sizeof *a);if(!a)return PN_NO_MEMORY;*a=(asset_t){.pool=pool,.media=media,.reference={.kind=PN_FONT_FILE,.size=size}};strcpy(a->reference.path,path);
    status=pn_media_acquire(media,PN_MEDIA_READ,&a->lease);if(status==PN_OK)status=pn_text_file_open(&a->file,media,&a->lease,path,&a->source);if(status==PN_OK && a->source.size!=size)status=PN_STALE_JOB;
    if(status==PN_OK)status=pn_identity_file(media,&a->lease,path,PN_FONT_REFERENCE_MAX_BYTES,&a->reference.identity);
    if(status==PN_OK)status=pn_font_open(&a->font,pool,&a->source,pixels);
    if(status==PN_OK)status=pn_font_info(&a->font,&a->info);
    if(status==PN_OK)status=pn_media_validate(media,guard);
    if(status!=PN_OK){(void)release(a);return status;}asset->impl=a;return PN_OK;
}
pn_status_t pn_font_asset_close(pn_font_asset_t *asset){if(!asset)return PN_INVALID;if(!asset->impl)return PN_OK;pn_status_t status=release(asset->impl);asset->impl=NULL;return status;}
pn_font_t *pn_font_asset_font(pn_font_asset_t *asset){return asset && asset->impl?&((asset_t *)asset->impl)->font:NULL;}
pn_status_t pn_font_asset_details(const pn_font_asset_t *asset,pn_font_reference_t *reference,pn_font_info_t *info){
    if(!asset || !asset->impl || (!reference && !info))return PN_INVALID;
    const asset_t *a=asset->impl;pn_status_t status=pn_media_validate(a->media,&a->lease);if(status!=PN_OK)return status;if(reference)*reference=a->reference;if(info)*info=a->info;return PN_OK;
}
pn_status_t pn_font_asset_sample(pn_font_asset_t *asset,const uint32_t *points,size_t count,unsigned *missing){
    if(!asset || !asset->impl || !points || !count || count>128 || !missing)return PN_INVALID;
    asset_t *a=asset->impl;pn_status_t status=pn_media_validate(a->media,&a->lease);if(status!=PN_OK)return status;
    for(size_t i=0;i<count;i++)if(points[i]>0x10ffff || (points[i]>=0xd800 && points[i]<=0xdfff))return PN_INVALID;
    uint8_t *pixels=pn_alloc(a->pool,128*128/2);if(!pixels)return PN_NO_MEMORY;pn_frame_t frame;unsigned absent=0;if(!pn_frame_bind(&frame,pixels,128*128/2,128,128)){pn_free(pixels);return PN_INVALID;}
    for(size_t i=0;status==PN_OK && i<count;i++){int32_t advance;status=pn_font_advance(&a->font,points[i],&advance);if(status==PN_EMPTY){absent++;status=PN_OK;continue;}if(status==PN_OK){pn_frame_clear(&frame,15);status=pn_font_draw(&a->font,&frame,points[i],16*64+17,104,PN_FONT_GRAY);}}
    if(status==PN_OK)*missing=absent;
    pn_free(pixels);return status;
}

void pn_font_asset_suspend(pn_font_asset_t *asset){if(asset && asset->impl)pn_font_close(&((asset_t *)asset->impl)->font);}
pn_status_t pn_font_asset_ensure(pn_font_asset_t *asset,int pixels,pn_font_t **out){
    if(!asset || !asset->impl || !out || pixels<8 || pixels>128)return PN_INVALID;
    asset_t *a=asset->impl;pn_status_t status=pn_media_validate(a->media,&a->lease);if(status!=PN_OK)return status;
    status=a->font.impl?pn_font_size(&a->font,pixels):pn_font_open(&a->font,a->pool,&a->source,pixels);if(status==PN_OK)*out=&a->font;return status;
}
pn_status_t pn_font_asset_source(const pn_font_asset_t *asset,pn_text_source_t *out){
    if(!asset || !asset->impl || !out)return PN_INVALID;
    const asset_t *a=asset->impl;pn_status_t status=pn_media_validate(a->media,&a->lease);if(status==PN_OK)*out=a->source;return status;
}

pn_status_t pn_font_reference_capture(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *guard,const char *path,pn_font_reference_t *out){
    if(!path || !*path || !out)return PN_INVALID;
#ifdef ESP_PLATFORM
    const char *absolute=path;
#else
    char resolved[4096];if(!realpath(path,resolved))return PN_IO;const char *absolute=resolved;
#endif
    pn_font_asset_t asset={0};pn_status_t status=pn_font_asset_open(&asset,pool,media,guard,absolute,0,44);if(status==PN_OK)status=pn_font_asset_details(&asset,out,NULL);(void)pn_font_asset_close(&asset);return status;
}
