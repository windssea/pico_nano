/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：受限资源与绘制的公共契约，不依赖设备或操作系统。
 * English: bounded resource and drawing contracts, independent of hardware and OS.
 *
 * 冻结：调用者串行访问；不修改硬件电源或设置。
 * Frozen: callers serialize access; never modify hardware power or settings.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct pn_pool pn_pool_t;
typedef enum {
    PN_CACHE_NEIGHBOR, ///< 未显示邻页 / Undisplayed neighbor pages
    PN_CACHE_GLYPH, ///< 可重建字形 / Rebuildable glyphs
    PN_CACHE_COVER, ///< 非活动封面 / Inactive covers
    PN_CACHE_CHAPTER, ///< 已验证章节语义，可在压力时重建 / Verified chapter semantics, rebuildable under pressure
    PN_CACHE_COUNT ///< 类别数 / Class count
} pn_cache_class_t;
typedef struct pn_cache_owner pn_cache_owner_t;
typedef size_t (*pn_cache_bytes_fn)(void *ctx); ///< 纯查询回调 / Pure query callback
typedef void (*pn_cache_drop_fn)(void *ctx); ///< 无分配释放回调 / Allocation-free release callback
struct pn_cache_owner {
    pn_pool_t *pool; ///< 注册池，必须先注销再释放owner / Registered pool; detach before freeing owner
    pn_cache_owner_t *next; ///< 无堆分配注册链 / Allocation-free registration chain
    void *ctx; ///< 寿命稳定的owner / Lifetime-stable owner
    pn_cache_bytes_fn bytes; ///< 仅查询可回收计费字节 / Query reclaimable charged bytes only
    pn_cache_drop_fn drop; ///< 仅释放缓存，禁止分配/IO/注销 / Free caches only; no allocation, IO or detachment
    pn_cache_class_t kind; ///< 回收顺序 / Reclamation order
    unsigned pins; ///< 借用期间禁止回收 / Prevent reclamation during borrowing
};
typedef struct {
    size_t bytes,peak,hits,misses,denied,evictions,reclaimed; ///< 当前/峰值、命中/未命中、拒绝/淘汰/回收字节 / Current/peak, hits/misses, denials/evictions/reclaimed bytes
} pn_cache_stats_t;

typedef void *(*pn_malloc_fn)(void *ctx, size_t bytes);
typedef void (*pn_free_fn)(void *ctx, void *ptr);

typedef enum {
    PN_MEM_OTHER, ///< 未分类/基础分配 / Unclassified and infrastructure allocations
    PN_MEM_FRAME, ///< 产品帧与过渡 / Product frames and transitions
    PN_MEM_FONT, ///< 字体引擎与字形 / Font engines and glyphs
    PN_MEM_DOCUMENT, ///< 容器、解析与章缓存 / Containers, parsing and chapter caches
    PN_MEM_LAYOUT, ///< 排版节点与阅读对象 / Layout nodes and reader objects
    PN_MEM_IMAGE, ///< 图片解码及子池 / Image decoding and nested pools
    PN_MEM_INDEX, ///< 可重建索引 / Rebuildable indexes
    PN_MEM_UI, ///< 界面模型与展示资源 / UI models and display assets
    PN_MEM_TRANSFER, ///< 传输会话与接收 / Transfer sessions and receiving
    PN_MEM_COUNT ///< 分类数量，不能用于分配 / Class count, invalid for allocation
} pn_memory_tag_t; ///< 分配所有者分类，池外资源另计 / Allocation owner classes; external resources are counted separately
typedef struct {
    size_t used,peak,live; ///< 含分配头的当前/峰值/活动块 / Charged current/peak bytes and live blocks
    size_t allocations,failures; ///< 成功与失败请求数 / Successful and failed requests
} pn_memory_stats_t;
struct pn_pool {
    size_t limit; ///< 含分配头的总上限 / Total cap including allocation headers
    size_t used; ///< 当前计费字节 / Current charged bytes
    size_t peak; ///< 最大计费字节 / Peak charged bytes
    size_t live; ///< 当前分配数量 / Number of live allocations
    size_t attempts; ///< 有效非零请求次数 / Valid nonzero request count
    size_t fail_at; ///< 指定失败请求，零为禁用 / Failure request index, zero disables
    pn_malloc_fn malloc_fn; ///< 底层分配器 / Backing allocator
    pn_free_fn free_fn; ///< 底层释放器 / Backing deallocator
    void *ctx; ///< 底层上下文 / Backing context
    pn_memory_stats_t memory[PN_MEM_COUNT]; ///< 各类独立峰值不可相加当同时峰值 / Class peaks must not be summed as a simultaneous peak
    pn_cache_owner_t *caches; ///< owner串行访问的回收链 / Serialized owner reclamation chain
    size_t cache_limit,cache_reserve,cache_caps[PN_CACHE_COUNT]; ///< 总缓存/前台余量/分项上限 / Aggregate cache cap, foreground headroom and class caps
    pn_cache_stats_t cache_stats[PN_CACHE_COUNT]; ///< 池内可回收缓存诊断 / Reclaimable pool-cache diagnostics
    unsigned speculative; ///< 预排范围不回收或重试 / Speculative scopes never reclaim or retry
    pn_cache_class_t speculative_kind; ///< 最外层可选分配类别 / Outermost optional allocation class
    size_t speculative_begin; ///< 最外层预排开始用量 / Usage at outer speculative entry
    bool reclaiming; ///< 回收递归保护 / Reclamation recursion guard
};

