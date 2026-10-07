/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：最近20本快照，完整路径与内容身份，顺序不依赖RTC。
 * English: latest 20 books with full paths and content identity, independent of RTC ordering.
 * 冻结：预览进度不替代逐书位置；不因丢文件清记录。
 * Frozen: preview progress never replaces per-book positions; missing files do not clear records.
 */
#pragma once
#include "pn_blob.h"
#include "pn_identity.h"
#define PN_RECENT_MAX 20
#define PN_RECENT_PATH_MAX 1024
#define PN_RECENT_UNKNOWN_PROGRESS 65535
typedef struct {
    pn_book_id_t book; ///< 原内容SHA / Original content SHA
    uint64_t source_size; ///< 原文件大小 / Original file size
    uint16_t format; ///< 1 TXT/2 EPUB/3 PDF/4 FB2/5 CBZ，识别不代表支持 / Recognition, not support
    uint16_t progress; ///< 0..10000预览或UNKNOWN / 0..10000 preview or UNKNOWN
    char path[PN_RECENT_PATH_MAX]; ///< 完整UTF-8实例路径 / Full UTF-8 instance path
} pn_recent_item_t;
typedef struct {
    size_t count; ///< 最多20条 / At most 20 entries
    pn_recent_item_t items[PN_RECENT_MAX]; ///< 最近在前 / Most recent first
} pn_recent_snapshot_t;
/// 输出仅在完整有效时改变；不存在返回EMPTY，不擦除损坏。
/// Change output only for complete valid snapshots; EMPTY if absent, preserving corruption.
pn_status_t pn_recent_load(const pn_journal_io_t *io,pn_pool_t *pool,pn_recent_snapshot_t *snapshot);
/// 内容SHA去重，新实例路径替换旧路径；新条目排首，超过20移除最旧条目。
/// Deduplicate by content SHA, replace old instance paths, prepend and evict oldest beyond 20.
pn_status_t pn_recent_touch(const pn_journal_io_t *io,pn_pool_t *pool,const pn_recent_item_t *item);
/// 绑定既有root的recent.a/b；不创建目录或挂载。
/// Bind recent.a/b under an existing root without creating directories or mounting.
pn_status_t pn_recent_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io);
