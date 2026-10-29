/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实睡眠适配器：扩展器读清中断、无边沿按键、定时复核及退出清理。
 * English: real sleep adapter: read-cleared expander IRQs, missed edges, timer checks and exit cleanup.
 */
#include <assert.h>
#include <string.h>
#include "device_sleep.h"
#include "device_card_presence.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "read_pico.h"
#include "read_pico_pmu.h"
static int64_t clock_ms=1000,release_at;
static bool waking,short_pending,release_sent,key_pending,line_low,timer_armed,gpio_armed;
static unsigned polls;
static unsigned sleeps,clears,drains,timer_off,gpio_off;
static uint8_t inputs=1u<<2;
static unsigned pmu_edges;
static bool sample_failed;
static enum {NORMAL,NONKEY,BEFORE_SLEEP,MISSED_EDGE,TIMER_FAILURE,SLEEP_FAILURE,HELD_ENTRY,EARLY_KEY,LEVEL_ONLY,PMU_FAILURE,SHARED_IRQ,LONG_TIMER,PMU_PENDING,PMU_GPIO,PMU_LONG,CARD_SWAP,PMU_CARD,IOE_FAILURE,UNKNOWN_PENDING,PGOOD_CHANGE,KEY_CARD} scenario;
static esp_sleep_wakeup_cause_t cause;
static pmu_snapshot_t snapshot;
static void update(void){if(waking && clock_ms>=release_at && !release_sent){short_pending=true;release_sent=true;}snapshot.key_state=scenario==HELD_ENTRY || (waking && clock_ms<release_at);}
static void press(void){waking=key_pending=line_low=true;release_at=clock_ms+200;}
int64_t esp_timer_get_time(void){return clock_ms*1000;}
void vTaskDelay(unsigned ms){
#ifdef PN_SLEEP_POLL_TEST
    // 诊断轮询的500ms延时复用同一假时钟事件，不调用真实SDK睡眠。/ Feed identical fake-clock events into diagnostic polling's 500 ms delay, without real SDK sleep.
    if(ms==500 && gpio_armed){assert(esp_light_sleep_start()==ESP_OK);update();return;}
#endif
    clock_ms+=ms;if(scenario==EARLY_KEY && !waking && clock_ms>=1040)press();update();assert(clock_ms<(scenario==PMU_LONG?40000000:scenario==LONG_TIMER?15000000:10000));
}
#ifdef PN_SLEEP_POLL_TEST
bool pn_device_test_console_take_power(void){return false;}
#endif
void read_pico_clear_ioe_int(void){clears++;line_low=scenario==SHARED_IRQ && waking;}
pn_status_t pn_device_card_inputs(uint8_t *out){
    // 读原始输入同时清INT；在PMU应答前保留卡位与PMU位的独立变化。/ Reading raw inputs also clears INT; preserve independent card/PMU changes before PMU ACK.
    if(scenario==IOE_FAILURE && sleeps==1 && !sample_failed){sample_failed=true;return PN_IO;}
    *out=inputs;read_pico_clear_ioe_int();return PN_OK;
}
void read_pico_pmu_drain_events(void){drains++;update();short_pending=key_pending=false;}
// 正常场景在准备睡眠时插入一次读错；无效快照不能冒充新按键。/ Inject a transient entry read failure in the normal case; an invalid snapshot must never impersonate a new key.
esp_err_t read_pico_pmu_poll(void){update();polls++;snapshot.status_ok=!(scenario==PMU_FAILURE && gpio_armed) && !(scenario==NORMAL && polls==2);return snapshot.status_ok?ESP_OK:ESP_FAIL;}
const pmu_snapshot_t *read_pico_pmu_get(void){return &snapshot;}
bool read_pico_pmu_take_key_wakeup(void){
    // 读清扩展器后仍须读PMU队列；只消费事件，不伪造中断电平。/ Read the PMU queue even after clearing the expander; consume events without inventing an IRQ level.
    bool key=scenario!=LEVEL_ONLY && (key_pending || short_pending);key_pending=short_pending=false;return key;
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
    assert(++sleeps<=(scenario==PMU_LONG?70000u:scenario==LONG_TIMER?28800u:6u));assert(scenario!=BEFORE_SLEEP && scenario!=HELD_ENTRY && scenario!=TIMER_FAILURE);
    if(scenario==SLEEP_FAILURE)return ESP_FAIL;
    if(scenario==PMU_FAILURE){clock_ms+=500;cause=ESP_SLEEP_WAKEUP_TIMER;return ESP_OK;}
    clock_ms+=500;
    // 四小时仅定时复核：时间本身不应使介质证明过期。/ Four hours of timer checks: elapsed time alone must not expire media continuity.
    if(scenario==LONG_TIMER && sleeps<28800){cause=ESP_SLEEP_WAKEUP_TIMER;return ESP_OK;}
    if(scenario==PMU_LONG && sleeps<70000){
        cause=ESP_SLEEP_WAKEUP_TIMER;
        if(sleeps%2500==0){inputs^=1u<<2;line_low=true;pmu_edges++;}
        return ESP_OK;
    }
    if((scenario==PMU_PENDING || scenario==PMU_GPIO) && sleeps<=2){inputs^=1u<<2;line_low=true;pmu_edges++;cause=scenario==PMU_GPIO?ESP_SLEEP_WAKEUP_GPIO:ESP_SLEEP_WAKEUP_TIMER;return ESP_OK;}
    if((scenario==CARD_SWAP || scenario==PMU_CARD || scenario==PGOOD_CHANGE || scenario==UNKNOWN_PENDING || scenario==IOE_FAILURE) && sleeps==1){
        if(scenario==CARD_SWAP || scenario==PMU_CARD)inputs|=1u<<6;
        if(scenario==PMU_CARD)inputs^=1u<<2;
        if(scenario==PGOOD_CHANGE)inputs^=1u<<5;
        line_low=true;cause=scenario==UNKNOWN_PENDING?ESP_SLEEP_WAKEUP_TIMER:ESP_SLEEP_WAKEUP_GPIO;return ESP_OK;
    }
    if(scenario==CARD_SWAP || scenario==PMU_CARD)inputs&=~(1u<<6);
    if(scenario==KEY_CARD)inputs|=1u<<6;
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
    const char *names[]={"normal","nonkey","before_sleep","missed_edge","timer_failure","sleep_failure","held_entry","early_key","level_only","pmu_failure","shared_irq","long_timer","pmu_pending","pmu_gpio","pmu_long","card_swap","pmu_card","ioe_failure","unknown_pending","pgood_change","key_card"};
    bool found=false;for(unsigned i=0;i<sizeof names/sizeof *names;i++)if(!strcmp(argv[1],names[i])){scenario=i;found=true;}assert(found);
    bool ok=pn_device_sleep_until_key();
    if(scenario==HELD_ENTRY){assert(!ok && !sleeps && !gpio_armed && !timer_armed);return 0;}
    if(scenario==TIMER_FAILURE || scenario==SLEEP_FAILURE || scenario==PMU_FAILURE){assert(!ok && !timer_armed && !gpio_armed && gpio_off && !pn_device_sleep_media_unchanged());return 0;}
    assert(ok && release_sent && !short_pending && !key_pending && clock_ms>=release_at+150);
    assert(!timer_armed && !gpio_armed && gpio_off==(scenario==EARLY_KEY?0u:1u) && timer_off==(scenario==EARLY_KEY?0u:1u) && clears);
    bool cold=scenario==NONKEY || scenario>=CARD_SWAP;
    assert(pn_device_sleep_media_unchanged()==!cold);
    const char *nonkey_reason="nonkey-wake";
#ifdef PN_SLEEP_POLL_TEST
    nonkey_reason="pending-irq";
#endif
    const char *reason=scenario==NONKEY?nonkey_reason:scenario==CARD_SWAP || scenario==PMU_CARD || scenario==KEY_CARD?"card-removed":scenario==IOE_FAILURE?"ioe-read":scenario==UNKNOWN_PENDING?"pending-irq":scenario==PGOOD_CHANGE?"ioe-change":"unchanged";
    assert(!strcmp(pn_device_sleep_media_reason(),reason));
    assert(sleeps==(scenario==BEFORE_SLEEP || scenario==EARLY_KEY?0:scenario==NONKEY || (scenario>=CARD_SWAP && scenario!=KEY_CARD)?2:scenario==MISSED_EDGE || scenario==PMU_PENDING || scenario==PMU_GPIO?3:scenario==LONG_TIMER?28800:scenario==PMU_LONG?70000:1));
    if(scenario==PMU_LONG)assert(pmu_edges==27);
    return 0;
}
