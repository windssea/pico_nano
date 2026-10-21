/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
/// 无线开始边界；SDK管理USB共存，不切换pad，当前返回false表示无需恢复。 / Radio-start boundary: the SDK manages USB coexistence without pad switching; false means no restoration is needed.
bool pn_device_usb_radio_suspend(void);
/// 无线停止边界；不重配SDK或其他owner管理的USB状态。 / Radio-stop boundary: never reconfigure USB state managed by the SDK or another owner.
void pn_device_usb_radio_restore(bool enabled);
