/* SPDX-License-Identifier: Apache-2.0
 * 中文：设备输入/显示端口，产品worker独占池和页面状态。/ English: device input/display port; product worker owns pool and page state. */
#pragma once
#include "pn_async.h"
#include "pn_reader_app.h"
#include "pn_key.h"
#include "pn_feedback.h"
#include "read_pico.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if defined(CONFIG_PN_TEST_CONSOLE) && CONFIG_PN_TEST_CONSOLE
#define PN_DEVICE_TEST_CONSOLE 1 ///< 诊断镜像启用串口测试钩子 / Diagnostic image enables serial test hooks
#else
#define PN_DEVICE_TEST_CONSOLE 0 ///< 产品镜像不含测试钩子 / Product image has no test hooks
#endif
typedef enum {PN_DASYNC_TOUCH,PN_DASYNC_KEY,PN_DASYNC_ACTION,PN_DASYNC_POWER,PN_DASYNC_OPEN_CANCEL} pn_device_input_kind_t; ///< 输入种类；开书取消保留原上下文语义 / Input kind; opening cancellation retains its original context semantics
typedef struct {
    pn_device_input_kind_t kind; ///< 归一输入种类 / Normalized input kind
    uint64_t at; ///< 捕获时刻 / Capture time
    unsigned count;int x,y;bool valid,ending; ///< 活动触点及显式UP终点 / Active contacts and explicit UP endpoint
    int key;pn_key_event_t key_event;pn_reader_action_t action; ///< 三键/阅读动作 / Key/reader action
} pn_device_input_event_t;
#if PN_DEVICE_TEST_CONSOLE
/// 诊断钩子：只在诊断镜像存在；由输入/显示任务在各自线程调用，不得触碰产品模型或池。
/// Diagnostic hooks present only in diagnostic images; called by the input/display tasks on their own threads and never touching product models or the pool.
typedef struct {
    bool (*touch)(void *ctx,uint64_t now,unsigned *count,int *x,int *y,bool *ending); ///< 返回true时以合成采样替代触摸读数 / True replaces the touch read with a synthetic sample
    bool (*power)(void *ctx); ///< 消费一次合成电源短按 / Consume one synthetic power short press
    void (*display_job)(void *ctx,unsigned kind,uint64_t start_ms,uint64_t end_ms,pn_status_t status); ///< 每个显示job完成后记录 / Record after every display job
    void (*display_idle)(void *ctx); ///< 显示任务两job之间的空隙 / Gap between display jobs on the display task
    void *ctx; ///< 钩子上下文 / Hook context
} pn_device_async_diag_t;
#endif
typedef struct {
    pn_async_t input; ///< 产品输入邮箱 / Product input mailbox
    read_pico_handle_t *hardware; ///< 只借用稳定BSP句柄 / Borrow stable BSP handle
    pn_reader_present_fn panel;void *panel_ctx; ///< 仅显示owner调用 / Called only by display owner
    void (*idle)(uint64_t);void (*off)(void); ///< 显示轨owner操作 / Display-rail owner operations
    atomic_bool paused,body;atomic_uint flags; ///< 输入暂停意图与阅读快照 / Input pause intent and reader snapshot
    atomic_uint pause_request,pause_ack; ///< 单字包含代次和暂停位，确认必须对应本次请求 / Generation and pause bit in one word; acknowledgement must match this request
    bool input_active,display_active; ///< 已启动端口 / Started ports
    uint32_t revision; ///< 产品worker拥有的管理页呈现代次 / Management presentation revision owned by product worker
    void *display_queue,*display_done;pn_status_t display_result; ///< IDF同步对象 / IDF synchronization objects
    void *input_task_handle,*display_task_handle; ///< 稳定任务句柄，仅诊断读取水位 / Stable task handles used only for diagnostic watermarks
    void *product_task; ///< 输入到达时通知的产品任务 / Product task notified when input arrives
    pn_status_t (*overlap)(void *); ///< 等待扫描回执时由产品任务调用的小步工作，OK继续、其他停止 / Small product-task steps while awaiting a scan receipt; OK continues, anything else stops
    void *overlap_ctx; ///< overlap上下文 / overlap context
    bool touch_wake; ///< 触摸INT中断可用 / Touch INT interrupt available
    pn_feedback_present_fn feedback; ///< 局部反馈端口，语义页面代次不变 / Local feedback port preserving the semantic view revision
    atomic_uint touch_reads,touch_errors,events_sent; ///< 输入健康计数，仅诊断 / Input-health counters for diagnostics only
    atomic_bool first_reader; ///< 首屏扫描期间识别新触点，仅暂存一次翻页 / Recognize new contacts during first-page scanning, holding only one turn
#if PN_DEVICE_TEST_CONSOLE
    _Atomic(const pn_device_async_diag_t *) diag; ///< 诊断钩子，启动后发布 / Diagnostic hooks published after start
#endif
} pn_device_async_t;
/// 将驱动原始记录归一为活动触点；UP可带有效终点但不算按住，坏记录返回INVALID。
/// Normalize driver records to active contacts; UP may carry an endpoint but is not held, and malformed records return INVALID.
pn_status_t pn_device_touch_normalize(cst836u_touch_t *touch,bool *ending);
/// 在VCOM门与BSP初始化成功后启动端口；失败降级不启动半初始化硬件。/ Start after VCOM/BSP gates; failures degrade without using partially initialized hardware.
void pn_device_async_start(pn_device_async_t *port,read_pico_handle_t *hardware,pn_reader_present_fn panel,void (*idle)(uint64_t),void (*off)(void));
/// 产品worker呈现不可变借用帧并等真实面板回执；不提交阅读位置。/ Product worker lends an immutable frame and waits for actual panel receipt without committing progress.
pn_status_t pn_device_async_present(pn_device_async_t *port,const pn_frame_t *frame,pn_refresh_t profile);
/// 借用局部像素等真实反馈回执；不预热、不推进页面/阅读代次。/ Borrow local pixels until the real feedback receipt, without warming or advancing view/reader revisions.
pn_status_t pn_device_async_feedback(pn_device_async_t *port,const pn_feedback_patch_t *patch);
/// 显示owner执行端口任务并等真实回执；不预热/推进view，可读冻结job但不得修改模型/池或保留ctx。/ Display owner executes a port task until real receipt; no warming/view revision; it may read frozen jobs but never mutate models/pool or retain ctx.
pn_status_t pn_device_async_native(pn_device_async_t *,pn_status_t (*)(void *),void *);
/// 产品worker发布输入上下文，不暴露页面对象。/ Product worker publishes input context without exposing page objects.
void pn_device_async_view(pn_device_async_t *port,uint32_t view,bool body,uint8_t flags);
/// 在唯一首屏呈现前预备阅读输入，旧触点随上下文变化取消；翻页留待确认。 / Prepare reading input before the sole first-page presentation, cancelling old contacts and holding turns for confirmation.
pn_status_t pn_device_async_first_begin(pn_device_async_t *port,uint8_t flags);
/// 首屏与模型提交成功才交付暂存翻页；失败丢弃，紧急控制保留。 / Release a held turn only after successful screen/model commit; discard on failure and retain urgent controls.
void pn_device_async_first_complete(pn_device_async_t *port,bool success);
/// 同时发布连续固定底栏资格；只用于四个一级页，其他调用使用普通view撤销资格。
/// Publish continuous fixed-navigation eligibility together; only for the four roots, while ordinary view publication revokes it.
void pn_device_async_root_view(pn_device_async_t *port,uint32_t view,bool body,uint8_t flags,bool root);
/// 产品worker取一个事件，空EMPTY，溢出CANCELLED。/ Product worker takes an event; EMPTY when empty, CANCELLED after overflow.
pn_status_t pn_device_async_take(pn_device_async_t *port,pn_device_input_event_t *event);
/// 等输入owner暂停后才进入睡眠/PMU边界；超时不睡眠。/ Wait for input owner pause before sleep/PMU handoff; never sleep on timeout.
bool pn_device_async_pause(pn_device_async_t *port,bool paused);
/// 由产品任务调用一次，之后输入投递会通知它。/ Called once by the product task so input submissions notify it.
void pn_device_async_bind_product(pn_device_async_t *port);
/// 产品任务空闲等待：输入到达立即返回，否则最多timeout_ms。/ Product idle wait: returns at once when input arrives, otherwise after at most timeout_ms.
void pn_device_async_wait(pn_device_async_t *port,unsigned timeout_ms);
/// 显示owner断轨屏障。/ Display-owner rail-off barrier.
bool pn_device_async_off(pn_device_async_t *port);
