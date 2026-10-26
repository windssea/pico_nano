/* SPDX-License-Identifier: Apache-2.0
 * 中文：诊断镜像的USB串口测试控制台。指令经pn_test_console解析；合成触点在输入任务采样层注入，截屏在显示任务两job之间读取已显示参考。
 * English: USB serial test console for diagnostic images. Commands parse through pn_test_console; synthetic contacts inject at the input task's sampling layer and screenshots read the shown reference between display jobs.
 * 只输出"PNT "开头的行；时间均为esp_timer毫秒，与产品日志中的计时同源。
 * Emits only lines starting with "PNT "; all times are esp_timer milliseconds, the same base as product log timings.
 */
#include "device_test_console.h"
#if PN_DEVICE_TEST_CONSOLE
#include "pn_test_console.h"
#include "pn_crc32.h"
#include "pn_brand.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define SHOT_CHUNK 384u ///< 每行原始字节，base64后512字符 / Raw bytes per line, 512 characters after base64
#define RING 32u ///< 显示job记录环 / Display-job record ring
#define STALL_MS 3000u ///< 手势超过按住时长后仍未完成即报停滞 / Report a stall when a gesture outlives its hold by this margin

static const char *TAG="pn_test";
typedef struct {unsigned kind;uint64_t start,end;pn_status_t status;} record_t;
typedef enum {GESTURE_IDLE,GESTURE_ARMED,GESTURE_DONE} gesture_state_t;
static struct {
    pn_device_async_t *port;pn_test_console_shown_fn shown;pn_device_async_diag_t hooks;
    pn_tc_gesture_t gesture;uint32_t gesture_seq;uint64_t armed_at;atomic_uint gesture_state; ///< 输入任务推进，控制台仅在ARMED前后读写 / Advanced by the input task; console touches it only outside ARMED
    atomic_bool power; ///< 待注入电源短按 / Pending synthetic power press
    record_t ring[RING];atomic_uint head,tail,dropped; ///< 显示任务单生产者，控制台单消费者 / Single display-task producer and single console consumer
    atomic_uint shot_seq,heap_seq; ///< 非0为待处理截屏/堆统计序号 / Nonzero is a pending screenshot/heap sequence
    atomic_uint cache_fault; ///< owner待处理读故障加1 / Owner-pending read fault plus one
    atomic_uint loading_phase; ///< 姿态加1，0表示没有请求 / Pose plus one, zero when no request is pending
    char line[640];
} console;

static uint64_t now_ms(void){return (uint64_t)(esp_timer_get_time()/1000);}
// 整行一次写入驱动环形缓冲，日志字符不会插入行内；未连接时丢弃。/ Write a whole line into the driver ring at once so log characters cannot split it; drop while disconnected.
// 默认不等待：电脑未读取时发送环满即丢弃，任何任务都不会被串口拖住。/ No waiting by default: drop when the transmit ring is full because the host is not reading, so no task is held by the serial port.
// 连接检测在大流量时可能瞬时为假；需要送达的行只按超时写入，不先检查连接。/ The connection check can read false momentarily under heavy traffic; lines that must arrive write with a timeout only, without that check.
static bool emit_wait(const char *text,size_t bytes,TickType_t wait){return usb_serial_jtag_write_bytes(text,bytes,wait)==(int)bytes;}
static void emit(const char *text,size_t bytes){if(usb_serial_jtag_is_connected())(void)emit_wait(text,bytes,0);}
// 产品日志整行非阻塞写入；替代驱动VFS逐字符50ms超时，后者在电脑不读时会让产品任务近乎停住。
// Product logs are written whole and non-blocking, replacing the driver VFS per-character 50 ms timeout that nearly stalls product tasks when the host is not reading.
static int log_line(const char *format,va_list args){char text[200];int n=vsnprintf(text,sizeof text,format,args);if(n>0)emit(text,(size_t)n<sizeof text?(size_t)n:sizeof text-1u);return n;}
static void reply(const char *format,...) __attribute__((format(printf,1,2)));
static void reply(const char *format,...){
    char text[192];text[0]='\n';va_list args;va_start(args,format);int n=vsnprintf(text+1,sizeof text-2,format,args);va_end(args);
    if(n<0)return;
    size_t length=1u+((size_t)n<sizeof text-3u?(size_t)n:sizeof text-3u);text[length++]='\n';
    // 控制台回执可短暂等待，避免日志突发占满发送环时丢失；产品日志仍不等待。/ Console replies may wait briefly so a log burst filling the ring cannot drop them; product logs still never wait.
    (void)emit_wait(text,length,pdMS_TO_TICKS(50));
}
static size_t base64(const uint8_t *in,size_t bytes,char *out){
    static const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t o=0;
    for(size_t i=0;i<bytes;i+=3){uint32_t v=(uint32_t)in[i]<<16;if(i+1<bytes)v|=(uint32_t)in[i+1]<<8;if(i+2<bytes)v|=in[i+2];
        out[o++]=table[(v>>18)&63];out[o++]=table[(v>>12)&63];out[o++]=i+1<bytes?table[(v>>6)&63]:'=';out[o++]=i+2<bytes?table[v&63]:'=';}
    return o;
}

