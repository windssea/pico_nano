/* SPDX-License-Identifier: Apache-2.0
 * 中文：WiFi连接测试由短轮询推进；成功、失败或取消都先完整关闭无线再交出结果。
 * English: short polls advance the WiFi connection test; success, failure and cancellation all close the radio fully before reporting. */
#include "device_wifi_probe.h"
#include "pn_network_store.h"
#include "esp_wifi.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#define PROBE_LIMIT_MS 30000u
typedef struct {
 pn_device_wifi_probe_state_t state;
 pn_device_wifi_t wifi;
 pn_device_wifi_config_t config;
 uint64_t deadline;
 bool started,stopping,cancel;
} probe_t;
static void wipe(probe_t *s){volatile unsigned char *p=(volatile unsigned char *)&s->config;for(size_t i=0;i<sizeof s->config;i++)p[i]=0;}
static void finish(probe_t *s,pn_status_t result){if(!s->stopping){s->stopping=true;s->state.result=result;}}
pn_status_t pn_device_wifi_probe_classify(unsigned reason){
 switch(reason){
 case WIFI_REASON_NO_AP_FOUND:case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:return PN_EMPTY;
 case WIFI_REASON_AUTH_FAIL:case WIFI_REASON_AUTH_EXPIRE:case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:case WIFI_REASON_HANDSHAKE_TIMEOUT:case WIFI_REASON_MIC_FAILURE:case WIFI_REASON_802_1X_AUTH_FAILED:return PN_INVALID;
 default:return PN_IO;
 }
}
pn_status_t pn_device_wifi_probe_open(pn_device_wifi_probe_t *out,const pn_device_wifi_config_t *config){
 if(!out || !config || config->mode!=PN_WIFI_STA || !memchr(config->ssid,0,sizeof config->ssid) || !memchr(config->password,0,sizeof config->password))return PN_INVALID;
 if(out->impl)return PN_BUSY;
 pn_network_credentials_t c;memcpy(c.ssid,config->ssid,sizeof c.ssid);memcpy(c.password,config->password,sizeof c.password);
 pn_status_t valid=pn_network_validate(&c,false);pn_network_wipe(&c);if(valid!=PN_OK)return valid;
#ifdef ESP_PLATFORM
 probe_t *s=heap_caps_calloc(1,sizeof *s,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
 probe_t *s=calloc(1,sizeof *s);
#endif
 if(!s)return PN_NO_MEMORY;
 s->config=*config;s->state.result=PN_IO;out->impl=s;return PN_OK;
}
pn_status_t pn_device_wifi_probe_poll(pn_device_wifi_probe_t *out,uint64_t now){
 if(!out || !out->impl)return PN_INVALID;
 probe_t *s=out->impl;if(s->state.released)return PN_OK;
 if(s->cancel)finish(s,PN_CANCELLED);
 if(!s->stopping && !s->started){
  s->started=true;s->deadline=now+PROBE_LIMIT_MS;pn_status_t e=pn_device_wifi_open(&s->wifi,&s->config);wipe(s);
  if(e!=PN_OK)finish(s,PN_IO);
 }
 if(!s->stopping){
  pn_status_t e=pn_device_wifi_poll(&s->wifi);pn_device_wifi_state_t w={0};(void)pn_device_wifi_state(&s->wifi,&w);s->state.reason=w.last_disconnect_reason;
  // 拿到IP才算密码与DHCP都可用；失败按最近断开原因归类。/ Only an IP address proves the password and DHCP work; failures use the latest disconnect reason.
  if(w.phase==PN_WIFI_READY)finish(s,PN_OK);
  else if(e!=PN_OK || w.phase==PN_WIFI_FAILED)finish(s,pn_device_wifi_probe_classify(w.last_disconnect_reason));
  else if(now>=s->deadline)finish(s,w.last_disconnect_reason?pn_device_wifi_probe_classify(w.last_disconnect_reason):PN_IO);
 }
 if(s->stopping && pn_device_wifi_close(&s->wifi)==PN_OK){wipe(s);s->state.released=true;}
 return PN_OK;
}
pn_status_t pn_device_wifi_probe_stop(pn_device_wifi_probe_t *out){if(!out || !out->impl)return PN_INVALID;((probe_t *)out->impl)->cancel=true;return PN_OK;}
pn_status_t pn_device_wifi_probe_state(const pn_device_wifi_probe_t *out,pn_device_wifi_probe_state_t *state){if(!out || !out->impl || !state)return PN_INVALID;*state=((const probe_t *)out->impl)->state;return PN_OK;}
pn_status_t pn_device_wifi_probe_close(pn_device_wifi_probe_t *out){
 if(!out)return PN_INVALID;
 if(!out->impl)return PN_OK;
 probe_t *s=out->impl;if(!s->state.released)return PN_BUSY;
 wipe(s);
#ifdef ESP_PLATFORM
 heap_caps_free(s);
#else
 free(s);
#endif
 out->impl=NULL;return PN_OK;
}
