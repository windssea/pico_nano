/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：收藏夹：最多50本书的完整路径，最近收藏在前，保存在内部状态目录的favorites.a/b。
 * English: favorites: full paths of up to 50 books, most recently added first, stored as favorites.a/b in the internal state directory.
 * 冻结：只按路径记录，不读书籍内容；文件不存在时记录保留，由界面标出。
 * Frozen: entries are paths only and never read book contents; records stay when a file is missing and the UI marks it.
 */
#pragma once
#include "pn_blob.h"
#define PN_FAVORITES_MAX 50
#define PN_FAVORITES_PATH_MAX 512
typedef struct {
    size_t count; ///< 条目数 / Entry count
    char paths[PN_FAVORITES_MAX][PN_FAVORITES_PATH_MAX]; ///< 最近收藏在前 / Most recent first
} pn_favorites_t;
/// 绑定root下的favorites.a/b；不创建目录或挂载。/ Bind favorites.a/b under an existing root without creating directories or mounting.
pn_status_t pn_favorites_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io);
/// 读取；没有记录返回PN_EMPTY；错误不改输出。/ Load; PN_EMPTY without a record; errors keep the output.
pn_status_t pn_favorites_load(const pn_journal_io_t *io,pn_pool_t *pool,pn_favorites_t *favorites);
/// 加入或移除path；favorite输出操作后是否在收藏里。满50本时加入返回PN_LIMIT。
/// Add or remove path; favorite reports whether it is a favorite afterwards. Adding at 50 entries returns PN_LIMIT.
pn_status_t pn_favorites_toggle(const pn_journal_io_t *io,pn_pool_t *pool,const char *path,bool *favorite);
/// path是否在收藏里。/ Whether path is a favorite.
bool pn_favorites_contains(const pn_favorites_t *favorites,const char *path);
