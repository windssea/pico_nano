/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：有限缓冲显示任务的公共契约，不执行扫描或波形操作。
 * English: bounded display-job contracts without scanning or waveform operations.
 *
 * 冻结：服务owner串行仲裁；扫描缓冲在完成前不得修改或复用。
 * Frozen: service-owner arbitration; never modify or reuse scanning buffers before completion.
 */
#pragma once
#include "pn_types.h"
#include "pn_frame.h"
#define PN_DISPLAY_MAX_BUFFERS 3

typedef enum {
    PN_REFRESH_GC16=0, ///< 清残影整刷 / Ghost-clearing full update
    PN_REFRESH_GL16, ///< 清晰页刷新 / Clear page update
    PN_REFRESH_DU ///< 快速二值刷新 / Fast binary update
} pn_refresh_t;
typedef enum {
    PN_BUFFER_FREE=0, ///< 空闲 / Free
    PN_BUFFER_DRAWING, ///< 绘制者独占 / Exclusive drawing
    PN_BUFFER_READY, ///< 不可变排队 / Immutable queued frame
    PN_BUFFER_SCANNING ///< 不可变扫描 / Immutable scanning frame
} pn_buffer_state_t;

typedef struct pn_display pn_display_t;
typedef struct {
    pn_display_t *owner; ///< 所属调度器 / Owning scheduler
    uint64_t ticket; ///< 不重复任务号 / Nonrepeating job ID
    size_t slot; ///< 缓冲槽号 / Buffer slot
    pn_job_token_t token; ///< 会话与代次 / Session and generation
} pn_draw_lease_t;
typedef struct {
    pn_draw_lease_t lease; ///< 扫描所有权句柄 / Scan ownership handle
    const pn_frame_t *frame; ///< 完成前有效，只读 / Valid until completion, read-only
    uint64_t page_id; ///< 调用者页面标识 / Caller page identity
    pn_refresh_t profile; ///< 请求刷新档位 / Requested refresh profile
} pn_display_job_t;
typedef struct {
    pn_frame_t frame; ///< 初始化后几何和存储不变 / Geometry and storage fixed after initialization
    pn_buffer_state_t state; ///< 当前所有权状态 / Current ownership state
    pn_draw_lease_t lease; ///< 本次缓冲租约 / Current buffer lease
    uint64_t page_id; ///< 准备好的页面标识 / Prepared page ID
    pn_refresh_t profile; ///< 准备好的刷新档 / Prepared refresh profile
} pn_display_slot_t;
struct pn_display {
    pn_job_token_t current; ///< 当前有效代次 / Current valid generation
    uint64_t next_ticket; ///< 下一任务号，零表示耗尽 / Next job ID, zero means exhausted
    size_t count; ///< 绑定缓冲数量 / Bound buffer count
    int queued; ///< 排队槽，负一为无 / Queued slot, minus one means none
    int active; ///< 扫描槽，负一为无 / Scanning slot, minus one means none
    pn_display_slot_t slots[PN_DISPLAY_MAX_BUFFERS]; ///< 固定缓冲状态 / Fixed buffer state
};

/// 绑定1至3个等几何且存储不重叠的帧；像素和对象须保持到全部任务结束。
/// Bind one to three equally sized frames with nonoverlapping storage, alive until all jobs finish.
pn_status_t pn_display_init(pn_display_t *display, const pn_frame_t *frames, size_t count, pn_job_token_t token);
/// 代次只前进；丢弃旧排队帧，正在绘制和扫描的旧帧仍由其owner释放。
/// Advance generations only; drop old queued frames while old drawing/scanning owners retain cleanup duties.
pn_status_t pn_display_set_token(pn_display_t *display, pn_job_token_t token);
/// 取得空闲帧的独占绘制租约；输出像素只能在租约有效且未publish时写入。
/// Acquire an exclusive free-frame draw lease; write pixels only while valid and unpublished.
pn_status_t pn_display_begin_draw(pn_display_t *display, pn_job_token_t token, pn_draw_lease_t *lease, pn_frame_t **frame);
/// 检查绘制租约并返回可写帧；过期代次须停止写入并discard。
/// Validate a draw lease and return a writable frame; expired generations must stop writing and discard.
pn_status_t pn_display_draw_frame(pn_display_t *display, const pn_draw_lease_t *lease, pn_frame_t **frame);
/// 发布后像素不可改；替换尚未开始的旧排队帧，最多一帧排队。
/// Pixels become immutable on publish; replace an unstarted queued frame, keeping at most one queued frame.
pn_status_t pn_display_publish(pn_display_t *display, const pn_draw_lease_t *lease, uint64_t page_id, pn_refresh_t profile);
/// 放弃绘制租约；不允许撤销正在扫描的帧。
/// Abandon a draw lease; never revoke a scanning frame.
pn_status_t pn_display_discard(pn_display_t *display, const pn_draw_lease_t *lease);
/// 唯一显示owner取得排队帧；已有扫描时返回BUSY。
/// The sole display owner takes a queued frame; return BUSY if a scan is already active.
pn_status_t pn_display_start(pn_display_t *display, pn_display_job_t *job);
/// 扫描安全结束后释放；过期完成返回STALE_JOB，不能更新新会话。
/// Release after a safe scan end; expired completion returns STALE_JOB and must not update the new session.
pn_status_t pn_display_complete(pn_display_t *display, const pn_display_job_t *job, bool success);
