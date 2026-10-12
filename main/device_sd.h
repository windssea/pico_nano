/* SPDX-License-Identifier: Apache-2.0
 * 中文：TF重新挂载的有界异步适配，不格式化。/ English: bounded asynchronous TF remount adapter, without formatting.
 */
#pragma once
#include "pn_storage.h"
typedef struct {
    bool pending; ///< 正在等待底层探测结果 / Waiting for the board probe result
    uint64_t deadline_ms; ///< 本次探测的10秒截止时间 / Ten-second deadline for this probe
    uint64_t poll_at_ms; ///< 下一次检查时间，间隔100ms / Next check time, at 100 ms intervals
} pn_device_sd_t;
/// 在旧消费者关闭后准备TF；成功后调用方才挂接新介质代次。BUSY可能为挂载进行中或旧租约尚未释放。
/// Prepare TF after old consumers close; callers attach a new media epoch only on success. BUSY means an active probe or unreleased old leases.
pn_status_t pn_device_sd_prepare(pn_device_sd_t *state,pn_media_t *media,uint64_t now_ms);
/// 仅检查正在进行的挂载，不重新发起或重置截止时间；EMPTY表示没有待处理挂载。
/// Poll the current mount without restarting it or resetting its deadline; EMPTY means no mount is pending.
pn_status_t pn_device_sd_poll(pn_device_sd_t *state,uint64_t now_ms);
