/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源字体管理页：TF字体列表、真实信息预览、设为全局默认与确认删除。
 * English: shared font-management page: TF font list, real-information preview, set as global default and confirmed deletion.
 * 冻结：调用方先关闭阅读与字体消费者；删除前关闭本页字体源；不清除任何选择记录。
 * Frozen: callers close readers and font consumers first; this page closes its own font source before deleting; no selection record is cleared.
 */
#pragma once
#include "pn_catalog.h"
#include "pn_display.h"
#include "pn_font_asset.h"
#include "pn_list_motion.h"
#define PN_FMU_BACK 1
#define PN_FMU_PREVIOUS 2
#define PN_FMU_NEXT 3
#define PN_FMU_DEFAULT 4
#define PN_FMU_DELETE 5
#define PN_FMU_CONFIRM 6
#define PN_FMU_CANCEL 7
#define PN_FMU_ROW 16
typedef pn_status_t (*pn_font_manage_present_fn)(void *,const pn_frame_t *,pn_refresh_t); ///< 与阅读呈现同签名 / Same signature as reader presentation
typedef enum {PN_FMU_LIST=0,PN_FMU_DETAIL=1,PN_FMU_CONFIRMING=2} pn_font_manage_screen_t; ///< 列表/详情/删除确认 / List/detail/delete confirmation
typedef struct {
    void *impl; ///< 初始NULL，不可复制 / Initially NULL, never copy
    bool active; ///< 返回后false，由调用方关闭 / False after Back; caller closes
    bool presented; ///< 最近绘制已呈现 / Latest drawing was presented
    pn_font_manage_screen_t screen; ///< 当前页面 / Current screen
    pn_status_t last; ///< 最近一次写操作结果，未操作EMPTY / Latest write result, EMPTY before any
} pn_font_manage_t;

/// 安静时由控制层调用：每轮最多准备一个可见字体小样，全部结论就绪后合并一次GL16；绘制函数不打开小样字体。
/// Controller-only idle work: prepare at most one visible face per call, then batch one GL16 after all conclusions; drawing never opens sample faces.
/// CANCELLED保留槽供重试，瞬态失败一轮后至少等待1秒；调用方先让输入/触摸/显示交接优先，单次字体读取仍可能阻塞。
/// CANCELLED retains its slot; retry transient failures after at least one second per round. Callers prioritize input/touch/display handoff; a face read may still block.
pn_status_t pn_font_manage_samples_idle(pn_font_manage_t *,uint64_t,pn_font_manage_present_fn,void *);

/// 打开：font_media/font_dir为TF字体目录；state_media/state_dir为全局字体记录所在内部目录（可NULL则不能设默认）。
/// Open: font_media/font_dir locate TF fonts; state_media/state_dir hold the global font record (NULL disables setting the default).
pn_status_t pn_font_manage_open(pn_font_manage_t *ui,pn_pool_t *pool,pn_media_t *font_media,const char *font_dir,
    pn_media_t *state_media,const char *state_dir,pn_font_manage_present_fn present,void *ctx);
/// 处理命令（PN_FMU_*或ROW+i）并重绘。/ Handle a command (PN_FMU_* or ROW+i) and repaint.
pn_status_t pn_font_manage_event(pn_font_manage_t *ui,int command,pn_font_manage_present_fn present,void *ctx);
/// 重画。/ Repaint.
pn_status_t pn_font_manage_present(pn_font_manage_t *ui,pn_font_manage_present_fn present,void *ctx);
/// 命中，空白-1。/ Hit test, -1 for blank.
int pn_font_manage_hit(const pn_font_manage_t *ui,int x,int y);
/// 释放资源与字体源，幂等。/ Release resources and font sources idempotently.
void pn_font_manage_close(pn_font_manage_t *ui);
/// 接入可选连续列表端口，低预算或六项以内EMPTY保留分页；ROW命令在此模式使用全局序号。/ Attach an optional continuous-list port; EMPTY keeps paging on low budgets or at most six items; ROW commands use global indices in this mode.
pn_status_t pn_font_manage_motion(pn_font_manage_t *,pn_list_present_fn,void *);
/// 连续列表指针，consumed为真时外层不得重复点击；拖动不选中字体。/ Continuous-list pointer; a consumed event must not also click in the outer controller; dragging never selects a font.
bool pn_font_manage_pointer(pn_font_manage_t *,unsigned,int,int,bool,uint64_t,pn_font_manage_present_fn,void *,pn_status_t *);
/// 空闲/运动每轮一相及定稿，真实工作OK、无任务EMPTY。/ One phase/settling per idle or movement iteration, OK for actual work, EMPTY without work.
pn_status_t pn_font_manage_tick(pn_font_manage_t *,uint64_t);
/// 普通内容/导航前归还运动状态，下一内容恢复责任交端口。/ Return motion state before normal content/navigation, handing subsequent recovery to the port.
pn_status_t pn_font_manage_motion_abort(pn_font_manage_t *);
/// 只读同源滚动状态。/ Read-only shared scrolling state.
pn_list_motion_stats_t pn_font_manage_motion_stats(const pn_font_manage_t *);
