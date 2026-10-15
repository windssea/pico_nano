/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实睡眠适配器：扩展器读清中断、无边沿按键、定时复核及退出清理。
 * English: real sleep adapter: read-cleared expander IRQs, missed edges, timer checks and exit cleanup.
 */
#include <assert.h>
#include <string.h>
#include "device_sleep.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "read_pico.h"
#include "read_pico_pmu.h"
static int64_t clock_ms=1000,release_at;
static bool waking,short_pending,release_sent,key_pending,line_low,timer_armed,gpio_armed;
static unsigned sleeps,clears,drains,timer_off,gpio_off;
static enum {NORMAL,NONKEY,BEFORE_SLEEP,MISSED_EDGE,TIMER_FAILURE,SLEEP_FAILURE,HELD_ENTRY} scenario;
static esp_sleep_wakeup_cause_t cause;
static pmu_snapshot_t snapshot;
static void update(void){if(waking && clock_ms>=release_at && !release_sent){short_pending=true;release_sent=true;}snapshot.key_state=scenario==HELD_ENTRY || (waking && clock_ms<release_at);}
static void press(void){waking=key_pending=line_low=true;release_at=clock_ms+200;}
int64_t esp_timer_get_time(void){return clock_ms*1000;}
void vTaskDelay(unsigned ms){clock_ms+=ms;update();assert(clock_ms<10000);}
void read_pico_clear_ioe_int(void){clears++;line_low=false;}
void read_pico_pmu_drain_events(void){drains++;update();short_pending=key_pending=false;}
esp_err_t read_pico_pmu_poll(void){update();return ESP_OK;}
const pmu_snapshot_t *read_pico_pmu_get(void){return &snapshot;}
bool read_pico_pmu_take_key_wakeup(void){
    // 读清扩展器后仍须读PMU队列；只消费事件，不伪造中断电平。/ Read the PMU queue even after clearing the expander; consume events without inventing an IRQ level.
    bool key=key_pending || short_pending;key_pending=short_pending=false;return key;
}
esp_err_t gpio_config(const gpio_config_t *c){assert(c->mode==GPIO_MODE_INPUT);if(scenario==BEFORE_SLEEP)press();return ESP_OK;}
int gpio_get_level(gpio_num_t pin){(void)pin;return line_low?0:1;}
esp_err_t gpio_wakeup_enable(gpio_num_t pin,int level){(void)pin;assert(level==GPIO_INTR_LOW_LEVEL);gpio_armed=true;return ESP_OK;}
esp_err_t gpio_wakeup_disable(gpio_num_t pin){(void)pin;gpio_armed=false;gpio_off++;return ESP_OK;}
esp_err_t esp_sleep_enable_gpio_wakeup(void){return ESP_OK;}
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us){assert(us==500000);if(scenario==TIMER_FAILURE)return ESP_FAIL;timer_armed=true;return ESP_OK;}
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t source){assert(source==ESP_SLEEP_WAKEUP_TIMER || source==ESP_SLEEP_WAKEUP_GPIO);if(source==ESP_SLEEP_WAKEUP_TIMER){timer_armed=false;timer_off++;}return ESP_OK;}
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void){return cause;}
esp_err_t esp_light_sleep_start(void){
    assert(++sleeps<=6);assert(scenario!=BEFORE_SLEEP && scenario!=HELD_ENTRY && scenario!=TIMER_FAILURE);
    if(scenario==SLEEP_FAILURE)return ESP_FAIL;
    clock_ms+=500;
    if(scenario==NONKEY && sleeps==1){line_low=true;cause=ESP_SLEEP_WAKEUP_GPIO;return ESP_OK;}
    if(scenario==MISSED_EDGE){
        // 丢GPIO通知时，定时读取仍须找到已松开的短按。/ If the GPIO notification is lost, timer checks must still find a completed short press.
        assert(timer_armed);cause=ESP_SLEEP_WAKEUP_TIMER;
        if(sleeps==3){waking=true;release_sent=true;release_at=clock_ms-100;short_pending=true;}
        return ESP_OK;
    }
    press();cause=ESP_SLEEP_WAKEUP_GPIO;return ESP_OK;
}
int main(int argc,char **argv){
    assert(argc==2);
    const char *names[]={"normal","nonkey","before_sleep","missed_edge","timer_failure","sleep_failure","held_entry"};
    bool found=false;for(unsigned i=0;i<sizeof names/sizeof *names;i++)if(!strcmp(argv[1],names[i])){scenario=i;found=true;}assert(found);
    bool ok=pn_device_sleep_until_key();
    if(scenario==HELD_ENTRY){assert(!ok && !sleeps && !gpio_armed && !timer_armed);return 0;}
    if(scenario==TIMER_FAILURE || scenario==SLEEP_FAILURE){assert(!ok && !timer_armed && !gpio_armed && gpio_off && !pn_device_sleep_media_unchanged());return 0;}
    assert(ok && release_sent && !short_pending && !key_pending && clock_ms>=release_at+150);
    assert(!timer_armed && !gpio_armed && gpio_off==1 && timer_off==1 && clears && drains);
    assert(pn_device_sleep_media_unchanged()==(scenario!=NONKEY));
    assert(sleeps==(scenario==BEFORE_SLEEP?0:scenario==NONKEY?2:scenario==MISSED_EDGE?3:1));
    return 0;
}
