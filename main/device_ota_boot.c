/* SPDX-License-Identifier: Apache-2.0
 * 中文：仅待确认应用启用首次启动看门狗；确认成功才解除回滚。
 * English: only pending applications get a first-boot watchdog; successful confirmation cancels rollback. */
#include "device_ota.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_system.h"
static esp_timer_handle_t boot_timer;
static void boot_timeout(void *ctx){(void)ctx;esp_restart();}
void pn_device_ota_boot_begin(void){
    if(boot_timer)return;
    const esp_partition_t *p=esp_ota_get_running_partition();esp_ota_img_states_t state;
    if(p && esp_ota_get_state_partition(p,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY){
        esp_timer_create_args_t args={.callback=boot_timeout,.name="pn-ota-boot"};
        if(esp_timer_create(&args,&boot_timer)!=ESP_OK || esp_timer_start_once(boot_timer,45000000)!=ESP_OK)esp_restart();
    }
}
pn_status_t pn_device_ota_boot_confirm(bool healthy){
    if(!boot_timer)return PN_OK;
    const esp_partition_t *p=esp_ota_get_running_partition();esp_ota_img_states_t state;
    if(!p)return PN_UNSUPPORTED;
    if(esp_ota_get_state_partition(p,&state)!=ESP_OK || state!=ESP_OTA_IMG_PENDING_VERIFY)return PN_IO;
    if(!healthy){(void)esp_ota_mark_app_invalid_rollback_and_reboot();return PN_IO;}
    if(esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK)return PN_IO;
    (void)esp_timer_stop(boot_timer);(void)esp_timer_delete(boot_timer);boot_timer=NULL;
    return PN_OK;
}