/// 初始化无活动分配的池；成对提供回调或都为空使用libc。
/// Initialize a pool without live allocations; provide both callbacks or neither for libc.
int pn_pool_init(pn_pool_t *pool, size_t limit, pn_malloc_fn alloc, pn_free_fn release, void *ctx);
/// 分配成功计费实际请求及头；零、溢出、超限或指定故障返回空。
/// Charge payload and header on success; return null for zero, overflow, cap or injected failure.
void *pn_alloc(pn_pool_t *pool, size_t bytes);
/// 分类计费；分类错误拒绝，释放时使用块内原分类。/ Charge by class; reject invalid classes and free using the stored original class.
void *pn_alloc_tagged(pn_pool_t *pool,pn_memory_tag_t tag,size_t bytes);
/// 稳定诊断名称，不分配或修改池。/ Stable diagnostic name without allocations or pool mutations.
const char *pn_memory_tag_name(pn_memory_tag_t tag);
/// 空指针安全；仅释放本接口返回的活动指针，池须比指针活得久。
/// Null-safe; free only live pointers returned here, whose pool must outlive them.
void pn_free(void *ptr);
/// 查询本接口活动块的计费字节，NULL为零。/ Charged bytes of a live allocation from this API; zero for NULL.
size_t pn_alloc_size(const void *ptr);
/// 每个块的分配头计费尺寸。/ Charged allocation-header size per block.
size_t pn_alloc_overhead(void);
/// 初始化零值owner；同一owner先注销再重新注册。/ Attach a zero-initialized owner; detach before registering it again.
void pn_cache_attach(pn_pool_t *,pn_cache_owner_t *,pn_cache_class_t,void *,pn_cache_bytes_fn,pn_cache_drop_fn);
/// 关闭前注销；不得在回收回调内调用。/ Detach before closing; never call inside a reclamation callback.
void pn_cache_detach(pn_cache_owner_t *);
/// 短期借用保护，可嵌套；仅已注册owner生效。/ Nested short borrowing protection; applies only to attached owners.
void pn_cache_pin(pn_cache_owner_t *);
/// 结束一次借用保护。/ End one borrowing protection scope.
void pn_cache_unpin(pn_cache_owner_t *);
/// 可选缓存准入，不触发回收，保留前台余量。/ Optional cache admission without reclamation, preserving foreground headroom.
bool pn_cache_allow(pn_pool_t *,pn_cache_class_t,size_t bytes);
/// 可选分配不抢占其他缓存或重试；仍保留故障注入。/ Optional allocation never evicts other caches or retries; retains failure injection.
void *pn_cache_alloc(pn_pool_t *,pn_cache_class_t,pn_memory_tag_t,size_t bytes);
/// 更新字节快照并记命中；不分配。/ Refresh byte snapshots and record a hit/miss without allocation.
void pn_cache_observe(pn_pool_t *,pn_cache_class_t,bool hit);
/// 刷新当前/峰值字节，不修改命中计数。/ Refresh current/peak bytes without changing hit counts.
void pn_cache_snapshot(pn_pool_t *);
/// 前台安全边界按邻页、字形、非活动封面回收至所需余量。/ At a safe foreground boundary reclaim neighbors, glyphs, inactive covers to the required headroom.
size_t pn_cache_reclaim(pn_pool_t *,size_t headroom);
/// 预排所有临时分配受共享预算约束；必须成对退出。/ All speculative temporary allocations share the budget; entry/exit must be paired.
void pn_cache_speculate(pn_pool_t *,bool enter);
