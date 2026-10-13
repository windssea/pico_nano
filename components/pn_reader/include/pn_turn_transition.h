/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界同源翻页过渡，硬件呈现由调用方负责。/ English: bounded shared page transitions, with presentation owned by the caller. */
#pragma once
#include "pn_reader_app.h"
typedef enum {
    PN_TURN_NONE=0, ///< 默认直接呈现 / Default direct presentation
    PN_TURN_SWEEP=1, ///< 历史扫除ID，产品直接显示 / Legacy sweep ID, product presents directly
    PN_TURN_CURL=2 ///< 历史页折ID，产品直接显示 / Legacy fold ID, product presents directly
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
/// 各历史样式仅呈现最终页一次；失败尽力恢复借用旧帧，无分配。/ Every legacy style presents the final page once; best-effort restore from the borrowed old frame on failure, without allocation.
pn_status_t pn_turn_present(void *,const pn_frame_t *,pn_refresh_t);
