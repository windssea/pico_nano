/* SPDX-License-Identifier: Apache-2.0
 * 中文：触摸INT（GPIO43）任意边沿唤醒输入任务；中断不可用时调用方继续定时轮询。
 * English: touch INT (GPIO43) any-edge wake for the input task; callers keep timed polling when the interrupt is unavailable. */
#pragma once
#include <stdbool.h>
/// 安装中断；成功返回true，失败不改变引脚以外的状态。/ Install the interrupt; true on success, failures leave other state unchanged.
bool pn_device_touch_wake_init(void);
/// 等待触摸边沿或超时；返回是否由中断唤醒。/ Wait for a touch edge or the timeout; return whether the interrupt woke us.
bool pn_device_touch_wake_wait(unsigned timeout_ms);
