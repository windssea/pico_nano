/* SPDX-License-Identifier: Apache-2.0
 * 中文：PMU确认事件会改变扩展器输入；用独立引脚/读比较值模型核对睡眠交接。
 * English: PMU event ACK changes expander inputs; check sleep handoff with independent pins and read-comparison state.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "device_sleep.h"
#include "device_card_presence.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "read_pico_pmu.h"
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
#include "esp_pm.h"
static bool guarded;
static int guard_token;
esp_err_t esp_pm_lock_create(esp_pm_lock_type_t type,int arg,const char *name,esp_pm_lock_handle_t *out){assert(type==ESP_PM_NO_LIGHT_SLEEP && !arg && !strcmp(name,"pn-lock-wait"));*out=&guard_token;return ESP_OK;}
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle){assert(handle==&guard_token && !guarded);guarded=true;return ESP_OK;}
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle){assert(handle==&guard_token && guarded);guarded=false;return ESP_OK;}
#endif
enum {CW=1u<<2,PGOOD=1u<<5,CARD=1u<<6};
static enum {QUIET,ACK_GPIO,ACK_TIMER,ACK_LONG,ACK_ENTRY,ACK_CARD,ACK_PGOOD,ACK_READ_FAIL,ACK_STUCK,UNKNOWN,KEY_CARD,KEY_DURING_READ,KEY_AFTER_READ} scenario;
static uint8_t pins=CW,compared=CW;
static bool forced_low,event,key_event,key_down,short_event,released,fail_next,key_at_read;
static bool gpio_armed,timer_armed;
static int64_t clock_ms=1000,release_at;
static uint64_t timer_us;
static unsigned sleeps,main_reads,followups,acks,delays,failed_reads,presses;
static unsigned planned_sleeps;
static esp_sleep_wakeup_cause_t cause;
static pmu_snapshot_t snapshot;
/// 中断取决于真实输入与上次读比较值，不由测试步骤直接清空。/ IRQ depends on live inputs versus the last read comparison, not a scripted clear.
static bool irq_low(void){return forced_low || ((pins^compared)&(CW|PGOOD|CARD))!=0;}
static void update(void){
    if(key_down && clock_ms>=release_at){key_down=false;released=true;short_event=true;event=true;pins&=(uint8_t)~CW;}
    snapshot.key_state=key_down?1u:0u;snapshot.status_ok=true;
}
static void notify(bool key){event=true;key_event=key;pins&=(uint8_t)~CW;if(key){assert(++presses==1);key_down=true;release_at=clock_ms+200;}update();}
int64_t esp_timer_get_time(void){return clock_ms*1000;}
void vTaskDelay(unsigned ms){
#ifdef PN_SLEEP_POLL_TEST
    if(ms==500 && timer_armed){assert(esp_light_sleep_start()==ESP_OK);return;}
#endif
    assert(ms==20);delays++;assert(delays<=40);clock_ms+=ms;update();
}
pn_status_t pn_device_card_inputs(uint8_t *out){
    if(timer_armed)main_reads++;
    if(fail_next){fail_next=false;failed_reads++;return PN_IO;}
    // 按键可在读回中到达，也可在其后重新拉低INT；读输入不得丢掉PMU事件。
    // A key may arrive during the reread or reassert INT just afterward; reading inputs must not lose the PMU event.
    bool inject=key_at_read && !presses;key_at_read=false;
    if(inject && scenario==KEY_DURING_READ)notify(true);
    *out=pins;compared=pins;
    if(inject && scenario==KEY_AFTER_READ)notify(true);
    return PN_OK;
}
esp_err_t read_pico_pmu_poll(void){update();return ESP_OK;}
const pmu_snapshot_t *read_pico_pmu_get(void){return &snapshot;}
static bool take(void){
    (void)read_pico_pmu_poll();bool key=key_event || short_event;
    if(event){
        // 真实ACK释放PMU的CW_INT；扩展器尚未读回时，新的输入变化会再次使INT有效。
        // A real ACK releases PMU CW_INT; the changed input asserts the expander IRQ until sampled again.
        bool sample=!key;event=key_event=short_event=false;pins|=CW;
        if(sample){acks++;followups++;
            if(scenario==ACK_CARD)pins|=CARD;
            if(scenario==ACK_PGOOD)pins^=PGOOD;
            if(scenario==ACK_READ_FAIL)fail_next=true;
            if(scenario==ACK_STUCK)forced_low=true;
            if(scenario==KEY_DURING_READ || scenario==KEY_AFTER_READ)key_at_read=true;
        }else if(scenario==KEY_CARD)pins|=CARD;
    }
    return key;
}
bool read_pico_pmu_take_key_wakeup(void){return take();}
void read_pico_pmu_drain_events(void){(void)take();}
#ifdef PN_SLEEP_POLL_TEST
bool pn_device_test_console_take_power(void){return false;}
#endif
esp_err_t gpio_config(const gpio_config_t *config){assert(config->mode==GPIO_MODE_INPUT);if(scenario==ACK_ENTRY)notify(false);if(scenario==UNKNOWN)forced_low=true;return ESP_OK;}
int gpio_get_level(gpio_num_t pin){(void)pin;return !irq_low();}
esp_err_t gpio_wakeup_enable(gpio_num_t pin,int level){(void)pin;assert(level==GPIO_INTR_LOW_LEVEL);gpio_armed=true;return ESP_OK;}
esp_err_t gpio_wakeup_disable(gpio_num_t pin){(void)pin;gpio_armed=false;return ESP_OK;}
esp_err_t esp_sleep_enable_gpio_wakeup(void){return ESP_OK;}
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us){assert(us==500000);timer_us=us;timer_armed=true;return ESP_OK;}
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t source){if(source==ESP_SLEEP_WAKEUP_TIMER)timer_armed=false;else assert(source==ESP_SLEEP_WAKEUP_GPIO);return ESP_OK;}
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void){return cause;}
esp_err_t esp_light_sleep_start(void){
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
    assert(guarded);
#endif
    assert(timer_armed && timer_us==500000 && ++sleeps<=planned_sleeps);
    // 若已知INT仍低必须使用定时兜底；不能增加零时长唤醒或清醒轮询。
    // A known low IRQ must use the timer fallback, never zero-duration wakeups or awake polling.
    assert(!gpio_armed || !irq_low());clock_ms+=500;cause=ESP_SLEEP_WAKEUP_TIMER;
    if(scenario==ACK_CARD && sleeps==2)pins&=(uint8_t)~CARD;
    if(sleeps==planned_sleeps){if(!presses)notify(true);cause=gpio_armed && irq_low()?ESP_SLEEP_WAKEUP_GPIO:ESP_SLEEP_WAKEUP_TIMER;}
    else if((scenario==ACK_LONG && sleeps%60==1) || (sleeps==1 && ((scenario>=ACK_GPIO && scenario<=ACK_STUCK && scenario!=ACK_LONG && scenario!=ACK_ENTRY) || scenario==KEY_DURING_READ || scenario==KEY_AFTER_READ))){
        notify(false);if(scenario!=ACK_TIMER && gpio_armed)cause=ESP_SLEEP_WAKEUP_GPIO;
    }
    update();return ESP_OK;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);const char *names[]={"quiet","ack_gpio","ack_timer","ack_long","ack_entry","ack_card","ack_pgood","ack_read_fail","ack_stuck","unknown","key_card","key_during_read","key_after_read"};
    bool found=false;for(unsigned i=0;i<sizeof names/sizeof *names;i++)if(!strcmp(argv[1],names[i])){scenario=i;found=true;}assert(found);
    planned_sleeps=scenario==ACK_LONG?14400u:scenario==KEY_DURING_READ || scenario==KEY_AFTER_READ?2u:4u;
    if(argc==3){
        pins|=PGOOD;assert(!pn_device_sleep_handoff_begin() && !strcmp(pn_device_sleep_media_reason(),"rails-pgood"));pins&=(uint8_t)~PGOOD;
        fail_next=true;assert(!pn_device_sleep_handoff_begin() && !strcmp(pn_device_sleep_media_reason(),"ioe-read"));
        assert(pn_device_sleep_handoff_begin());
    }
    bool ok=pn_device_sleep_until_key();pn_device_sleep_stats_t stats=pn_device_sleep_stats();
    assert(ok && sleeps==planned_sleeps && presses==1 && released && !key_down && !event && !key_event && !short_event);
    assert(!timer_armed && !gpio_armed && clock_ms>=release_at+150 && delays<=40);
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
    assert(!guarded);
#endif
#ifdef PN_SLEEP_POLL_TEST
    assert(!stats.attempts && !stats.successes && !stats.sleep_call_us);
#else
    assert(stats.attempts==sleeps && stats.successes==sleeps && stats.sleep_call_us==(uint64_t)sleeps*500000u);
#endif
    printf("scenario=%s sleeps=%u call_us=%llu main_reads=%u ack=%u held=%u timer_only=%u continuity=%s\n",argv[1],sleeps,(unsigned long long)stats.sleep_call_us,main_reads,acks,stats.held,stats.timer_only,pn_device_sleep_media_reason());fflush(stdout);
    bool cold=scenario>=ACK_CARD && scenario!=KEY_DURING_READ;assert(pn_device_sleep_media_unchanged()==!cold);
    const char *reason=scenario==ACK_CARD || scenario==KEY_CARD?"card-removed":scenario==ACK_PGOOD?"ioe-change":scenario==ACK_READ_FAIL?"ioe-read":scenario==ACK_STUCK || scenario==KEY_AFTER_READ?"held-irq":scenario==UNKNOWN?"pending-irq":"unchanged";
    assert(!strcmp(pn_device_sleep_media_reason(),reason));
    if(!cold){assert(!stats.held && !stats.timer_only && !stats.irq_cooldowns);assert(main_reads==sleeps+1+followups);}
    if(scenario==ACK_LONG)assert(acks==240);
    if(scenario==ACK_READ_FAIL)assert(failed_reads==1);
    if(scenario==UNKNOWN)assert(main_reads==sleeps+1);
    if(scenario==ACK_STUCK)assert(main_reads<=sleeps+2);
    if(scenario==ACK_STUCK || scenario==UNKNOWN){assert(stats.held);
#ifndef PN_SLEEP_POLL_TEST
        assert(stats.timer_only);
#endif
    }
    if(argc==3){
        assert(scenario==QUIET);
        // 触摸复位等待期间的新输入不能被外围读回清除或遗漏。/ Inputs arriving during touch-reset waiting must not be cleared or missed by peripheral readback.
        const char *expected="unchanged";
        if(!strcmp(argv[2],"card")){pins|=CARD;expected="card-removed";}
        else if(!strcmp(argv[2],"pgood")){pins^=PGOOD;expected="ioe-change";}
        else if(!strcmp(argv[2],"read_fail")){fail_next=true;expected="ioe-read";}
        else if(!strcmp(argv[2],"unknown")){forced_low=true;expected="restore-irq";}
        else if(!strcmp(argv[2],"pmu"))pins^=CW;
        else assert(!strcmp(argv[2],"quiet"));
        unsigned before=sleeps;pn_device_sleep_handoff_end();assert(sleeps==before && !key_down && !key_event && !short_event);
        assert(!strcmp(pn_device_sleep_media_reason(),expected));assert(pn_device_sleep_media_unchanged()==!strcmp(expected,"unchanged"));
    }
    return 0;
}
