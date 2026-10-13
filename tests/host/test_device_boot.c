/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：编译真实设备main，替换硬件验证启动保护和挂载配置。
 * English: compile real device main with hardware substitutes to verify boot gates and mount policy.
 */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "read_pico.h"
#include "read_pico_pmu.h"
#include "esp_vfs_fat.h"
#include "esp_app_desc.h"
#include "esp_littlefs.h"
#include "pn_alloc.h"
#include "device_transfer.h"
// 启动保护场景不能开启网络；调用这些桩即为回归。/ Boot-gate scenarios must never start networking; reaching these stubs is a regression.
pn_status_t pn_device_transfer_open(pn_device_transfer_t *s,pn_media_t *m,const char *r,const pn_device_wifi_config_t *c){(void)s;(void)m;(void)r;(void)c;assert(false);return PN_UNSUPPORTED;}
pn_status_t pn_device_transfer_request_stop(pn_device_transfer_t *s){(void)s;assert(false);return PN_UNSUPPORTED;}
pn_status_t pn_device_transfer_state(pn_device_transfer_t *s,pn_device_transfer_state_t *out){(void)s;(void)out;assert(false);return PN_UNSUPPORTED;}
pn_status_t pn_device_transfer_close(pn_device_transfer_t *s){(void)s;assert(false);return PN_UNSUPPORTED;}
static bool wake_idle;
static unsigned sleep_calls,loop_delays;
static int64_t clock_us=1000000;
static bool sd_async,sd_probing;
static unsigned sd_remounts,sd_polls;
bool pn_device_sleep_until_key(void){assert(wake_idle);sleep_calls++;clock_us+=1000000;return true;}
bool pn_device_sleep_media_unchanged(void){return true;}
pn_status_t pn_device_transfer_take_network(pn_device_transfer_t *s,pn_network_credentials_t *c,bool *f){(void)s;(void)c;(void)f;assert(false);return PN_UNSUPPORTED;}
pn_status_t pn_device_transfer_network_result(pn_device_transfer_t *s,pn_status_t r,const char *n){(void)s;(void)r;(void)n;assert(false);return PN_UNSUPPORTED;}
void app_main(void);
static jmp_buf end;
static void (*worker)(void *);
static unsigned vcom_reads,presents,board_calls,mounts,poweroffs,pclk_calls,clear_calls,gc_updates;
static int last_pclk;
static bool underrun,panel_fault;
static bool nvs_bad,vcom_bad,mount_bad,ready_bad;
const char *esp_err_to_name(esp_err_t e){(void)e;return "mock";}
esp_err_t nvs_flash_init(void){return nvs_bad?ESP_FAIL:ESP_OK;}
esp_err_t read_pico_init(read_pico_handle_t *h){static uint8_t framebuffer[684u*1216u/2u];board_calls++;memset(h,0,sizeof *h);h->pmu_ready=true;h->framebuffer=framebuffer;return ESP_OK;}
void read_pico_deinit(read_pico_handle_t *h){(void)h;}
esp_err_t read_pico_pmu_vcom_get(int *mv){vcom_reads++;if(vcom_bad)return ESP_FAIL;*mv=1230;return ESP_OK;}
void epd_set_vcom(uint16_t v){assert(v==1230 && vcom_reads==1);}
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t *f){(void)x;(void)y;(void)c;(void)f;assert(vcom_reads==1 && !vcom_bad);}
void read_pico_epd_use_scan(int s){assert(s==READ_PICO_EPD_SCAN_FULL);}
void epd_poweron(void){assert(!vcom_bad && vcom_reads==1);}
void epd_poweroff(void){poweroffs++;}
void read_pico_epd_set_pclk(int mhz){pclk_calls++;last_pclk=mhz;}
void epd_clear(void){clear_calls++;}
enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState *h,enum EpdDrawMode m,int t){(void)h;(void)m;(void)t;presents++;if(panel_fault)return EPD_DRAW_FAILURE;if(underrun && presents==1)return EPD_DRAW_EMPTY_LINE_QUEUE;return EPD_DRAW_SUCCESS;}
enum EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState *h,enum EpdDrawMode m,int t){if(m==MODE_GC16)gc_updates++;return epd_hl_update_screen_from_white(h,m,t);}
enum EpdDrawError epd_hl_update_area_full(EpdiyHighlevelState *h,enum EpdDrawMode m,int t,EpdRect area){(void)area;return epd_hl_update_screen_from_white(h,m,t);}
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *c){if(wake_idle && !strcmp(c->partition_label,"wallpaper"))return ESP_FAIL;mounts++;assert(!c->format_if_mount_failed && !c->grow_on_mount && !c->dont_mount && !c->read_only);assert(strcmp(c->base_path,"/data")==0 && strcmp(c->partition_label,"data")==0);return mount_bad?ESP_FAIL:ESP_OK;}
esp_err_t esp_vfs_littlefs_unregister(const char *s){(void)s;return ESP_OK;}
esp_err_t esp_littlefs_info(const char *s,size_t *total,size_t *used){(void)s;*total=1024*1024;*used=8192;return ESP_OK;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *i){memset(i,0,sizeof *i);if(sd_async){i->present=true;i->mounted=sd_probing && ++sd_polls>=3;return i->mounted?ESP_OK:sd_probing?ESP_ERR_NOT_FINISHED:ESP_FAIL;}return ESP_OK;}
esp_err_t read_pico_sd_remount(void){assert(sd_async);sd_remounts++;sd_probing=true;sd_polls=0;return ESP_ERR_NOT_FINISHED;}
esp_err_t cst836u_read(void *h,cst836u_touch_t *t){(void)h;(void)t;assert(false);return ESP_FAIL;}
void read_pico_pmu_drain_events(void){}
void esp_fill_random(void *p,size_t n){memset(p,0xa5,n);}
esp_err_t read_pico_pmu_report_ready(void){return ready_bad?ESP_FAIL:ESP_OK;}
bool read_pico_pmu_take_key_short(void){return wake_idle && loop_delays==1 && sleep_calls==0;}
bool read_pico_pmu_ready(void){return true;}
esp_err_t esp_vfs_fat_info(const char *root,uint64_t *total,uint64_t *free_bytes){(void)root;*total=32ull<<30;*free_bytes=12ull<<30;return ESP_OK;}
const esp_app_desc_t *esp_app_get_description(void){static const esp_app_desc_t app={"0.0.57"};return &app;}
esp_err_t read_pico_pmu_cmd(uint16_t code,const uint8_t *payload,uint8_t plen){(void)code;(void)payload;(void)plen;return ESP_OK;}
esp_err_t read_pico_pmu_poll(void){return ESP_OK;}
const pmu_snapshot_t *read_pico_pmu_get(void){static const pmu_snapshot_t snapshot={.qb_soc=873,.qb_flags=2};return &snapshot;}
void *heap_caps_malloc(size_t n,int caps){assert(caps==3);return malloc(n);}
void heap_caps_free(void *p){free(p);}
int64_t esp_timer_get_time(void){return clock_us;}
void vTaskDelete(void *t){(void)t;longjmp(end,1);}
void vTaskDelay(unsigned n){assert(n==10);if(sd_async && loop_delays++<10){clock_us+=100000;return;}if(wake_idle && loop_delays++==0){clock_us=301000000;return;}longjmp(end,1);}
int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle,int core){(void)name;(void)arg;(void)priority;(void)handle;assert(stack==32768 && core==1);worker=fn;return 1;}
int main(int argc,char **argv){assert(argc==2);sd_async=strcmp(argv[1],"sd_async")==0;if(sd_async){(void)mkdir("/data",0700);(void)mkdir("/data/progress",0700);}wake_idle=strcmp(argv[1],"wake_idle")==0;nvs_bad=strcmp(argv[1],"nvs")==0;vcom_bad=strcmp(argv[1],"vcom")==0;mount_bad=strcmp(argv[1],"mount")==0;ready_bad=strcmp(argv[1],"ready")==0;underrun=strcmp(argv[1],"underrun")==0;app_main();assert(worker);if(!setjmp(end))worker(NULL);
    // 睡眠推进时钟后，同一循环不能把新last_input减出无符号下溢并再次锁屏。/ Advancing time in sleep must not underflow the new last_input and lock again in the same iteration.
    if(sd_async)assert(sd_remounts==1 && sd_polls>=3 && presents>=3);
    else if(wake_idle)assert(sleep_calls==1);
    else if(nvs_bad)assert(board_calls==0 && vcom_reads==0 && presents==0 && mounts==0);
    else if(vcom_bad)assert(board_calls==1 && vcom_reads==1 && presents==0 && mounts==0);
    else if(ready_bad)assert(board_calls==1 && vcom_reads==1 && presents==0 && mounts==0);
    else if(underrun){
        // 欠载：退回12MHz并白场重建，随后成功；轨保活不断电。/ Underrun: back to 12 MHz and rebuild from white, then succeed; rails stay up.
        assert(board_calls==1 && vcom_reads==1 && presents==3 && mounts==1);
        assert(pclk_calls==1 && last_pclk==READ_PICO_EPD_PCLK_MIN_MHZ && poweroffs==0);
    }
    else {
        assert(board_calls==1 && vcom_reads==1 && presents==2 && mounts==1);
        // 正常刷新后高压轨保活，空闲期满前不断电。/ After a normal present the rails stay up until the idle period elapses.
        assert(pclk_calls==0 && poweroffs==0);
    }
    return 0;
}

size_t heap_caps_get_free_size(unsigned caps){(void)caps;return 8u*1024u*1024u;}
