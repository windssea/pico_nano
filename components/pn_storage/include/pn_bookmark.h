/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：单个TXT书签的持久记录和操作，不负责书签列表索引。
 * English: persistent single-TXT-bookmark records and operations, excluding list indexing.
 * 冻结：每个书签独立A/B路径；删除留墓碑，不影响阅读进度。
 * Frozen: distinct A/B paths per bookmark; deletion uses tombstones and never changes reading progress.
 */
#pragma once
#include "pn_progress.h"
#define PN_BOOKMARK_LABEL_MAX 48

typedef struct {
    pn_txt_progress_t position; ///< 内容身份和源位置 / Content identity and source position
    uint64_t id; ///< 非零稳定书签号，路径索引分配 / Nonzero stable bookmark ID, allocated by path index
    char label[PN_BOOKMARK_LABEL_MAX+1]; ///< 至多48 UTF-8字节 / At most 48 UTF-8 bytes
    bool deleted; ///< 持久删除墓碑 / Persistent deletion tombstone
} pn_txt_bookmark_t;
/// 仅在双槽均不存在时新建；不能覆盖已存在或损坏的记录。
/// Create only when both slots are absent; never overwrite existing or corrupt records.
pn_status_t pn_txt_bookmark_create(const pn_journal_io_t *io,const pn_txt_progress_t *position,uint64_t id,const char *label);
/// 错书/错书签号拒绝恢复，错误保持输出；deleted仍可读用于索引恢复。
/// Reject other books/IDs, preserving output on error; deleted records remain readable for index recovery.
pn_status_t pn_txt_bookmark_load(const pn_journal_io_t *io,const pn_book_id_t *book,uint64_t id,pn_txt_bookmark_t *bookmark);
/// 改名同步成功才算完成；不修改位置。
/// Rename completes only after synchronized success; position remains unchanged.
pn_status_t pn_txt_bookmark_rename(const pn_journal_io_t *io,const pn_book_id_t *book,uint64_t id,const char *label);
/// 幂等删除并保存墓碑；失败保留原记录。
/// Idempotent deletion with a persistent tombstone; failures retain the original record.
pn_status_t pn_txt_bookmark_delete(const pn_journal_io_t *io,const pn_book_id_t *book,uint64_t id);
/// 取跳转位置；已删除返回EMPTY，不更改当前位置。
/// Get jump position; deleted returns EMPTY without changing the current position.
pn_status_t pn_txt_bookmark_position(const pn_journal_io_t *io,const pn_book_id_t *book,uint64_t id,pn_txt_progress_t *position);
