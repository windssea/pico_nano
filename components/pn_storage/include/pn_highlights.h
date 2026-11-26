/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：每书按句高亮摘录存储（独立 A/B 日志目录，支持 TXT 与 EPUB，最多 64 条）。
 * English: per-book sentence highlight excerpt storage (A/B journal directory, TXT & EPUB, up to 64 live items).
 * 冻结：借用内部存储租约；不删墓碑、不复用 ID、不更改阅读进度。
 * Frozen: borrows internal-storage leases; preserves tombstones, never reuses IDs, never alters reading progress.
 */
#pragma once
#include "pn_identity.h"
#include "pn_journal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PN_HIGHLIGHTS_MAX 64 ///< 单书最大有效高亮摘录条数 / Max live highlights per book
#define PN_HIGHLIGHT_PATH_MAX 64 ///< EPUB 章节相对路径上限（含 NUL）/ Max EPUB chapter path length incl. NUL
#define PN_HIGHLIGHT_TEXT_MAX 119 ///< 摘录 UTF-8 文本上限（含 NUL）/ Max UTF-8 excerpt text incl. NUL

typedef struct {
    uint64_t id; ///< 单调递增摘录 ID / Monotonic highlight ID
    pn_book_id_t book; ///< 归属书内容身份 / Owning book identity
    bool deleted; ///< 墓碑标记 / Tombstone flag
    bool is_epub; ///< false=TXT 字节区间，true=EPUB (element<<32)|offset 区间 / false=TXT byte span, true=EPUB (element<<32)|offset
    uint64_t start; ///< 起始锚点（TXT 字节偏移或 EPUB 打包位置）/ Start anchor (TXT byte offset or packed EPUB pos)
    uint64_t end; ///< 结束锚点（不含末偏移，或同 start 表示单点）/ End anchor (exclusive)
    uint32_t start_run; ///< EPUB 起始文本 run 序号（v2 持久化，v1 读出为 0）/ EPUB start text run index (persisted in v2, 0 for legacy v1)
    uint32_t end_run; ///< EPUB 结束文本 run 序号（v2 持久化，v1 读出为 0）/ EPUB end text run index (persisted in v2, 0 for legacy v1)
    uint64_t text_hash; ///< 句子规范化 FNV-1a 哈希 / Normalized sentence FNV-1a hash
    char path[PN_HIGHLIGHT_PATH_MAX]; ///< EPUB 章节路径（TXT 为空）/ EPUB chapter path (empty for TXT)
    char text[PN_HIGHLIGHT_TEXT_MAX]; ///< UTF-8 摘录摘要 / UTF-8 sentence excerpt preview
} pn_highlight_t;

typedef struct {
    pn_media_t *media; ///< 已挂载内部存储 owner / Mounted internal-storage owner
    pn_media_lease_t lease; ///< 借用租约 / Borrowed lease
    pn_book_id_t book; ///< 书内容身份 / Book content identity
    char directory[PN_JOURNAL_PATH_MAX]; ///< 独立摘录目录 / Separate highlight directory
} pn_highlights_t;

/// 打包 EPUB (element, offset) 为 64 位可比较键。/ Pack EPUB (element, offset) into a comparable 64-bit key.
static inline uint64_t pn_highlight_epub_key(uint32_t element,uint32_t offset){
    return ((uint64_t)element<<32)|offset;
}
/// 解包 EPUB 64 位键的 element 部分。/ Unpack EPUB element from a 64-bit key.
static inline uint32_t pn_highlight_epub_element(uint64_t key){return (uint32_t)(key>>32);}
/// 解包 EPUB 64 位键的 offset 部分。/ Unpack EPUB offset from a 64-bit key.
static inline uint32_t pn_highlight_epub_offset(uint64_t key){return (uint32_t)(key&0xffffffffu);}

/// 绑定单书摘录目录（<root>/<sha>.highlights），不创建目录。/ Bind per-book highlights directory without creating it.
pn_status_t pn_highlights_init(pn_highlights_t *hl,pn_media_t *media,const pn_media_lease_t *lease,
    const char *root,const pn_book_id_t *book);
/// 分页列出有效摘录（按 ID 升序）。/ List live highlights in ascending ID order.
pn_status_t pn_highlights_list(pn_highlights_t *hl,uint64_t after,pn_highlight_t *items,
    size_t capacity,size_t *count,bool *more);
/// 加载全部有效摘录供正文渲染匹配（最多 capacity 条）。/ Load all live highlights for fast page rendering.
pn_status_t pn_highlights_load_all(pn_highlights_t *hl,pn_highlight_t *items,size_t capacity,size_t *count);
/// 切换句子高亮：若已存在同句/重叠摘录则删之（*added=false），否则新增（*added=true）。
/// Toggle a sentence highlight: delete if matching/overlapping exists (*added=false), else add (*added=true).
pn_status_t pn_highlights_toggle(pn_highlights_t *hl,const pn_highlight_t *candidate,bool *added,uint64_t *id_out);
/// 按 ID 写入删除墓碑。/ Delete a highlight by writing a tombstone.
pn_status_t pn_highlights_delete(pn_highlights_t *hl,uint64_t id);
/// 读取指定 ID 的有效摘录（不存在或已删返回 EMPTY）。/ Fetch a live highlight by ID (EMPTY if absent/deleted).
pn_status_t pn_highlights_get(pn_highlights_t *hl,uint64_t id,pn_highlight_t *out);
/// 计算 UTF-8 句子的 FNV-1a 64 位哈希（跳过空白）。/ Compute 64-bit FNV-1a hash of a UTF-8 sentence (ignoring spaces).
uint64_t pn_highlight_text_hash(const char *utf8);
/// 扫描状态目录下全部 *.highlights 子目录，汇总最多 capacity 条有效摘录（使用短 READ 租约，不创建目录）。
/// Scan all *.highlights subdirectories under state_dir, collecting up to capacity live highlights under a short READ lease.
pn_status_t pn_highlights_scan_all(pn_media_t *media,const char *state_dir,pn_highlight_t *out,size_t capacity,size_t *count_out);
