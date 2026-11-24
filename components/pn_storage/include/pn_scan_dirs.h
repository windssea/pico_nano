/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：外部扫描目录配置（图书/字体/壁纸/阅读背景各最多4个，支持最多3层子目录递归，A/B日志持久化）。
 * English: External scan directory registry (up to 4 extra folders per category with bounded 3-level recursive scanning, persisted via A/B journals).
 */
#pragma once
#include "pn_journal.h"
#include "pn_sd_layout.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// 支持配置外部扫描目录的资源类别。/ Resource categories supporting external scan directories.
typedef enum {
    PN_SCAN_CAT_BOOKS=0, ///< 图书 / Books
    PN_SCAN_CAT_FONTS=1, ///< 字体 / Fonts
    PN_SCAN_CAT_WALLPAPERS=2, ///< 壁纸与主页横幅 / Wallpapers and Home banners
    PN_SCAN_CAT_BACKGROUNDS=3, ///< 阅读背景 / Reading backgrounds
    PN_SCAN_CAT_COUNT=4
} pn_scan_cat_t;

#define PN_SCAN_DIR_MAX 4u ///< 每类最多4个外部目录 / Max 4 external directories per category
#define PN_SCAN_REL_MAX 60u ///< 单条相对TF根路径最大字节（含NUL）/ Max relative path bytes including NUL
#define PN_SCAN_PATH_MAX 320u ///< 拼接后的绝对目录路径上限 / Max absolute directory path bytes
#define PN_SCAN_MAX_DEPTH 3u ///< 目录递归扫描最大深度（根目录为0，最多深入3层子文件夹）/ Max recursive scan depth below each root

/// 全部四类外部扫描目录集合（不含始终保留的默认 piconano/* 目录）。
/// External scan directory configuration across all 4 categories (excluding always-present default piconano/* folders).
typedef struct {
    unsigned char count[PN_SCAN_CAT_COUNT]; ///< 各类已登记外部目录数（0..4）/ Registered count per category
    char dirs[PN_SCAN_CAT_COUNT][PN_SCAN_DIR_MAX][PN_SCAN_REL_MAX]; ///< 相对TF根路径（不含前导/）/ Paths relative to TF mount root
} pn_scan_dirs_t;

/// 返回空配置（仅使用各默认目录）。/ Return an empty configuration (default directories only).
pn_scan_dirs_t pn_scan_dirs_default(void);
/// 返回类别对应的默认相对目录（如 "piconano/books"）。/ Return default relative folder for a category.
const char *pn_scan_dirs_default_rel(pn_scan_cat_t cat);
/// 校验相对路径是否合法（拒绝越界、隐藏目录、.readpico及与默认目录重叠）。
/// Validate a relative TF path (rejects traversal, hidden segments, .readpico, and overlap with default directory).
bool pn_scan_dirs_valid_rel(pn_scan_cat_t cat,const char *rel);
/// 校验相对路径是否适合作为外部目录加入（非重复、未满且不与现有目录互相包含）。
/// Add an external directory relative to TF mount root; returns PN_OK, PN_BUSY (duplicate/overlap), PN_LIMIT (full), or PN_INVALID.
pn_status_t pn_scan_dirs_add(pn_scan_dirs_t *dirs,pn_scan_cat_t cat,const char *rel);
/// 移除指定索引的外部目录（仅移出配置，不删除卡上任何文件）。/ Remove an external directory registration without deleting files.
pn_status_t pn_scan_dirs_remove(pn_scan_dirs_t *dirs,pn_scan_cat_t cat,size_t index);

/// 从内部存储加载四类外部目录配置；缺项按空配置返回 PN_OK。/ Load all 4 categories from internal A/B journals; missing entries default to empty.
pn_status_t pn_scan_dirs_load_all(pn_media_t *state_media,const char *state_dir,pn_scan_dirs_t *out);
/// 保存单个类别的外部目录配置到内部 A/B 日志。/ Save one category's external directories to internal A/B journal.
pn_status_t pn_scan_dirs_save_cat(pn_media_t *state_media,const char *state_dir,pn_scan_cat_t cat,const pn_scan_dirs_t *dirs);

/// 设置进程当前生效的外部目录快照与 TF 挂载根（如 "/sdcard"）。/ Publish active external scan directories and TF mount root.
void pn_scan_dirs_set_active(const pn_scan_dirs_t *dirs,const char *mount_root);
/// 获取当前生效的外部目录快照（只读）。/ Get current active external scan directories (read-only).
const pn_scan_dirs_t *pn_scan_dirs_active(void);
/// 获取当前挂载根路径（默认 PN_SD_MOUNT）。/ Get active TF mount root (defaults to PN_SD_MOUNT).
const char *pn_scan_dirs_mount(void);
/// 判断目录路径是否为某类别的默认根目录（<mount>/piconano/<cat>）。/ Check whether directory matches a category's default root.
bool pn_scan_dirs_match_default(const char *directory,pn_scan_cat_t *cat_out);
/// 汇总某类别全部扫描根（默认目录 + 已登记外部目录，自动去重）。/ Collect default root + active external roots for a category.
size_t pn_scan_dirs_collect_roots(pn_scan_cat_t cat,const char *default_dir,char out[][PN_SCAN_PATH_MAX],size_t cap);
/// 校验文件完整路径是否安全位于该类别默认目录或任意外部目录（含最多3层子目录）内。
/// Check whether full_path is a safe file inside the category's default dir or any active external dir (up to depth 3).
bool pn_scan_dirs_path_allowed(pn_scan_cat_t cat,const char *full_path);
/// 在默认目录及外部目录（含最多3层子目录）中按文件名查找首个存在的常规文件。
/// Resolve a bare filename across the default dir and active external dirs (up to depth 3), returning the first existing regular file.
bool pn_scan_dirs_find_file(pn_scan_cat_t cat,const char *default_dir,const char *filename,char *out_path,size_t out_cap);

#ifdef __cplusplus
}
#endif
