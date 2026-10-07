/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：严格流式TXT解码，保留每个字符的原始字节范围。
 * English: strict streamed TXT decoding preserving original byte ranges per character.
 * 冻结：不静默替换、不整书分配；source不可并发变更，owner串行调用。
 * Frozen: no silent replacement or whole-book allocation; immutable source and serialized owner calls.
 */
#pragma once
#include "pn_progress.h"
#define PN_TEXT_CACHE_BYTES 256
#define PN_TEXT_AUTO ((pn_text_encoding_t)0)
typedef struct {
    void *ctx; ///< 源适配上下文 / Source adapter context
    uint64_t size; ///< 不变的原始字节数 / Immutable original byte size
    pn_status_t (*read_at)(void *,uint64_t,uint8_t *,size_t,size_t *); ///< 有界读取，短读可继续 / Bounded read; short reads may continue
    pn_status_t (*validate)(void *); ///< 可选：每步校验缓存源仍有效 / Optional: validate cached source on each step
} pn_text_source_t;
typedef struct {
    uint32_t codepoint; ///< Unicode标量，CR/CRLF归一成LF / Unicode scalar with CR/CRLF normalized to LF
    uint64_t begin; ///< 原文件字符起点 / Original character start
    uint64_t end; ///< 原文件字符末尾之后 / One past original character end
} pn_text_char_t;
typedef struct {
    pn_text_source_t source; ///< 不变源 / Immutable source
    pn_text_encoding_t encoding; ///< 已解析实际编码 / Resolved encoding
    uint64_t content_begin; ///< 跳过匹配BOM后的起点 / Start after matching BOM
    uint64_t cursor; ///< 下个字符源偏移 / Next character source offset
    uint64_t cache_begin; ///< 缓存起点 / Cache start
    size_t cache_size; ///< 当前缓存长度 / Cached byte count
    uint8_t cache[PN_TEXT_CACHE_BYTES]; ///< 有界缓存 / Bounded cache
} pn_text_reader_t;
/// AUTO识别BOM，无BOM先选UTF8；整源探测/GBK选择由独立probe或用户完成。
/// AUTO recognizes BOM, otherwise chooses UTF8; full-source probing/GBK choice belongs to a separate probe or the user.
pn_status_t pn_text_open(pn_text_reader_t *reader,const pn_text_source_t *source,pn_text_encoding_t encoding);
/// EOF返回EMPTY；损坏或I/O错误不推进cursor、不改输出。
/// EOF returns EMPTY; corruption or I/O errors preserve cursor and output.
pn_status_t pn_text_next(pn_text_reader_t *reader,pn_text_char_t *character);
/// 仅接受字符边界和EOF，拒绝UTF8续字节、代理对尾部及CRLF内部。
/// Accept character boundaries and EOF only; reject UTF8 continuation bytes, low surrogates and CRLF interiors.
pn_status_t pn_text_seek(pn_text_reader_t *reader,uint64_t offset);

/// 在预算内完整验证源：BOM优先，否则严格UTF8再GBK；超预算不猜编码。
/// Validate the complete source within budget: BOM first, otherwise strict UTF8 then GBK; no guess over budget.
pn_status_t pn_text_probe(const pn_text_source_t *source,uint64_t byte_limit,pn_text_encoding_t *encoding);
