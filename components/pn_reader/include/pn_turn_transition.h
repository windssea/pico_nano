/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界同源翻页过渡，硬件呈现由调用方负责。/ English: bounded shared page transitions, with presentation owned by the caller. */
#pragma once
#include "pn_reader_app.h"
typedef enum {
    PN_TURN_NONE=0, ///< 默认直接呈现 / Default direct presentation
    PN_TURN_SWEEP=1, ///< 从右到左扫除 / Right-to-left sweep
    PN_TURN_CURL=2 ///< 仿真页折 / Simulated page fold
} pn_turn_style_t;
typedef struct {
    pn_pool_t *pool; ///< 临时帧预算 / Temporary frame budget
    const pn_frame_t *before; ///< 已确认正文，不借扫描中的帧 / Confirmed body, never an active scan frame
    pn_turn_style_t style; ///< 样式 / Style
    bool reverse; ///< 回翻方向 / Previous-page direction
    pn_reader_present_fn present; ///< 唯一显示适配 / Sole display adapter
    void *ctx; ///< 显示上下文 / Display context
} pn_turn_t;
/// 纯绘制：step=0为旧页，step=total为新页；禁止输出与输入重叠。/ Paint only: zero is old and total is new; output cannot alias input.
pn_status_t pn_turn_frame(const pn_frame_t *,const pn_frame_t *,pn_frame_t *,pn_turn_style_t,unsigned step,unsigned total,bool reverse);
/// 两帧过渡后呈现最终页；失败尝试恢复旧页，内存不足直接呈现。/ Two transitions then final; restore old on failure, present directly on low memory.
pn_status_t pn_turn_present(void *,const pn_frame_t *,pn_refresh_t);
