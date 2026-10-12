/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：存储卡公开目录与内部目录的唯一命名契约。
 * English: single naming contract for public and internal SD directories.
 * 冻结：上传通道不得自行选择保存目录；不迁移或删除旧文件。
 * Frozen: upload transports never choose destination directories; no legacy migration or deletion.
 */
#pragma once
#define PN_SD_MOUNT "/sdcard" ///< 设备挂载点 / Device mount point
#define PN_SD_BOOKS "books" ///< 书籍 / Books
#define PN_SD_FONTS "fonts" ///< 字体 / Fonts
#define PN_SD_COVERS "covers" ///< 封面原图 / Original covers
#define PN_SD_WALLPAPERS "wallpapers" ///< 锁屏原图 / Wallpaper originals
#define PN_SD_PRIVATE ".readpico" ///< 内部数据，不进入书架 / Internal data, excluded from the shelf
#define PN_SD_UPLOADS "uploads" ///< 内部上传暂存 / Internal upload staging
#define PN_SD_COVER_CACHE PN_SD_PRIVATE "/covers" ///< 可重建缩略图缓存 / Rebuildable thumbnail cache
