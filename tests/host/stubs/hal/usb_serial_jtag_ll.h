/* 中文：USB pad生命周期桩。 / English: USB pad lifecycle stub. */
#pragma once
#include <stdbool.h>
bool usb_serial_jtag_ll_module_is_enabled(void);
bool usb_serial_jtag_ll_phy_is_pad_enabled(void);
void usb_serial_jtag_ll_phy_enable_pad(bool enabled);
void usb_serial_jtag_ll_enable_bus_clock(bool enabled);
