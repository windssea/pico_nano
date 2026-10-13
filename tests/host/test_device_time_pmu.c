/* 中文：时钟提交仅写TIME_SYNC并读回TIME_GET。 / English: clock commit writes only TIME_SYNC and verifies TIME_GET. */
#include "device_time.h"
#include "read_pico_pmu.h"
#include <assert.h>
#include <stdio.h>
static pmu_snapshot_t pmu;
static bool ready=true;
static unsigned commands,fail_at;
bool read_pico_pmu_ready(void){return ready;}
const pmu_snapshot_t *read_pico_pmu_get(void){return &pmu;}
esp_err_t read_pico_pmu_cmd(uint16_t code,const uint8_t *p,uint8_t n){commands++;assert(code==PMU_CMD_TIME_SYNC || code==PMU_CMD_TIME_GET);if(code==PMU_CMD_TIME_SYNC){assert(n==4 && p);assert(((uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24))==1780000000u);}else assert(!p && !n);return commands==fail_at?ESP_FAIL:ESP_OK;}
int main(void){pn_device_time_state_t s={.phase=PN_TIME_SUCCEEDED,.released=true,.utc=1780000000u};uint32_t out=7;
 s.released=false;assert(pn_device_time_commit(&s,&out)==PN_BUSY && !commands && out==7);s.released=true;s.phase=PN_TIME_CANCELLED;assert(pn_device_time_commit(&s,&out)==PN_INVALID && !commands);s.phase=PN_TIME_SUCCEEDED;
 ready=false;assert(pn_device_time_commit(&s,&out)==PN_IO && !commands);ready=true;
 for(unsigned i=1;i<=2;i++){commands=0;fail_at=i;assert(pn_device_time_commit(&s,&out)==PN_IO && out==7);}fail_at=0;
 pmu=(pmu_snapshot_t){.time_ok=true,.time_synced=1,.unix_sec=s.utc-1};assert(pn_device_time_commit(&s,&out)==PN_IO && out==7);pmu.unix_sec=s.utc+11;assert(pn_device_time_commit(&s,&out)==PN_IO && out==7);pmu.unix_sec=s.utc+2;pmu.time_synced=0;assert(pn_device_time_commit(&s,&out)==PN_IO && out==7);pmu.time_synced=1;
 assert(pn_device_time_commit(&s,&out)==PN_OK && out==s.utc+2);puts("PMU time: release gate, restricted commands, little endian, readback validation and preserved output passed");return 0;}
