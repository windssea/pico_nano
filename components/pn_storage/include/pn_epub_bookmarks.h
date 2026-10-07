/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：每书100条EPUB语义书签与独立双槽记录。
 * English: up to 100 semantic EPUB bookmarks per book with separate dual-slot records.
 * 冻结：保留墓碑与ID，不写阅读进度，不猜测损坏记录。
 * Frozen: preserve tombstones and IDs; no progress writes or guesses through damaged records.
 */
#pragma once
#include "pn_epub_progress.h"
#include "pn_bookmark.h"
#define PN_EPUB_BOOKMARKS_MAX 100
typedef struct {
    pn_epub_progress_t position; ///< 内容身份和原文位置 / Content identity and original location
    uint64_t id; ///< 非零稳定编号 / Nonzero stable ID
    char label[PN_BOOKMARK_LABEL_MAX+1]; ///< UTF-8名称 / UTF-8 label
    bool deleted; ///< 持久删除墓碑 / Persistent deletion tombstone
} pn_epub_bookmark_t;
typedef struct {
    pn_pool_t *pool; ///< 借用内存owner / Borrowed memory owner
    pn_media_t *media; ///< 内部存储owner / Internal-storage owner
    pn_media_lease_t lease; ///< 调用者保持租约 / Caller maintains lease
    pn_book_id_t book; ///< 原文件SHA / Original-file SHA
    char directory[PN_JOURNAL_PATH_MAX]; ///< 独立命名空间 / Separate namespace
} pn_epub_bookmarks_t;
/// 绑定已存在root，不新建目录；owner串行操作。/ Bind existing root without creating directories; owner serializes calls.
pn_status_t pn_epub_bookmarks_init(pn_epub_bookmarks_t *,pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const char *,const pn_book_id_t *);
/// ID升序分页，错误count=0/more=false。/ Page by ascending ID; errors set count=0/more=false.
pn_status_t pn_epub_bookmarks_list(pn_epub_bookmarks_t *,uint64_t,pn_epub_bookmark_t *,size_t,size_t *,bool *);
/// 相同原文位置去重；最多100有效项，不复用墓碑ID。/ Deduplicate original locations; at most 100 live items, never reuse tombstone IDs.
pn_status_t pn_epub_bookmarks_add(pn_epub_bookmarks_t *,const pn_epub_progress_t *,const char *,uint64_t *);
/// 改名，不移动位置；READ租约拒绝写入。/ Rename without moving; READ leases reject writes.
pn_status_t pn_epub_bookmarks_rename(pn_epub_bookmarks_t *,uint64_t,const char *);
/// 幂等墓碑删除，确认界面由调用方负责。/ Idempotent tombstone deletion; caller owns confirmation UI.
pn_status_t pn_epub_bookmarks_delete(pn_epub_bookmarks_t *,uint64_t);
/// 返回持久语义位置；错误保持输出，删除返回EMPTY。/ Return durable location, preserving outputs on error; deleted returns EMPTY.
pn_status_t pn_epub_bookmarks_position(pn_epub_bookmarks_t *,uint64_t,pn_epub_progress_t *);
