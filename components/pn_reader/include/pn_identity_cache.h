/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书籍身份预计算缓存：书库空闲时为尚未读过的书按32KiB分步计算整书SHA，按“路径+大小+修改时间”记住；
 *       点开时据此走既有可信快开，开书后仍在后台完整复核，不符则按完整路径重开。缓存可整份丢弃重建。
 * English: precomputed book-identity cache: while the library is idle, compute the whole-book SHA of not-yet-read books in 32 KiB steps
 *       and remember it by path + size + modification time; opening such a book then takes the existing trusted fast path, still verified
 *       in full in the background and reopened completely on mismatch. The cache can be discarded and rebuilt at any time.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pn_identity.h"
#include "pn_storage.h"

#define PN_IDENTITY_CACHE_MAX 256 ///< 最多记住的书；大书库避免预计算结果被过早淘汰 / Maximum remembered books; large libraries must not evict precomputed results too early

/// 一条记录：路径哈希、大小、修改时间与整书身份。/ One record: path hash, size, modification time and whole-book identity.
typedef struct {uint64_t key;uint64_t size;int64_t mtime;pn_book_id_t id;} pn_identity_cache_entry_t;
/// 内存表，最近写入在前；dirty表示需要保存。/ In-memory table, most recent first; dirty means a save is due.
typedef struct {pn_identity_cache_entry_t entries[PN_IDENTITY_CACHE_MAX];unsigned count;bool dirty;} pn_identity_cache_t;

/// 读入缓存文件；缺失返回EMPTY，损坏/版本不符返回CORRUPT，两者都留下空表。/ Load the cache file; missing returns EMPTY and damaged/foreign versions return CORRUPT, both leaving an empty table.
pn_status_t pn_identity_cache_load(pn_identity_cache_t *cache,const char *file);
/// 先写临时文件再替换；成功清dirty。调用方持有该目录的写租约。/ Write a temporary file then replace; success clears dirty. The caller holds a write lease on the directory.
pn_status_t pn_identity_cache_save(pn_identity_cache_t *cache,const char *file);
/// 路径、大小、修改时间都一致才命中。/ Hit only when path, size and modification time all match.
bool pn_identity_cache_find(const pn_identity_cache_t *cache,const char *path,uint64_t size,int64_t mtime,pn_book_id_t *id);
/// 记入或更新一条，满时淘汰最旧。/ Record or update one entry, evicting the oldest when full.
void pn_identity_cache_put(pn_identity_cache_t *cache,const char *path,uint64_t size,int64_t mtime,const pn_book_id_t *id);
/// 删除该路径的记录（复核不符时）。/ Drop the record for a path (after a verification mismatch).
void pn_identity_cache_forget(pn_identity_cache_t *cache,const char *path);

/// 后台计算任务：自持READ租约与文件句柄，close前不可复制。/ Background computation job: owns a READ lease and file handle; never copy a live job.
typedef struct {void *impl;} pn_identity_cache_job_t;
/// 打开书并开始计算，记下开始时的大小与修改时间。/ Open the book and start computing, noting its size and modification time at the start.
pn_status_t pn_identity_cache_job_begin(pn_identity_cache_job_t *job,pn_pool_t *pool,pn_media_t *media,const char *path);
/// 推进至多32KiB；硬件小块软预算可提前让出。BUSY未完，OK时已写入cache。/ Advance at most 32 KiB; the hardware small-buffer soft budget may yield earlier. BUSY means unfinished; OK means the entry is in the cache.
pn_status_t pn_identity_cache_job_step(pn_identity_cache_job_t *job,pn_identity_cache_t *cache);
/// 当前任务路径；无任务或关闭后为NULL。/ Current job path; NULL without a job or after close.
const char *pn_identity_cache_job_path(const pn_identity_cache_job_t *job);
/// 释放句柄与租约，幂等。/ Release the handle and lease, idempotently.
void pn_identity_cache_job_close(pn_identity_cache_job_t *job);
/// 文件修改时间（stat），失败返回false。/ File modification time via stat; false on failure.
bool pn_identity_cache_stat(const char *path,uint64_t *size,int64_t *mtime);

/// 最近两书的调度去重，不是来源证明，也不持久化。/ Scheduling deduplication for two recent books, never source proof or persistent state.
typedef struct {
    const pn_media_t *media; ///< 当前介质对象 / Current media object
    uint64_t instance,epoch,writes,activity,paths[2]; ///< 失效代次与本输入周期候选 / Invalidation generations and candidates in this input cycle
    unsigned count; ///< 本周期最多两条 / At most two per cycle
} pn_identity_warm_t;
/// 已有弱身份记录也可完整预校验；同代强证明命中或本周期已尝试返回EMPTY。/ Fully preverify even books with weak metadata; return EMPTY for a strong same-generation proof or an attempted candidate this cycle.
pn_status_t pn_identity_cache_warm_begin(pn_identity_warm_t *warm,pn_identity_cache_job_t *job,pn_pool_t *pool,pn_media_t *media,const char *path,uint64_t activity);
