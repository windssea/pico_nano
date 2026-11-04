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

#else
// 未启用调频：只记录意图，供主机测试核对取放时机。/ DFS disabled: record intent only, so host tests can check acquire/release points.
void pn_device_power_init(void){}
void pn_device_power_busy(pn_device_power_owner_t owner,bool busy){if((unsigned)owner<PN_POWER_OWNERS)held[owner]=busy;}
bool pn_device_power_active(void){return false;}
bool pn_device_power_light_sleep(void){return false;}
#endif
bool pn_device_power_held(pn_device_power_owner_t owner){return (unsigned)owner<PN_POWER_OWNERS && held[owner];}

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "esp_log.h"
bool pn_device_power_sleep_pins(void){
    // 仅配置睡眠寄存器；唤醒由硬件切回原LCD/SD复用，清醒方向、电平和矩阵不变。/ Configure only sleep registers; hardware restores the original LCD/SD mux on wake, leaving active direction, levels and matrix unchanged.
    static const struct {int pin;gpio_pull_mode_t pull;} pins[]={
        {42,GPIO_PULLUP_ONLY},{44,GPIO_PULLUP_ONLY}, // SD CMD/D0（BSP read_pico_sd）
        {CONFIG_READ_PICO_I2C_SDA_GPIO,GPIO_PULLUP_ONLY},{CONFIG_READ_PICO_I2C_SCL_GPIO,GPIO_PULLUP_ONLY},
        {CONFIG_READ_PICO_IOE_INT_GPIO,GPIO_PULLUP_ONLY},{CONFIG_READ_PICO_TP_INT_GPIO,GPIO_PULLUP_ONLY},
        {2,GPIO_PULLDOWN_ONLY}, // 蜂鸣器 / buzzer
        {4,GPIO_PULLDOWN_ONLY},{5,GPIO_PULLDOWN_ONLY},{6,GPIO_PULLDOWN_ONLY},{7,GPIO_PULLDOWN_ONLY},
        {8,GPIO_PULLDOWN_ONLY},{9,GPIO_PULLDOWN_ONLY},{10,GPIO_PULLDOWN_ONLY},{11,GPIO_PULLDOWN_ONLY},
        {12,GPIO_PULLDOWN_ONLY},{13,GPIO_PULLDOWN_ONLY},{14,GPIO_PULLDOWN_ONLY},{15,GPIO_PULLDOWN_ONLY},
        {16,GPIO_PULLDOWN_ONLY},{17,GPIO_PULLDOWN_ONLY},{18,GPIO_PULLDOWN_ONLY},{45,GPIO_PULLDOWN_ONLY}, // EPD D0-D15 / 屏数据
        {21,GPIO_PULLDOWN_ONLY},{48,GPIO_PULLDOWN_ONLY},{3,GPIO_PULLDOWN_ONLY},{46,GPIO_PULLDOWN_ONLY},{47,GPIO_PULLDOWN_ONLY}, // XCL/CKV/XLE/XSTL/SPV / 屏控制
        {38,GPIO_PULLDOWN_ONLY}, // SD CLK / 存储卡时钟
    };
    unsigned failed=0;
    for(size_t i=0;i<sizeof pins/sizeof *pins;i++){
        gpio_num_t pin=(gpio_num_t)pins[i].pin;
        if(gpio_sleep_set_direction(pin,GPIO_MODE_INPUT)!=ESP_OK || gpio_sleep_set_pull_mode(pin,pins[i].pull)!=ESP_OK || gpio_sleep_sel_en(pin)!=ESP_OK)failed++;
    }
    ESP_LOGI("pn_power","Sleep pin pulls set=%u failed=%u",(unsigned)(sizeof pins/sizeof *pins),failed);
    return failed==0;
}
#else
bool pn_device_power_sleep_pins(void){return true;}
#endif
