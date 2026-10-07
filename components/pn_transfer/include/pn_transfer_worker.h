/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：传输专用串行任务、有界请求副本与异步停止入口。
 * English: dedicated serial transfer task, bounded request copies, and asynchronous stop admission.
 * 冻结：借用已交出的唯一介质owner，不能与阅读或USB并行访问。
 * Frozen: borrow the sole handed-off media owner, never access it concurrently with reading or USB.
 */
#pragma once
#include "pn_transfer_service.h"
typedef enum {PN_TWORK_STARTING,PN_TWORK_ACTIVE,PN_TWORK_STOPPING,PN_TWORK_STOPPED,PN_TWORK_FAILED} pn_transfer_worker_phase_t; ///< 任务生命周期 / Task lifecycle
typedef struct {
 pn_transfer_worker_phase_t phase; ///< 生命周期 / Lifecycle
 bool busy; ///< 已接收请求正在排队或执行 / Accepted request pending or executing
 pn_status_t error; ///< 启动/停止错误 / Startup/stop error
 size_t pool_peak; ///< 专用事务池峰值，不含固定请求槽/任务栈 / Private transaction pool peak, excluding fixed request slot/task stack
} pn_transfer_worker_state_t;
typedef struct {void *impl;} pn_transfer_worker_t; ///< 不复制活动对象 / Never copy a live object
/// 调用前关闭所有TF消费者并移交media；任务拥有独立受限池，allocator不能借用UI池。
/// Close all TF consumers and hand off media first; task owns a separate bounded pool, allocator cannot borrow a UI pool.
pn_status_t pn_transfer_worker_open(pn_transfer_worker_t *,pn_media_t *,const pn_upload_files_options_t *,size_t,pn_malloc_fn,pn_free_fn,void *);
/// 单请求槽；并发调用BUSY，停止后CANCELLED；同步等待已接收请求，错误不改reply。
/// One request slot; concurrent calls return BUSY, stopped calls CANCELLED; wait for accepted work, errors preserve reply.
pn_status_t pn_transfer_worker_execute(pn_transfer_worker_t *,const pn_transfer_command_t *,pn_transfer_reply_t *);
/// 立即关闭新请求入口，已接收请求执行完后释放介质；不等待耗时文件校验。
/// Immediately close admission; drain accepted work then release media, without waiting for lengthy validation.
pn_status_t pn_transfer_worker_request_stop(pn_transfer_worker_t *);
/// 只读锁保护快照，不访问任务的pool/media对象。/ Read a locked snapshot without accessing the task's pool/media objects.
pn_status_t pn_transfer_worker_state(pn_transfer_worker_t *,pn_transfer_worker_state_t *);
/// 先停止并join网络调用方，随后等待任务退出并释放；未返回前不释放平台ctx/media。
/// Stop and join network callers first, then wait for task exit and release; retain platform ctx/media until return.
pn_status_t pn_transfer_worker_close(pn_transfer_worker_t *);
