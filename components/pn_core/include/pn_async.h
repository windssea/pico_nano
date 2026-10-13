/* SPDX-License-Identifier: Apache-2.0
 * 中文：共享输入邮箱：导航合并为一项，控制边沿有界排队；状态只有产品worker修改。/ English: shared input mailbox with one coalesced navigation and bounded control edges; product state has one worker owner. */
#pragma once
#include <stdatomic.h>
#include "pn_cancel.h"
#define PN_ASYNC_PAYLOAD 64 ///< 端口事件最大载荷 / Maximum port-event payload
#define PN_ASYNC_CONTROLS 24 ///< 输入边沿队列上限 / Input-edge queue capacity
typedef struct {
    uint8_t bytes; ///< 有效载荷字节 / Valid payload bytes
    bool navigation,urgent; ///< 可合并导航与优先控制 / Coalesced navigation and priority control
    uint64_t view; ///< 捕获输入时的页面身份 / View identity at capture
    uint8_t payload[PN_ASYNC_PAYLOAD]; ///< 不含产品对象指针的端口载荷 / Port payload without product-object pointers
} pn_async_event_t;
typedef struct {
    atomic_flag guard; ///< 仅短拷贝使用的锁 / Lock used only for short copies
    atomic_uint_fast32_t epoch; ///< 协作取消代次 / Cooperative cancellation generation
    atomic_bool background; ///< 正在执行空闲准备 / Idle preparation is active
    atomic_uint_fast32_t view; ///< 已发布页面身份 / Published view identity
    pn_async_event_t controls[PN_ASYNC_CONTROLS],navigation; ///< 有界边沿与唯一待执行导航 / Bounded edges and sole pending navigation
    unsigned first,count; ///< 队列首及长度 / Queue head and length
    bool has_navigation,overflow; ///< 待执行导航与溢出取消指针标记 / Pending navigation and overflow pointer-reset flag
    uint64_t coalesced,dropped; ///< 合并与丢弃计数 / Coalesced and dropped counts
} pn_async_t;
typedef struct {
    pn_async_t *owner; ///< 邮箱 / Mailbox
    uint_fast32_t epoch; ///< 本次准备代次 / Preparation generation
    pn_cancel_scope_t cancel; ///< worker局部取消范围 / Worker-local cancellation scope
} pn_async_scope_t;
/// 初始化无活动worker的邮箱。/ Initialize a mailbox before workers run.
void pn_async_init(pn_async_t *queue);
/// 输入owner投递；导航替换待执行项，后台准备收到新输入即取消。/ Input owner submits; navigation replaces the pending item and new input cancels background work.
pn_status_t pn_async_send(pn_async_t *queue,const pn_async_event_t *event);
/// 产品worker取优先导航再取边沿，空EMPTY；溢出先返回CANCELLED清指针状态。/ Product worker takes priority navigation then edges; EMPTY when empty, CANCELLED first after overflow to reset pointer state.
pn_status_t pn_async_take(pn_async_t *queue,pn_async_event_t *event);
/// 开始可取消工作；idle=true使新输入取消预排/封面。/ Begin cancellable work; idle allows new input to cancel prefetch/covers.
void pn_async_begin(pn_async_t *queue,pn_async_scope_t *scope,bool idle);
/// 结束工作并恢复外层范围。/ End work and restore the outer scope.
void pn_async_end(pn_async_scope_t *scope);
