/* SPDX-License-Identifier: Apache-2.0
 * 中文：设备输入/显示端口，产品worker独占池和页面状态。/ English: device input/display port; product worker owns pool and page state. */
#pragma once
#include "pn_async.h"
#include "pn_reader_app.h"
#include "pn_key.h"
#include "read_pico.h"
typedef enum {PN_DASYNC_TOUCH,PN_DASYNC_KEY,PN_DASYNC_ACTION,PN_DASYNC_POWER,PN_DASYNC_OPEN_CANCEL} pn_device_input_kind_t; ///< 输入种类；开书取消保留原上下文语义 / Input kind; opening cancellation retains its original context semantics
typedef struct {
    pn_device_input_kind_t kind; ///< 归一输入种类 / Normalized input kind
    uint64_t at; ///< 捕获时刻 / Capture time
    unsigned count;int x,y;bool valid; ///< 触摸边沿 / Touch edge
    int key;pn_key_event_t key_event;pn_reader_action_t action; ///< 三键/阅读动作 / Key/reader action
} pn_device_input_event_t;
typedef struct {
    pn_async_t input; ///< 产品输入邮箱 / Product input mailbox
    read_pico_handle_t *hardware; ///< 只借用稳定BSP句柄 / Borrow stable BSP handle
    pn_reader_present_fn panel;void *panel_ctx; ///< 仅显示owner调用 / Called only by display owner
    void (*idle)(uint64_t);void (*off)(void); ///< 显示轨owner操作 / Display-rail owner operations
    atomic_bool paused,pause_ack,body;atomic_uint flags; ///< 输入暂停与阅读快照 / Input pause and reader snapshot
    bool input_active,display_active; ///< 已启动端口 / Started ports
    uint32_t revision; ///< 产品worker拥有的管理页呈现代次 / Management presentation revision owned by product worker
    void *display_queue,*display_done;pn_status_t display_result; ///< IDF同步对象 / IDF synchronization objects
    void *input_task_handle,*display_task_handle; ///< 稳定任务句柄，仅诊断读取水位 / Stable task handles used only for diagnostic watermarks
} pn_device_async_t;
/// 在VCOM门与BSP初始化成功后启动端口；失败降级不启动半初始化硬件。/ Start after VCOM/BSP gates; failures degrade without using partially initialized hardware.
void pn_device_async_start(pn_device_async_t *port,read_pico_handle_t *hardware,pn_reader_present_fn panel,void (*idle)(uint64_t),void (*off)(void));
/// 产品worker呈现不可变借用帧并等真实面板回执；不提交阅读位置。/ Product worker lends an immutable frame and waits for actual panel receipt without committing progress.
pn_status_t pn_device_async_present(pn_device_async_t *port,const pn_frame_t *frame,pn_refresh_t profile);
/// 产品worker发布输入上下文，不暴露页面对象。/ Product worker publishes input context without exposing page objects.
void pn_device_async_view(pn_device_async_t *port,uint32_t view,bool body,uint8_t flags);
/// 产品worker取一个事件，空EMPTY，溢出CANCELLED。/ Product worker takes an event; EMPTY when empty, CANCELLED after overflow.
pn_status_t pn_device_async_take(pn_device_async_t *port,pn_device_input_event_t *event);
/// 等输入owner暂停后才进入睡眠/PMU边界；超时不睡眠。/ Wait for input owner pause before sleep/PMU handoff; never sleep on timeout.
bool pn_device_async_pause(pn_device_async_t *port,bool paused);
/// 显示owner断轨屏障。/ Display-owner rail-off barrier.
bool pn_device_async_off(pn_device_async_t *port);