// 输入任务线程：只推进已武装的时间线。/ Input-task thread: advance only an armed timeline.
static bool hook_touch(void *ctx,uint64_t now,unsigned *count,int *x,int *y,bool *ending){
    (void)ctx;if(atomic_load(&console.gesture_state)!=GESTURE_ARMED)return false;
    if(pn_tc_gesture_sample(&console.gesture,now,count,x,y,ending))return true;
    unsigned armed=GESTURE_ARMED;(void)atomic_compare_exchange_strong(&console.gesture_state,&armed,GESTURE_DONE);return false;
}
static bool hook_power(void *ctx){(void)ctx;return atomic_exchange(&console.power,false);}
bool pn_device_test_console_take_power(void){return console.port && atomic_exchange(&console.power,false);}
bool pn_device_test_console_take_loading(unsigned *phase){if(!phase || !console.port)return false;unsigned value=atomic_exchange(&console.loading_phase,0);if(!value)return false;*phase=value-1;return true;}
bool pn_device_test_console_take_cache(unsigned *mode){if(!mode || !console.port)return false;unsigned value=atomic_exchange(&console.cache_fault,0);if(!value)return false;*mode=value-1;return true;}
// 显示任务线程：回执前只写环，不做IO。/ Display-task thread: only write the ring before the receipt, no IO.
static void hook_display_job(void *ctx,unsigned kind,uint64_t start,uint64_t end,pn_status_t status){
    (void)ctx;unsigned head=atomic_load(&console.head);
    if(head-atomic_load(&console.tail)>=RING){atomic_fetch_add(&console.dropped,1);return;}
    console.ring[head%RING]=(record_t){kind,start,end,status};atomic_store(&console.head,head+1);
}
// 显示任务两job之间：显示任务独占已显示参考，可安全回传。/ Between display jobs the display task owns the shown reference and may stream it safely.
static void heap_report(unsigned seq);
static void hook_display_idle(void *ctx){
    (void)ctx;unsigned heap=atomic_exchange(&console.heap_seq,0);if(heap)heap_report(heap);
    unsigned seq=atomic_exchange(&console.shot_seq,0);if(!seq)return;
    const uint8_t *buffer=NULL;size_t bytes=0;bool known=console.shown && console.shown(&buffer,&bytes);
    if(!buffer || !bytes){reply("PNT %u err unavailable",seq);return;}
    uint64_t started=now_ms();reply("PNT %u shot begin bytes=%u known=%d layout=board-1216x684-4bpp",seq,(unsigned)bytes,(int)known);
    uint32_t total=0;
    for(size_t at=0;at<bytes;at+=SHOT_CHUNK){size_t n=bytes-at<SHOT_CHUNK?bytes-at:SHOT_CHUNK;uint32_t crc=pn_crc32_update(0,buffer+at,n);total=pn_crc32_update(total,buffer+at,n);
        int head=snprintf(console.line,sizeof console.line,"\nPNT %u shot %u %08lx ",seq,(unsigned)at,(unsigned long)crc);
        size_t length=(size_t)head+base64(buffer+at,n,console.line+head);console.line[length++]='\n';
        // 截屏行允许短暂等待；电脑停止读取时中止，不长时间占住显示任务。/ Screenshot lines may wait briefly; abort when the host stops reading instead of holding the display task.
        unsigned tries=0;while(!emit_wait(console.line,length,pdMS_TO_TICKS(200)) && ++tries<10)vTaskDelay(pdMS_TO_TICKS(20));
        if(tries>=10){reply("PNT %u err stream",seq);return;}}
    reply("PNT %u shot end crc=%08lx ms=%llu",seq,(unsigned long)total,(unsigned long long)(now_ms()-started));
}

