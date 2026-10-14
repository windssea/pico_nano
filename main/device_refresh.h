/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "device_frame.h"
#define PN_REFRESH_BANDS 19
/// 成功呈现后才提交的区域维护记录。/ Regional maintenance committed only after successful output.
typedef struct {
    unsigned management; ///< 成功管理更新上限计数 / Successful management update ceiling
    unsigned char bands[PN_REFRESH_BANDS]; ///< 各64px逻辑带的局部更新次数 / Local updates per logical 64px band
} pn_device_refresh_t;
/// 本次扫描的候选维护方案，不修改历史。/ Candidate maintenance plan without changing history.
typedef struct {
    bool content_clean; ///< 大面积灰/深色内容擦回白底 / Large gray/dark content returning to white
    bool regional; ///< 扩展为区域整理 / Expand into regional settling
    pn_device_box_t area; ///< 合并后单次扫描区域 / Combined single-scan area
} pn_device_refresh_plan_t;
/// 使用真实板级前后帧估算风险；非光学残影检测。/ Estimate risk from actual board buffers; not optical ghost detection.
pn_device_refresh_plan_t pn_device_refresh_plan(const pn_device_refresh_t *state,const uint8_t *target,const uint8_t *shown,bool reading,pn_device_box_t dirty);
/// 成功后计数；全屏GC16重置全部，区域整理只重置覆盖带。/ Count after success; full GC16 resets all, regional settling resets only covered bands.
void pn_device_refresh_commit(pn_device_refresh_t *state,bool reading,bool gc16,bool full,pn_device_refresh_plan_t plan);
