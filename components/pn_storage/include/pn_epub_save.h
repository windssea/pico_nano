/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：确认显示的EPUB位置保存策略，保留失败dirty与关闭屏障。
 * English: saving confirmed EPUB locations, retaining dirty state and close barriers on failure.
 * 冻结：不接收准备页，不格式化。/ Frozen: no speculative pages or formatting.
 */
#pragma once
#include "pn_epub_progress.h"
typedef struct {
    pn_journal_io_t io; ///< 稳定适配 / Stable adapter
    pn_pool_t *pool; ///< 借用快照预算 / Borrowed snapshot budget
    pn_book_id_t expected; ///< 当前内容身份 / Current content identity
    pn_epub_progress_t current; ///< 最新已显示位置 / Latest displayed position
    uint64_t dirty_since,last_now,last_failed; ///< 时钟与重试 / Clock and retry times
    unsigned turns; ///< 未保存真实翻页数，最多5 / Unsaved real turns capped at five
    bool has_location,dirty,retry_pending; ///< 已显示、待存、待重试 / Displayed, unsaved, awaiting retry
} pn_epub_save_t;
/// baseline只能是已加载持久记录；可NULL。/ Baseline must be loaded durable data, or NULL.
pn_status_t pn_epub_save_init(pn_epub_save_t *,pn_pool_t *,const pn_journal_io_t *,const pn_book_id_t *,const pn_epub_progress_t *,uint64_t);
/// 只接确认位置，不接未显示草稿。/ Accept confirmed locations only, never speculative drafts.
pn_status_t pn_epub_save_presented(pn_epub_save_t *,const pn_epub_progress_t *,bool,uint64_t);
/// 5次真实翻页或30秒保存；失败1秒后重试。/ Save at five real turns or thirty seconds; retry failures after one second.
pn_status_t pn_epub_save_tick(pn_epub_save_t *,uint64_t);
/// 离书/锁屏立即保存；失败必须保留会话并阻止切电。/ Flush on exit/lock; failures must retain session and block power-off.
pn_status_t pn_epub_save_flush(pn_epub_save_t *,uint64_t);
/// 用作pn_epub_reader确认回调，消费确认位置再检查阈值。/ Use as pn_epub_reader callback, consuming confirmed locations then checking thresholds.
pn_status_t pn_epub_save_confirm(void *,const pn_epub_progress_t *,bool,uint64_t);
