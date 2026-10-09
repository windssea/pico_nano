/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：网络无关的串行传输owner，同源会话调度及停止屏障。
 * English: network-independent serial transfer owner with shared session dispatch and a stop barrier.
 * 冻结：不管理授权/线程，不猜测错误后的ACK，不创建第二个介质服务。
 * Frozen: no authorization/threads, guessed ACK after errors, or second media service.
 */
#pragma once
#include "pn_upload_files.h"
typedef enum {
 PN_TRANSFER_BEGIN=1, ///< 新上传 / New upload
 PN_TRANSFER_OPEN, ///< 重开并恢复持久状态 / Reopen and restore durable state
 PN_TRANSFER_CHUNK, ///< 同步分块 / Synchronized chunk
 PN_TRANSFER_COMPLETE, ///< 校验安装 / Validate and install
 PN_TRANSFER_CANCEL, ///< 持久取消 / Durable cancellation
 PN_TRANSFER_FILE, ///< 旧资源身份 / Existing resource identity
 PN_TRANSFER_STOP, ///< 关闭消费者并永久停止本实例 / Close consumers and permanently stop this instance
 PN_TRANSFER_LIST, ///< 按名称字节序分页列出已安装字体 / List installed fonts in name byte order, paged
 PN_TRANSFER_DELETE ///< 核对身份后删除一个已安装字体 / Delete one installed font after an identity check
} pn_transfer_operation_t;
#define PN_TRANSFER_LIST_MAX 8 ///< 单次列表最多条数 / Maximum entries per listing page
typedef struct {
 pn_transfer_operation_t operation; ///< 操作 / Operation
 pn_upload_request_t request; ///< BEGIN请求；FILE/DELETE使用kind/name；LIST用kind和name作游标（空为起点，只返回字节序更大的名称） / BEGIN request; FILE/DELETE use kind/name; LIST uses kind and name as the cursor (empty starts over, only strictly greater names are returned)
 uint8_t id[PN_UPLOAD_ID_BYTES]; ///< OPEN/CHUNK/COMPLETE/CANCEL会话 / Session for OPEN/CHUNK/COMPLETE/CANCEL
 bool replace; ///< BEGIN明确覆盖许可 / Explicit BEGIN replacement grant
 uint64_t old_size; ///< BEGIN/DELETE预期旧长度 / Expected old length for BEGIN/DELETE
 pn_book_id_t old_digest; ///< BEGIN/DELETE预期旧摘要 / Expected old digest for BEGIN/DELETE
 uint64_t offset; ///< CHUNK位置 / CHUNK position
 const uint8_t *bytes; ///< CHUNK借用只读数据，execute返回前不可释放 / Borrowed read-only CHUNK data, retained until execute returns
 size_t length; ///< CHUNK字节数 / CHUNK byte count
 pn_book_id_t digest; ///< CHUNK块摘要或COMPLETE完整摘要 / CHUNK digest or COMPLETE full digest
} pn_transfer_command_t;
typedef struct {
 char name[PN_UPLOAD_NAME_MAX]; ///< UTF8文件名 / UTF8 file name
 uint64_t size; ///< 文件字节数 / File size in bytes
} pn_transfer_entry_t;
typedef struct {
 bool has_list; ///< LIST成功（count可为0） / LIST succeeded (count may be zero)
 bool more; ///< 末项之后还有条目 / More entries after the last one
 bool deleted; ///< DELETE已删除并同步目录 / DELETE removed the file and synchronized the directory
 size_t count; ///< list有效条数 / Valid entries in list
 pn_transfer_entry_t list[PN_TRANSFER_LIST_MAX]; ///< 名称字节序升序 / Ascending name byte order
 bool has_upload; ///< 存在已确认上传快照 / Confirmed upload snapshot present
 pn_upload_request_t request; ///< 持久请求身份 / Durable request identity
 pn_upload_state_t state; ///< 持久位置和阶段 / Durable offset and phase
 bool cleanup_pending; ///< 新主已装好，备份待清理 / New target installed, backup cleanup pending
 bool has_file; ///< 存在旧资源身份 / Existing resource identity present
 uint64_t file_size; ///< 旧资源精确长度 / Existing resource exact length
 pn_book_id_t file_digest; ///< 旧资源完整摘要 / Existing resource full digest
} pn_transfer_reply_t;
typedef struct {void *impl;} pn_transfer_service_t; ///< 活动服务不可复制 / Never copy a live service
/// 借用已有pool/media及平台ctx，复制根和默认选项；不取得WRITE，不写文件。
/// Borrow existing pool/media and platform ctx, copy root/defaults; no WRITE acquisition or file writes.
pn_status_t pn_transfer_service_open(pn_transfer_service_t *,pn_pool_t *,pn_media_t *,const pn_upload_files_options_t *);
/// 仅owner串行同步执行；错误不改reply，输入保留至本次调用返回。
/// Execute synchronously on the serial owner only; errors preserve reply, retain input until this call returns.
pn_status_t pn_transfer_service_execute(pn_transfer_service_t *,const pn_transfer_command_t *,pn_transfer_reply_t *);
/// 停止并释放内存/租约，保留未完成持久会话；失效介质也允许关闭。
/// Stop and release memory/leases, retaining unfinished durable sessions; stale media may close.
pn_status_t pn_transfer_service_close(pn_transfer_service_t *);
