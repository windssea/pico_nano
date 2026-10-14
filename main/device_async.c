/* SPDX-License-Identifier: Apache-2.0
 * 中文：IDF输入与唯一显示任务；不并发调用产品模型/池。/ English: IDF input and sole display tasks without concurrent model/pool calls. */
#include "device_async.h"
#include "pn_reader_input.h"
#include "pn_input_prefs.h"
#include "pn_profile.h"
#include "read_pico_pmu.h"
#include "device_power.h"
#include <string.h>
#include <stdlib.h>
_Static_assert(sizeof(pn_device_input_event_t)<=PN_ASYNC_PAYLOAD,"Device input payload must stay bounded");
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
typedef struct {const pn_frame_t *frame;pn_refresh_t profile;bool off;} display_t;
static uint64_t now_ms(void){return (uint64_t)(esp_timer_get_time()/1000);}
static void display_task(void *ctx){pn_device_async_t *p=ctx;for(;;){display_t job;if(xQueueReceive(p->display_queue,&job,pdMS_TO_TICKS(20))==pdTRUE){// 扫描期间行供数依赖CPU，持最高频锁防欠载。/ Line feeding during a scan is CPU-bound, so hold the max lock to avoid underruns.
            pn_device_power_busy(PN_POWER_DISPLAY,true);if(job.off){p->off();p->display_result=PN_OK;}else p->display_result=p->panel(NULL,job.frame,job.profile);pn_device_power_busy(PN_POWER_DISPLAY,false);xSemaphoreGive(p->display_done);}p->idle(now_ms());}}
