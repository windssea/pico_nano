/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实睡眠适配器的按下唤醒/松开短按回归。/ English: down-to-wake and release-short regression for the real sleep adapter.
 */
#include <assert.h>
#include "device_sleep.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "read_pico.h"
#include "read_pico_pmu.h"
static int64_t clock_ms=1000,release_at;
static bool waking,short_pending,release_sent,nonkey;
static unsigned sleeps;
static pmu_snapshot_t snapshot;
static void update(void){if(waking && clock_ms>=release_at && !release_sent){short_pending=true;release_sent=true;}snapshot.key_state=waking && clock_ms<release_at;}
int64_t esp_timer_get_time(void){return clock_ms*1000;}
void vTaskDelay(unsigned ms){clock_ms+=ms;update();}
void read_pico_clear_ioe_int(void){}
void read_pico_pmu_drain_events(void){update();short_pending=false;}
esp_err_t read_pico_pmu_poll(void){update();return ESP_OK;}
const pmu_snapshot_t *read_pico_pmu_get(void){return &snapshot;}
bool read_pico_pmu_take_key_wakeup(void){return waking;}
esp_err_t gpio_config(const gpio_config_t *c){assert(c->mode==GPIO_MODE_INPUT);return ESP_OK;}
int gpio_get_level(gpio_num_t pin){(void)pin;return 1;}
esp_err_t gpio_wakeup_enable(gpio_num_t pin,int level){(void)pin;assert(level==GPIO_INTR_LOW_LEVEL);return ESP_OK;}
esp_err_t gpio_wakeup_disable(gpio_num_t pin){(void)pin;return ESP_OK;}
esp_err_t esp_sleep_enable_gpio_wakeup(void){return ESP_OK;}
esp_err_t esp_light_sleep_start(void){sleeps++;clock_ms+=1000;if(nonkey && sleeps==1)return ESP_OK;waking=true;release_at=clock_ms+200;return ESP_OK;}
int main(void){
    assert(pn_device_sleep_until_key());
    // 唤醒按键必须完整松开且短按尾事件已消费，才可交回正常锁屏轮询。/ Return to normal lock polling only after release and consumption of the trailing short event.
    assert(sleeps==1 && release_sent && !short_pending && clock_ms>=release_at+150 && pn_device_sleep_media_unchanged());
    nonkey=true;sleeps=0;waking=short_pending=release_sent=false;
    assert(pn_device_sleep_until_key() && sleeps==2 && !pn_device_sleep_media_unchanged());
    return 0;
}
