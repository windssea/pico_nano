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
#include "pn_save_policy.h"
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
