/* SPDX-License-Identifier: Apache-2.0
 * 中文：单owner小步预绘制的可回收阅读帧，不提交显示或位置。
 * English: reclaimable reader frames painted in small single-owner steps, without display/location commits. */
#pragma once
#include "pn_alloc.h"
#include "pn_frame.h"
#include "pn_types.h"
typedef struct {
    size_t bytes,hits,steps,completed,denied; ///< 当前计费与命中/步骤/完成/拒绝次数 / Current charge and hit/step/completion/denial counts
} pn_page_bitmap_stats_t;
typedef struct {
    pn_cache_owner_t owner; ///< 独立像素类别，活动对象地址须稳定 / Independent pixel class; the active object address must remain stable
    pn_frame_t frame; ///< 独立4bpp帧，不借用正在扫描的帧 / Independent 4bpp frame, never borrowing a scanned frame
    size_t cursor,total; ///< 已绘制/总节点数，失败清零 / Painted/total nodes, reset on failure
    unsigned missing; ///< 与前台相同的缺字计数 / Missing-glyph count matching foreground painting
    bool enabled,working,ready; ///< 可选开关、准备中、完整有效 / Optional enable flag, in progress, fully valid
    pn_page_bitmap_stats_t stats; ///< 同源诊断，不保存到持久状态 / Shared diagnostics, never persisted
} pn_page_bitmap_t;
typedef pn_status_t (*pn_page_bitmap_paint_fn)(void *,pn_frame_t *,size_t,size_t,unsigned *); ///< 绘制指定节点段，只修改帧/缺字数 / Paint the requested node range, modifying only pixels/missing count
/// 注册可选帧到像素分项及既有总缓存预算，初始不分配。/ Register an optional frame under pixel and existing aggregate cache caps without initial allocation.
void pn_page_bitmap_init(pn_page_bitmap_t *,pn_pool_t *);
/// 禁用释放帧；再次启用按需准备，不改阅读/显示状态。/ Disabling releases the frame; enabling prepares lazily without changing reader/display state.
void pn_page_bitmap_enable(pn_page_bitmap_t *,bool);
/// 保留内存但取消部分/完整结果；外层另外维护语义键。/ Retain storage but invalidate partial/complete results; callers own semantic keys separately.
void pn_page_bitmap_invalidate(pn_page_bitmap_t *);
/// 新目标清空独立帧，准入/分配失败返回EMPTY，保留原前台路径。/ Clear an independent frame for a new target; admission/allocation failures return EMPTY, preserving foreground fallback.
pn_status_t pn_page_bitmap_begin(pn_page_bitmap_t *,size_t);
/// 最多budget节点；绘制前/完整小步后取消保留游标，回调失败废弃未知像素；不等待其他任务。
/// Paint at most budget nodes; preserve progress on cancellation before/after a complete step, discard uncertain callback failures, and never wait for other tasks.
pn_status_t pn_page_bitmap_step(pn_page_bitmap_t *,pn_page_bitmap_paint_fn,void *,size_t);
/// 完整结果才复制，外层先验证语义键和源；不修改进度。/ Copy only complete results after callers validate semantic keys/sources, without changing progress.
pn_status_t pn_page_bitmap_copy(pn_page_bitmap_t *,pn_frame_t *,unsigned *);
/// 注销后释放，幂等；不调用硬件。/ Detach then release idempotently, without hardware calls.
void pn_page_bitmap_close(pn_page_bitmap_t *);
/// 只读统计，bytes包含实际分配头。/ Read-only stats with bytes including allocation headers.
pn_page_bitmap_stats_t pn_page_bitmap_stats(const pn_page_bitmap_t *);
