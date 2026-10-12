/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：网络无关的整文件流接收，复用分块持久化和固定目录安装。
 * English: network-independent whole-file streaming over durable chunks and fixed-directory installs.
 * 冻结：调用方先授权并确认接收；不创建第二个文件owner，不宣称协议互通。
 * Frozen: caller authorizes and confirms reception first; no second file owner or interoperability claim.
 */
#pragma once
#include "pn_transfer_service.h"
/// 使用已有串行服务或worker；仅同步执行完成后返回。/ Use the existing serial service or worker; return after synchronous completion.
typedef pn_status_t (*pn_transfer_execute_fn)(void *,const pn_transfer_command_t *,pn_transfer_reply_t *);
/// PN_OK须返回1..capacity字节，精确EOF返回PN_EMPTY及0；超时/取消由调用方实现。/ PN_OK yields 1..capacity bytes, exact EOF yields PN_EMPTY/0; caller implements timeout/cancellation.
typedef pn_status_t (*pn_transfer_read_fn)(void *,uint8_t *,size_t,size_t *);
typedef struct {
    bool committed; ///< 安装成功并确认 / Installation succeeded and confirmed
    bool cleanup_pending; ///< 新主已确认，安装备份仍待清理 / New target confirmed; installation backup still needs cleanup
    bool finish_started; ///< 已调用安装，失败可能需恢复，不可假定未安装 / Installation attempted; failure may require recovery, never assume no install
    uint64_t received; ///< 实际读入字节，不是持久ACK / Bytes read, never a durable ACK
    pn_status_t cancel_status; ///< 失败时取消结果；PN_EMPTY为尚未开始 / Cancellation result on failure; PN_EMPTY means not started
    pn_book_id_t digest; ///< 仅committed时有效的完整内容身份 / Full identity valid only when committed
} pn_transfer_receive_result_t;
/// 内存固定为一个64KiB块；可缺省SHA；不覆盖同名文件，不重试网络输入。
/// Memory is one fixed 64 KiB chunk; SHA may be omitted; no replacement or network-input retries.
/// 网络调用使用独立受限pool，不跨线程借用UI池。/ Network callers use an independent bounded pool, never the UI pool across threads.
/// 失败保留原错误并报告取消结果；finish_started且未committed须由owner恢复/停止屏障收口。
/// Preserve original errors and report cancellation; finish_started without committed requires owner recovery/stop barriers.
pn_status_t pn_transfer_receive(pn_pool_t *,pn_transfer_execute_fn,void *,const pn_upload_request_t *,pn_transfer_read_fn,void *,pn_transfer_receive_result_t *);
