/* SPDX-License-Identifier: Apache-2.0
 * 中文：USB PHY与无线互斥的板级适配，不触碰PMU或TF。
 * English: board adapter separating USB PHY and radio, without PMU or TF access. */
#include "device_usb_radio.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_private/periph_ctrl.h"
#if CONFIG_ESP_PHY_ENABLE_USB
#error "Radio sessions require CONFIG_ESP_PHY_ENABLE_USB disabled"
#endif
#endif
#if defined(ESP_PLATFORM) || defined(PN_USB_RADIO_STUB)
#include "hal/usb_serial_jtag_ll.h"
#ifndef ESP_PLATFORM
#define PERIPH_RCC_ATOMIC() if(true)
#endif
#endif
bool pn_device_usb_radio_suspend(void){
#if defined(ESP_PLATFORM) || defined(PN_USB_RADIO_STUB)
    bool enabled=false;
    PERIPH_RCC_ATOMIC(){
        enabled=usb_serial_jtag_ll_module_is_enabled() && usb_serial_jtag_ll_phy_is_pad_enabled();
        if(enabled)usb_serial_jtag_ll_phy_enable_pad(false);
    }
    return enabled;
#else
    return false;
#endif
}
void pn_device_usb_radio_restore(bool enabled){
#if defined(ESP_PLATFORM) || defined(PN_USB_RADIO_STUB)
    PERIPH_RCC_ATOMIC(){
        if(enabled){
            usb_serial_jtag_ll_enable_bus_clock(true);
            usb_serial_jtag_ll_phy_enable_pad(true);
        }
    }
#else
    (void)enabled;
#endif
}
