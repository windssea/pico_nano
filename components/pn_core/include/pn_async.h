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
    uint64_t root_epoch; ///< 邮箱盖章的固定底栏连续上下文，普通投递为零 / Mailbox-stamped continuous fixed-navigation context, zero for ordinary submissions
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
    bool controls_blocked; ///< 开书期间仅接纳紧急导航，由guard保护 / Only urgent navigation is admitted during opening; protected by guard
    uint32_t first_view; ///< 首屏呈现期间预备的阅读上下文，0为未预备；guard保护 / Prepared first-page reading context, zero when inactive; guarded
    bool first_navigation; ///< 唯一导航槽持有尚未确认首屏的新翻页；guard保护 / Sole navigation slot holds a turn awaiting first-page confirmation; guarded
    bool root_enabled; ///< 当前画面允许固定底栏，guard保护 / Current screen permits fixed navigation, protected by guard
    uint64_t root_epoch; ///< 进出固定底栏时换代；耗尽后拒绝跨帧接纳 / Changes on entering/leaving fixed navigation; exhaustion rejects cross-frame admission
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
/// 端口只为有效单点底栏事件调用；同锁复核捕获view并盖上下文印记，不改变优先级或顺序。
/// Ports use this only for valid single-contact navigation hits; stamp context after checking the captured view under the same lock, preserving priority/order.
pn_status_t pn_async_send_root(pn_async_t *queue,const pn_async_event_t *event);
/// 产品owner同时发布页面身份和固定底栏资格；离开/重新进入使旧底栏事件永久失效。
/// Product owner publishes view and fixed-navigation eligibility together; leaving/re-entering permanently invalidates old root events.
void pn_async_set_view(pn_async_t *queue,uint32_t view,bool root);
/// 呈现非一级页之前撤销资格，覆盖两次主循环之间的临时子页/模态。
/// Revoke eligibility before presenting non-root screens, including transient subpages/modals between loop iterations.
void pn_async_set_root(pn_async_t *queue,bool root);
/// 仅判断印记是否仍在同一连续底栏上下文；端口仍须验证事件种类和固定几何。
/// Check only continuous root context; ports must still validate event type and fixed geometry.
bool pn_async_root_current(pn_async_t *queue,const pn_async_event_t *event);
/// 已确认根页导航的前台owner取出当前普通边沿快照，以合并目标并消费释放；不执行其中旧内容命令。
/// A foreground owner with confirmed root navigation snapshots pending ordinary edges to merge targets and consume releases, never executing obsolete content commands.
/// 导航优先项存在返回BUSY且不动队列；溢出返回CANCELLED；成功仅接纳已取批次的取消代次，随后输入仍可取消。
/// Pending priority navigation returns BUSY without changing the queue; overflow returns CANCELLED. Success accepts only the drained batch generation, so later input can still cancel.
pn_status_t pn_async_drain_root_controls(pn_async_scope_t *scope,pn_async_event_t *events,size_t capacity,size_t *count);
/// 产品worker取优先导航再取边沿，空EMPTY；溢出先返回CANCELLED清指针状态。/ Product worker takes priority navigation then edges; EMPTY when empty, CANCELLED first after overflow to reset pointer state.
pn_status_t pn_async_take(pn_async_t *queue,pn_async_event_t *event);
/// 开始可取消工作；idle=true使新输入取消预排/封面。/ Begin cancellable work; idle allows new input to cancel prefetch/covers.
void pn_async_begin(pn_async_t *queue,pn_async_scope_t *scope,bool idle);
/// 结束工作并恢复外层范围。/ End work and restore the outer scope.
void pn_async_end(pn_async_scope_t *scope);
/// 开书控制门：进入时丢弃已有普通事件，保留紧急导航；不中断当前worker。/ Opening gate drops ordinary pending events, retaining urgent navigation without cancelling the current worker.
void pn_async_block_controls(pn_async_t *queue,bool blocked);
/// 控制门关闭时预备首屏上下文，旧触点必须由端口取消；不提前执行翻页。 / Prepare a first-page context under the closed gate; the port must cancel old contacts, and no turn executes yet.
pn_status_t pn_async_first_view(pn_async_t *queue,uint32_t view);
/// 仅端口已识别的新翻页调用；首屏期最多留一项且不取消开书，紧急导航优先。 / Only new recognized turns use this; hold at most one without cancelling opening, and retain urgent priority.
pn_status_t pn_async_first_navigation(pn_async_t *queue,const pn_async_event_t *event);
/// 成功后交付唯一翻页，失败丢弃；开放控制门，不改变页面身份。 / Release the sole turn on success or discard it on failure; open the gate without changing view identity.
void pn_async_first_complete(pn_async_t *queue,bool success);
