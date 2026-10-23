/* SPDX-License-Identifier: Apache-2.0
 * 中文：可删除的分页边界窗口，独立于进度及书签。/ English: disposable page-boundary windows, independent of progress and bookmarks. */
#pragma once
#include "pn_font_chain.h"
#include "pn_style.h"
#include "pn_semantic.h"
#define PN_PAGE_INDEX_MAX 512
typedef struct {
    uint32_t section; ///< TXT为0，EPUB为spine序号 / Zero for TXT, spine ordinal for EPUB
    uint64_t begin,end; ///< 字节或本版解析器的派生事件顺序，仅缓存 / Byte offsets or derived parser event order, cache only
    pn_xhtml_position_t first,next; ///< EPUB语义边界，TXT不用 / EPUB semantic boundaries, unused for TXT
    unsigned flags; ///< TXT段首/后继段首或EPUB后继状态 / TXT paragraph flags or EPUB continuation state
} pn_page_index_entry_t;
typedef struct {void *impl;} pn_page_index_t; ///< NULL初始化，串行owner / Initialize NULL, serialized owner
typedef pn_status_t (*pn_page_index_space_fn)(void *,uint64_t *); ///< 查询实际状态卷可用空间 / Query actual free bytes on the state volume
/// 单owner启动时装配容量port；NULL恢复主机statvfs，不进行IO。/ Wire the capacity port at owner startup; NULL restores host statvfs, without I/O.
void pn_page_index_space_port(pn_page_index_space_fn,void *);
/// 书籍、真实字体摘要、样式及引擎版本组成key；错误不改输出。/ Key includes book, actual font digests, style and engine versions; preserve output on errors.
pn_status_t pn_page_index_key(const pn_book_id_t *,const pn_style_t *,const pn_text_source_t *,const pn_font_chain_t *,unsigned,pn_book_id_t *);
/// 同上，外置主字体已有已验证预期摘要时直接采用，避免冷开首屏前重复整读字体；链上其他源仍按实际摘要。
/// Same as above; use the verified expected primary digest when known, avoiding a duplicate whole-font read before the cold first frame; other chain sources still use actual digests.
pn_status_t pn_page_index_key_known(const pn_book_id_t *,const pn_style_t *,const pn_text_source_t *,const pn_font_chain_t *,unsigned,const pn_book_id_t *,pn_book_id_t *);
/// 缓存目录单独且受128KiB配额约束，文件无效按缺失处理。/ Separate cache folder with a 128 KiB quota; invalid files behave as missing caches.
pn_status_t pn_page_index_open(pn_page_index_t *,pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const char *,const pn_book_id_t *,const pn_book_id_t *);
/// 最近不晚于before的边界；strict排除当前位置。/ Nearest boundary at/before before; strict excludes the current position.
bool pn_page_index_near(pn_page_index_t *,uint32_t,uint64_t,bool,pn_page_index_entry_t *);
/// 完整解析后记录边界，不写进度；内存最多512项。/ Record after complete parsing without progress writes; at most 512 entries in memory.
void pn_page_index_add(pn_page_index_t *,const pn_page_index_entry_t *);
/// 分批同步；force用于关闭，可选写失败本会话退避，错误仅影响缓存。/ Batch synchronization; force on close, with session backoff after optional write failures affecting caches only.
pn_status_t pn_page_index_flush(pn_page_index_t *,bool force);
/// 是否有尚未写出的记录；只读内存状态。/ Whether unwritten records exist; reads in-memory state only.
bool pn_page_index_pending(const pn_page_index_t *);
/// 释放内存；不隐式保存。/ Free memory without implicit saving.
void pn_page_index_close(pn_page_index_t *);
