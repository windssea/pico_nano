#pragma once
#include "esp_err.h"
#include <stdbool.h>
esp_err_t read_pico_pmu_vcom_get(int *);void read_pico_pmu_drain_events(void);esp_err_t read_pico_pmu_report_ready(void);bool read_pico_pmu_take_key_short(void);
