/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界较大A/B快照，复用租约文件port，不改变PNJR。
 * English: bounded larger A/B snapshots sharing the leased file port without changing PNJR.
 * 冻结：未知版本/IO拒绝猜测；不格式化或自动清除。
 * Frozen: never guess through unknown versions or I/O; no formatting or automatic clearing.
 */
#pragma once
#include "pn_alloc.h"
#include "pn_journal.h"
#define PN_BLOB_MAX 24576
/// 错误保持输出；EMPTY只代表双槽均不存在。
/// Preserve outputs on errors; EMPTY means both slots are absent.
pn_status_t pn_blob_load(const pn_journal_io_t *io,pn_pool_t *pool,uint8_t *payload,size_t capacity,size_t *size);
/// 写另一槽并同步读回，失败可能已经提交，随后应重新load。
/// Write alternate slot with synchronized readback; failures may already commit, so reload afterwards.
pn_status_t pn_blob_save(const pn_journal_io_t *io,pn_pool_t *pool,const uint8_t *payload,size_t size);
