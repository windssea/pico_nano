/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：等按键空闲，使用IOE_INT与定时复核浅睡，只有PMU确认的新按键才解锁。
 * English: wait for key idle, light-sleep with IOE_INT and timer checks, and unlock only for a new PMU-confirmed key.
 * 冻结：不支持拿起唤醒；不改变PMU设置。/ Frozen: no pickup wake; PMU settings untouched.
 */
#include "device_sleep.h"
#include "device_card_presence.h"
#include <stddef.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "read_pico_pmu.h"
#if defined(CONFIG_PN_DIAG_NO_LOCK_SLEEP) && CONFIG_PN_DIAG_NO_LOCK_SLEEP
#include "device_test_console.h"
#define PN_LOCK_SLEEP 0 ///< 诊断镜像锁屏只轮询，USB串口保持在线 / Diagnostic images only poll while locked, keeping USB serial alive
#else
#define PN_LOCK_SLEEP 1 ///< 产品镜像锁屏浅睡 / Product images light-sleep while locked
#endif
static const char *TAG="pn_sleep";
static int64_t now_ms(void){return esp_timer_get_time()/1000;}
static const char *media_reason="not-entered";
bool pn_device_sleep_media_unchanged(void){return media_reason==NULL;}
const char *pn_device_sleep_media_reason(void){return media_reason?media_reason:"unchanged";}
/// 保留首次原因，不把后续共享中断覆盖成另一种误导原因。/ Retain the first reason rather than overwrite it with a later shared interrupt.
static void media_uncertain(const char *reason){if(!media_reason){media_reason=reason;ESP_LOGW(TAG,"Sleep continuity lost reason=%s at_ms=%lld",reason,(long long)now_ms());}}
enum {CW_INT=1u<<2,SY_PGOOD=1u<<5,SD_CD=1u<<6};
typedef struct {
    uint8_t previous; ///< 上次成功读取的输入位 / Last successfully read input bits
    uint8_t before,failed_before,failed_after; ///< 本轮之前及首次失效的原始输入 / Previous sample and raw inputs at the first invalidation
    bool known; ///< 有可比较的原始快照 / A raw comparison snapshot is available
    bool failure_sample; ///< 首次失效存在可报告的原始输入 / Raw inputs are available for the first invalidation
    unsigned pmu_irqs; ///< 仅CW_INT变化且卡检测稳定的中断数 / IRQs with only CW_INT changed and stable card detect
} sleep_inputs_t;
/// 唤醒汇总保留首次失效输入，串口晚接也能看到，不在轮询中重复打印。/ Retain first-failure inputs for the wake summary even if serial connects late; never print repeatedly while polling.
static void inputs_uncertain(sleep_inputs_t *state,const char *reason,uint8_t before,uint8_t after){
    if(!media_reason){state->failed_before=before;state->failed_after=after;state->failure_sample=true;}
    media_uncertain(reason);
}
/// 用读输入替换盲清INT，卡检测和未知变化不能被PMU队列消费掩盖。/ Replace blind IRQ clearing with input reads so PMU draining cannot hide card or unknown changes.
static uint8_t sample_inputs(sleep_inputs_t *state){
    uint8_t value=0;pn_status_t status=pn_device_card_inputs(&value);
    if(status!=PN_OK){state->known=false;inputs_uncertain(state,"ioe-read",state->previous,0xff);return 0;}
    uint8_t changed=state->known?(value^state->previous)&(CW_INT|SY_PGOOD|SD_CD):0;
    if(value&SD_CD)inputs_uncertain(state,"card-removed",state->previous,value);
    else if(changed&SD_CD)inputs_uncertain(state,"card-change",state->previous,value);
    else if(changed&SY_PGOOD)inputs_uncertain(state,"ioe-change",state->previous,value);
    state->before=state->previous;state->previous=value;state->known=true;return changed;
}
/* 电源键有效松开150ms才算空闲；其他共享中断不能阻止解锁。/ Require a valid released key for 150 ms; unrelated shared interrupts cannot prevent unlocking. */
static bool wait_key_idle(int timeout_ms,bool watch_new,bool *new_key,sleep_inputs_t *inputs){
    int64_t start=now_ms(),high_from=0;
    bool released=false;
    while(now_ms()-start<timeout_ms){
        (void)sample_inputs(inputs);
        if(!watch_new)read_pico_pmu_drain_events();
        bool valid=read_pico_pmu_poll()==ESP_OK && read_pico_pmu_get()->status_ok;
        bool held=!valid || (read_pico_pmu_get()->key_state&0x01)!=0;
        bool key=watch_new && read_pico_pmu_take_key_wakeup();
        if(watch_new && released && (key || (valid && held)))*new_key=true;
        if(!held)released=true;
        if(!held){if(!high_from)high_from=now_ms();if(now_ms()-high_from>=150)return true;}
        else high_from=0;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}
/// 退出时归还本适配器的显式唤醒源，不能污染之后的自动浅睡。/ Release this adapter's explicit wake sources without leaking them into later automatic light sleep.
static void wake_sources_close(void){
    (void)gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}
bool pn_device_sleep_until_key(void){
    media_reason=NULL;
    sleep_inputs_t inputs={0};
    bool early_key=false;
    if(!wait_key_idle(800,true,&early_key,&inputs)){media_uncertain("entry-key-busy");ESP_LOGW(TAG,"Lock key not idle; staying awake");return false;}
    if(early_key){ESP_LOGI(TAG,"Wake key received while preparing sleep");return true;}
    gpio_config_t io={.pin_bit_mask=1ULL<<READ_PICO_IOE_INT_GPIO,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    if(gpio_config(&io)!=ESP_OK){media_uncertain("gpio-config");ESP_LOGW(TAG,"IOE_INT config failed; staying awake");return false;}
    if(gpio_wakeup_enable((gpio_num_t)READ_PICO_IOE_INT_GPIO,GPIO_INTR_LOW_LEVEL)!=ESP_OK || esp_sleep_enable_gpio_wakeup()!=ESP_OK){
        media_uncertain("gpio-wake-setup");(void)gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);ESP_LOGW(TAG,"Wake source unavailable; staying awake");return false;}
    // 不能仅依赖读清式扩展器中断：500ms复核PMU队列，不刷新屏幕，也不访问TF。
    // A read-cleared expander IRQ cannot be the only wake path: check the PMU queue every 500 ms without drawing or accessing TF.
    if(esp_sleep_enable_timer_wakeup(500000)!=ESP_OK){media_uncertain("timer-setup");wake_sources_close();ESP_LOGW(TAG,"Wake timer unavailable; staying awake");return false;}
    unsigned poll_failures=0,timers=0,gpios=0,other=0,pending=0,held=0;
    bool woke=false;esp_sleep_wakeup_cause_t cause=ESP_SLEEP_WAKEUP_TIMER;
    for(;;){
        // 先保留原始输入再消费PMU；读清扩展器不代表PMU队列为空，首次入睡前的新按键也有效。
        // Capture raw inputs before draining the PMU: reading the expander does not empty the PMU queue, and a new key before the first sleep is valid too.
        bool low=gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO)==0;
        uint8_t changed=sample_inputs(&inputs);
        bool key=read_pico_pmu_take_key_wakeup();
#if !PN_LOCK_SLEEP
        key=key || pn_device_test_console_take_power();
#endif
        bool valid=read_pico_pmu_poll()==ESP_OK && read_pico_pmu_get()->status_ok;
        if(key || (valid && (read_pico_pmu_get()->key_state&1u)))break;
        poll_failures=valid?0:poll_failures+1;
        if(poll_failures>=3){media_uncertain("pmu-status");wake_sources_close();ESP_LOGW(TAG,"PMU status unavailable; returning to awake key polling");return false;}
        if(low)pending++;
        // 只有原始输入证明仅PMU线变化才归因；单看“有卡”或PMU快照有效不够。/ Attribute only a raw PMU-only transition; present-card or valid-PMU snapshots alone are insufficient.
        if(low || (woke && cause==ESP_SLEEP_WAKEUP_GPIO)){
            if(changed==CW_INT)inputs.pmu_irqs++;
            else inputs_uncertain(&inputs,woke && cause!=ESP_SLEEP_WAKEUP_TIMER?"nonkey-wake":"pending-irq",inputs.before,inputs.previous);
        }else if(woke && cause!=ESP_SLEEP_WAKEUP_TIMER)inputs_uncertain(&inputs,"nonkey-wake",inputs.before,inputs.previous);
        woke=false;
        // 入睡前中断线仍为低：先处理积压事件，不带着挂起唤醒入睡。/ Line still low before sleeping: drain pending events instead of sleeping with a pending wake.
        if(gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO)==0){
            // PMU应答可能刚改变CW_INT，再读一轮归因；持续低且无输入变化仍拒绝热恢复。/ PMU ACK may just have changed CW_INT: sample again; a persistent low without an input change still rejects warm restore.
            held++;vTaskDelay(pdMS_TO_TICKS(20));continue;
        }
#if PN_LOCK_SLEEP
        if(esp_light_sleep_start()!=ESP_OK){media_uncertain("sleep-failed");wake_sources_close();ESP_LOGW(TAG,"Light sleep failed; staying awake");return false;}
        cause=esp_sleep_get_wakeup_cause();
#else
        // 诊断：以同周期延时代替浅睡，保留PMU复核与按键空闲判定。/ Diagnostic: a delay of the same period replaces light sleep, keeping PMU checks and key-idle rules.
        vTaskDelay(pdMS_TO_TICKS(500));cause=ESP_SLEEP_WAKEUP_TIMER;
#endif
        if(cause==ESP_SLEEP_WAKEUP_TIMER)timers++;else if(cause==ESP_SLEEP_WAKEUP_GPIO)gpios++;else other++;
        woke=true;
    }
    wake_sources_close();
    // KEY_DOWN可先唤醒，松开后才产生KEY_SHORT；完整消费这一按键周期，避免同一次按键再次锁屏。
    // KEY_DOWN can wake before KEY_SHORT arrives on release; consume the whole press so it cannot lock again.
    if(!wait_key_idle(800,false,NULL,&inputs)){ESP_LOGW(TAG,"Wake key not idle; staying locked until release");return false;}
    // 汇总只在退出睡眠时输出，不在500ms复核中增加日志或磁盘写入。/ Summarize only on leaving sleep, without logging or disk writes on the 500 ms checks.
    (void)timers;(void)gpios;(void)other;(void)pending;(void)held;
    ESP_LOGI(TAG,"Woken by power key light_sleep=%u timer=%u gpio=%u other=%u pending=%u held=%u pmu_irq=%u continuity=%s ioe_sample=%u ioe_before=%02x ioe_after=%02x",(unsigned)PN_LOCK_SLEEP,timers,gpios,other,pending,held,inputs.pmu_irqs,pn_device_sleep_media_reason(),(unsigned)inputs.failure_sample,(unsigned)inputs.failed_before,(unsigned)inputs.failed_after);return true;
}
