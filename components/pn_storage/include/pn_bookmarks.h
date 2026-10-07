/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：每书100条TXT书签管理，从独立A/B记录恢复列表。
 * English: up to 100 TXT bookmarks per book, reconstructing lists from separate A/B records.
 * 冻结：借用内部存储租约；不删墓碑、不复用ID、不更改阅读进度。
 * Frozen: borrow internal-storage leases; no tombstone removal, ID reuse or reading-progress changes.
 */
#pragma once
#include "pn_bookmark.h"
#define PN_BOOKMARKS_MAX 100
typedef struct {
    pn_media_t *media; ///< 已挂载内部存储owner / Mounted internal-storage owner
    pn_media_lease_t lease; ///< 调用方保持有效的租约 / Caller keeps the lease valid
    pn_txt_progress_t expected; ///< 身份、长度和解析策略；offset忽略 / Identity, size and parsing policy; offset ignored
    char directory[PN_JOURNAL_PATH_MAX]; ///< 独立书签命名空间 / Separate bookmark namespace
} pn_bookmarks_t;
/// root已存在；仅绑定，不创建目录。owner串行操作。
/// Root already exists; bind without creating directories. Owner serializes operations.
pn_status_t pn_bookmarks_init(pn_bookmarks_t *marks,pn_media_t *media,const pn_media_lease_t *lease,
    const char *root,const pn_txt_progress_t *expected);
/// 按ID升序分页；capacity为1至100，错误count=0，条目输出不可使用。
/// Page in ascending ID order; capacity is 1 through 100, errors set count=0 and invalidate entries.
pn_status_t pn_bookmarks_list(pn_bookmarks_t *marks,uint64_t after,pn_txt_bookmark_t *items,
    size_t capacity,size_t *count,bool *more);
/// 已有相同源位置返回原ID；否则分配递增ID，最多100条有效书签。
/// Return existing ID at the same source offset; otherwise allocate increasing ID, up to 100 live marks.
pn_status_t pn_bookmarks_add(pn_bookmarks_t *marks,const pn_txt_progress_t *position,const char *label,uint64_t *id);
/// 同步改名，不改变位置；READ租约禁止写入。
/// Synchronously rename without moving; READ leases prohibit writes.
pn_status_t pn_bookmarks_rename(pn_bookmarks_t *marks,uint64_t id,const char *label);
/// 仅保存墓碑；确认UI由调用方负责。
/// Save a tombstone only; caller owns confirmation UI.
pn_status_t pn_bookmarks_delete(pn_bookmarks_t *marks,uint64_t id);
/// 取有效跳转位置，已删除或不存在返回EMPTY，错误不改输出。
/// Get live jump location; EMPTY for deleted/absent, preserving output on errors.
pn_status_t pn_bookmarks_position(pn_bookmarks_t *marks,uint64_t id,pn_txt_progress_t *position);
