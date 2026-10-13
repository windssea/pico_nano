/* 中文：保留返回码的卡检测桩。 / English: card-detect stub preserving error codes. */
#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef void *fca9555_handle_t;
fca9555_handle_t read_pico_fca9555(void);
esp_err_t fca9555_read_reg(fca9555_handle_t,uint8_t,uint8_t *);
