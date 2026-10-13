/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
/// 无线会话暂停USB数据；返回原来是否启用，充电不变。 / Suspend USB data for radio; return prior enabled state, leaving charging intact.
bool pn_device_usb_radio_suspend(void);
/// 无线完全结束后恢复此前USB数据状态。 / Restore prior USB data state after radio teardown finishes.
void pn_device_usb_radio_restore(bool enabled);
