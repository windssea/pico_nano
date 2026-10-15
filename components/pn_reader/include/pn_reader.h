/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享TXT阅读会话，区分准备页和实际显示位置。
 * English: shared TXT reading session separating prepared pages from visible positions.
 * 冻结：owner串行调用；仅成功显示提交进度；源和glyph缓冲保持有效。
 * Frozen: serialized owner calls; commit progress only after successful presentation; source and glyph buffers remain valid.
 */
#pragma once
#include "pn_layout.h"
#include "pn_alloc.h"
#include "pn_save_policy.h"
#include "pn_page_index.h"
#define PN_READER_HISTORY 32

typedef enum {
    PN_READ_FIRST=0, ///< 回到书首 / Go to beginning
    PN_READ_NEXT, ///< 下一页 / Next page
    PN_READ_PREVIOUS, ///< 上一页 / Previous page
    PN_READ_CURRENT, ///< 重排当前位置 / Reflow current position
    PN_READ_JUMP ///< 源位置跳转 / Jump to source position
} pn_read_intent_t;
typedef struct {
    uint64_t begin; ///< 页首源位置 / Page start source offset
    uint64_t end; ///< 下页源位置 / Next page source offset
    bool paragraph; ///< 页首是否段首 / Whether page begins a paragraph
    bool next_paragraph; ///< 下页是否段首 / Whether next page begins a paragraph
} pn_reader_anchor_t;
typedef struct pn_reader pn_reader_t;
typedef struct {
    pn_reader_t *owner; ///< 原会话对象 / Original session object
    pn_job_token_t token; ///< 阅读/布局代次 / Reading and layout generation
    uint64_t ticket; ///< 不重复准备号 / Nonrepeating preparation ID
    pn_read_intent_t intent; ///< 原导航意图 / Original navigation intent
    pn_reader_anchor_t anchor; ///< 准备位置 / Prepared position
} pn_reader_receipt_t;
struct pn_reader {
    pn_page_index_t *index; ///< 可选同布局页边界窗口，借用 / Optional borrowed page boundaries for this layout
    const pn_reader_anchor_t *checkpoints; ///< 整源验证所得、布局无关的借用解码检查点 / Borrowed layout-independent decoder checkpoints from full validation
    size_t checkpoint_count; ///< 有界检查点数量，最多512 / Bounded checkpoint count, at most 512
    pn_cache_owner_t cache; ///< 邻页借用保护，绑定后reader地址须稳定 / Neighbor protection; reader address must remain stable after binding
    pn_pool_t *neighbor_pool; ///< 可选邻页预算池，owner串行 / Optional neighbor pool, serialized owner
    pn_text_page_t ahead,behind; ///< 有界下一页/上一页节点 / Bounded next/previous nodes
    size_t ahead_warm; ///< 下一页已预热节点数，缓存替换时清零 / Warmed next-page node count, reset on replacement
    pn_reader_anchor_t ahead_anchor,behind_anchor; ///< 缓存锚点 / Cached anchors
    pn_job_token_t neighbor_token; ///< 缓存布局代次 / Cache layout generation
    pn_text_reader_t decoder; ///< 不变源的解码上下文 / Decoder context for immutable source
    pn_book_id_t book; ///< 内容身份 / Content identity
    pn_layout_t layout; ///< 当前排版 / Current layout
    pn_font_metrics_t metrics; ///< 有效字体度量 / Valid font metrics
    pn_text_page_t page; ///< 准备的glyph节点 / Prepared glyph nodes
    pn_job_token_t token; ///< 当前代次 / Current generation
    uint64_t next_ticket; ///< 下个准备号，0耗尽 / Next preparation ID, 0 exhausted
    pn_reader_receipt_t pending; ///< 等待呈现结果 / Awaiting presentation result
    pn_reader_anchor_t visible; ///< 成功显示页 / Successfully displayed page
    pn_reader_anchor_t history[PN_READER_HISTORY]; ///< 最近上一页锚点 / Recent previous-page anchors
    size_t history_count; ///< 缓存锚点数量 / Cached anchor count
    pn_save_policy_t *save; ///< 可选、寿命稳定的保存策略 / Optional stable save policy
    bool has_visible; ///< 已确认过显示 / Has a confirmed presentation
    bool preparing; ///< 有待确认页 / Has an unconfirmed page
};
/// 字体/source/glyphs/save由调用方拥有；save必须对应同书且已初始化。
/// Caller owns font/source/glyphs/save; save must be initialized for the same book.
pn_status_t pn_reader_init(pn_reader_t *reader,const pn_text_source_t *source,pn_text_encoding_t encoding,
    const pn_book_id_t *book,const pn_layout_t *layout,const pn_font_metrics_t *metrics,
    pn_page_glyph_t *glyphs,size_t capacity,pn_job_token_t token,pn_save_policy_t *save);
