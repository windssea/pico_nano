/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：生成全新1MiB数据或壁纸分区镜像，仅写指定输出，不访问设备。
 * English: generate a fresh 1 MiB data or wallpaper partition image at the specified output, never accessing a device.
 */
#include "lfs.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IMAGE_SIZE (1024u*1024u)
static int read_block(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,void *out,lfs_size_t n){if(b>=c->block_count || off>c->block_size || n>c->block_size-off)return LFS_ERR_IO;memcpy(out,(uint8_t *)c->context+b*c->block_size+off,n);return 0;}
static int write_block(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,const void *data,lfs_size_t n){if(b>=c->block_count || off>c->block_size || n>c->block_size-off)return LFS_ERR_IO;uint8_t *dest=(uint8_t *)c->context+b*c->block_size+off;const uint8_t *src=data;for(lfs_size_t i=0;i<n;i++){if((dest[i]&src[i])!=src[i])return LFS_ERR_CORRUPT;dest[i]&=src[i];}return 0;}
static int erase_block(const struct lfs_config *c,lfs_block_t b){if(b>=c->block_count)return LFS_ERR_IO;memset((uint8_t *)c->context+b*c->block_size,255,c->block_size);return 0;}
static int sync_block(const struct lfs_config *c){(void)c;return 0;}
int main(int argc,char **argv){
    // 第二参数wallpaper生成不含目录的空壁纸分区。/ A second argument "wallpaper" creates an empty wallpaper partition without directories.
    bool wallpaper=argc==3 && !strcmp(argv[2],"wallpaper");
    if(argc!=2 && !wallpaper)return 2;
    uint8_t *bytes=malloc(IMAGE_SIZE);if(!bytes)return 1;memset(bytes,255,IMAGE_SIZE);
    struct lfs_config config={.context=bytes,.read=read_block,.prog=write_block,.erase=erase_block,.sync=sync_block,.read_size=128,.prog_size=128,.block_size=4096,.block_count=256,.block_cycles=512,.cache_size=512,.lookahead_size=128};lfs_t fs;
    int result=lfs_format(&fs,&config);if(!result)result=lfs_mount(&fs,&config);
    if(!result){if(!wallpaper)result=lfs_mkdir(&fs,"progress");int closed=lfs_unmount(&fs);if(!result)result=closed;}
    if(!result){FILE *out=fopen(argv[1],"wb");if(!out)result=-1;else{if(fwrite(bytes,1,IMAGE_SIZE,out)!=IMAGE_SIZE)result=-1;if(fclose(out)!=0)result=-1;}}
    free(bytes);if(result){fprintf(stderr,"image creation failed: %d\n",result);return 1;}
    puts(wallpaper?"Fresh wallpaper image: 1 MiB, LittleFS 2.1 disk format, empty; not flashed":"Fresh data image: 1 MiB, LittleFS 2.1 disk format, progress directory; not flashed");return 0;
}
