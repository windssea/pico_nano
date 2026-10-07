/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB原生语义位置A/B快照，不接触正文或显示。
 * English: A/B snapshots of native EPUB semantic positions without body/display access.
 * 冻结：只保存已确认位置，不以临时页号替代；未知载荷不擦除。
 * Frozen: save confirmed locations only, never temporary page numbers; never erase unknown payloads.
 */
#pragma once
#include "pn_semantic.h"
#include "pn_identity.h"
#include "pn_blob.h"
typedef struct {
    pn_book_id_t book; ///< 外层owner验证的内容SHA / Content SHA verified by the outer owner
    pn_epub_location_t location; ///< 仅已确认显示位置 / Confirmed displayed position only
} pn_epub_progress_t;
/// 校验持久位置的版本、UTF8规范路径及位置字段；不能证明目标仍在原文中。
/// Validate durable versions, canonical UTF-8 paths and fields; it does not prove the target still exists in text.
pn_status_t pn_epub_progress_validate(const pn_epub_progress_t *);
/// 保存最大1091byte载荷到PNBL A/B，不自动格式化。/ Save up to 1091 bytes in PNBL A/B without automatic formatting.
pn_status_t pn_epub_progress_save(const pn_journal_io_t *,pn_pool_t *,const pn_epub_progress_t *);
/// 错误保持输出，EMPTY仅表示双槽不存在；SHA不符STALE_JOB。
/// Preserve outputs on errors; EMPTY only when both slots are absent; SHA mismatch is STALE_JOB.
pn_status_t pn_epub_progress_load(const pn_journal_io_t *,pn_pool_t *,const pn_book_id_t *,pn_epub_progress_t *);
