/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：手动已读/未读标记：最多64本书路径与状态，保存在内部状态目录的read_marks.a/b。
 * English: manual read/unread marks: up to 64 book paths and states stored as read_marks.a/b in the internal state directory.
 * 冻结：只按路径记录，不读书籍内容；自动100%完读无需写入记录。
 * Frozen: records paths only without reading book contents; automatic 100% completion needs no entry.
 */
#pragma once
#include "pn_blob.h"
#define PN_READ_MARKS_MAX 64
#define PN_READ_MARK_PATH_MAX 512
/// 单本书的手动阅读状态覆写。/ Per-book manual read-state override.
typedef enum {
    PN_READ_MARK_AUTO = 0,   ///< 跟随阅读进度（100%自动显示已读）/ Follow reading progress (100% auto-marks read)
    PN_READ_MARK_READ = 1,   ///< 手动标记已读 / Manually marked as read
    PN_READ_MARK_UNREAD = 2  ///< 手动标记未读 / Manually marked as unread
} pn_read_mark_t;
typedef struct {
    size_t count; ///< 已记录条目数 / Stored entry count
    uint8_t states[PN_READ_MARKS_MAX]; ///< pn_read_mark_t（仅READ或UNREAD）/ pn_read_mark_t (READ or UNREAD only)
    char paths[PN_READ_MARKS_MAX][PN_READ_MARK_PATH_MAX]; ///< 书籍路径 / Book paths
} pn_read_marks_t;
/// 绑定root下的read_marks.a/b；不创建目录或挂载。/ Bind read_marks.a/b under an existing root without creating directories or mounting.
pn_status_t pn_read_marks_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io);
/// 读取；没有记录返回PN_EMPTY；错误不改输出。/ Load; PN_EMPTY without a record; errors preserve output.
pn_status_t pn_read_marks_load(const pn_journal_io_t *io,pn_pool_t *pool,pn_read_marks_t *marks);
/// 查询path的手动覆写状态；无记录或marks为NULL返回PN_READ_MARK_AUTO。/ Query manual override for path; returns PN_READ_MARK_AUTO when absent or NULL.
pn_read_mark_t pn_read_marks_get(const pn_read_marks_t *marks,const char *path);
/// 结合手动标记与进度判断本书是否显示为“已读”。/ Decide whether a book displays as "Read" combining manual marks and progress.
bool pn_read_marks_is_read(const pn_read_marks_t *marks,const char *path,bool has_progress,uint16_t progress);
/// 设置path的状态（AUTO会移除该条记录）；超过64条返回PN_LIMIT。/ Set state for path (AUTO removes the entry); exceeding 64 entries returns PN_LIMIT.
pn_status_t pn_read_marks_set(const pn_journal_io_t *io,pn_pool_t *pool,const char *path,pn_read_mark_t state);
