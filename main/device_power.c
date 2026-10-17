/* SPDX-License-Identifier: Apache-2.0
 * 中文：esp_pm动态调频；下限保持PLL时钟（≥80MHz），MSPI 120MHz时序与APB外设时钟不变。
 * English: esp_pm DFS; the floor stays on the PLL (>=80 MHz), so MSPI 120 MHz timing and APB peripheral clocks are unchanged. */
#include "device_power.h"
#include <stddef.h>
#ifdef ESP_PLATFORM
// 必须先取得配置宏，否则下面的#if恒为假，调频/浅睡被静默编译掉（0.6.1–0.6.2真机日志缺DFS行即此原因）。
// Configuration macros must be visible first; otherwise the #if below is always false and DFS/light sleep silently compile out (why 0.6.1-0.6.2 device logs lacked the DFS line).
#include "sdkconfig.h"
#if !defined(CONFIG_IDF_TARGET)
#error "sdkconfig.h did not provide IDF configuration"
#endif
#if defined(CONFIG_PN_POWER_DFS) && !CONFIG_PM_ENABLE
#error "PN_POWER_DFS requires CONFIG_PM_ENABLE"
#endif
#endif
static bool held[PN_POWER_OWNERS];
#if defined(ESP_PLATFORM) && CONFIG_PM_ENABLE && CONFIG_PN_POWER_DFS
#include "esp_pm.h"
#include "esp_log.h"
static const char *TAG="pn_power";
static esp_pm_lock_handle_t locks[PN_POWER_OWNERS];
/// 联网/USB会话另持禁浅睡锁：CPU满频锁不阻止自动浅睡，浅睡会让WiFi收包与TCP应答抖动甚至断流。
/// Network/USB sessions also hold a no-light-sleep lock: the max-CPU lock does not stop automatic light sleep, which makes WiFi reception and TCP acks stall or drop.
static esp_pm_lock_handle_t session_awake;
static bool active,light;
void pn_device_power_init(void){
    static const char *const names[PN_POWER_OWNERS]={"pn-product","pn-display","pn-session","pn-rails"};
    for(unsigned i=0;i<PN_POWER_OWNERS;i++){
        // 先建锁并持有，配置调频前后都不会降频。/ Create and hold locks first so frequency never drops around configuration.
        if(esp_pm_lock_create(i==PN_POWER_RAILS?ESP_PM_NO_LIGHT_SLEEP:ESP_PM_CPU_FREQ_MAX,0,names[i],&locks[i])!=ESP_OK || esp_pm_lock_acquire(locks[i])!=ESP_OK){ESP_LOGW(TAG,"DFS lock unavailable; staying at maximum");return;}
        held[i]=true;
    }
    if(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP,0,"pn-session-awake",&session_awake)!=ESP_OK || esp_pm_lock_acquire(session_awake)!=ESP_OK){ESP_LOGW(TAG,"Session wake lock unavailable; staying at maximum");return;}
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
    if((busy?esp_pm_lock_acquire(locks[owner]):esp_pm_lock_release(locks[owner]))!=ESP_OK)return;
    held[owner]=busy;
    if(owner==PN_POWER_SESSION)(void)(busy?esp_pm_lock_acquire(session_awake):esp_pm_lock_release(session_awake));
}
bool pn_device_power_active(void){return active;}
bool pn_device_power_light_sleep(void){return light;}
#include "driver/gpio.h"
void pn_device_power_sleep_pins(void){
    if(!light)return;
    // 隔离仍保留（输入/输出关闭），只加拉电阻防浮空：SD CMD/D0、I2C、两个低有效中断上拉，蜂鸣器下拉保持关闭。
    // Isolation stays (input/output off); only pulls are added against floating: SD CMD/D0, I2C and both active-low interrupts pull up, the buzzer pulls down to stay off.
    static const struct {int pin;gpio_pull_mode_t pull;} pins[]={
        {42,GPIO_PULLUP_ONLY},{44,GPIO_PULLUP_ONLY}, // SD CMD/D0（BSP read_pico_sd）
        {CONFIG_READ_PICO_I2C_SDA_GPIO,GPIO_PULLUP_ONLY},{CONFIG_READ_PICO_I2C_SCL_GPIO,GPIO_PULLUP_ONLY},
        {CONFIG_READ_PICO_IOE_INT_GPIO,GPIO_PULLUP_ONLY},{CONFIG_READ_PICO_TP_INT_GPIO,GPIO_PULLUP_ONLY},
        {2,GPIO_PULLDOWN_ONLY}, // 蜂鸣器 / buzzer
    };
    unsigned failed=0;
    for(size_t i=0;i<sizeof pins/sizeof *pins;i++)if(gpio_sleep_set_pull_mode((gpio_num_t)pins[i].pin,pins[i].pull)!=ESP_OK)failed++;
    ESP_LOGI(TAG,"Sleep pin pulls set=%u failed=%u",(unsigned)(sizeof pins/sizeof *pins),failed);
}
#else
// 未启用调频：只记录意图，供主机测试核对取放时机。/ DFS disabled: record intent only, so host tests can check acquire/release points.
void pn_device_power_init(void){}
void pn_device_power_busy(pn_device_power_owner_t owner,bool busy){if((unsigned)owner<PN_POWER_OWNERS)held[owner]=busy;}
bool pn_device_power_active(void){return false;}
bool pn_device_power_light_sleep(void){return false;}
void pn_device_power_sleep_pins(void){}
#endif
bool pn_device_power_held(pn_device_power_owner_t owner){return (unsigned)owner<PN_POWER_OWNERS && held[owner];}
