/* SPDX-License-Identifier: Apache-2.0
 * 中文：串行owner的无分配阶段诊断，线程端口独立。/ English: allocation-free phase diagnostics with thread-local owner ports. */
#pragma once
#include <stdbool.h>
#include "pn_alloc.h"
#include "pn_types.h"
typedef struct {
    uint64_t bytes,reads,seeks,hits; ///< 累计实际文件IO，不含推测工作 / Actual cumulative file I/O, excluding speculative estimates
} pn_profile_io_t;
typedef struct {
    const char *phase; ///< 阶段名称仅在同步回调期间借用 / Phase name borrowed only during the synchronous callback
    uint64_t elapsed_us; ///< 单调时钟耗时，包含嵌套阶段 / Monotonic elapsed time including nested phases
    pn_status_t status; ///< 操作真实结果，失败样本不隐藏 / Actual operation result, including failures
    size_t begin_bytes,end_bytes,peak_bytes,attempts; ///< 本阶段池内起止/同时峰值/请求数 / Phase start/end/simultaneous peak and requests
    const pn_pool_t *pool; ///< 仅回调期间借用，由当前owner独占 / Borrowed only during callback, exclusively owned by the caller
    pn_profile_io_t io; ///< 可选IO快照，普通scope为零 / Optional I/O snapshot, zero for ordinary scopes
    const char *resource; ///< 仅同步回调期间借用的诊断路径，不授予文件访问权 / Diagnostic path borrowed only during the synchronous callback, never file authority
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
/// 仅诊断启用时读取微秒时钟；禁用为0，不作为产品调度时钟。/ Read microseconds only with diagnostics enabled; zero when disabled, never a product scheduling clock.
uint64_t pn_profile_time(void);
/// 输出跨让出的累计诊断，不保留栈scope、不分配；IO可NULL。/ Emit a cumulative observation across yields without retaining stack scopes or allocating; I/O may be NULL.
void pn_profile_observe(pn_pool_t *,const char *,pn_status_t,uint64_t,const pn_profile_io_t *);
/// 无分配记录文件生命周期，不额外读写文件，路径只在回调中有效。/ Record file lifecycle without allocation or extra I/O; the path is valid only during the callback.
void pn_profile_file(pn_pool_t *,const char *,pn_status_t,const char *);
/// 开始栈scope，必须在同线程逆序end；禁用时不读时钟。/ Begin a stack scope; end in reverse order on the same thread; disabled scopes do not read the clock.
void pn_profile_begin(pn_profile_span_t *,pn_pool_t *,const char *);
/// 结束并同步输出；回调耗时不计入本阶段。/ End and emit synchronously; callback time is excluded from this phase.
void pn_profile_end(pn_profile_span_t *,pn_status_t);
/// 分配器内部更新活动scope峰值，不执行输出或分配。/ Allocator-internal scope peak update without output or allocation.
void pn_profile_allocation(pn_pool_t *);
