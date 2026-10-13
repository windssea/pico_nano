/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：存储卡公开目录与内部目录的唯一命名契约。
 * English: single naming contract for public and internal SD directories.
 * 冻结：上传通道不得自行选择保存目录；不迁移或删除旧文件。
 * Frozen: upload transports never choose destination directories; no legacy migration or deletion.
 */
#pragma once
#include <stdbool.h>
#include <string.h>
#define PN_SD_MOUNT "/sdcard" ///< 设备挂载点 / Device mount point
#define PN_SD_APP "piconano" ///< TF上的应用根目录 / Application root on TF
#define PN_SD_BOOKS PN_SD_APP "/books" ///< 书籍 / Books
#define PN_SD_FONTS PN_SD_APP "/fonts" ///< 字体 / Fonts
#define PN_SD_COVERS PN_SD_APP "/covers" ///< 封面原图 / Original covers
#define PN_SD_WALLPAPERS PN_SD_APP "/wallpapers" ///< 锁屏原图 / Wallpaper originals
#define PN_SD_PRIVATE PN_SD_APP "/.readpico" ///< 内部数据，不进入书架 / Internal data, excluded from the shelf
#define PN_SD_UPLOADS "uploads" ///< 内部上传暂存 / Internal upload staging
#define PN_SD_COVER_CACHE PN_SD_PRIVATE "/covers" ///< 可重建缩略图缓存 / Rebuildable thumbnail cache
#define PN_SD_INDEX_CACHE PN_SD_PRIVATE "/indexes" ///< 可重建书库索引 / Rebuildable catalog indexes
/// 检查设备资源是否位于固定目录下的单层文件，不接受子目录或旧目录。/ Check a device resource is a flat file in the fixed folder, rejecting subfolders and legacy roots.
static inline bool pn_sd_resource_path(const char *path,const char *folder){
    const char *mount=PN_SD_MOUNT "/";size_t n=strlen(mount),f=folder?strlen(folder):0;
    if(!path || !f || strncmp(path,mount,n))return false;
    const char *relative=path+n;if(strlen(relative)<=f || strncmp(relative,folder,f) || relative[f]!='/')return false;
    const char *leaf=relative+f+1;return *leaf && !strchr(leaf,'/') && !strchr(leaf,'\\') && strcmp(leaf,".") && strcmp(leaf,"..");
}