static const char *kind_name(unsigned kind){return kind==0?"frame":kind==1?"feedback":kind==2?"native":"off";}
static void status(uint32_t seq){
    pn_device_async_t *p=console.port;
    reply("PNT %lu status t=%llu view=%08x body=%d paused=%d events=%u touch_reads=%u touch_errors=%u gesture=%u dropped=%u",(unsigned long)seq,(unsigned long long)now_ms(),
        (unsigned)atomic_load(&p->input.view),(int)atomic_load(&p->body),(int)atomic_load(&p->paused),atomic_load(&p->events_sent),atomic_load(&p->touch_reads),atomic_load(&p->touch_errors),atomic_load(&console.gesture_state),atomic_load(&console.dropped));
}
// 显示任务两job之间执行：遍历堆持自旋锁并屏蔽中断，扫描中执行会饿死行DMA造成欠载（真机已复现）。
// Runs between display jobs on the display task: heap walks hold a spinlock with interrupts masked, which starved line DMA into underruns when done mid-scan (reproduced on hardware).
static void heap_report(unsigned seq){
    pn_device_async_t *p=console.port;
    reply("PNT %u heap internal=%u internal_largest=%u internal_min=%u psram=%u psram_largest=%u psram_min=%u",seq,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    if(p->input_task_handle && p->display_task_handle)reply("PNT %u stack input=%u display=%u",seq,(unsigned)uxTaskGetStackHighWaterMark(p->input_task_handle),(unsigned)uxTaskGetStackHighWaterMark(p->display_task_handle));
}
static void execute(const char *text){
    pn_tc_command_t c;if(pn_tc_parse(text,&c)!=PN_OK){reply("PNT 0 err invalid");return;}
    unsigned long seq=(unsigned long)c.seq;
    switch(c.kind){
    case PN_TC_PING:reply("PNT %lu ok pong t=%llu",seq,(unsigned long long)now_ms());break;
    case PN_TC_VERSION:{const esp_app_desc_t *app=esp_app_get_description();char sha[17]={0};(void)esp_app_get_elf_sha256(sha,sizeof sha);
        reply("PNT %lu ok version product=%s app=%.32s idf=%.32s elf=%s",seq,PN_PRODUCT_VERSION,app?app->version:"?",app?app->idf_ver:"?",sha);break;}
    case PN_TC_STATUS:status(c.seq);break;
    case PN_TC_SHOT:{unsigned expected=0;if(!atomic_compare_exchange_strong(&console.shot_seq,&expected,(unsigned)c.seq))reply("PNT %lu err busy",seq);else reply("PNT %lu ok shot-pending",seq);break;}
    case PN_TC_HEAP:{unsigned expected=0;if(!atomic_compare_exchange_strong(&console.heap_seq,&expected,(unsigned)c.seq))reply("PNT %lu err busy",seq);else reply("PNT %lu ok heap-pending",seq);break;}
    case PN_TC_LOADING:{unsigned expected=0;if(!atomic_compare_exchange_strong(&console.loading_phase,&expected,c.phase+1))reply("PNT %lu err busy",seq);else reply("PNT %lu ok loading-pending",seq);break;}
    case PN_TC_CACHE:{unsigned expected=0;if(!atomic_compare_exchange_strong(&console.cache_fault,&expected,c.phase+1))reply("PNT %lu err busy",seq);else reply("PNT %lu ok cache-pending",seq);break;}
    case PN_TC_POWER:atomic_store(&console.power,true);reply("PNT %lu ok power t=%llu",seq,(unsigned long long)now_ms());break;
    default:
        if(!console.port->hardware || !console.port->hardware->touch_ready){reply("PNT %lu err unavailable",seq);break;}
        if(atomic_load(&console.gesture_state)!=GESTURE_IDLE){reply("PNT %lu err busy",seq);break;}
        if(pn_tc_gesture_init(&console.gesture,&c)!=PN_OK){reply("PNT %lu err invalid",seq);break;}
        console.gesture_seq=c.seq;console.armed_at=now_ms();atomic_store(&console.gesture_state,GESTURE_ARMED);
        reply("PNT %lu ok queued t=%llu",seq,(unsigned long long)console.armed_at);break;
    }
}
static void poll_events(void){
    for(unsigned tail=atomic_load(&console.tail);tail!=atomic_load(&console.head);tail++){record_t r=console.ring[tail%RING];atomic_store(&console.tail,tail+1);
        reply("PNT 0 display kind=%s start=%llu end=%llu ms=%llu status=%d",kind_name(r.kind),(unsigned long long)r.start,(unsigned long long)r.end,(unsigned long long)(r.end-r.start),(int)r.status);}
    unsigned state=atomic_load(&console.gesture_state);
    if(state==GESTURE_DONE){reply("PNT %lu done start=%llu end=%llu",(unsigned long)console.gesture_seq,(unsigned long long)console.gesture.start_ms,(unsigned long long)console.gesture.end_ms);atomic_store(&console.gesture_state,GESTURE_IDLE);}
    else if(state==GESTURE_ARMED && now_ms()-console.armed_at>console.gesture.hold_ms+STALL_MS){
        // 输入暂停（锁屏/传书）时没有采样；撤销后等一个采样周期再允许新手势。/ No samples while input is paused (lock/transfer); after revoking, wait one sample period before accepting another gesture.
        unsigned armed=GESTURE_ARMED;if(atomic_compare_exchange_strong(&console.gesture_state,&armed,GESTURE_IDLE)){vTaskDelay(pdMS_TO_TICKS(60));reply("PNT %lu err stalled started=%d",(unsigned long)console.gesture_seq,(int)console.gesture.started);}}
}
static void console_task(void *ctx){
    (void)ctx;char input[PN_TC_LINE_MAX+2];size_t used=0;bool overflow=false;
    reply("PNT 0 ready product=%s",PN_PRODUCT_VERSION);
    for(;;){uint8_t bytes[64];int n=usb_serial_jtag_read_bytes(bytes,sizeof bytes,pdMS_TO_TICKS(10));
        for(int i=0;i<n;i++){char ch=(char)bytes[i];
            if(ch=='\n' || ch=='\r'){if(used && !overflow){input[used]=0;execute(input);}else if(overflow)reply("PNT 0 err invalid");used=0;overflow=false;continue;}
            if(used<PN_TC_LINE_MAX)input[used++]=ch;else overflow=true;}
        poll_events();}
}
void pn_device_test_console_start(pn_device_async_t *port,pn_test_console_shown_fn shown){
    if(!port || console.port)return;
    usb_serial_jtag_driver_config_t config={.rx_buffer_size=256,.tx_buffer_size=4096};
    if(!usb_serial_jtag_is_driver_installed()){esp_err_t err=usb_serial_jtag_driver_install(&config);if(err!=ESP_OK){ESP_LOGW(TAG,"Serial driver unavailable: %s",esp_err_to_name(err));return;}}
    // 日志也改走驱动，与控制台共用同一发送环，整行写入不被拆开。/ Route logs through the driver too so they share the console's transmit ring and whole lines stay intact.
    usb_serial_jtag_vfs_use_driver();
    (void)esp_log_set_vprintf(log_line);
#if CONFIG_PM_ENABLE
    // 自动浅睡会让USB串口停止响应；诊断镜像常持禁浅睡锁，DFS降频保留。因此诊断镜像不能用于功耗测量。
    // Automatic light sleep stops the USB serial port; the diagnostic image permanently holds a no-light-sleep lock while keeping DFS, so it is unfit for power measurement.
    static esp_pm_lock_handle_t awake;
    if(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP,0,"pn-test",&awake)!=ESP_OK || esp_pm_lock_acquire(awake)!=ESP_OK)ESP_LOGW(TAG,"No-light-sleep lock unavailable; console may stall while idle");
#endif
    console.port=port;console.shown=shown;atomic_init(&console.gesture_state,GESTURE_IDLE);
    console.hooks=(pn_device_async_diag_t){.touch=hook_touch,.power=hook_power,.display_job=hook_display_job,.display_idle=hook_display_idle,.ctx=NULL};
    atomic_store(&port->diag,&console.hooks);
    if(xTaskCreatePinnedToCore(console_task,"pn-test",4096,NULL,3,NULL,tskNO_AFFINITY)!=pdPASS){atomic_store(&port->diag,NULL);ESP_LOGW(TAG,"Test console task unavailable");return;}
    ESP_LOGW(TAG,"Diagnostic test console active; not a product image");
}
#endif
