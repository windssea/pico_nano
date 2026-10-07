/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：固定根目录的真实上传文件、同步与覆盖恢复port。
 * English: real upload files, synchronization and replacement recovery within fixed roots.
 * 冻结：未核对身份不覆盖，不能格式化；不管理网络授权。
 * Frozen: never replace without identity checks or format media; no network authorization management.
 */
#pragma once
#include "pn_upload.h"
#define PN_UPLOAD_FILES_ROOT_MAX 96
typedef struct {
 const char *root; ///< 已挂载可信绝对根，子目录由kind决定 / Trusted mounted absolute root, kind selects subdirectories
 void *ctx; ///< 平台回调上下文 / Platform callback context
 pn_status_t (*sync_directory)(void *,const char *); ///< 命名空间正向同步；PC可NULL默认fsync目录，ESP必须提供 / Positive namespace sync; NULL defaults to PC directory fsync, required on ESP
 pn_status_t (*space_free)(void *,const char *,uint64_t *); ///< 当前可用空间；PC可NULL，ESP必须提供 / Current free space; optional on PC, required on ESP
 bool replace; ///< 本次创建明确允许覆盖指定原身份 / Explicitly permit this creation to replace the specified original identity
 uint64_t old_size; ///< 覆盖前预期原文件大小 / Expected original size before replacement
 pn_book_id_t old_digest; ///< 覆盖前预期原文件完整SHA / Expected complete original SHA before replacement
} pn_upload_files_options_t;
typedef struct {void *impl;} pn_upload_files_t; ///< 活动port不可复制 / Never copy a live port
/// 仅分配配置，文件操作留给upload的WRITE租约；恢复采用持久覆盖意图。
/// Allocate configuration only; file operations require upload WRITE lease; resume uses durable replacement intent.
pn_status_t pn_upload_files_open(pn_upload_files_t *,pn_pool_t *,const pn_upload_files_options_t *,pn_upload_port_t *);
/// upload关闭后释放port；仍绑定时BUSY。/ Release after upload close; BUSY while bound.
pn_status_t pn_upload_files_close(pn_upload_files_t *);
/// 新主已安装时清理失败是警告，查询持久阶段以便提示。/ Cleanup failure after successful installation is a warning; query durable phase for UI.
pn_status_t pn_upload_files_cleanup_pending(pn_upload_files_t *,bool *);
/// 同ID有效WRITE绑定下重试备份/重复暂存清理，绝不删除新主。/ Retry backup/duplicate staging cleanup under the same ID WRITE binding; never delete the new target.
pn_status_t pn_upload_files_cleanup(pn_upload_files_t *);
