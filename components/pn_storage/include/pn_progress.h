/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：将TXT源位置、内容身份和解析策略写入有版本的载荷。
 * English: versioned payload for TXT source position, content identity and parsing policy.
 * 冻结：不存永久页码；字符边界由解析器保证，不跨身份恢复。
 * Frozen: no permanent page numbers; parser ensures character boundaries; never restore across identities.
 */
#pragma once
#include "pn_journal.h"
#include "pn_identity.h"
typedef enum {
    PN_TEXT_UTF8=1, ///< UTF-8编码 / UTF-8 encoding
    PN_TEXT_UTF16_LE, ///< UTF-16小端 / UTF-16 little endian
    PN_TEXT_UTF16_BE, ///< UTF-16大端 / UTF-16 big endian
    PN_TEXT_GBK ///< GBK编码 / GBK encoding
} pn_text_encoding_t;
typedef struct {
    pn_book_id_t book; ///< 对应原文件摘要 / Original-file digest
    uint64_t source_size; ///< 原文件字节数 / Original-file byte size
    uint64_t source_offset; ///< 原文件字节位置，不是页码 / Original byte position, not page number
    pn_text_encoding_t encoding; ///< 正文编码 / Text encoding
    uint32_t paragraph_version; ///< 非零段落策略版本 / Nonzero paragraph-policy version
} pn_txt_progress_t;
/// 保存有版本的TXT位置；实际字符边界由解析器保证。
/// Save versioned TXT position; parser ensures actual character boundaries.
pn_status_t pn_txt_progress_save(const pn_journal_io_t *io, const pn_txt_progress_t *progress);
/// 仅返回对应内容身份的位置；错误不改输出，其他书籍返回STALE_JOB。
/// Return positions only for the expected content identity; errors preserve output, other books return STALE_JOB.
pn_status_t pn_txt_progress_load(const pn_journal_io_t *io, const pn_book_id_t *expected, pn_txt_progress_t *progress);