/// 准备后返回receipt；pending期间拒绝再次准备；不更改visible或保存状态。
/// Return receipt after preparation; reject another request while pending; preserve visible/save state.
pn_status_t pn_reader_prepare(pn_reader_t *reader,pn_read_intent_t intent,uint64_t offset,pn_reader_receipt_t *receipt);
/// 把任意字节位置对齐到不早于它的字符起点（超过末尾则取最后一个字符），供按比例跳转使用；只解码，不排版。
/// Align an arbitrary byte position to the first character start at or after it (the last character past the end), for proportional jumps; decodes only, never paginates.
pn_status_t pn_reader_align(const pn_reader_t *reader,uint64_t offset,uint64_t *aligned);
/// 显示owner确认同receipt/token成功后提交；失败/过期不保存，重复确认无效。
/// Commit only matching receipt/token after display-owner success; failure/stale never saves, repeat completion is invalid.
pn_status_t pn_reader_complete(pn_reader_t *reader,const pn_reader_receipt_t *receipt,bool success,uint64_t now_ms);
/// 更新layout并推进generation，丢弃pending/历史，保留visible源锚点；随后prepare CURRENT。
/// Update layout and generation, discard pending/history, retain visible source anchor; then prepare CURRENT.
pn_status_t pn_reader_reflow(pn_reader_t *reader,const pn_layout_t *layout);
/// 读取已显示的持久位置；尚未显示返回EMPTY，不返回预绘制位置。
/// Get durable-location input for displayed position; EMPTY before any display, never a speculative position.
pn_status_t pn_reader_progress(const pn_reader_t *reader,pn_txt_progress_t *progress);
/// 绑定可选邻页池，NULL禁用并释放；不扩大全局预算。/ Bind an optional neighbor pool, NULL disables/releases without enlarging global budgets.
void pn_reader_neighbors(pn_reader_t *reader,pn_pool_t *pool);
/// 空闲预排NEXT，不呈现、不写位置；新输入可协作取消。/ Idle NEXT preparation without presentation or position writes; new input can cancel cooperatively.
pn_status_t pn_reader_prefetch(pn_reader_t *reader);
typedef pn_status_t (*pn_reader_next_fn)(void *,const pn_text_page_t *,pn_reader_anchor_t,pn_job_token_t); ///< 只读借用完整邻页，不重入阅读会话 / Read-only borrowing of a complete neighbor, without reader reentry
/// 回调期pin保护，复查源及邻页键，不提交位置；借用只在回调内有效。/ Pin and validate the source/neighbor key for a callback, without committing progress; borrowing lasts only for the callback.
pn_status_t pn_reader_visit_next(pn_reader_t *,pn_reader_next_fn,void *);
/// 仅查询键匹配的完整邻页，不读源；借用仍须visit_next验证。/ Query a complete key-matching neighbor without source IO; borrowing still requires visit_next validation.
bool pn_reader_next_ready(const pn_reader_t *);
typedef pn_status_t (*pn_reader_warm_fn)(void *,const pn_page_glyph_t *); ///< 可选字形预热，不呈现/保存 / Optional glyph warming without presentation or saving
/// 最多推进budget个已验证邻页节点，借用保护；缺字跳过、无内存停本页预热，新输入取消，不改变当前位置。 / Advance up to budget verified neighbor nodes under borrow protection; skip missing glyphs, stop warming this page on OOM, cancel on new input and preserve visible position.
pn_status_t pn_reader_warm_next(pn_reader_t *,pn_reader_warm_fn,void *,size_t budget);
/// 已排好的下一页还有未预热字形。/ The prepared next page still has unwarmed glyphs.
bool pn_reader_warm_pending(const pn_reader_t *);
