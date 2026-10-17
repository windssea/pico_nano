/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：CFF字形深栈worker；同一时刻只服务一个调用方，worker自身调用时直接执行。
 * English: deep-stack worker for CFF glyphs; serves one caller at a time and runs directly when called from the worker itself.
 */
#include "device_font_stack.h"
#include "pn_font.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static TaskHandle_t worker;
static SemaphoreHandle_t guard,start,done;
static void (*job_fn)(void *);
static void *job_ctx;

static void run(void *arg){
    (void)arg;
    for(;;){xSemaphoreTake(start,portMAX_DELAY);job_fn(job_ctx);xSemaphoreGive(done);}
}
/// 调用方阻塞到字形完成；任务通知不被占用，产品任务其他等待不受影响。/ The caller blocks until the glyph finishes; task notifications stay untouched so other product waits are unaffected.
static bool deep_run(void (*fn)(void *),void *ctx){
    if(!fn)return false;
    if(xTaskGetCurrentTaskHandle()==worker){fn(ctx);return true;}
    xSemaphoreTake(guard,portMAX_DELAY);
    job_fn=fn;job_ctx=ctx;xSemaphoreGive(start);xSemaphoreTake(done,portMAX_DELAY);job_fn=NULL;job_ctx=NULL;
    xSemaphoreGive(guard);return true;
}
/// 没有深栈时拒绝执行，CFF字形报内存不足，绝不退回小栈。/ Without a deep stack refuse to run, so CFF glyphs report out-of-memory and never fall back to the small stack.
static bool refuse(void (*fn)(void *),void *ctx){(void)fn;(void)ctx;return false;}
bool pn_device_font_stack_start(void){
    if(worker)return true;
    guard=xSemaphoreCreateMutex();start=xSemaphoreCreateBinary();done=xSemaphoreCreateBinary();
    // 栈放PSRAM：只读卡与池分配，不做闪存写（与HTTP任务相同前提）。/ Stack in PSRAM: only card reads and pool allocations, no flash writes (same premise as the HTTP tasks).
    if(!guard || !start || !done || xTaskCreatePinnedToCoreWithCaps(run,"pn_font_deep",PN_DEVICE_FONT_STACK_BYTES,NULL,3,&worker,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){
        ESP_LOGE("pn_font_deep","Deep font stack unavailable; CFF glyphs will report out of memory");
        if(guard){vSemaphoreDelete(guard);}
        if(start){vSemaphoreDelete(start);}
        if(done){vSemaphoreDelete(done);}
        guard=start=done=NULL;worker=NULL;pn_font_set_deep_runner(refuse);return false;
    }
    pn_font_set_deep_runner(deep_run);return true;
}
