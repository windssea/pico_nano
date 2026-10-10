#pragma once
#include "esp_err.h"
#include <stdbool.h>
esp_err_t read_pico_pmu_vcom_get(int *);void read_pico_pmu_drain_events(void);esp_err_t read_pico_pmu_report_ready(void);bool read_pico_pmu_take_key_short(void);
#include <stdint.h>
/* 电量快照的主机替身，只含书架用到的字段。/ Host stand-in for the battery snapshot, with only the fields the shelf uses. */
typedef struct {uint16_t qb_soc;uint8_t qb_flags;uint32_t unix_sec;uint8_t time_synced;bool time_ok;} pmu_snapshot_t;
#define PMU_CMD_TIME_GET 0x0007
esp_err_t read_pico_pmu_cmd(uint16_t code,const uint8_t *payload,uint8_t plen);
bool read_pico_pmu_ready(void);esp_err_t read_pico_pmu_poll(void);const pmu_snapshot_t *read_pico_pmu_get(void);
