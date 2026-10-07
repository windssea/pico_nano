/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：上传分块、持久确认与恢复状态机，文件操作由专有port执行。
 * English: upload chunks, durable acknowledgments and recovery; dedicated ports own file operations.
 * 冻结：单owner独占介质；未同步不能ACK，不执行配对或网络操作。
 * Frozen: single owner with exclusive media; no ACK before sync, no pairing or network operations.
 */
#pragma once
#include "pn_blob.h"
#include "pn_identity.h"
#define PN_UPLOAD_CHUNK 65536u
#define PN_UPLOAD_NAME_MAX 256
#define PN_UPLOAD_ID_BYTES 16
typedef enum {PN_UPLOAD_BOOK=1,PN_UPLOAD_FONT,PN_UPLOAD_COVER,PN_UPLOAD_WALLPAPER} pn_upload_kind_t; ///< 资源目录类别 / Resource directory kind
typedef enum {PN_UPLOAD_RECEIVING=0,PN_UPLOAD_VERIFIED,PN_UPLOAD_COMMITTED,PN_UPLOAD_CANCELLED} pn_upload_phase_t; ///< 持久阶段 / Durable phase
typedef struct {
 uint8_t id[PN_UPLOAD_ID_BYTES]; ///< owner生成的随机非零ID，不是授权 / Owner-generated random nonzero ID, not authorization
 pn_upload_kind_t kind; ///< 资源类别 / Resource kind
 char name[PN_UPLOAD_NAME_MAX]; ///< UTF8单文件名，不能包含路径 / UTF8 basename without paths
 uint64_t size; ///< 精确总长度 / Exact total length
 bool has_digest; ///< 创建时是否已知完整摘要 / Complete digest known at creation
 pn_book_id_t digest; ///< 完整摘要，未知时全零 / Complete digest, all zero if unknown
} pn_upload_request_t;
typedef struct {uint64_t offset,size;pn_upload_phase_t phase;} pn_upload_state_t; ///< 仅已持久进度 / Durable progress only
typedef struct {
 void *ctx; ///< 稳定专有上下文，活动会话期间不可移动 / Stable dedicated context, never moved during a session
 pn_status_t (*bind)(void *,pn_media_t *,const pn_media_lease_t *,const uint8_t *,pn_journal_io_t *); ///< 绑定ID日志，不能擦旧记录 / Bind ID journal without erasing existing records
 pn_status_t (*size)(void *,uint64_t *); ///< 暂存大小，EMPTY仅代表不存在 / Staging size; EMPTY means absent only
 pn_status_t (*truncate_sync)(void *,uint64_t); ///< 创建或截断暂存并同步 / Create or truncate staging and synchronize
 pn_status_t (*write_sync)(void *,uint64_t,const uint8_t *,size_t); ///< 精确顺序写入并同步 / Exact sequential write and synchronize
 pn_identity_read_fn read; ///< 暂存只读流 / Read-only staging stream
 pn_status_t (*validate)(void *,pn_upload_kind_t,const char *); ///< 实际格式校验，不只看扩展名 / Actual format validation beyond extensions
 pn_status_t (*install_sync)(void *,pn_upload_kind_t,const char *,const pn_book_id_t *); ///< 可恢复幂等安装，核对新主文件摘要再成功 / Recoverable idempotent install, verify new target digest before success
 pn_status_t (*remove_sync)(void *); ///< 只移除本ID暂存并同步，不能删已安装资源 / Remove this ID staging only and sync, never installed resources
 pn_status_t (*admit)(void *,const pn_upload_request_t *); ///< 可选：空间/配额和覆盖意图，创建暂存前执行 / Optional space/quota and replace-intent check before staging creation
 void (*unbind)(void *); ///< 可选：释放绑定状态，不删除持久文件 / Optional unbind without deleting durable files
 pn_status_t (*recover_initial)(void *,pn_upload_request_t *); ///< 可选：从已持久意图恢复尚无上传日志的空暂存 / Optional recovery of empty staging with durable intent but no upload journal
} pn_upload_port_t;
typedef struct {void *impl;} pn_upload_t; ///< 活动对象不可复制 / Never copy live objects
/// 校验名称、类别扩展名、上限和ID；不代表资源解析兼容。/ Validate name, kind extension, limits and ID; not parser compatibility.
pn_status_t pn_upload_request_validate(const pn_upload_request_t *);
/// 获取WRITE租约后创建，已有日志或暂存拒覆盖；失败释放资源。/ Acquire WRITE then create; refuse existing journal/staging, release resources on failure.
pn_status_t pn_upload_begin(pn_upload_t *,pn_pool_t *,pn_media_t *,const pn_upload_port_t *,const pn_upload_request_t *);
/// 用同媒体ID新租约恢复，校验上一块并截去未确认尾部。/ Resume with a fresh lease on the same media ID; check last chunk and trim unacknowledged tails.
pn_status_t pn_upload_resume(pn_upload_t *,pn_pool_t *,pn_media_t *,const pn_upload_port_t *,const uint8_t *);
/// 仅最后块可小于64KiB，上一块精确重发幂等；失败不改ACK。输入调用期间保持不变。
/// Only final chunks may be smaller than 64 KiB; exact last-chunk retries are idempotent; errors preserve ACK. Input stays immutable during calls.
pn_status_t pn_upload_chunk(pn_upload_t *,uint64_t,const uint8_t *,size_t,const pn_book_id_t *,uint64_t *);
/// 完整摘要/实际格式通过后记录VERIFIED，再幂等安装和记录COMMITTED。
/// Verify complete digest/actual format, persist VERIFIED, then idempotent install and persist COMMITTED.
pn_status_t pn_upload_complete(pn_upload_t *,const pn_book_id_t *);
/// RECEIVING先保存取消标记再清暂存；VERIFIED不能撤销可能已安装的资源。
/// Persist cancellation before clearing RECEIVING staging; VERIFIED cannot undo possibly installed resources.
pn_status_t pn_upload_cancel(pn_upload_t *);
/// 查询已持久状态，失效介质/不确定日志禁止猜测。/ Query durable state without guessing through stale media/uncertain journals.
pn_status_t pn_upload_state(pn_upload_t *,pn_upload_state_t *);
/// 释放内存/租约，不取消持久会话，便于断网后重开。/ Release memory/lease without cancelling durable sessions, permitting resume after disconnect.
pn_status_t pn_upload_close(pn_upload_t *);

/// 查询当前持久请求身份，未确认/失效记录不输出。/ Query durable request identity without output through uncertain/stale records.
pn_status_t pn_upload_info(pn_upload_t *,pn_upload_request_t *);
