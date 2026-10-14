/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行实际IDF端口分支，用线程替身验证显示借用、独立输入与停止屏障。/ English: run the actual IDF port branch with thread stand-ins to verify frame borrowing, independent input and stop barriers. */
#include "device_power.h"
#include "device_async.h"
#include "freertos/task.h"
#include "pn_input_prefs.h"
#include <assert.h>
#include <threads.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
typedef struct {mtx_t mutex;bool ready;size_t bytes;unsigned char data[64];} channel_t;
typedef struct {void (*run)(void *);void *ctx;thrd_t thread;} task_t;
static task_t tasks[2];static unsigned task_count,objects;
static atomic_bool stop,reject,entered,release_panel;
static atomic_uint count,x,y,touch_reads,off_calls;
int64_t esp_timer_get_time(void){struct timespec t;timespec_get(&t,TIME_UTC);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static uint64_t ms(void){return (uint64_t)esp_timer_get_time()/1000;}
void vTaskDelay(unsigned ticks){if(atomic_load(&stop))thrd_exit(0);struct timespec t={.tv_sec=ticks/1000,.tv_nsec=(long)(ticks%1000)*1000000};thrd_sleep(&t,NULL);}
static int task_entry(void *ctx){task_t *t=ctx;t->run(t->ctx);return 0;}
int xTaskCreatePinnedToCore(void (*run)(void *),const char *name,unsigned stack,void *ctx,unsigned priority,void *handle,int core){
 (void)handle;assert(core==0);assert((!strcmp(name,"pn-input") && priority==6 && stack==4096) || (!strcmp(name,"pn-display") && priority==5 && stack==8192));if(atomic_load(&reject))return 0;
 assert(task_count<2);task_t *t=&tasks[task_count++];*t=(task_t){.run=run,.ctx=ctx};assert(thrd_create(&t->thread,task_entry,t)==thrd_success);return 1;
}
static channel_t *channel(size_t bytes){channel_t *c=calloc(1,sizeof *c);assert(c && bytes<=sizeof c->data);assert(mtx_init(&c->mutex,mtx_plain)==thrd_success);c->bytes=bytes;objects++;return c;}
void *xQueueCreate(unsigned length,size_t bytes){assert(length==1);return channel(bytes);}
void *xSemaphoreCreateBinary(void){return channel(0);}
int xQueueSend(void *ctx,const void *value,unsigned timeout){channel_t *c=ctx;uint64_t end=ms()+timeout;do{mtx_lock(&c->mutex);bool room=!c->ready;if(room){if(c->bytes)memcpy(c->data,value,c->bytes);c->ready=true;}mtx_unlock(&c->mutex);if(room)return 1;vTaskDelay(1);}while(ms()<end);return 0;}
int xQueueReceive(void *ctx,void *out,unsigned timeout){channel_t *c=ctx;uint64_t end=ms()+timeout;do{mtx_lock(&c->mutex);bool have=c->ready;if(have){if(c->bytes)memcpy(out,c->data,c->bytes);c->ready=false;}mtx_unlock(&c->mutex);if(have)return 1;vTaskDelay(1);}while(ms()<end);return 0;}
int xSemaphoreTake(void *ctx,unsigned timeout){return xQueueReceive(ctx,NULL,timeout);}
int xSemaphoreGive(void *ctx){return xQueueSend(ctx,NULL,100);}
void vQueueDelete(void *ctx){channel_t *c=ctx;mtx_destroy(&c->mutex);free(c);objects--;}
void vSemaphoreDelete(void *ctx){vQueueDelete(ctx);}
bool read_pico_pmu_take_key_short(void){return false;}
esp_err_t cst836u_read(void *ctx,cst836u_touch_t *touch){(void)ctx;atomic_fetch_add(&touch_reads,1);*touch=(cst836u_touch_t){.count=atomic_load(&count),.x=atomic_load(&x),.y=atomic_load(&y)};return ESP_OK;}
// 触摸INT不可用：输入任务回到10ms轮询；通知替身记录投递。/ Touch INT unavailable: the input task falls back to 10 ms polling; the notification double counts submissions.
static atomic_uint notified;
bool pn_device_touch_wake_init(void){return false;}
bool pn_device_touch_wake_wait(unsigned timeout_ms){vTaskDelay(timeout_ms);return false;}
void xTaskNotifyGive(TaskHandle_t task){(void)task;atomic_fetch_add(&notified,1);}
unsigned ulTaskNotifyTake(int clear,unsigned ticks){(void)clear;vTaskDelay(ticks);return 0;}
TaskHandle_t xTaskGetCurrentTaskHandle(void){return (TaskHandle_t)&notified;}
static void idle(uint64_t now){(void)now;}
static void off(void){atomic_fetch_add(&off_calls,1);}
static pn_status_t panel(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)ctx;(void)profile;assert(pn_frame_get(frame,0,0)==7);assert(pn_device_power_held(PN_POWER_DISPLAY));atomic_store(&entered,true);while(!atomic_load(&release_panel))vTaskDelay(1);assert(pn_frame_get(frame,0,0)==7);return PN_OK;}
static int present(void *ctx){pn_device_async_t *p=ctx;uint8_t pixels[2]={0x77,0x77};pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,sizeof pixels,2,2));assert(pn_device_async_present(p,&frame,PN_REFRESH_GL16)==PN_OK);return 0;}
static void wait_entered(void){uint64_t end=ms()+1000;while(!atomic_load(&entered) && ms()<end)vTaskDelay(1);assert(atomic_load(&entered));}
int main(void){
 read_pico_handle_t hw={.touch_ready=true};pn_device_async_t p;atomic_store(&reject,true);pn_device_async_start(&p,&hw,panel,idle,off);assert(!p.input_active && !p.display_active && !objects && !task_count);
 atomic_store(&reject,false);pn_device_async_start(&p,&hw,panel,idle,off);assert(p.input_active && p.display_active && objects==2 && task_count==2 && !p.touch_wake);pn_device_async_bind_product(&p);assert(p.product_task);pn_device_async_view(&p,15,true,0);vTaskDelay(35);
 thrd_t foreground;assert(thrd_create(&foreground,present,&p)==thrd_success);wait_entered();atomic_store(&x,342);atomic_store(&y,500);atomic_store(&count,1);vTaskDelay(35);atomic_store(&count,0);
 pn_device_input_event_t e;bool action=false;uint64_t end=ms()+1000;while(ms()<end && !action){if(pn_device_async_take(&p,&e)==PN_OK && e.kind==PN_DASYNC_ACTION){assert(e.action==PN_APP_NEXT);action=true;}else vTaskDelay(1);}assert(action && atomic_load(&entered) && !atomic_load(&release_panel) && atomic_load(&notified)>0); // 输入投递通知产品任务 / Input submissions notify the product task
 atomic_store(&release_panel,true);thrd_join(foreground,NULL);assert(!pn_device_power_held(PN_POWER_DISPLAY)); // 扫描期间持最高频锁，结束释放。/ The max lock is held during the scan and released afterwards.
 assert(pn_device_async_off(&p) && atomic_load(&off_calls)==1);
 // 关闭三键翻页仍保留中键工具，左右键不发阅读动作。/ Disabling key turns retains middle-key tools and suppresses left/right reader keys.
 pn_device_async_view(&p,15,true,PN_INPUT_NO_KEYS);atomic_store(&x,240);atomic_store(&y,1300);atomic_store(&count,1);vTaskDelay(35);atomic_store(&count,0);bool middle=false;end=ms()+1000;
 while(ms()<end && !middle){if(pn_device_async_take(&p,&e)==PN_OK && e.kind==PN_DASYNC_KEY){assert(e.key==PN_KEY_2 && e.key_event==PN_KEY_SHORT);middle=true;}else vTaskDelay(1);}assert(middle);
 atomic_store(&x,80);atomic_store(&count,1);vTaskDelay(35);atomic_store(&count,0);vTaskDelay(40);while(pn_device_async_take(&p,&e)!=PN_EMPTY)assert(e.kind!=PN_DASYNC_KEY || e.key!=PN_KEY_1);
 assert(pn_device_async_pause(&p,true));unsigned reads=atomic_load(&touch_reads);vTaskDelay(25);assert(atomic_load(&touch_reads)==reads);assert(pn_device_async_pause(&p,false));vTaskDelay(25);assert(atomic_load(&touch_reads)>reads);
 // 开书中的返回键必须保留取消语义，不能在正文/书架恢复后变成翻页或确认。/ Back during opening must retain cancellation semantics after the body/shelf context changes.
 while(pn_device_async_take(&p,&e)!=PN_EMPTY){}
 pn_device_async_view(&p,18,false,0);pn_async_block_controls(&p.input,true);
 atomic_store(&x,80);atomic_store(&y,1300);atomic_store(&count,1);vTaskDelay(35);atomic_store(&count,0);
 bool opening_back=false;end=ms()+1000;
 while(ms()<end && !opening_back){if(pn_device_async_take(&p,&e)==PN_OK && e.key_event==PN_KEY_SHORT && e.key==PN_KEY_1){assert(e.kind==PN_DASYNC_OPEN_CANCEL);opening_back=true;}else vTaskDelay(1);}assert(opening_back);
 pn_async_block_controls(&p.input,false);
 // 旧管理帧的点击被丢弃，成功回执推进代次。/ Discard clicks from old management frames; successful receipts advance their revision.
 while(pn_device_async_take(&p,&e)!=PN_EMPTY){}
 pn_device_async_view(&p,8,false,0);pn_async_event_t stale={.bytes=sizeof e,.view=atomic_load(&p.input.view)};memcpy(stale.payload,&e,sizeof e);pn_async_send(&p.input,&stale);p.revision++;pn_device_async_view(&p,8,false,0);assert(pn_device_async_take(&p,&e)==PN_CANCELLED);
 atomic_store(&stop,true);for(unsigned i=0;i<task_count;i++)thrd_join(tasks[i].thread,NULL);vQueueDelete(p.display_queue);vSemaphoreDelete(p.display_done);assert(!objects);
 puts("Device async port: failed startup cleanup, immutable borrow, input during blocked display, off/pause barriers and stale controls passed");return 0;
}
