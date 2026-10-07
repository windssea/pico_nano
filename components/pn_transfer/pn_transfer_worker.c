/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：单槽传输任务，独立事务池；停止只关闭接收，不强杀文件消费者。
 * English: single-slot transfer task with a private transaction pool; stopping closes admission without killing file consumers.
 * 冻结：media必须完整移交，关闭前先join调用方；任务栈不作为整本缓存。
 * Frozen: fully hand off media, join callers before close, never cache whole books on the task stack.
 */
#include "pn_transfer_worker.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#else
#include <pthread.h>
#endif
typedef struct {
 pn_pool_t pool;
 pn_media_t *media;
 pn_upload_files_options_t options;
 char root[PN_UPLOAD_FILES_ROOT_MAX];
 pn_transfer_service_t service;
 pn_transfer_command_t command;
 pn_transfer_reply_t reply;
 uint8_t bytes[PN_UPLOAD_CHUNK];
 pn_transfer_worker_state_t state;
 pn_status_t startup,result;
 bool started,closing,finished,pending,job_done;
 pn_free_fn release;
 void *allocator_ctx;
#ifdef ESP_PLATFORM
 SemaphoreHandle_t mutex,call,wake,changed;
 TaskHandle_t task;
#else
 pthread_mutex_t mutex,call;
 pthread_cond_t changed;
 pthread_t task;
#endif
} worker_t;
static void *default_allocate(void *ctx,size_t n){
 (void)ctx;
#ifdef ESP_PLATFORM
 return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
 return malloc(n);
#endif
}
static void default_release(void *ctx,void *ptr){
 (void)ctx;
#ifdef ESP_PLATFORM
 heap_caps_free(ptr);
#else
 free(ptr);
#endif
}
#ifdef ESP_PLATFORM
static void lock(worker_t *w){xSemaphoreTake(w->mutex,portMAX_DELAY);}
static void unlock(worker_t *w){xSemaphoreGive(w->mutex);}
static void changed(worker_t *w){xSemaphoreGive(w->changed);}
static void wake(worker_t *w){xSemaphoreGive(w->wake);}
static void wait_changed(worker_t *w){unlock(w);xSemaphoreTake(w->changed,portMAX_DELAY);lock(w);}
static void wait_work(worker_t *w){unlock(w);xSemaphoreTake(w->wake,portMAX_DELAY);lock(w);}
static bool take_call(worker_t *w,bool wait){return xSemaphoreTake(w->call,wait?portMAX_DELAY:0)==pdTRUE;}
static void release_call(worker_t *w){xSemaphoreGive(w->call);}
static void join(worker_t *w){(void)w;}
static void destroy(worker_t *w){if(w->changed)vSemaphoreDelete(w->changed);if(w->wake)vSemaphoreDelete(w->wake);if(w->call)vSemaphoreDelete(w->call);if(w->mutex)vSemaphoreDelete(w->mutex);}
static bool initialize(worker_t *w){w->mutex=xSemaphoreCreateMutex();w->call=xSemaphoreCreateMutex();w->wake=xSemaphoreCreateBinary();w->changed=xSemaphoreCreateBinary();return w->mutex && w->call && w->wake && w->changed;}
#else
static void lock(worker_t *w){pthread_mutex_lock(&w->mutex);}
static void unlock(worker_t *w){pthread_mutex_unlock(&w->mutex);}
static void changed(worker_t *w){pthread_cond_broadcast(&w->changed);}
static void wake(worker_t *w){changed(w);}
static void wait_changed(worker_t *w){pthread_cond_wait(&w->changed,&w->mutex);}
static void wait_work(worker_t *w){wait_changed(w);}
static bool take_call(worker_t *w,bool wait){return (wait?pthread_mutex_lock(&w->call):pthread_mutex_trylock(&w->call))==0;}
static void release_call(worker_t *w){pthread_mutex_unlock(&w->call);}
static void join(worker_t *w){pthread_join(w->task,NULL);}
static void destroy(worker_t *w){pthread_cond_destroy(&w->changed);pthread_mutex_destroy(&w->call);pthread_mutex_destroy(&w->mutex);}
static bool initialize(worker_t *w){
 if(pthread_mutex_init(&w->mutex,NULL))return false;
 if(pthread_mutex_init(&w->call,NULL)){pthread_mutex_destroy(&w->mutex);return false;}
 if(pthread_cond_init(&w->changed,NULL)){pthread_mutex_destroy(&w->call);pthread_mutex_destroy(&w->mutex);return false;}
 return true;
}
#endif
static void run(worker_t *w){
 pn_status_t status=pn_transfer_service_open(&w->service,&w->pool,w->media,&w->options);
 lock(w);w->startup=status;w->started=true;w->state.error=status;
 w->state.phase=status==PN_OK?PN_TWORK_ACTIVE:PN_TWORK_FAILED;changed(w);
 if(status==PN_OK){
  for(;;){
   while(!w->pending && !w->closing)wait_work(w);
   if(!w->pending)break;
   w->pending=false;unlock(w);
   pn_transfer_reply_t reply;
   status=pn_transfer_service_execute(&w->service,&w->command,&reply);
   lock(w);w->result=status;if(status==PN_OK)w->reply=reply;
   w->state.busy=false;w->state.pool_peak=w->pool.peak;w->job_done=true;changed(w);
  }
 }
 unlock(w);
 pn_status_t closed=pn_transfer_service_close(&w->service);
 if(closed==PN_OK && (w->pool.used || w->pool.live || pn_media_active(w->media)))closed=PN_CORRUPT;
 lock(w);w->state.error=w->startup==PN_OK?closed:w->startup;
 w->state.phase=w->state.error==PN_OK?PN_TWORK_STOPPED:PN_TWORK_FAILED;
 w->state.pool_peak=w->pool.peak;w->finished=true;changed(w);unlock(w);
 // 解锁后不再访问w；FreeRTOS栈由idle回收，存储对象已停止。
 // Never access w after unlocking; FreeRTOS idle reclaims the stack after storage has stopped.
}
#ifdef ESP_PLATFORM
static void entry(void *ctx){run(ctx);vTaskDelete(NULL);}
static bool start(worker_t *w){return xTaskCreatePinnedToCore(entry,"pn_transfer",32768,w,4,&w->task,0)==pdPASS;}
#else
static void *entry(void *ctx){run(ctx);return NULL;}
static bool start(worker_t *w){return pthread_create(&w->task,NULL,entry,w)==0;}
#endif
pn_status_t pn_transfer_worker_open(pn_transfer_worker_t *out,pn_media_t *media,const pn_upload_files_options_t *options,size_t limit,pn_malloc_fn allocate,pn_free_fn release,void *ctx){
 if(!out || !media || !options || !options->root || !limit || (!allocate!=!release))return PN_INVALID;
 if(out->impl || pn_media_active(media))return PN_BUSY;
 if(!media->available)return PN_STALE_MEDIA;
 size_t n=strlen(options->root);if(n>=PN_UPLOAD_FILES_ROOT_MAX)return PN_LIMIT;
 if(!allocate){allocate=default_allocate;release=default_release;}
 worker_t *w=allocate(ctx,sizeof *w);if(!w)return PN_NO_MEMORY;
 memset(w,0,sizeof *w);w->release=release;w->allocator_ctx=ctx;w->media=media;w->options=*options;
 memcpy(w->root,options->root,n+1);w->options.root=w->root;w->state.phase=PN_TWORK_STARTING;
 if(pn_pool_init(&w->pool,limit,allocate,release,ctx)!=PN_OK){release(ctx,w);return PN_INVALID;}
 if(!initialize(w)){
#ifdef ESP_PLATFORM
  destroy(w);
#endif
  release(ctx,w);return PN_NO_MEMORY;
 }
 if(!start(w)){destroy(w);release(ctx,w);return PN_NO_MEMORY;}
 lock(w);while(!w->started)wait_changed(w);pn_status_t status=w->startup;
 if(status!=PN_OK){while(!w->finished)wait_changed(w);unlock(w);join(w);destroy(w);release(ctx,w);return status;}
 unlock(w);out->impl=w;return PN_OK;
}
pn_status_t pn_transfer_worker_execute(pn_transfer_worker_t *out,const pn_transfer_command_t *command,pn_transfer_reply_t *reply){
 if(!out || !out->impl || !command || !reply || command->operation==PN_TRANSFER_STOP)return PN_INVALID;
 if(command->operation==PN_TRANSFER_CHUNK && (!command->bytes || !command->length || command->length>PN_UPLOAD_CHUNK))return PN_INVALID;
 worker_t *w=out->impl;lock(w);bool closing=w->closing || w->finished;unlock(w);
 if(closing)return PN_CANCELLED;
 if(!take_call(w,false))return PN_BUSY;
 lock(w);
 if(w->closing || w->finished){unlock(w);release_call(w);return PN_CANCELLED;}
 w->command=*command;
 if(command->operation==PN_TRANSFER_CHUNK){memcpy(w->bytes,command->bytes,command->length);w->command.bytes=w->bytes;}
 w->job_done=false;w->pending=true;w->state.busy=true;wake(w);
 while(!w->job_done)wait_changed(w);
 pn_status_t status=w->result;if(status==PN_OK)*reply=w->reply;
 unlock(w);release_call(w);return status;
}
pn_status_t pn_transfer_worker_request_stop(pn_transfer_worker_t *out){
 if(!out || !out->impl)return PN_INVALID;
 worker_t *w=out->impl;lock(w);w->closing=true;
 if(!w->finished)w->state.phase=PN_TWORK_STOPPING;
 wake(w);unlock(w);return PN_OK;
}
pn_status_t pn_transfer_worker_state(pn_transfer_worker_t *out,pn_transfer_worker_state_t *state){
 if(!out || !out->impl || !state)return PN_INVALID;
 worker_t *w=out->impl;lock(w);*state=w->state;unlock(w);return PN_OK;
}
pn_status_t pn_transfer_worker_close(pn_transfer_worker_t *out){
 if(!out)return PN_INVALID;
 if(!out->impl)return PN_OK;
 worker_t *w=out->impl;(void)pn_transfer_worker_request_stop(out);
 if(!take_call(w,true))return PN_BUSY;
 lock(w);while(!w->finished)wait_changed(w);pn_status_t status=w->state.error;unlock(w);
 release_call(w);if(status!=PN_OK)return status;
 join(w);pn_free_fn release=w->release;void *ctx=w->allocator_ctx;
 destroy(w);release(ctx,w);out->impl=NULL;return PN_OK;
}
