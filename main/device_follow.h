/* SPDX-License-Identifier: Apache-2.0
 * 中文：借用原FOLLOW相位的扫描适配，不改LUT、PMU或默认产品路径。
 * English: scan adapter borrowing an original FOLLOW phase, without changing LUTs, PMU or default product paths. */
#pragma once
#include "pn_follow.h"
#include "epd_waveform.h"
typedef pn_status_t (*pn_device_follow_draw_fn)(void *,const EpdWaveform *,const pn_follow_job_t *,uint8_t *,bool *,uint8_t *,bool *); ///< 同步回执；显示owner只读借用job并填SDK差分，started表示硬件交接，不保留指针 / Synchronous receipt; display owner reads the borrowed job and fills SDK differences; started means hardware handoff; never retain pointers
typedef struct {
    void *ctx;pn_device_follow_draw_fn draw; ///< 显示owner端口 / Display-owner port
    uint8_t *difference;size_t capacity;bool *lines;uint8_t *columns; ///< 既有SDK缓冲，831744字节/684行/608列字节 / Existing SDK buffers: 831744 bytes, 684 rows, 608 column bytes
    bool qualified,vcom_valid; ///< 技术门及有效VCOM，缺任一不调用端口 / Qualification gate and valid VCOM; call no port without both
} pn_device_follow_port_t;
/// 原表第0相只读视图，黑7/白8计数与原表一致；真实回执后完成job。不含灰阶归一/定稿/电源管理，控制层必须先处理。/ Read-only view of original phase zero, matching original black7/white8 counts; complete the job after a real receipt. Gray normalization/settling/power must be handled by the control layer first.
pn_status_t pn_device_follow_scan(const pn_device_follow_port_t *,const pn_follow_job_t *);
