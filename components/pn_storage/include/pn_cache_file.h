/* SPDX-License-Identifier: Apache-2.0
 * 中文：可重建缓存的共用回退、目录与分步提交；不处理用户记录。
 * English: shared fallback, directories and staged commits for rebuildable caches, never user records. */
#pragma once
#include "pn_storage.h"

typedef pn_status_t (*pn_cache_file_read_fn)(void *,const char *); ///< 完整验证候选，失败不发布结果 / Fully validate a candidate, publishing nothing on failure
typedef struct {
    pn_media_t *media; ///< 借用稳定介质对象 / Borrowed stable media object
    pn_media_lease_t lease; ///< 借用WRITE/CACHE_WRITE资格，不释放 / Borrowed WRITE/CACHE_WRITE permission, never released here
    const char *target,*temporary,*backup; ///< 调用方持有同目录固定路径直到完成/放弃 / Caller-owned fixed sibling paths until completion/abort
    unsigned phase; ///< 每步至多一次变更操作 / At most one mutation per step
    bool moved,committed,active; ///< 原包移动、提交与活动状态 / Old-packet move, commit and active state
    bool preserve_backup; ///< 调用方已完整验证且正在读取备份时保留它直到提交 / Preserve the caller's fully verified, authoritative backup until publication
} pn_cache_commit_t;

/// 可选缓存失败可转为MISS；取消、失卡及编程错误保留。/ Optional cache failures may become MISS; preserve cancellation, lost media and programming errors.
bool pn_cache_file_miss(pn_status_t);
/// 主包失败后尝试同样完整验证的.bak；不读取.tmp，不修改文件。/ Try an equally validated .bak after a primary miss; never read .tmp or mutate files.
pn_status_t pn_cache_file_load(pn_media_t *,const pn_media_lease_t *,const char *,pn_cache_file_read_fn,void *);
/// 只由可信owner传固定派生目录；拒绝符号链接、点路径及只读/USB资格，不格式化。/ Trusted owners provide fixed derived directories; reject symlinks, dot paths and READ/USB permissions, never format.
pn_status_t pn_cache_file_directory(pn_media_t *,const pn_media_lease_t *,const char *);
/// 暂存已关闭且完整读回验证后开始；三路径必须是target及其.tmp/.bak兄弟。/ Begin after closing and fully read-verifying staging; paths must be target and its .tmp/.bak siblings.
pn_status_t pn_cache_commit_begin(pn_cache_commit_t *,pn_media_t *,const pn_media_lease_t *,const char *,const char *,const char *);
/// BUSY继续，OK表示发布；不承诺FAT掉电原子性。/ BUSY continues, OK means published; no FAT power-loss atomicity promise.
pn_status_t pn_cache_commit_step(pn_cache_commit_t *);
/// 仅当前代有效时恢复旧包/清暂存；旧租约失效后不触碰可能重挂载的路径。/ Restore/clean only in the valid generation; never touch potentially remounted paths after lease expiry.
void pn_cache_commit_abort(pn_cache_commit_t *);