static void send(pn_device_async_t *p,const pn_device_input_event_t *input,bool navigation,bool urgent){pn_async_event_t e={.bytes=sizeof *input,.navigation=navigation,.urgent=urgent,.view=atomic_load(&p->input.view)};memcpy(e.payload,input,sizeof *input);(void)pn_async_send(&p->input,&e);}
static void input_task(void *ctx){
    pn_device_async_t *p=ctx;pn_key_t keys={0};pn_reader_input_t reader={0};unsigned empty=0;uint64_t last_power=0,last_hold=0;uint32_t old_view=0;int last_x=-1,last_y=-1;
    for(;;){if(atomic_load(&p->paused)){atomic_store(&p->pause_ack,true);vTaskDelay(pdMS_TO_TICKS(10));continue;}atomic_store(&p->pause_ack,false);
        uint64_t now=now_ms();uint32_t view=(uint32_t)atomic_load(&p->input.view);
        if(view!=old_view){pn_key_cancel(&keys);pn_reader_input_cancel(&reader);old_view=view;empty=0;}
        if(now-last_power>=100){last_power=now;if(read_pico_pmu_take_key_short()){pn_device_input_event_t e={.kind=PN_DASYNC_POWER,.at=now};send(p,&e,true,true);}}
        if(p->hardware->touch_ready && view){cst836u_touch_t touch={0};bool valid=cst836u_read(p->hardware->touch,&touch)==ESP_OK;
            int key=-1;pn_key_event_t ke=pn_key_feed(&keys,touch.count,touch.x,touch.y,valid,now,&key);
            bool body=atomic_load(&p->body);unsigned flags=atomic_load(&p->flags);
            if((view&0xffffu)==18u){
                if(ke==PN_KEY_SHORT && (key==PN_KEY_1 || key==PN_KEY_2)){pn_device_input_event_t e={.kind=PN_DASYNC_OPEN_CANCEL,.at=now,.key=key,.key_event=ke};send(p,&e,true,true);}
            }else if(ke!=PN_KEY_NONE && (!body || !(flags&PN_INPUT_NO_KEYS) || key==PN_KEY_2)){pn_device_input_event_t e={.kind=PN_DASYNC_KEY,.at=now,.key=key,.key_event=ke};send(p,&e,body || key==PN_KEY_1 || key==PN_KEY_2,key==PN_KEY_2 || (!body && key==PN_KEY_1));}
            reader.config=(pn_reader_input_config_t){.left_hand=(flags&PN_INPUT_LEFT_HAND)!=0,.no_swipe=(flags&PN_INPUT_NO_SWIPE)!=0,.no_edge_tap=(flags&PN_INPUT_NO_EDGE_TAP)!=0,.no_keys=true};
            pn_reader_action_t action;if(atomic_load(&p->body) && pn_reader_input_feed(&reader,touch.count,touch.x,touch.y,valid,&action)){pn_device_input_event_t e={.kind=PN_DASYNC_ACTION,.at=now,.action=action};send(p,&e,true,action==PN_APP_TOOLS);}
            // 两个空边沿保证抬起识别；持有每100ms保留一次，避免邮箱被10ms采样淹没。/ Two empty edges detect release; held samples are capped at 100 ms to avoid flooding the mailbox.
            bool post=!valid || (touch.count && (empty || last_x<0 || abs(touch.x-last_x)>=8 || abs(touch.y-last_y)>=8 || now-last_hold>=100)) || (!touch.count && empty<2);
            if(post){last_x=touch.count?touch.x:-1;last_y=touch.count?touch.y:-1;}
            if(touch.count){if(post)last_hold=now;empty=0;}else empty++;
            if(post && (touch.y<PN_KEY_AREA_TOP || !touch.count)){pn_device_input_event_t e={.kind=PN_DASYNC_TOUCH,.at=now,.count=touch.count,.x=touch.x,.y=touch.y,.valid=valid};send(p,&e,false,false);}
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif
void pn_device_async_start(pn_device_async_t *p,read_pico_handle_t *hardware,pn_reader_present_fn panel,void (*idle)(uint64_t),void (*off)(void)){
    memset(p,0,sizeof *p);pn_async_init(&p->input);atomic_init(&p->paused,false);atomic_init(&p->pause_ack,false);atomic_init(&p->body,false);atomic_init(&p->flags,0);p->hardware=hardware;p->panel=panel;p->idle=idle;p->off=off;
#ifdef ESP_PLATFORM
    p->display_queue=xQueueCreate(1,sizeof(display_t));p->display_done=xSemaphoreCreateBinary();
    TaskHandle_t display_handle=NULL;
    if(p->display_queue && p->display_done && xTaskCreatePinnedToCore(display_task,"pn-display",8192,p,5,&display_handle,0)==pdPASS){p->display_active=true;p->display_task_handle=display_handle;}
    if(!p->display_active){if(p->display_queue)vQueueDelete(p->display_queue);if(p->display_done)vSemaphoreDelete(p->display_done);p->display_queue=NULL;p->display_done=NULL;}
    TaskHandle_t input_handle=NULL;
    if(xTaskCreatePinnedToCore(input_task,"pn-input",4096,p,6,&input_handle,0)==pdPASS){p->input_active=true;p->input_task_handle=input_handle;}
#endif
}
static pn_status_t receipt(pn_device_async_t *p,pn_status_t status){if(status==PN_OK && !atomic_load(&p->body))p->revision++;return status;}
pn_status_t pn_device_async_present(pn_device_async_t *p,const pn_frame_t *frame,pn_refresh_t profile){
    pn_status_t cancel=pn_cancel_checkpoint();if(cancel!=PN_OK)return cancel;
#ifdef ESP_PLATFORM
    if(p->display_active){display_t job={.frame=frame,.profile=profile};pn_profile_span_t span;
        pn_profile_begin(&span,NULL,"display.queue");bool queued=xQueueSend(p->display_queue,&job,pdMS_TO_TICKS(100))==pdTRUE;pn_profile_end(&span,queued?PN_OK:PN_BUSY);
        if(!queued)return PN_BUSY; // 队列交接前允许取消，扫描开始后等回执。/ Cancellation is allowed before handoff; once scanning starts, wait for its receipt.
        pn_profile_begin(&span,NULL,"display.wait");bool completed=xSemaphoreTake(p->display_done,portMAX_DELAY)==pdTRUE;
        pn_status_t status=completed?p->display_result:PN_IO;pn_profile_end(&span,status);return receipt(p,status);}
#endif
    pn_device_power_busy(PN_POWER_DISPLAY,true);pn_status_t status=p->panel(NULL,frame,profile);pn_device_power_busy(PN_POWER_DISPLAY,false);
    return receipt(p,status);
}
void pn_device_async_view(pn_device_async_t *p,uint32_t view,bool body,uint8_t flags){atomic_store(&p->flags,flags);atomic_store(&p->body,body);atomic_store(&p->input.view,(view&0xffffu)|(p->revision<<16));}
pn_status_t pn_device_async_take(pn_device_async_t *p,pn_device_input_event_t *event){pn_async_event_t e;pn_status_t status=pn_async_take(&p->input,&e);if(status==PN_OK){memcpy(event,e.payload,sizeof *event);if((!e.navigation || !e.urgent) && e.view!=atomic_load(&p->input.view))return PN_CANCELLED;}return status;}
bool pn_device_async_pause(pn_device_async_t *p,bool paused){atomic_store(&p->paused,paused);
#ifdef ESP_PLATFORM
    if(paused && p->input_active){uint64_t end=now_ms()+300;while(!atomic_load(&p->pause_ack) && now_ms()<end)vTaskDelay(pdMS_TO_TICKS(5));return atomic_load(&p->pause_ack);}
#endif
    return true;
}
bool pn_device_async_off(pn_device_async_t *p){
#ifdef ESP_PLATFORM
    if(p->display_active){display_t job={.off=true};if(xQueueSend(p->display_queue,&job,pdMS_TO_TICKS(100))!=pdTRUE)return false;return xSemaphoreTake(p->display_done,portMAX_DELAY)==pdTRUE && p->display_result==PN_OK;}
#endif
    if(p->off)p->off();
    return true;
}
