/* SPDX-License-Identifier: Apache-2.0
 * 中文：esp_pm动态调频；下限保持PLL时钟（≥80MHz），MSPI 120MHz时序与APB外设时钟不变。
 * English: esp_pm DFS; the floor stays on the PLL (>=80 MHz), so MSPI 120 MHz timing and APB peripheral clocks are unchanged. */
#include "device_power.h"
#include <stddef.h>
static bool held[PN_POWER_OWNERS];
#if defined(ESP_PLATFORM) && CONFIG_PM_ENABLE && CONFIG_PN_POWER_DFS
#include "esp_pm.h"
#include "esp_log.h"
static const char *TAG="pn_power";
static esp_pm_lock_handle_t locks[PN_POWER_OWNERS];
static bool active,light;
void pn_device_power_init(void){
    static const char *const names[PN_POWER_OWNERS]={"pn-product","pn-display","pn-session","pn-rails"};
    for(unsigned i=0;i<PN_POWER_OWNERS;i++){
        // 先建锁并持有，配置调频前后都不会降频。/ Create and hold locks first so frequency never drops around configuration.
        if(esp_pm_lock_create(i==PN_POWER_RAILS?ESP_PM_NO_LIGHT_SLEEP:ESP_PM_CPU_FREQ_MAX,0,names[i],&locks[i])!=ESP_OK || esp_pm_lock_acquire(locks[i])!=ESP_OK){ESP_LOGW(TAG,"DFS lock unavailable; staying at maximum");return;}
        held[i]=true;
    }
#if CONFIG_PN_POWER_LIGHT_SLEEP
    const bool sleep=true;
#else
    const bool sleep=false;
#endif
    esp_pm_config_t config={.max_freq_mhz=CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,.min_freq_mhz=CONFIG_PN_POWER_MIN_MHZ,.light_sleep_enable=sleep};
    esp_err_t error=esp_pm_configure(&config);
    active=error==ESP_OK;light=active && sleep;
    ESP_LOGI(TAG,"DFS max=%d min=%d MHz light_sleep=%d status=%s",config.max_freq_mhz,config.min_freq_mhz,(int)sleep,esp_err_to_name(error));
}
void pn_device_power_busy(pn_device_power_owner_t owner,bool busy){
    if(!active || (unsigned)owner>=PN_POWER_OWNERS || held[owner]==busy)return;
    if((busy?esp_pm_lock_acquire(locks[owner]):esp_pm_lock_release(locks[owner]))==ESP_OK)held[owner]=busy;
}
bool pn_device_power_active(void){return active;}
bool pn_device_power_light_sleep(void){return light;}
#else
// 未启用调频：只记录意图，供主机测试核对取放时机。/ DFS disabled: record intent only, so host tests can check acquire/release points.
void pn_device_power_init(void){}
void pn_device_power_busy(pn_device_power_owner_t owner,bool busy){if((unsigned)owner<PN_POWER_OWNERS)held[owner]=busy;}
bool pn_device_power_active(void){return false;}
bool pn_device_power_light_sleep(void){return false;}
#endif
bool pn_device_power_held(pn_device_power_owner_t owner){return (unsigned)owner<PN_POWER_OWNERS && held[owner];}
