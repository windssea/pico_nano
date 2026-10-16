/* SPDX-License-Identifier: Apache-2.0
 * 中文：SDL线程端口；主线程事件/显示，产品worker独占模型和池。/ English: SDL thread port; main owns events/display and the product worker owns models/pool. */
#pragma once
#include "pn_async.h"
#include "pn_reader_app.h"
#include "pn_feedback.h"
#include "input_script.h"
#include <SDL.h>
typedef struct {
    pn_async_t input; ///< 共享有界邮箱 / Shared bounded mailbox
    SDL_mutex *mutex;SDL_cond *display_done;SDL_sem *wake; ///< 端口同步 / Port synchronization
    SDL_Thread *worker; ///< 产品worker / Product worker
    SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture; ///< 主线程独占SDL资源 / SDL resources owned by main
    atomic_bool done; ///< worker已退出 / Worker has exited
    const pn_frame_t *frame; ///< 等待显示的不可变借用，回执前不能释放 / Immutable borrow until display receipt
    const pn_feedback_patch_t *patch; ///< 局部反馈借用，回执不推进页面代次 / Local feedback borrow without advancing the view revision
    pn_status_t (*native)(void *);void *native_ctx; ///< 主线程端口任务借用，不访问模型/池 / Borrowed main-thread port task, never touching models/pool
    pn_feedback_present_fn feedback; ///< 主线程绘制局部像素 / Main-thread local pixel renderer
    pn_refresh_t profile;pn_status_t shown; ///< 呈现类型与回执 / Presentation profile and receipt
    bool pending,ack,scope_active; ///< 显示/脚本回执/取消范围 / Display/script receipt/cancellation scope
    bool defer_ack; ///< worker跨轮加载时保留原脚本请求，直到完成或取消；不拦截真实输入 / Worker retains a scripted request across loading ticks until completion/cancellation, without blocking real input
    SDL_Event received,ack_event; ///< 上一事件与脚本回执 / Last event and script receipt
    char title[256];bool title_pending;int text_input; ///< 主线程窗口命令 / Main-thread window commands
    atomic_uint mouse; ///< 输入owner发布的按钮状态 / Button state published by the input owner
    pn_async_scope_t scope; ///< 产品工作范围 / Product work scope
    sim_script_t script; ///< 主线程拥有的脚本源 / Script source owned by main
    pn_status_t (*render)(void *,const pn_frame_t *);void *render_ctx; ///< 主线程呈现回调 / Main-thread rendering callback
    int (*run)(void *);void *run_ctx;int result; ///< 产品worker入口与结果 / Product-worker entry and result
    uint64_t deadline;unsigned delay_ms; ///< 可测试的面板延迟，不暂停事件泵 / Testable panel delay without stopping event pumping
    uint32_t revision,context; ///< 管理页成功呈现代次与当前上下文 / Successful management-presentation revision and current context
    bool discarded_release; ///< 本轮因旧页面丢弃了真实左键松手，供输入所有权释放 / A real left release discarded for an old view this round, allowing input ownership to end
} pn_sdl_async_t;
/// 主线程初始化窗口；失败不启动worker。/ Main initializes the window; failures never start a worker.
bool pn_sdl_async_init(pn_sdl_async_t *port,pn_status_t (*render)(void *,const pn_frame_t *),void *ctx);
/// 主线程启动worker并持续事件/显示，退出后join；不调用产品模型。/ Main starts/joins the worker and pumps events/display without calling models.
int pn_sdl_async_run(pn_sdl_async_t *port,int (*worker)(void *),void *ctx);
/// worker等待输入/空闲时钟，同时发布固定底栏资格并切换取消范围。
/// Worker waits for input/idle ticks, publishing fixed-navigation eligibility and switching cancellation scopes.
bool pn_sdl_async_wait(pn_sdl_async_t *port,SDL_Event *event,unsigned timeout_ms,uint32_t view,bool root);
/// 已确认底栏后取普通边沿批次，保留最后脚本回执；只合并已排队内容，不等待新事件。
/// Drain ordinary edges after confirmed navigation, retaining the last script acknowledgement; merge only queued input without waiting for new events.
pn_status_t pn_sdl_async_drain_root(pn_sdl_async_t *port,pn_async_event_t *events,size_t capacity,size_t *count);
/// worker借用不可变帧等待主线程回执；返回前帧保持有效。/ Worker lends an immutable frame until the main-thread receipt.
pn_status_t pn_sdl_async_present(pn_sdl_async_t *port,const pn_frame_t *frame,pn_refresh_t profile);
/// 产品worker等待局部反馈真实端口回执，保持同次按压的输入身份。/ Product worker waits for the local feedback receipt while preserving this press's input identity.
pn_status_t pn_sdl_async_feedback(pn_sdl_async_t *,const pn_feedback_patch_t *);
/// 主线程执行借用窗口任务，等真实回执且不推进语义view；不保留ctx。/ Main executes a borrowed window task until real receipt without advancing semantic view; never retain ctx.
pn_status_t pn_sdl_async_native(pn_sdl_async_t *,pn_status_t (*)(void *),void *);
/// 主线程销毁端口，须已join。/ Main destroys the port after join.
void pn_sdl_async_close(pn_sdl_async_t *port);
/// worker投递窗口标题与文本输入状态，主线程执行。/ Worker posts title/text-input state for main-thread execution.
void pn_sdl_async_title(pn_sdl_async_t *port,const char *title);
void pn_sdl_async_text(pn_sdl_async_t *port,bool enabled);
