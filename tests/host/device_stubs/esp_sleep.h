#pragma once
#include "esp_err.h"
esp_err_t esp_sleep_enable_gpio_wakeup(void);
esp_err_t esp_light_sleep_start(void);
