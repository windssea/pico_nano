/* SPDX-License-Identifier: Apache-2.0
 * 中文：USB/无线生命周期边界；PHY由SDK管理，不触碰PMU或TF。
 * English: USB/radio lifecycle boundary; the SDK owns PHY management, without PMU or TF access. */
#include "device_usb_radio.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#if !CONFIG_ESP_PHY_ENABLE_USB
#error "USB radio coexistence requires CONFIG_ESP_PHY_ENABLE_USB"
#endif
#endif
bool pn_device_usb_radio_suspend(void){
    // 中断USB pad会破坏主机枚举；保留调用边界，但不再绕过SDK切换PHY或引脚。
    // Interrupting USB pads breaks host enumeration; retain the lifecycle boundary without switching PHY or pads behind the SDK.
    return false;
}
void pn_device_usb_radio_restore(bool enabled){
    (void)enabled;
}
