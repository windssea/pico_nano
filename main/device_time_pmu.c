/* SPDX-License-Identifier: Apache-2.0
 * 中文：控制层提交UTC并读回，绝不写VCOM或电源时序。
 * English: controller-level UTC commit/readback without VCOM or power-sequence writes. */
#include "device_time.h"
#include "read_pico_pmu.h"
#include <stddef.h>
pn_status_t pn_device_time_commit(const pn_device_time_state_t *state,uint32_t *utc){
 if(!state || !utc)return PN_INVALID;
 if(!state->released)return PN_BUSY;
 if(state->phase!=PN_TIME_SUCCEEDED || state->error!=PN_OK || state->utc<1704067200u || state->utc>4102444799u)return PN_INVALID;
 if(!read_pico_pmu_ready())return PN_IO;
 uint8_t payload[4]={(uint8_t)state->utc,(uint8_t)(state->utc>>8),(uint8_t)(state->utc>>16),(uint8_t)(state->utc>>24)};
 if(read_pico_pmu_cmd(PMU_CMD_TIME_SYNC,payload,sizeof payload)!=ESP_OK || read_pico_pmu_cmd(PMU_CMD_TIME_GET,NULL,0)!=ESP_OK)return PN_IO;
 const pmu_snapshot_t *snapshot=read_pico_pmu_get();
 if(!snapshot || !snapshot->time_ok || !snapshot->time_synced || snapshot->unix_sec<state->utc || snapshot->unix_sec-state->utc>10u)return PN_IO;
 *utc=snapshot->unix_sec;return PN_OK;
}
