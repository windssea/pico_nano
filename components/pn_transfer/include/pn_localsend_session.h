/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：LocalSend有界会话与确认授权，网络/JSON/UI由适配层接入。
 * English: bounded LocalSend sessions and confirmation grants; adapters own networking, JSON and UI.
 * 冻结：单owner串行调用；未确认不发令牌，活动接收/恢复未收口不销毁。
 * Frozen: one serialized owner; no grants before confirmation or destruction during receive/recovery.
 */
#pragma once
#include "pn_transfer_receive.h"
#define PN_LS_FILES_MAX 8 ///< 单会话文件上限 / Files per session
#define PN_LS_ID_MAX 65 ///< 文件ID UTF8字节上限含NUL / File-ID UTF8 byte capacity including NUL
#define PN_LS_CONFIRM_MS 60000u ///< 等待确认时限 / Confirmation deadline
#define PN_LS_READY_MS 300000u ///< 确认后等待上传时限 / Post-confirmation upload deadline
#define PN_LS_IDLE_MS 10000u ///< 读正文空闲时限 / Body-read idle deadline
#define PN_LS_TRANSFER_MS 1800000u ///< 单文件总时限 / Absolute per-file deadline
typedef enum {PN_LS_IDLE,PN_LS_PENDING,PN_LS_READY,PN_LS_RECEIVING,PN_LS_VERIFYING,PN_LS_DONE,PN_LS_REJECTED,PN_LS_CANCELLED,PN_LS_EXPIRED,PN_LS_RECOVERY} pn_ls_phase_t; ///< 会话状态 / Session state
typedef struct {unsigned family;uint8_t address[16];} pn_ls_peer_t; ///< 来自实际连接的IPv4/IPv6地址，不含易变的源端口 / Actual connection IPv4/IPv6 address, excluding transient source port
typedef struct {
    char id[PN_LS_ID_MAX]; ///< 协议文件ID，从不作为文件路径 / Protocol file ID, never a filesystem path
    char name[PN_UPLOAD_NAME_MAX]; ///< 已校验保存名称 / Validated destination name
    uint64_t size; ///< 声明字节数 / Declared bytes
    bool has_digest; ///< 是否声明SHA / Whether SHA is supplied
    pn_book_id_t digest; ///< 可选完整SHA / Optional full SHA
    pn_upload_kind_t kind; ///< 推断类别；图片确认前为0 / Inferred kind; images remain zero until confirmation
} pn_ls_file_t;
typedef struct {
    char alias[129]; ///< 发送方申报名称，不能当身份认证 / Sender-claimed name, never authentication
    char fingerprint[129]; ///< 发送方申报指纹 / Sender-claimed fingerprint
    char version[16]; ///< 协议2.x版本 / Protocol 2.x version
    size_t count; ///< 文件数 / File count
    pn_ls_file_t files[PN_LS_FILES_MAX]; ///< 有界元数据 / Bounded metadata
} pn_ls_request_t;
typedef struct {bool accept;pn_upload_kind_t image_kind;} pn_ls_choice_t; ///< 用户选择；图片须为封面或锁屏 / User choice; images require cover or wallpaper
typedef pn_status_t (*pn_ls_random_fn)(void *,uint8_t *,size_t); ///< 平台安全随机源；失败不得降级 / Platform secure entropy; never fall back on failure
typedef struct {void *impl;} pn_ls_session_t; ///< 活动对象不可复制 / Live objects must not be copied
typedef struct {
    pn_ls_phase_t phase; ///< 当前状态 / Current phase
    uint64_t generation; ///< 确认界面绑定的请求代次 / Request generation bound to the confirmation UI
    pn_ls_request_t request; ///< 展示元数据，不含令牌或内部上传ID / Display metadata, no tokens or internal upload IDs
    size_t installed; ///< 已确认安装数 / Confirmed installs
    size_t accepted; ///< 用户选择接收数 / User-selected receive count
    size_t failed; ///< 已处理但未安装数，包含取消 / Processed but uninstalled count, including cancellations
    bool cancel_requested; ///< 等待接收端取消收口 / Waiting for receiver cancellation
    bool recovery_install; ///< 恢复须重试安装，否则取消未安装接收 / Recovery retries installation; otherwise cancel uninstalled reception
    bool unresolved; ///< 停止后仍有未确认持久事务，不代表已撤销 / Unconfirmed durable transaction remains after stop, never proof of rollback
} pn_ls_snapshot_t;
typedef struct {
    char session_id[33]; ///< 128位会话ID十六进制 / Hex 128-bit session ID
    size_t count; ///< 当前待上传文件数 / Files awaiting upload
    struct {char id[PN_LS_ID_MAX];char token[33];} files[PN_LS_FILES_MAX]; ///< 逐文件128位令牌 / Per-file 128-bit tokens
} pn_ls_grants_t;
/// 借用受限pool和安全随机源，无网络或文件操作。/ Borrow a bounded pool and secure entropy; no networking or files.
pn_status_t pn_ls_session_open(pn_ls_session_t *,pn_pool_t *,pn_ls_random_fn,void *);
/// 活动接收/恢复返回BUSY；调用方先停止并join网络调用。/ Active receive/recovery returns BUSY; caller stops and joins network users first.
pn_status_t pn_ls_session_close(pn_ls_session_t *);
/// 校验元数据后进入等待确认，已有活动会话返回BUSY。/ Validate metadata and await confirmation; active sessions return BUSY.
pn_status_t pn_ls_prepare(pn_ls_session_t *,const pn_ls_peer_t *,const pn_ls_request_t *,uint64_t);
/// 确认须绑定snapshot.generation；随机生成全部成功才发布授权。/ Confirmation binds snapshot.generation; publish grants only after all entropy succeeds.
pn_status_t pn_ls_decide(pn_ls_session_t *,uint64_t,const pn_ls_choice_t *,uint64_t);
/// 只读展示快照，调用方提供存储。/ Read-only display snapshot into caller storage.
pn_status_t pn_ls_snapshot(pn_ls_session_t *,pn_ls_snapshot_t *);
/// 仅READY输出授权，错误不改输出。/ Output grants only in READY; preserve output on failure.
pn_status_t pn_ls_grants(pn_ls_session_t *,pn_ls_grants_t *,uint64_t);
/// 校验来源/会话/文件/令牌后给共享接收层生成请求，错误不改输出。/ Check origin/session/file/token and construct a receive request; preserve output on errors.
pn_status_t pn_ls_authorize(pn_ls_session_t *,const pn_ls_peer_t *,const char *,const char *,const char *,uint64_t,pn_upload_request_t *);
/// 收字节数非持久ACK；调用方读回调在网络锁外执行。/ Received bytes are not ACKs; caller performs network reads outside state locks.
pn_status_t pn_ls_progress(pn_ls_session_t *,uint64_t,uint64_t);
/// 精确EOF后进入校验，不再应用网络空闲超时。/ After exact EOF enter verification, disabling network-idle timeout.
pn_status_t pn_ls_body_done(pn_ls_session_t *,uint64_t);
/// 接收/恢复回执收口；已安装文件不因取消而删除。/ Resolve receive/recovery receipts; cancellation never deletes installed files.
pn_status_t pn_ls_finish(pn_ls_session_t *,const pn_transfer_receive_result_t *,uint64_t);
/// 周期检查超时；活动接收仅请求取消，不释放会话。/ Poll deadlines; active reception requests cancellation without freeing the session.
pn_status_t pn_ls_poll(pn_ls_session_t *,uint64_t);
/// 本机取消。/ Local cancellation.
pn_status_t pn_ls_cancel(pn_ls_session_t *,uint64_t);
/// 网络取消须核对实际来源和会话ID。/ Network cancellation checks actual origin and session ID.
pn_status_t pn_ls_cancel_peer(pn_ls_session_t *,const pn_ls_peer_t *,const char *,uint64_t);
/// 仅RECOVERY返回内部请求；根据snapshot.recovery_install重试安装或取消。/ Return internal request only in RECOVERY; snapshot.recovery_install selects install retry or cancellation.
pn_status_t pn_ls_recovery_request(pn_ls_session_t *,pn_upload_request_t *);
/// 仅在关闭网络入口、join全部调用方并关闭worker后使用；撤销令牌，保留未确认事务提示。/ Only after closing network admission, joining callers and closing the worker; revoke grants and retain unresolved-transaction notice.
pn_status_t pn_ls_stop_after_join(pn_ls_session_t *);
