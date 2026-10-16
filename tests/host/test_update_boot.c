/* SPDX-License-Identifier: Apache-2.0
 * 中文：编译真实启动端口，验证SDK调用顺序与失败保留看门狗。
 * English: compile the real boot port to verify SDK call order and watchdog retention on failures. */
#include "device_ota.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include <assert.h>
#include <stdio.h>
static esp_partition_t part;
static esp_ota_img_states_t state=ESP_OTA_IMG_VALID;
static esp_timer_create_args_t timer;
static unsigned created,deleted,started,stopped,valid,invalid,restarts,queries;
static bool mark_fail;
const esp_partition_t *esp_ota_get_running_partition(void){return &part;}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p,esp_ota_img_states_t *out){assert(p==&part);queries++;*out=state;return ESP_OK;}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void){valid++;return mark_fail?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void){invalid++;return ESP_FAIL;}
void esp_restart(void){restarts++;}
esp_err_t esp_timer_create(const esp_timer_create_args_t *a,esp_timer_handle_t *out){timer=*a;created++;*out=&timer;return ESP_OK;}
esp_err_t esp_timer_start_once(esp_timer_handle_t h,uint64_t us){assert(h==&timer && us==45000000);started++;return ESP_OK;}
esp_err_t esp_timer_stop(esp_timer_handle_t h){assert(h==&timer);stopped++;return ESP_OK;}
esp_err_t esp_timer_delete(esp_timer_handle_t h){assert(h==&timer);deleted++;return ESP_OK;}
int main(void){
 pn_device_ota_boot_begin();assert(!created && !started);unsigned q=queries;
 assert(pn_device_ota_boot_confirm(true)==PN_OK && queries==q && !valid);
 state=ESP_OTA_IMG_PENDING_VERIFY;pn_device_ota_boot_begin();pn_device_ota_boot_begin();assert(created==1 && started==1);
 mark_fail=true;assert(pn_device_ota_boot_confirm(true)==PN_IO && valid==1 && !stopped && !deleted);
 timer.callback(timer.arg);assert(restarts==1);
 mark_fail=false;assert(pn_device_ota_boot_confirm(true)==PN_OK && valid==2 && stopped==1 && deleted==1);
 q=queries;assert(pn_device_ota_boot_confirm(true)==PN_OK && queries==q);
 pn_device_ota_boot_begin();assert(created==2);assert(pn_device_ota_boot_confirm(false)==PN_IO && invalid==1 && stopped==1);
 assert(pn_device_ota_boot_confirm(true)==PN_OK && stopped==2 && deleted==2);
 puts("OTA boot: pending-only timer, no early confirmation, failed confirmation retains reset/rollback, healthy commit and no repeated flash query passed");return 0;
}
