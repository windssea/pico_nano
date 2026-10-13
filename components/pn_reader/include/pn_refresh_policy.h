/* SPDX-License-Identifier: Apache-2.0
 * 中文：只按成功逻辑翻页计数；物理深清由显示适配负责。
 * English: count successful logical turns only; physical cleaning belongs to the display adapter. */
#pragma once
#include "pn_display.h"
typedef struct {
    unsigned period; ///< 全局周期，0使用14 / Global period, zero means 14
    unsigned turns; ///< 最近全刷后已成功翻页 / Successful turns since full refresh
} pn_refresh_policy_t;
/// 当前页/预览不计页；第N次翻页即全刷。/ Repaint/preview do not count; turn N requests full refresh.
static inline pn_refresh_t pn_refresh_policy_next(const pn_refresh_policy_t *p,bool force,bool turn){
    unsigned period=p->period?p->period:14u;
    return force || (turn && p->turns+1>=period)?PN_REFRESH_GC16:PN_REFRESH_GL16;
}
/// 仅成功显示后提交，保存故障不撤销已显示计数。/ Commit only after successful display; save errors do not undo a shown turn.
static inline void pn_refresh_policy_commit(pn_refresh_policy_t *p,pn_refresh_t mode,bool turn){
    if(mode==PN_REFRESH_GC16)p->turns=0;
    else if(turn)p->turns++;
}
