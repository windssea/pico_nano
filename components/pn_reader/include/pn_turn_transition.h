/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界同源翻页过渡，硬件呈现由调用方负责。/ English: bounded shared page transitions, with presentation owned by the caller. */
#pragma once
#include "pn_reader_app.h"
typedef enum {
    PN_TURN_NONE=0, ///< 默认直接刷新（优化：平铺仅变化像素+静默白推）/ Default direct refresh (optimized: flat changed-only + white-push)
    PN_TURN_SWEEP=1, ///< 兼容旧名；现为高清无闪（完整48相GL16+静默白推）/ Legacy alias; now crisp flash-free (full 48-phase GL16 + white-push)
    PN_TURN_CRISP=1, ///< 高清无闪（完整48相GL16+仅变化像素+静默白推）/ Crisp flash-free (full 48-phase GL16 + changed-only + white-push)
    PN_TURN_CURL=2, ///< 兼容旧名；现为极速二值（20相DU快翻）/ Legacy alias; now fast binary (20-phase DU turn)
    PN_TURN_FAST=2, ///< 极速翻页（20相DU二值快翻，周期GC16保洁）/ Fast turn (20-phase DU binary, periodic GC16 clean)
    PN_TURN_RIPPLE=3 ///< 水波纹：弧形错峰+仅变化像素+静默白推，清理页波纹GC16 / Ripple: arc-staggered + changed-only + white-push, GC16 ripple on clean
} pn_turn_style_t;
typedef struct {
    pn_pool_t *pool; ///< 保留调用兼容，无动画分配 / Retained for call compatibility, no animation allocation
    const pn_frame_t *before; ///< 已确认正文，不借扫描中的帧 / Confirmed body, never an active scan frame
    pn_turn_style_t style; ///< 样式 / Style
    bool reverse; ///< 回翻方向 / Previous-page direction
    pn_reader_present_fn present; ///< 唯一显示适配 / Sole display adapter
    void *ctx; ///< 显示上下文 / Display context
} pn_turn_t;
/// 纯绘制：step=0为旧页，step=total为新页；禁止输出与输入重叠。/ Paint only: zero is old and total is new; output cannot alias input.
pn_status_t pn_turn_frame(const pn_frame_t *,const pn_frame_t *,pn_frame_t *,pn_turn_style_t,unsigned step,unsigned total,bool reverse);
/// 各样式仅呈现最终页一次；水波纹且有已确认旧页时改用水波纹刷新档，由显示适配决定能否执行；失败尽力恢复借用旧帧，无分配。
/// Every style presents the final page once; ripple with a confirmed old page switches to the ripple profile and the display adapter decides whether it can run; best-effort restore from the borrowed old frame on failure, without allocation.
pn_status_t pn_turn_present(void *,const pn_frame_t *,pn_refresh_t);
