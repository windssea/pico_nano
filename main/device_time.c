/* SPDX-License-Identifier: Apache-2.0
 * 中文：WiFi连接与SNTP均由短轮询推进，失败/取消先释放无线，控制层再决定提交时钟。
 * English: short polls advance WiFi/SNTP; failures and cancellation release radio before the controller decides to commit time. */
#include "device_time.h"
#include "pn_network_store.h"
#include "esp_netif_sntp.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
typedef struct {
 pn_device_time_state_t state;
 pn_device_wifi_t wifi;
 pn_device_wifi_config_t config;
 uint64_t deadline;
 bool started,sntp,cancel;
} time_sync_t;
static void wipe(time_sync_t *s){volatile unsigned char *p=(volatile unsigned char *)&s->config;for(size_t i=0;i<sizeof s->config;i++)p[i]=0;}
static void finish(time_sync_t *s,pn_status_t cause,int sdk){s->state.phase=PN_TIME_STOPPING;s->state.error=cause;s->state.sdk_error=sdk;}
static void cleanup(time_sync_t *s){
 if(s->sntp){esp_netif_sntp_deinit();s->sntp=false;}
 if(pn_device_wifi_close(&s->wifi)!=PN_OK)return;
 wipe(s);s->state.released=true;
 s->state.phase=s->cancel?PN_TIME_CANCELLED:s->state.error==PN_OK?PN_TIME_SUCCEEDED:PN_TIME_FAILED;
 if(s->state.phase!=PN_TIME_SUCCEEDED)s->state.utc=0;
}
pn_status_t pn_device_time_open(pn_device_time_t *out,const pn_device_wifi_config_t *config){
 if(!out || !config || config->mode!=PN_WIFI_STA || !memchr(config->ssid,0,sizeof config->ssid) || !memchr(config->password,0,sizeof config->password))return PN_INVALID;
 if(out->impl)return PN_BUSY;
 pn_network_credentials_t credentials;memcpy(credentials.ssid,config->ssid,sizeof credentials.ssid);memcpy(credentials.password,config->password,sizeof credentials.password);
 pn_status_t valid=pn_network_validate(&credentials,false);pn_network_wipe(&credentials);if(valid!=PN_OK)return valid;
#ifdef ESP_PLATFORM
 time_sync_t *s=heap_caps_malloc(sizeof *s,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
 time_sync_t *s=malloc(sizeof *s);
#endif
 if(!s)return PN_NO_MEMORY;
 *s=(time_sync_t){.state={.phase=PN_TIME_CONNECTING},.config=*config};out->impl=s;return PN_OK;
}
pn_status_t pn_device_time_poll(pn_device_time_t *out,uint64_t now){
 if(!out || !out->impl)return PN_INVALID;
 time_sync_t *s=out->impl;if(s->state.released)return PN_OK;
 if(s->cancel && s->state.phase!=PN_TIME_STOPPING)finish(s,PN_CANCELLED,ESP_OK);
 if(s->state.phase==PN_TIME_STOPPING){cleanup(s);return PN_OK;}
 if(!s->started){s->started=true;pn_status_t status=pn_device_wifi_open(&s->wifi,&s->config);wipe(s);if(status!=PN_OK){finish(s,status,ESP_FAIL);cleanup(s);return PN_OK;}}
 pn_status_t status=pn_device_wifi_poll(&s->wifi);pn_device_wifi_state_t network={0};
 pn_status_t snapshot=pn_device_wifi_state(&s->wifi,&network);if(status==PN_OK)status=snapshot;
 if(status!=PN_OK){finish(s,status,network.error);cleanup(s);return PN_OK;}
 if(s->state.phase==PN_TIME_CONNECTING && network.phase==PN_WIFI_READY){
  esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_err_t error=esp_netif_sntp_init(&config);
  if(error!=ESP_OK){finish(s,PN_IO,error);cleanup(s);return PN_OK;}
  s->sntp=true;s->deadline=now+12000u;s->state.phase=PN_TIME_WAITING;
 }
 if(s->state.phase==PN_TIME_WAITING){
  // 0 ticks不阻塞UI，只有收到本次SNTP确认才采纳UTC；不拿旧系统时钟冒充成功。 / Zero ticks never blocks UI; accept UTC only after this SNTP confirmation, never from an old system clock alone.
  esp_err_t error=esp_netif_sntp_sync_wait(0);
  if(error==ESP_OK){time_t utc=time(NULL);if(utc<1704067200 || (uint64_t)utc>4102444799u)finish(s,PN_CORRUPT,ESP_FAIL);else{s->state.utc=(uint32_t)utc;s->state.acquired_ms=now;finish(s,PN_OK,ESP_OK);}cleanup(s);}
  else if(error!=ESP_ERR_TIMEOUT && error!=ESP_ERR_NOT_FINISHED){finish(s,PN_IO,error);cleanup(s);}
  else if(now>=s->deadline){finish(s,PN_IO,ESP_ERR_TIMEOUT);cleanup(s);}
 }
 return PN_OK;
}
pn_status_t pn_device_time_request_stop(pn_device_time_t *out){if(!out || !out->impl)return PN_INVALID;time_sync_t *s=out->impl;if(!s->state.released){s->cancel=true;if(s->state.error==PN_OK)s->state.error=PN_CANCELLED;}return PN_OK;}
pn_status_t pn_device_time_state(const pn_device_time_t *out,pn_device_time_state_t *state){if(!out || !out->impl || !state)return PN_INVALID;*state=((time_sync_t *)out->impl)->state;return PN_OK;}
pn_status_t pn_device_time_close(pn_device_time_t *out){if(!out)return PN_INVALID;if(!out->impl)return PN_OK;time_sync_t *s=out->impl;if(!s->state.released)return PN_BUSY;wipe(s);
#ifdef ESP_PLATFORM
 heap_caps_free(s);
#else
 free(s);
#endif
 out->impl=NULL;return PN_OK;}
