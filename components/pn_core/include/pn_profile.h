/* SPDX-License-Identifier: Apache-2.0
 * 中文：串行owner的无分配阶段诊断，线程端口独立。/ English: allocation-free phase diagnostics with thread-local owner ports. */
#pragma once
#include <stdbool.h>
#include "pn_alloc.h"
#include "pn_types.h"
typedef struct {
    const char *phase; ///< 借用静态阶段名称 / Borrowed static phase name
    uint64_t elapsed_us; ///< 单调时钟耗时，包含嵌套阶段 / Monotonic elapsed time including nested phases
    pn_status_t status; ///< 操作真实结果，失败样本不隐藏 / Actual operation result, including failures
    size_t begin_bytes,end_bytes,peak_bytes,attempts; ///< 本阶段池内起止/同时峰值/请求数 / Phase start/end/simultaneous peak and requests
    const pn_pool_t *pool; ///< 仅回调期间借用，由当前owner独占 / Borrowed only during callback, exclusively owned by the caller
} pn_profile_event_t;
typedef uint64_t (*pn_profile_clock_fn)(void *); ///< 单调微秒时钟 / Monotonic microsecond clock
typedef void (*pn_profile_emit_fn)(void *,const pn_profile_event_t *); ///< 同步只读回调，不得递归产品操作 / Synchronous read-only callback without recursive product operations
typedef struct pn_profile_span {
    struct pn_profile_span *previous; ///< 借用上层栈scope / Borrowed parent stack scope
    pn_pool_t *pool; ///< 当前owner池，可为空 / Owner pool, optionally NULL
    const char *phase; ///< 静态阶段名 / Static phase name
    uint64_t started; ///< 起始微秒 / Starting microseconds
    size_t begin_bytes,peak_bytes,attempts; ///< 起始记账与局部峰值 / Initial accounting and local peak
    bool active; ///< 已启用的配对scope / Enabled paired scope
} pn_profile_span_t;
/// 当前线程装配诊断，NULL禁用；只在无活动scope时修改。/ Configure this thread while no scopes are active; NULL disables diagnostics.
void pn_profile_port(pn_profile_clock_fn,pn_profile_emit_fn,void *);
/// 开始栈scope，必须在同线程逆序end；禁用时不读时钟。/ Begin a stack scope; end in reverse order on the same thread; disabled scopes do not read the clock.
void pn_profile_begin(pn_profile_span_t *,pn_pool_t *,const char *);
/// 结束并同步输出；回调耗时不计入本阶段。/ End and emit synchronously; callback time is excluded from this phase.
void pn_profile_end(pn_profile_span_t *,pn_status_t);
/// 分配器内部更新活动scope峰值，不执行输出或分配。/ Allocator-internal scope peak update without output or allocation.
void pn_profile_allocation(pn_pool_t *);
