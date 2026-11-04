/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "pn_types.h"
/// 仅由显示owner调用，且输入已暂停、显示轨已关闭；失败也必须调用resume回滚。/ Display owner only, with input paused and panel rails off; call resume even after failure.
pn_status_t pn_device_sleep_peripherals_suspend(void *touch);
/// 恢复MODE/扩展器后复位触摸；失败保留未完成状态，下次可重试。/ Restore MODE/expander before resetting touch; retain unfinished state on failure for retry.
pn_status_t pn_device_sleep_peripherals_resume(void);
