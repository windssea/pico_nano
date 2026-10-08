/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：会话级WiFi驱动与事件生命周期，不读取或写入持久凭据。
 * English: session-level WiFi driver/event lifecycle without persistent credential access.
 * 冻结：单owner调用控制API；事件只更新状态，不自动切AP/STA或操作TF。
 * Frozen: one owner calls control APIs; events only update state, never switch modes or access TF.
 */
#define _POSIX_C_SOURCE 200809L
#include "device_wifi.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "bootloader_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
typedef struct {
 SemaphoreHandle_t mutex;
 esp_netif_t *netif;
 esp_event_handler_instance_t wifi_events,ip_events;
 pn_device_wifi_state_t state;
 bool loop_owned,initialized,started,start_attempted,closing,ap_ready,pending_connect;
 uint64_t window,next_retry;
} wifi_t;
static wifi_t *active;
static uint64_t now_ms(void){return (uint64_t)(esp_timer_get_time()/1000);}
static void lock(wifi_t *s){xSemaphoreTake(s->mutex,portMAX_DELAY);}
static void unlock(wifi_t *s){xSemaphoreGive(s->mutex);}
static void clear_secret(void *value,size_t n){volatile uint8_t *p=value;while(n--)*p++=0;}
static bool changed(wifi_t *s){if(s->state.generation==UINT64_MAX){s->state.phase=PN_WIFI_FAILED;s->state.error=ESP_FAIL;return false;}s->state.generation++;return true;}
static void address(char out[16],uint32_t ip){const uint8_t *bytes=(const uint8_t *)&ip;snprintf(out,16,"%u.%u.%u.%u",bytes[0],bytes[1],bytes[2],bytes[3]);}
static void disconnected(wifi_t *s){
 if(s->state.phase==PN_WIFI_READY){s->window=now_ms();s->state.attempts=0;}
 s->state.phase=PN_WIFI_CONNECTING;s->state.address[0]=0;s->pending_connect=true;s->next_retry=now_ms()+1000;(void)changed(s);
}
static void event(void *ctx,esp_event_base_t base,int32_t id,void *data){
 wifi_t *s=ctx;lock(s);if(s->closing || s->state.phase==PN_WIFI_FAILED){unlock(s);return;}
 if(base==WIFI_EVENT){
  if(s->state.mode==PN_WIFI_AP){
   if(id==WIFI_EVENT_AP_START)s->ap_ready=true;
   else if(id==WIFI_EVENT_AP_STOP){s->state.phase=PN_WIFI_FAILED;s->state.address[0]=0;s->state.clients=0;s->state.error=ESP_FAIL;(void)changed(s);}
   else if(id==WIFI_EVENT_AP_STACONNECTED && s->state.clients<2)s->state.clients++;
   else if(id==WIFI_EVENT_AP_STADISCONNECTED && s->state.clients)s->state.clients--;
  }else{
   if(id==WIFI_EVENT_STA_START){s->state.phase=PN_WIFI_CONNECTING;s->pending_connect=true;}
   else if(id==WIFI_EVENT_STA_DISCONNECTED)disconnected(s);
  }
 }else if(base==IP_EVENT && s->state.mode==PN_WIFI_STA){
  if(id==IP_EVENT_STA_GOT_IP && data){const ip_event_got_ip_t *got=data;if(got->ip_info.ip.addr){address(s->state.address,got->ip_info.ip.addr);s->state.phase=PN_WIFI_READY;s->state.error=ESP_OK;s->pending_connect=false;(void)changed(s);}}
  else if(id==IP_EVENT_STA_LOST_IP)disconnected(s);
 }
 unlock(s);
}
pn_status_t pn_device_wifi_close(pn_device_wifi_t *out){
 if(!out)return PN_INVALID;
 if(!out->impl)return PN_OK;
 wifi_t *s=out->impl;lock(s);s->closing=true;s->state.phase=PN_WIFI_STOPPING;s->state.address[0]=0;unlock(s);
 if(s->start_attempted){esp_err_t result=esp_wifi_stop();if(result!=ESP_OK && !(result==ESP_ERR_WIFI_NOT_STARTED && !s->started))return PN_IO;s->started=false;s->start_attempted=false;}
 if(s->ip_events){if(esp_event_handler_instance_unregister(IP_EVENT,ESP_EVENT_ANY_ID,s->ip_events)!=ESP_OK)return PN_IO;s->ip_events=NULL;}
 if(s->wifi_events){if(esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,s->wifi_events)!=ESP_OK)return PN_IO;s->wifi_events=NULL;}
 if(s->initialized){if(esp_wifi_deinit()!=ESP_OK)return PN_IO;s->initialized=false;}
 if(s->netif){if(esp_wifi_clear_default_wifi_driver_and_handlers(s->netif)!=ESP_OK)return PN_IO;esp_netif_destroy(s->netif);s->netif=NULL;}
 if(s->loop_owned){if(esp_event_loop_delete_default()!=ESP_OK)return PN_IO;s->loop_owned=false;}
 vSemaphoreDelete(s->mutex);clear_secret(s,sizeof *s);heap_caps_free(s);active=NULL;out->impl=NULL;return PN_OK;
}
pn_status_t pn_device_wifi_open(pn_device_wifi_t *out,const pn_device_wifi_config_t *config){
 if(!out || !config || (config->mode!=PN_WIFI_AP && config->mode!=PN_WIFI_STA))return PN_INVALID;
 if(out->impl || active)return PN_BUSY;
 if(config->mode==PN_WIFI_STA){
  if(!memchr(config->ssid,0,sizeof config->ssid) || !memchr(config->password,0,sizeof config->password))return PN_INVALID;
  size_t n=strlen(config->ssid),p=strlen(config->password);if(!n || n>32 || (p && p<8))return PN_INVALID;
  for(size_t i=0;i<n;i++)if((unsigned char)config->ssid[i]<32 || config->ssid[i]==127)return PN_INVALID;
  for(size_t i=0;i<p;i++)if((unsigned char)config->password[i]<32 || (unsigned char)config->password[i]>126)return PN_INVALID;
 }
 wifi_mode_t previous;if(esp_wifi_get_mode(&previous)!=ESP_ERR_WIFI_NOT_INIT)return PN_BUSY;
 if(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") || esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"))return PN_BUSY;
 wifi_t *s=heap_caps_malloc(sizeof *s,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!s)return PN_NO_MEMORY;memset(s,0,sizeof *s);
 s->mutex=xSemaphoreCreateMutex();if(!s->mutex){heap_caps_free(s);return PN_NO_MEMORY;}
 s->state.mode=config->mode;s->state.phase=PN_WIFI_STARTING;s->state.generation=1;s->window=now_ms();out->impl=s;active=s;
 wifi_config_t settings={0};esp_err_t result=ESP_OK;
 if(config->mode==PN_WIFI_AP){
  uint8_t random[12],mac[6];bootloader_random_enable();esp_fill_random(random,sizeof random);bootloader_random_disable();
  static const char alphabet[]="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  for(unsigned i=0;i<12;i++)s->state.ap_password[i]=alphabet[random[i]&31];
  clear_secret(random,sizeof random);
  result=esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP);
  if(result==ESP_OK){snprintf(s->state.ssid,sizeof s->state.ssid,"小纸 Pico-%02X%02X",mac[4],mac[5]);memcpy(settings.ap.ssid,s->state.ssid,strlen(s->state.ssid));memcpy(settings.ap.password,s->state.ap_password,12);settings.ap.ssid_len=(uint8_t)strlen(s->state.ssid);settings.ap.channel=1;settings.ap.max_connection=2;settings.ap.authmode=WIFI_AUTH_WPA2_PSK;settings.ap.pairwise_cipher=WIFI_CIPHER_TYPE_CCMP;}
 }else{
  strcpy(s->state.ssid,config->ssid);memcpy(settings.sta.ssid,config->ssid,strlen(config->ssid));memcpy(settings.sta.password,config->password,strlen(config->password));settings.sta.threshold.authmode=config->password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;settings.sta.pmf_cfg.capable=true;settings.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
 }
 if(result==ESP_OK)result=esp_netif_init();
 if(result==ESP_OK){result=esp_event_loop_create_default();if(result==ESP_OK)s->loop_owned=true;else if(result==ESP_ERR_INVALID_STATE)result=ESP_OK;}
 if(result==ESP_OK){
  esp_netif_config_t net=ESP_NETIF_DEFAULT_WIFI_AP();
  if(config->mode==PN_WIFI_STA){esp_netif_config_t station=ESP_NETIF_DEFAULT_WIFI_STA();net=station;}
  s->netif=esp_netif_new(&net);if(!s->netif)result=ESP_FAIL;
  if(result==ESP_OK)result=config->mode==PN_WIFI_AP?esp_netif_attach_wifi_ap(s->netif):esp_netif_attach_wifi_station(s->netif);
  if(result==ESP_OK)result=config->mode==PN_WIFI_AP?esp_wifi_set_default_wifi_ap_handlers():esp_wifi_set_default_wifi_sta_handlers();
 }
 if(result==ESP_OK){wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();init.nvs_enable=0;result=esp_wifi_init(&init);if(result==ESP_OK)s->initialized=true;}
 if(result==ESP_OK)result=esp_wifi_set_storage(WIFI_STORAGE_RAM);
 if(result==ESP_OK)result=esp_wifi_set_mode(config->mode==PN_WIFI_AP?WIFI_MODE_AP:WIFI_MODE_STA);
 if(result==ESP_OK)result=esp_wifi_set_config(config->mode==PN_WIFI_AP?WIFI_IF_AP:WIFI_IF_STA,&settings);
 clear_secret(&settings,sizeof settings);
 if(result==ESP_OK)result=esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,s,&s->wifi_events);
 if(result==ESP_OK && config->mode==PN_WIFI_STA)result=esp_event_handler_instance_register(IP_EVENT,ESP_EVENT_ANY_ID,event,s,&s->ip_events);
 if(result==ESP_OK){s->start_attempted=true;result=esp_wifi_start();if(result==ESP_OK)s->started=true;}
 if(result!=ESP_OK){lock(s);s->state.phase=PN_WIFI_FAILED;s->state.error=result;unlock(s);(void)pn_device_wifi_close(out);return PN_IO;}
 return PN_OK;
}
pn_status_t pn_device_wifi_poll(pn_device_wifi_t *out){
 if(!out || !out->impl)return PN_INVALID;
 wifi_t *s=out->impl;uint64_t now=now_ms();lock(s);
 if(s->closing){unlock(s);return PN_CANCELLED;}
 if(s->state.phase==PN_WIFI_FAILED){unlock(s);return PN_IO;}
 if(s->state.phase==PN_WIFI_READY){unlock(s);return PN_OK;}
 if(now<s->window || now-s->window>=20000){s->state.phase=PN_WIFI_FAILED;s->state.error=ESP_FAIL;unlock(s);return PN_IO;}
 bool ap=s->state.mode==PN_WIFI_AP && s->ap_ready;
 bool connect=s->state.mode==PN_WIFI_STA && s->pending_connect && now>=s->next_retry;
 if(connect){if(s->state.attempts>=3){s->state.phase=PN_WIFI_FAILED;s->state.error=ESP_FAIL;unlock(s);return PN_IO;}s->state.attempts++;s->pending_connect=false;s->next_retry=now+1000;}
 unlock(s);esp_err_t result=ESP_OK;
 if(ap){esp_netif_ip_info_t info={0};result=esp_netif_get_ip_info(s->netif,&info);lock(s);if(result==ESP_OK && info.ip.addr && !s->closing && s->state.phase==PN_WIFI_STARTING){address(s->state.address,info.ip.addr);s->state.phase=PN_WIFI_READY;(void)changed(s);}unlock(s);}
 if(connect)result=esp_wifi_connect();
 if(result!=ESP_OK){lock(s);if(!s->closing && (s->state.phase==PN_WIFI_STARTING || s->state.phase==PN_WIFI_CONNECTING)){s->state.error=result;if(connect)s->pending_connect=true;}unlock(s);}
 return PN_OK;
}
pn_status_t pn_device_wifi_state(pn_device_wifi_t *out,pn_device_wifi_state_t *state){if(!out || !out->impl || !state)return PN_INVALID;wifi_t *s=out->impl;lock(s);*state=s->state;unlock(s);return PN_OK;}
