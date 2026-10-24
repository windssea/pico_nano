/* SPDX-License-Identifier: Apache-2.0
 * 中文：可删除的分页边界窗口，独立于进度及书签。/ English: disposable page-boundary windows, independent of progress and bookmarks. */
#pragma once
#include "pn_font_chain.h"
#include "pn_style.h"
#include "pn_semantic.h"
#define PN_PAGE_INDEX_MAX 512
#define PN_BOOK_INDEX_BATCH_PAGES 256u ///< 连续编译可靠提交批次 / Durable contiguous compilation batch
#define PN_BOOK_INDEX_STEP_MAX 1024u ///< 8ms软预算内的小步硬顶 / Hard step cap within the 8ms soft budget
typedef struct {
    uint32_t section; ///< TXT为0，EPUB为spine序号 / Zero for TXT, spine ordinal for EPUB
    uint64_t begin,end; ///< 字节或本版解析器的派生事件顺序，仅缓存 / Byte offsets or derived parser event order, cache only
    pn_xhtml_position_t first,next; ///< EPUB语义边界，TXT不用 / EPUB semantic boundaries, unused for TXT
    unsigned flags; ///< TXT段首/后继段首或EPUB后继状态 / TXT paragraph flags or EPUB continuation state
} pn_page_index_entry_t;
typedef struct {void *impl;} pn_page_index_t; ///< NULL初始化，串行owner / Initialize NULL, serialized owner
typedef pn_status_t (*pn_page_index_space_fn)(void *,uint64_t *); ///< 查询实际状态卷可用空间 / Query actual free bytes on the state volume
typedef pn_status_t (*pn_page_index_tf_space_fn)(void *,const char *,uint64_t *); ///< 查询实际TF卷空闲容量 / Query actual free bytes on the TF volume
/// 单owner启动时装配容量port；NULL恢复主机statvfs，不进行IO。/ Wire the capacity port at owner startup; NULL restores host statvfs, without I/O.
void pn_page_index_space_port(pn_page_index_space_fn,void *);
/// 启动时设置TF容量port；主机NULL用statvfs，设备NULL禁止可选写。/ Set the TF space port at startup; NULL uses host statvfs or disables optional device writes.
void pn_page_index_tf_space_port(pn_page_index_tf_space_fn,void *);
/// 设备固定TF私有目录，主机PN_READING_CACHE为空时保留内部窗口。/ Fixed device TF-private directory; an empty host PN_READING_CACHE retains the internal window.
const char *pn_page_index_tf_directory(void);
/// 书籍、真实字体摘要、样式及引擎版本组成key；错误不改输出。/ Key includes book, actual font digests, style and engine versions; preserve output on errors.
pn_status_t pn_page_index_key(const pn_book_id_t *,const pn_style_t *,const pn_text_source_t *,const pn_font_chain_t *,unsigned,pn_book_id_t *);
/// 同上，外置主字体已有已验证预期摘要时直接采用，避免冷开首屏前重复整读字体；链上其他源仍按实际摘要。
/// Same as above; use the verified expected primary digest when known, avoiding a duplicate whole-font read before the cold first frame; other chain sources still use actual digests.
pn_status_t pn_page_index_key_known(const pn_book_id_t *,const pn_style_t *,const pn_text_source_t *,const pn_font_chain_t *,unsigned,const pn_book_id_t *,pn_book_id_t *);
/// 缓存目录单独且受128KiB配额约束，文件无效按缺失处理。/ Separate cache folder with a 128 KiB quota; invalid files behave as missing caches.
pn_status_t pn_page_index_open(pn_page_index_t *,pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const char *,const pn_book_id_t *,const pn_book_id_t *);
/// 绑定TF增量页表，借用原READ资格，内部窗口仅作缺失退路；不创建目录，不跳过源验证。/ Attach an incremental TF table using the original READ guard, retaining the internal window as a missing-cache fallback; no mkdir or bypassed source verification.
pn_status_t pn_page_index_use_tf(pn_page_index_t *,pn_media_t *,const pn_media_lease_t *,const char *);
/// 最近不晚于before的边界；strict排除当前位置。/ Nearest boundary at/before before; strict excludes the current position.
bool pn_page_index_near(pn_page_index_t *,uint32_t,uint64_t,bool,pn_page_index_entry_t *);
/// 完整解析后记录边界，不写进度；内存最多512项。/ Record after complete parsing without progress writes; at most 512 entries in memory.
void pn_page_index_add(pn_page_index_t *,const pn_page_index_entry_t *);
/// 内部窗口同步写；TF每次只推进一批，BUSY须后续继续，force只忽略批量阈值，关闭可放弃半成品。/ Internal windows write synchronously; TF advances one batch per call, BUSY needs later calls, force only bypasses batching, and close may abandon staging.
pn_status_t pn_page_index_flush(pn_page_index_t *,bool force);
/// 是否有尚未写出的记录；只读内存状态。/ Whether unwritten records exist; reads in-memory state only.
bool pn_page_index_pending(const pn_page_index_t *);
/// 释放内存；不隐式保存。/ Free memory without implicit saving.
void pn_page_index_close(pn_page_index_t *);
/// 前台工作前归还TF可选写任务，不清dirty或已提交表；内部同步窗口无操作。/ Return optional TF writer resources before foreground work without clearing dirty data or committed tables; synchronous internal windows are unchanged.
void pn_page_index_abort(pn_page_index_t *);
/// 只读后端类型，关闭TF时不启动立即要放弃的新任务。/ Query backend type without I/O so TF close never starts work it will immediately abandon.
bool pn_page_index_is_tf(const pn_page_index_t *);

/// 连续编译的下一页位置，仅派生数据；不等于用户阅读位置。/ Next position in a contiguous build; derived data, never the user's reading position.
typedef struct {
    uint32_t section,pages; ///< 下一spine及已完成页数 / Next spine and completed page count
    uint64_t offset; ///< TXT下一字节 / Next TXT byte
    pn_xhtml_position_t position; ///< EPUB下一语义位置，element零为章首 / Next EPUB position; zero element denotes chapter start
    bool paragraph,complete; ///< TXT段首与整书完成 / TXT paragraph start and whole-book completion
} pn_book_index_state_t;
/// 读取当前布局连续编译游标，无记录输出零；仅TF后端可用。/ Read this layout's contiguous cursor, zero if absent; available only for TF.
pn_status_t pn_page_index_build_state(pn_page_index_t *,pn_book_index_state_t *);
/// 仅旧游标仍匹配时推进，窗口淘汰导致回退则STALE_JOB；不写用户记录。/ Advance only if the previous cursor still matches; STALE_JOB after eviction rewind, never writing user records.
pn_status_t pn_page_index_build_advance(pn_page_index_t *,const pn_book_index_state_t *,const pn_book_index_state_t *);
/// 当前游标已随完整表发布，非仅RAM中的完成标志。/ Current cursor has been published with a complete verified table, beyond an in-RAM completion flag.
bool pn_page_index_build_saved(pn_page_index_t *);
