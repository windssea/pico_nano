/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：后台监督传输生命周期，按HTTP、文件任务、无线顺序归还资源。
 * English: supervise transfer lifecycle in the background, returning HTTP, file-task and wireless resources in order.
 * 冻结：不绘制/挂载/访问阅读设置，停止未完成不能归还media。
 * Frozen: no painting, mounting or reading-settings access; never return media before stopping finishes.
 */
#define _POSIX_C_SOURCE 200809L
#include "device_transfer.h"
#include "device_transfer_http.h"
#include "device_upload_storage.h"
#include "read_pico_sd.h"
#include <string.h>
#include <stdio.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#else
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#endif
typedef struct {
 pn_media_t *media;
 char root[PN_UPLOAD_FILES_ROOT_MAX];
 pn_device_wifi_config_t config;
 pn_device_upload_storage_t storage;
 pn_transfer_worker_t worker;
 pn_device_wifi_t wifi;
 pn_device_transfer_http_t http;
 pn_device_transfer_state_t state;
 bool stop,retry;
#ifdef ESP_PLATFORM
 SemaphoreHandle_t mutex;
 TaskHandle_t task;
#else
 pthread_mutex_t mutex;
 pthread_t task;
#endif
} transfer_t;
#ifdef ESP_PLATFORM
static void lock(transfer_t *s){xSemaphoreTake(s->mutex,portMAX_DELAY);}
static void unlock(transfer_t *s){xSemaphoreGive(s->mutex);}
static void pause_task(void){vTaskDelay(pdMS_TO_TICKS(20));}
static void *allocate(size_t n){return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
static void release(void *p){heap_caps_free(p);}
static void destroy(transfer_t *s){vSemaphoreDelete(s->mutex);}
#else
static void lock(transfer_t *s){pthread_mutex_lock(&s->mutex);}
static void unlock(transfer_t *s){pthread_mutex_unlock(&s->mutex);}
static void pause_task(void){struct timespec t={.tv_nsec=20000000};nanosleep(&t,NULL);}
static void *allocate(size_t n){return malloc(n);}
static void release(void *p){free(p);}
static void destroy(transfer_t *s){pthread_mutex_destroy(&s->mutex);}
#endif
static bool stopping(transfer_t *s){lock(s);bool value=s->stop;unlock(s);return value;}
static pn_status_t cleanup(transfer_t *s){
 pn_status_t status=PN_OK;
 if(s->http.impl){status=pn_device_transfer_http_close(&s->http);if(status!=PN_OK)return status;}
 if(s->worker.impl){status=pn_transfer_worker_close(&s->worker);if(status!=PN_OK)return status;}
 if(s->wifi.impl){status=pn_device_wifi_close(&s->wifi);if(status!=PN_OK)return status;}
 return pn_media_active(s->media)?PN_BUSY:PN_OK;
}
static void run(transfer_t *s){
 pn_upload_files_options_t options;
 pn_status_t cause=pn_device_upload_storage_options(&s->storage,s->media,s->root,&options);
 if(cause==PN_OK && !stopping(s))cause=pn_transfer_worker_open(&s->worker,s->media,&options,6u*1024u*1024u,NULL,NULL,NULL);
 if(cause==PN_OK && !stopping(s))cause=pn_device_wifi_open(&s->wifi,&s->config);
 volatile uint8_t *secret=(volatile uint8_t *)s->config.password;for(size_t i=0;i<sizeof s->config.password;i++)secret[i]=0;
 uint64_t network_generation=0;
 while(cause==PN_OK && !stopping(s)){
  read_pico_sd_info_t card={0};(void)read_pico_sd_get_info(&card);
  if(!card.present || !card.mounted){cause=PN_STALE_MEDIA;break;}
  cause=pn_device_wifi_poll(&s->wifi);if(cause!=PN_OK)break;
  pn_device_wifi_state_t network;cause=pn_device_wifi_state(&s->wifi,&network);if(cause!=PN_OK)break;
  if(s->http.impl && (network.phase!=PN_WIFI_READY || network.generation!=network_generation)){cause=PN_STALE_JOB;break;}
  if(!s->http.impl && network.phase==PN_WIFI_READY && !stopping(s)){
   cause=pn_device_transfer_http_open(&s->http,&s->worker,network.address,80);
   if(cause!=PN_OK)break;
   network_generation=network.generation;
  }
  pn_transfer_worker_state_t work={0};if(s->worker.impl)(void)pn_transfer_worker_state(&s->worker,&work);
  char pin[7]={0};if(s->http.impl)(void)pn_device_transfer_http_code(&s->http,pin);
  lock(s);s->state.network=network;s->state.work=work;
  if(!s->stop){s->state.phase=s->http.impl?PN_DTRANSFER_READY:PN_DTRANSFER_STARTING;memcpy(s->state.pin,pin,sizeof pin);}
  unlock(s);pause_task();
 }
 lock(s);s->stop=true;s->state.phase=PN_DTRANSFER_STOPPING;memset(s->state.pin,0,sizeof s->state.pin);unlock(s);
 if(s->http.impl)(void)pn_device_transfer_http_request_stop(&s->http);
 else if(s->worker.impl)(void)pn_transfer_worker_request_stop(&s->worker);
 for(;;){
  pn_status_t status=cleanup(s);
  lock(s);s->state.error=status==PN_OK?cause:status;
  if(status==PN_OK){s->state.released=true;s->state.phase=cause==PN_OK?PN_DTRANSFER_STOPPED:PN_DTRANSFER_FAILED;unlock(s);break;}
  s->state.phase=PN_DTRANSFER_FAILED;s->retry=false;unlock(s);
  for(;;){pause_task();lock(s);bool retry=s->retry;unlock(s);if(retry)break;}
 }
 // 此后不再访问上下文，调用方在released后join/释放。/ No further context access; caller joins/releases after released.
}
#ifdef ESP_PLATFORM
static void entry(void *ctx){run(ctx);vTaskDelete(NULL);}
#else
static void *entry(void *ctx){run(ctx);return NULL;}
#endif
pn_status_t pn_device_transfer_open(pn_device_transfer_t *out,pn_media_t *media,const char *root,const pn_device_wifi_config_t *config){
 if(!out || !media || !root || !config || strlen(root)>=PN_UPLOAD_FILES_ROOT_MAX)return PN_INVALID;
 if(out->impl || pn_media_active(media))return PN_BUSY;
 if(!media->available)return PN_STALE_MEDIA;
 transfer_t *s=allocate(sizeof *s);if(!s)return PN_NO_MEMORY;memset(s,0,sizeof *s);s->media=media;s->config=*config;strcpy(s->root,root);s->state.phase=PN_DTRANSFER_STARTING;
#ifdef ESP_PLATFORM
 s->mutex=xSemaphoreCreateMutex();if(!s->mutex){release(s);return PN_NO_MEMORY;}
 bool created=xTaskCreatePinnedToCore(entry,"pn_transfer_ctl",16384,s,4,&s->task,0)==pdPASS;
#else
 if(pthread_mutex_init(&s->mutex,NULL)){release(s);return PN_NO_MEMORY;}
 bool created=pthread_create(&s->task,NULL,entry,s)==0;
#endif
 if(!created){destroy(s);release(s);return PN_NO_MEMORY;}
 out->impl=s;return PN_OK;
}
pn_status_t pn_device_transfer_request_stop(pn_device_transfer_t *out){if(!out || !out->impl)return PN_INVALID;transfer_t *s=out->impl;lock(s);s->stop=true;s->retry=true;if(!s->state.released)s->state.phase=PN_DTRANSFER_STOPPING;memset(s->state.pin,0,sizeof s->state.pin);unlock(s);return PN_OK;}
pn_status_t pn_device_transfer_state(pn_device_transfer_t *out,pn_device_transfer_state_t *state){if(!out || !out->impl || !state)return PN_INVALID;transfer_t *s=out->impl;lock(s);*state=s->state;unlock(s);return PN_OK;}
pn_status_t pn_device_transfer_close(pn_device_transfer_t *out){
 if(!out)return PN_INVALID;
 if(!out->impl)return PN_OK;
 transfer_t *s=out->impl;lock(s);bool ready=s->state.released;unlock(s);if(!ready)return PN_BUSY;
#ifndef ESP_PLATFORM
 pthread_join(s->task,NULL);
#endif
 destroy(s);volatile uint8_t *bytes=(volatile uint8_t *)s;for(size_t i=0;i<sizeof *s;i++)bytes[i]=0;release(s);out->impl=NULL;return PN_OK;
}
