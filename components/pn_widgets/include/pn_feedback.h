/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界、可撤销的按下像素反馈；控制层提供快照和显示回执。
 * English: bounded reversible press pixels; controllers supply snapshots and display receipts. */
#pragma once
#include "pn_widgets.h"
#define PN_FEEDBACK_MAX_PIXELS 65536u ///< 每块最多32KiB，原/目标合计64KiB / At most 32 KiB per patch, 64 KiB for original/target
typedef struct {int x,y,width,height;} pn_feedback_box_t; ///< 逻辑屏幕上的精确矩形 / Exact logical-screen rectangle
typedef struct {
    pn_feedback_box_t box; ///< 在原屏幕的范围 / Bounds in the original screen
    pn_frame_t frame; ///< 局部坐标的不可变借用 / Immutable borrow in local coordinates
    uint64_t view; ///< 捕获时的语义页面身份 / Semantic view identity at capture
    int hit; ///< 原命中码 / Original hit code
    bool pressed; ///< 按下目标或恢复原图 / Pressed target or original restoration
    bool settle; ///< 当前已显示参考的定稿请求，无局部像素借用 / Settle the current shown reference without a pixel borrow
} pn_feedback_patch_t;
typedef int (*pn_feedback_hit_fn)(void *,int,int); ///< 无I/O命中函数 / Hit function without I/O
typedef pn_status_t (*pn_feedback_read_fn)(void *,pn_feedback_box_t,pn_frame_t *); ///< 控制层复制已显示区域，不借用UI对象 / Controller copies shown pixels without borrowing a UI object
typedef pn_status_t (*pn_feedback_present_fn)(void *,const pn_feedback_patch_t *); ///< 完成回执前不得返回或修改借用 / Do not return or change the borrow before completion
typedef struct {
    pn_feedback_patch_t patch; ///< 当前局部目标及身份 / Current local target and identity
    uint8_t *original,*target; ///< 自有原图和按下像素，关闭UI仍有效 / Owned original/pressed pixels, valid after UI closure
    bool active,borrowed; ///< 存储有效、回执期间不可释放 / Storage valid, immutable while awaiting receipt
} pn_feedback_t;
/// 沿按点找矩形并逐像素验证同码；大控件取底部提示条，全屏背景/不规则区域拒绝，不分配。/ Find the pressed rectangle and verify a uniform code pixel by pixel; use a bottom strip for large controls, rejecting screen-sized backgrounds/irregular regions without allocation.
pn_status_t pn_feedback_region(pn_feedback_hit_fn,void *,int,int,pn_feedback_box_t *,int *);
/// 准备自有原图/按下图；可选内存失败EMPTY，不提交操作、显示或位置。/ Prepare owned original/pressed pixels; optional admission failure returns EMPTY without committing actions/display/position.
pn_status_t pn_feedback_prepare(pn_feedback_t *,pn_pool_t *,pn_feedback_box_t,uint64_t,int,pn_feedback_read_fn,void *);
/// 提交按下或恢复图，等真实回执；失败保留原图供恢复。/ Submit press/restoration and wait for the real receipt; failures retain original pixels for recovery.
pn_status_t pn_feedback_present(pn_feedback_t *,bool,pn_feedback_present_fn,void *);
/// 内容替代反馈或恢复结束后释放，幂等；借用中返回BUSY。/ Release after content supersedes feedback or restoration completes, idempotently; BUSY while borrowed.
pn_status_t pn_feedback_discard(pn_feedback_t *);
#define PN_FEEDBACK_IDLE_MS 320u ///< 操作停止后的定稿等待 / Settle delay after activity stops
#define PN_FEEDBACK_RETRY_MS 1000u ///< 未完成定稿的限频重试 / Rate-limited retry after unsuccessful settling
typedef struct {
    pn_feedback_box_t area; ///< 尚未整理的反馈范围 / Unsettled feedback bounds
    uint64_t generation,due; ///< 当前变更代次与截止时刻 / Mutation generation and deadline
    size_t completed,failures; ///< 成功/失败定稿回执数 / Successful/failed settle receipts
    bool pending; ///< 有实际反馈等待整理 / Actual feedback awaits settling
} pn_feedback_idle_t;
typedef struct {pn_feedback_box_t area;uint64_t generation;} pn_feedback_idle_plan_t; ///< 不修改历史的定稿候选 / Settle candidate without mutating history
/// 产品owner记录成功短反馈，合并范围并延后定稿；不分配。/ Product owner records successful short feedback, merges bounds and delays settling without allocation.
void pn_feedback_idle_note(pn_feedback_idle_t *,pn_feedback_box_t,uint64_t);
/// 输入活动只延后已有定稿，不制造清理任务。/ Input activity delays existing settling without creating work.
void pn_feedback_idle_activity(pn_feedback_idle_t *,uint64_t);
/// 成功内容扫描完全覆盖范围时清除任务，局部覆盖保留。/ Clear work only when a successful content scan fully covers it, retaining partial coverage.
void pn_feedback_idle_cover(pn_feedback_idle_t *,pn_feedback_box_t);
/// 到期且无按压/工作时合并为一次64px带定稿，EMPTY表示不应提交。/ Once due and unoccupied, combine into one 64px-band settle; EMPTY means do not submit.
pn_status_t pn_feedback_idle_plan(const pn_feedback_idle_t *,uint64_t,bool,pn_feedback_idle_plan_t *);
/// 仅匹配候选的实际成功回执清任务；失败保留并限频，旧回执不能覆盖新变更。/ Only matching real success clears work; failures retain/rate-limit it, and stale receipts never overwrite newer changes.
void pn_feedback_idle_receipt(pn_feedback_idle_t *,pn_feedback_idle_plan_t,pn_status_t,uint64_t);
