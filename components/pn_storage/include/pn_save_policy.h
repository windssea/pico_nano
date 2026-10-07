/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读位置自动保存控制，接收已显示的位置。
 * English: automatic progress saving from positions already displayed.
 * 冻结：owner串行调用；失败保留dirty；同步I/O不承诺deadline。
 * Frozen: serialized owner calls; failures retain dirty state; synchronous I/O has no deadline guarantee.
 */
#pragma once
#include "pn_progress.h"
typedef struct {
    pn_journal_io_t io; ///< 稳定的适配引用 / Stable adapter references
    pn_book_id_t expected; ///< 当前书籍身份 / Current book identity
    pn_txt_progress_t current; ///< 最新已显示位置 / Latest displayed position
    uint64_t dirty_since_ms; ///< 首次未保存变化时间 / First unsaved change time
    uint64_t last_now_ms; ///< 单调时钟检查 / Monotonic clock check
    uint64_t last_failed_ms; ///< 上次保存失败时间 / Last failed save time
    bool retry_pending; ///< 自动重试等待，flush可绕过 / Automatic retry delay, bypassed by flush
    unsigned turns; ///< 未保存的翻页次数 / Unsaved page turns
    bool dirty; ///< 待保存，失败时保留 / Pending save, retained on failure
    bool has_location; ///< 已有有效位置 / Has a valid position
} pn_save_policy_t;
/// baseline可空；有值时代表已恢复的持久位置，不能伪装未保存状态。
/// Baseline may be null; otherwise it is a restored durable position, never an unsaved state.
pn_status_t pn_save_policy_init(pn_save_policy_t *policy, const pn_journal_io_t *io,
    const pn_book_id_t *expected, const pn_txt_progress_t *baseline, uint64_t now_ms);
/// 仅传入显示成功的当前位置；预绘制、失败和旧任务完成不得调用。
/// Submit successfully displayed positions only; never speculative, failed or stale completions.
pn_status_t pn_save_policy_presented(pn_save_policy_t *policy, const pn_txt_progress_t *progress,
    bool page_turn, uint64_t now_ms);
/// dirty达到5次翻页或30秒即保存；失败不重置计数和起点。
/// Save dirty progress at five turns or thirty seconds; failures retain count and start time.
/// 失败后自动重试至少间隔1秒，等待返回BUSY；flush可立即重试。
/// Automatic retries wait at least one second and return BUSY while waiting; flush retries immediately.
pn_status_t pn_save_policy_tick(pn_save_policy_t *policy, uint64_t now_ms);
/// 离书/锁屏时立即尝试保存；失败必须阻止上层继续切电。
/// Attempt immediate save on exit/lock; failure must block higher-level power-off.
pn_status_t pn_save_policy_flush(pn_save_policy_t *policy, uint64_t now_ms);
