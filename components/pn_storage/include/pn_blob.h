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
/// 同步修改已完整校验的最新载荷；present=false表示双槽缺失。只在changed=true时写盘，回调不得重入io或保留缓冲。
/// Synchronously edit the fully verified latest payload; present=false means both slots are absent. Write only on changed=true; never reenter io or retain the buffer.
typedef pn_status_t (*pn_blob_update_fn)(void *ctx,bool present,uint8_t *payload,size_t capacity,size_t *size,bool *changed);
/// 单owner独占读改写：一次完整双槽校验，修改后仍同步写另一槽并完整读回；失败可能已提交，须重新load。
/// Exclusive single-owner read/modify/write: verify both slots once, then sync the alternate slot and verify its complete readback; reload after uncertain failure.
pn_status_t pn_blob_update(const pn_journal_io_t *io,pn_pool_t *pool,pn_blob_update_fn update,void *ctx);
/// 小记录复用同一完整A/B协议，仅缩小本次载荷及双读缓冲上限；不截断大记录。/ Small records share the full A/B protocol with a smaller payload/read-buffer bound; never truncate larger records.
pn_status_t pn_blob_load_bounded(const pn_journal_io_t *,pn_pool_t *,uint8_t *,size_t,size_t *);
/// 同上，上限须包含完整载荷，旧入口保持原24KiB行为。/ Same protocol with an explicit complete-payload bound; legacy entry points retain 24 KiB behavior.
pn_status_t pn_blob_save_bounded(const pn_journal_io_t *,pn_pool_t *,const uint8_t *,size_t,size_t);
