/* SPDX-License-Identifier: Apache-2.0
 * 中文：列表拖动/惯性/逐相/灰阶定稿，同源单owner，不操作硬件。
 * English: shared single-owner list drag/inertia/phases/gray settling, without hardware operations. */
#pragma once
#include "pn_follow.h"
typedef struct {void *impl;} pn_list_motion_t; ///< 不复制活动对象 / Never copy live objects
typedef enum {PN_LIST_NORMALIZE,PN_LIST_PHASE,PN_LIST_REFERENCE,PN_LIST_SETTLE,PN_LIST_RECOVER,PN_LIST_ABANDON} pn_list_stage_t; ///< 区域二值归一/一相/确认参考/灰阶定稿/未知恢复/放弃 / Binary normalization, one phase, confirmed reference, gray settle, unknown recovery, abandonment
typedef pn_status_t (*pn_list_render_fn)(void *,int,bool,const pn_frame_t **); ///< 只绘制指定offset及二值区域，帧有效到回执 / Paint only the requested offset/binary region; retain frame until receipt
typedef pn_status_t (*pn_list_prepare_fn)(void *,int,bool); ///< 控制层准备可见行资源，可读介质；不提交显示 / Controller prepares visible-row resources and may read media; never submit display
typedef pn_status_t (*pn_list_present_fn)(void *,pn_list_stage_t,const pn_frame_t *,pn_follow_box_t,const pn_follow_job_t *); ///< 端口副作用；PHASE由port完成job，其他不伪造相位回执 / Port effects; PHASE completes its job in the port, others never fake phase receipts
typedef struct {int offset,maximum,velocity;bool down,dragging,coasting,active,settling;size_t pending;uint64_t phases;} pn_list_motion_stats_t; ///< 软件状态，非面板计时 / Software state, not panel timing
/// 固定viewport与内容高度，可选控制器不足EMPTY；render/present必须同owner串行。/ Fixed viewport/content height; EMPTY on optional-controller refusal; serialize render/present on the same owner.
pn_status_t pn_list_motion_open(pn_list_motion_t *,pn_pool_t *,pn_follow_box_t,int,int,pn_list_render_fn,void *,pn_list_present_fn,void *);
/// 活动前绑定资源准备，render仅读已准备行；NULL取消绑定。/ Bind resource preparation before activation so render only reads prepared rows; NULL unbinds.
pn_status_t pn_list_motion_prepare_port(pn_list_motion_t *,pn_list_prepare_fn);
/// 区域内捕获指针；返回consumed，click_y=-1或原按下位置，状态失败不生成点击。/ Capture pointers inside the area; return consumed, click_y is -1 or the original press position, never generating clicks on failure.
bool pn_list_motion_pointer(pn_list_motion_t *,unsigned,int,int,bool,uint64_t,int *,pn_status_t *);
/// 每轮至多扫描一相；未按压且惯性/相位排空320ms后灰阶定稿。/ Scan at most one phase per iteration; settle gray after release, inertia/phases drain and 320 ms quiet.
pn_status_t pn_list_motion_tick(pn_list_motion_t *,uint64_t);
/// 退出前显式放弃并要求下一内容从未知恢复；不扫描旧页面。/ Explicitly abandon before leaving, requiring recovery for subsequent content; never scan an old page.
pn_status_t pn_list_motion_abort(pn_list_motion_t *);
/// 静态设置offset，活动时BUSY，供分页键共用。/ Set a static offset, BUSY while active, shared with paging keys.
pn_status_t pn_list_motion_offset(pn_list_motion_t *,int);
/// 内容更新后重设范围，活动时BUSY。/ Reset the range after content updates, BUSY while active.
pn_status_t pn_list_motion_extent(pn_list_motion_t *,int);
/// 只读状态。/ Read-only state.
pn_list_motion_stats_t pn_list_motion_stats(const pn_list_motion_t *);
/// 退出屏障后释放，幂等；调用方随后提交新内容或睡眠锁页。/ Release after the exit barrier, idempotently; callers subsequently submit new content or a sleep lock frame.
void pn_list_motion_close(pn_list_motion_t *);
