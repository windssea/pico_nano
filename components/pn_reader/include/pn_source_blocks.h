/* SPDX-License-Identifier: Apache-2.0
 * 中文：已导入书籍版本的逐块完整校验，不等同于本次整书复核。
 * English: full per-block verification of an imported book version, never a fresh whole-file proof. */
#pragma once
#include "pn_text_file.h"

typedef struct {void *impl;} pn_source_blocks_t; ///< 单owner，源保活至close / Single owner; source outlives close
/// 仅调度提示：可信根/表候选存在即可优先尝试快开，不能据此消费内容或授予身份。/ Scheduling hint only: root/table candidates favor trying fast open, never authorize content consumption or identity.
bool pn_source_blocks_hint(const char *tf,const char *state,const pn_book_id_t *);
/// 完整校验可信摘要表后包装源；缺失/坏包返回EMPTY，不授予整书身份备忘。/ Authenticate the complete digest table and wrap the source; missing/bad packets return EMPTY, never whole-file memo authority.
pn_status_t pn_source_blocks_open(pn_source_blocks_t *,pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const pn_text_source_t *,const pn_book_id_t *,const char *,const char *,pn_text_source_t *);
/// 可选捕获器只借原整书扫描的字节，不额外读取；失败不阻止正常开书。/ Optional capture consumes the existing full scan without extra reads; failed admission never blocks normal opening.
pn_status_t pn_source_blocks_capture(pn_source_blocks_t *,pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const pn_text_source_t *,pn_text_source_t *);
/// 只接收本次完整摘要连续成功读取的字节，取消/失败不得finish。/ Feed only consecutive successful reads from the full digest; never finish a cancelled/failed digest.
void pn_source_blocks_feed(pn_source_blocks_t *,uint64_t,const uint8_t *,size_t);
/// 整书SHA成功后才冻结摘要表；不写盘，完成前不得作为来源证明。/ Freeze the table only after successful full SHA; no writes and no authority before completion.
void pn_source_blocks_finish(pn_source_blocks_t *,const pn_book_id_t *);
bool pn_source_blocks_ready(const pn_source_blocks_t *); ///< 可信摘要表可用 / Authenticated table available
bool pn_source_blocks_partial(const pn_source_blocks_t *); ///< 本会话未整书扫描 / This session did not scan the whole source
/// 校验覆盖区间的每一个完整块；未读区域允许稍后发现变化，已失败会话不可再用。/ Verify every complete block covering the range; unread changes are detected later and failed sessions stay unusable.
pn_status_t pn_source_blocks_check(pn_source_blocks_t *,uint64_t,uint64_t);
/// 复用原事务/配额分步落盘；调用方持有内部WRITE资格直到结束。/ Persist in steps with existing transactions/quotas; caller retains the state WRITE lease until completion.
pn_status_t pn_source_blocks_store(pn_source_blocks_t *,pn_media_t *,const pn_media_lease_t *,const char *,const char *);
bool pn_source_blocks_dirty(const pn_source_blocks_t *); ///< 摘要表尚未可靠提交 / Digest table not yet committed
bool pn_source_blocks_pending(const pn_source_blocks_t *); ///< 已开始事务须优先收口 / An active transaction must be allowed to finish
/// 取消写入保留RAM表；旧代不触碰文件。/ Cancel writes but keep the RAM table; never touch paths in a stale epoch.
void pn_source_blocks_abort(pn_source_blocks_t *);
void pn_source_blocks_close(pn_source_blocks_t *); ///< 释放事务、摘要表及全部借用 / Release transaction, table and borrows
