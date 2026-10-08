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
#include "read_pico.h"
#include "esp_littlefs.h"
#include "pn_alloc.h"
#include "device_transfer.h"
// 启动保护场景不能开启网络；调用这些桩即为回归。/ Boot-gate scenarios must never start networking; reaching these stubs is a regression.
pn_status_t pn_device_transfer_open(pn_device_transfer_t *s,pn_media_t *m,const char *r,const pn_device_wifi_config_t *c){(void)s;(void)m;(void)r;(void)c;assert(false);return PN_UNSUPPORTED;}
pn_status_t pn_device_transfer_request_stop(pn_device_transfer_t *s){(void)s;assert(false);return PN_UNSUPPORTED;}
pn_status_t pn_device_transfer_state(pn_device_transfer_t *s,pn_device_transfer_state_t *out){(void)s;(void)out;assert(false);return PN_UNSUPPORTED;}
pn_status_t pn_device_transfer_close(pn_device_transfer_t *s){(void)s;assert(false);return PN_UNSUPPORTED;}
void app_main(void);
static jmp_buf end;
static void (*worker)(void *);
static unsigned vcom_reads,presents,board_calls,mounts;
static bool nvs_bad,vcom_bad,mount_bad,ready_bad;
const char *esp_err_to_name(esp_err_t e){(void)e;return "mock";}
esp_err_t nvs_flash_init(void){return nvs_bad?ESP_FAIL:ESP_OK;}
esp_err_t read_pico_init(read_pico_handle_t *h){board_calls++;memset(h,0,sizeof *h);h->pmu_ready=true;return ESP_OK;}
void read_pico_deinit(read_pico_handle_t *h){(void)h;}
esp_err_t read_pico_pmu_vcom_get(int *mv){vcom_reads++;if(vcom_bad)return ESP_FAIL;*mv=1230;return ESP_OK;}
void epd_set_vcom(uint16_t v){assert(v==1230 && vcom_reads==1);}
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t *f){(void)x;(void)y;(void)c;(void)f;assert(vcom_reads==1 && !vcom_bad);}
void read_pico_epd_use_scan(int s){assert(s==READ_PICO_EPD_SCAN_FULL);}
void epd_poweron(void){assert(!vcom_bad && vcom_reads==1);}
void epd_poweroff(void){}
void epd_clear(void){}
enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState *h,enum EpdDrawMode m,int t){(void)h;(void)m;(void)t;presents++;return EPD_DRAW_SUCCESS;}
enum EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState *h,enum EpdDrawMode m,int t){return epd_hl_update_screen_from_white(h,m,t);}
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *c){mounts++;assert(!c->format_if_mount_failed && !c->grow_on_mount && !c->dont_mount && !c->read_only);assert(strcmp(c->base_path,"/data")==0 && strcmp(c->partition_label,"data")==0);return mount_bad?ESP_FAIL:ESP_OK;}
esp_err_t esp_vfs_littlefs_unregister(const char *s){(void)s;return ESP_OK;}
esp_err_t esp_littlefs_info(const char *s,size_t *total,size_t *used){(void)s;*total=1024*1024;*used=8192;return ESP_OK;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *i){memset(i,0,sizeof *i);return ESP_OK;}
esp_err_t read_pico_sd_remount(void){assert(false);return ESP_FAIL;}
esp_err_t cst836u_read(void *h,cst836u_touch_t *t){(void)h;(void)t;assert(false);return ESP_FAIL;}
void read_pico_pmu_drain_events(void){}
esp_err_t read_pico_pmu_report_ready(void){return ready_bad?ESP_FAIL:ESP_OK;}
bool read_pico_pmu_take_key_short(void){return false;}
void *heap_caps_malloc(size_t n,int caps){assert(caps==3);return malloc(n);}
void heap_caps_free(void *p){free(p);}
int64_t esp_timer_get_time(void){return 1000000;}
void vTaskDelete(void *t){(void)t;longjmp(end,1);}
void vTaskDelay(unsigned n){assert(n==10);longjmp(end,1);}
int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle,int core){(void)name;(void)arg;(void)priority;(void)handle;assert(stack==32768 && core==1);worker=fn;return 1;}
int main(int argc,char **argv){assert(argc==2);nvs_bad=strcmp(argv[1],"nvs")==0;vcom_bad=strcmp(argv[1],"vcom")==0;mount_bad=strcmp(argv[1],"mount")==0;ready_bad=strcmp(argv[1],"ready")==0;app_main();assert(worker);if(!setjmp(end))worker(NULL);
    if(nvs_bad)assert(board_calls==0 && vcom_reads==0 && presents==0 && mounts==0);
    else if(vcom_bad)assert(board_calls==1 && vcom_reads==1 && presents==0 && mounts==0);
    else if(ready_bad)assert(board_calls==1 && vcom_reads==1 && presents==0 && mounts==0);
    else assert(board_calls==1 && vcom_reads==1 && presents==1 && mounts==1);
    return 0;
}

size_t heap_caps_get_free_size(unsigned caps){(void)caps;return 8u*1024u*1024u;}
