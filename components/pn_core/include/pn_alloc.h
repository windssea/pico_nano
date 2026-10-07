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

typedef void *(*pn_malloc_fn)(void *ctx, size_t bytes);
typedef void (*pn_free_fn)(void *ctx, void *ptr);
typedef struct {
    size_t limit; ///< 含分配头的总上限 / Total cap including allocation headers
    size_t used; ///< 当前计费字节 / Current charged bytes
    size_t peak; ///< 最大计费字节 / Peak charged bytes
    size_t live; ///< 当前分配数量 / Number of live allocations
    size_t attempts; ///< 有效非零请求次数 / Valid nonzero request count
    size_t fail_at; ///< 指定失败请求，零为禁用 / Failure request index, zero disables
    pn_malloc_fn malloc_fn; ///< 底层分配器 / Backing allocator
    pn_free_fn free_fn; ///< 底层释放器 / Backing deallocator
    void *ctx; ///< 底层上下文 / Backing context
} pn_pool_t;

/// 初始化无活动分配的池；成对提供回调或都为空使用libc。
/// Initialize a pool without live allocations; provide both callbacks or neither for libc.
int pn_pool_init(pn_pool_t *pool, size_t limit, pn_malloc_fn alloc, pn_free_fn release, void *ctx);
/// 分配成功计费实际请求及头；零、溢出、超限或指定故障返回空。
/// Charge payload and header on success; return null for zero, overflow, cap or injected failure.
void *pn_alloc(pn_pool_t *pool, size_t bytes);
/// 空指针安全；仅释放本接口返回的活动指针，池须比指针活得久。
/// Null-safe; free only live pointers returned here, whose pool must outlive them.
void pn_free(void *ptr);
