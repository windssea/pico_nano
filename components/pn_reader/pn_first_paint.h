/* SPDX-License-Identifier: Apache-2.0
 * 中文：未显示正文的有界绘制状态；不拥有帧或显示回执。
 * English: bounded unshown-body painting state, owning neither frames nor display receipts. */
#pragma once
#include "pn_alloc.h"
#include "pn_types.h"
#include "pn_cancel.h"
typedef struct {
    pn_job_token_t token;uint64_t ticket; ///< 完整排版候选的身份 / Complete layout candidate identity
    size_t cursor;unsigned missing; ///< 完成的节点与缺字计数 / Completed nodes and missing glyph count
    int guide,pixels; ///< 跨片阅读线基线与末字字号 / Cross-chunk guide baseline and final glyph size
    bool ready,stopped; ///< 完整正文或已停止可选绘制 / Complete body or stopped optional painting
} pn_first_paint_t;
typedef struct {
    pn_cache_owner_t *held;size_t reserve;pn_cache_class_t kind; ///< 本步骤借用保护与原池策略 / Step borrow protection and original pool policy
} pn_first_scope_t;
/// 只比较同一候选，不把预绘制当作已显示。/ Compare the same candidate without treating painting as presentation.
static inline bool pn_first_paint_matches(const pn_first_paint_t *p,pn_job_token_t t,uint64_t ticket){return p->ticket==ticket && ticket && p->token.session==t.session && p->token.generation==t.generation;}
/// 单owner内保护已有缓存，临时提高余量，不抢占菜单；调用方须保证不在speculative范围。/ Protect existing caches and temporarily raise headroom without displacing menus; caller must be outside speculation.
static inline pn_first_scope_t pn_first_scope_enter(pn_pool_t *pool,size_t reserve){
    pn_first_scope_t s={pool->caches,pool->cache_reserve,pool->speculative_kind};
    for(pn_cache_owner_t *p=s.held;p;p=p->next)pn_cache_pin(p);
    if(pool->cache_reserve<reserve)pool->cache_reserve=reserve;
    pn_cache_speculate(pool,true);pool->speculative_kind=PN_CACHE_GLYPH;return s;
}
/// 归还本步骤策略与原链pin；新owner只在链首注册。/ Restore the step policy and original-chain pins; new owners register only at the head.
static inline void pn_first_scope_leave(pn_pool_t *pool,pn_first_scope_t s){
    pool->speculative_kind=s.kind;pn_cache_speculate(pool,false);pool->cache_reserve=s.reserve;
    for(pn_cache_owner_t *p=s.held;p;p=p->next)pn_cache_unpin(p);
}
