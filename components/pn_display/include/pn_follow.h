/* SPDX-License-Identifier: Apache-2.0
 * 中文：二值连续驱动的逐像素剩余相位，扫描由port执行。
 * English: per-pixel remaining phases for binary continuous driving; ports perform scans. */
#pragma once
#include "pn_types.h"
#include "pn_alloc.h"
#include "pn_frame.h"
typedef struct {int x,y,width,height;} pn_follow_box_t; ///< 逻辑区域 / Logical region
typedef struct {void *impl;uint64_t serial;} pn_follow_t; ///< 单owner，不复制活动对象；close后保留serial / Single owner, never copy live objects; preserve serial after close
typedef struct {pn_follow_t *owner;uint64_t ticket;pn_follow_box_t box;} pn_follow_job_t; ///< 真实扫描回执前的只读借用 / Read-only borrowing until the real scan receipt
typedef struct {const int8_t *cells;int x,width;} pn_follow_row_t; ///< 冻结区域行：±1..8待驱动，±9保持；只在job回执前有效 / Frozen area row: ±1..8 pending, ±9 hold; valid only until the job receipt
typedef struct {size_t bytes,pending;uint64_t marks,phases;bool known,borrowed;unsigned dark,light;} pn_follow_stats_t; ///< 成功扫描数不是光学帧率 / Successful scan count is not optical frame rate
/// 仅接受已确认的0/15底图，1..8压黑/擦白相位；可选内存不足EMPTY且不抢原缓存。/ Accept only a confirmed 0/15 baseline and 1..8 dark/light phases; EMPTY on optional-memory refusal, without evicting existing caches.
pn_status_t pn_follow_open(pn_follow_t *,pn_pool_t *,const pn_frame_t *,unsigned,unsigned);
/// 只跟踪一个确认的二值区域，框外可以是灰阶且始终保持；状态内存随区域大小增长。/ Track only one confirmed binary region; gray pixels outside always hold, with state memory proportional to region size.
pn_status_t pn_follow_open_area(pn_follow_t *,pn_pool_t *,const pn_frame_t *,pn_follow_box_t,unsigned,unsigned);
/// 从完整二值目标更新指定区域；同目标不重置未完成相位，反向重新给足次数；借用中BUSY。/ Update an area from a complete binary target; matching targets preserve pending phases, reversals restart the full count, BUSY while borrowed.
pn_status_t pn_follow_mark(pn_follow_t *,const pn_frame_t *,pn_follow_box_t);
/// 借用一相；无待驱动像素EMPTY，不提前扣次数或接受目标定稿。/ Borrow one phase; EMPTY without pending pixels, never decrement early or confirm final settling.
pn_status_t pn_follow_prepare(pn_follow_t *,pn_follow_job_t *);
/// 读取逻辑像素驱动方向：+1压黑/-1擦白/0保持；旧job拒绝。/ Read a logical pixel's drive direction: +1 dark/-1 light/0 hold; reject stale jobs.
pn_status_t pn_follow_drive(const pn_follow_job_t *,int,int,int *);
/// 只读借用一行，避免扫描转换逐像素跨模块调用；不分配/修改，job回执后不得使用。/ Borrow a read-only row to avoid per-pixel cross-module calls; never allocate/mutate or use after the job receipt.
pn_status_t pn_follow_row(const pn_follow_job_t *,int,pn_follow_row_t *);
/// 排空后校验二值帧等于确认目标；不提交硬件或改变状态，供任意列表确认back前使用。/ After draining, verify that the binary frame matches the confirmed target; never submit hardware or mutate state, for any list before confirming back.
pn_status_t pn_follow_confirmed(const pn_follow_t *,const pn_frame_t *);
/// 成功扫描才扣一次，失败变为未知并拒绝新目标，须确认恢复后reset；旧回执不生效。/ Decrement only after a successful scan; failure becomes unknown and rejects new targets until a confirmed reset; stale receipts have no effect.
pn_status_t pn_follow_complete(pn_follow_t *,const pn_follow_job_t *,bool);
/// 仅用于未交给硬件的取消，保留已知状态与全部次数。/ Cancel only before hardware handoff, preserving known state and all counts.
pn_status_t pn_follow_unsubmit(pn_follow_t *,const pn_follow_job_t *);
/// 确认恢复后的二值底图重置，借用中BUSY；灰阶定稿不能当二值底图。/ Reset from a confirmed recovered binary baseline, BUSY while borrowed; gray settling is not a binary baseline.
pn_status_t pn_follow_reset(pn_follow_t *,const pn_frame_t *);
/// 不读取硬件或分配。/ No hardware reads or allocation.
pn_follow_stats_t pn_follow_stats(const pn_follow_t *);
/// 明确放弃驱动，记录二值状态未知；借用中BUSY，不恢复面板，控制层须GC重建或确认灰阶定稿。/ Explicitly abandon driving and mark binary state unknown; BUSY while borrowed, never recovering the panel; control must rebuild with GC or confirm gray settling.
pn_status_t pn_follow_abandon(pn_follow_t *);
/// 借用或已知但仍有相位时BUSY，其余幂等释放，禁止悄悄丢掉半程驱动。/ BUSY while borrowed or known phases remain; otherwise idempotent release, never silently discarding a partial drive.
pn_status_t pn_follow_close(pn_follow_t *);
