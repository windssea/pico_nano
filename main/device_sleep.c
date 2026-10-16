/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：等按键空闲，使用IOE_INT与定时复核浅睡，只有PMU确认的新按键才解锁。
 * English: wait for key idle, light-sleep with IOE_INT and timer checks, and unlock only for a new PMU-confirmed key.
 * 冻结：不支持拿起唤醒；不改变PMU设置。/ Frozen: no pickup wake; PMU settings untouched.
 */
#include "device_sleep.h"
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
/* 电源键有效松开150ms才算空闲；其他共享中断不能阻止解锁。/ Require a valid released key for 150 ms; unrelated shared interrupts cannot prevent unlocking. */
static bool wait_key_idle(int timeout_ms,bool watch_new,bool *new_key){
    int64_t start=now_ms(),high_from=0;
    bool released=false;
    while(now_ms()-start<timeout_ms){
        if(!watch_new)read_pico_pmu_drain_events();
        bool valid=read_pico_pmu_poll()==ESP_OK && read_pico_pmu_get()->status_ok;
        bool held=!valid || (read_pico_pmu_get()->key_state&0x01)!=0;
        bool key=watch_new && read_pico_pmu_take_key_wakeup();
        if(watch_new && released && (key || (valid && held)))*new_key=true;
        if(!held)released=true;
        read_pico_clear_ioe_int();
        if(!held){if(!high_from)high_from=now_ms();if(now_ms()-high_from>=150)return true;}
        else high_from=0;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}
static bool media_unchanged;
bool pn_device_sleep_media_unchanged(void){return media_unchanged;}
/// 退出时归还本适配器的显式唤醒源，不能污染之后的自动浅睡。/ Release this adapter's explicit wake sources without leaking them into later automatic light sleep.
static void wake_sources_close(void){
    (void)gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}
bool pn_device_sleep_until_key(void){
    media_unchanged=true;
    bool early_key=false;
    if(!wait_key_idle(800,true,&early_key)){media_unchanged=false;ESP_LOGW(TAG,"Lock key not idle; staying awake");return false;}
    if(early_key){ESP_LOGI(TAG,"Wake key received while preparing sleep");return true;}
    gpio_config_t io={.pin_bit_mask=1ULL<<READ_PICO_IOE_INT_GPIO,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    if(gpio_config(&io)!=ESP_OK){media_unchanged=false;ESP_LOGW(TAG,"IOE_INT config failed; staying awake");return false;}
    if(gpio_wakeup_enable((gpio_num_t)READ_PICO_IOE_INT_GPIO,GPIO_INTR_LOW_LEVEL)!=ESP_OK || esp_sleep_enable_gpio_wakeup()!=ESP_OK){
        media_unchanged=false;(void)gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);ESP_LOGW(TAG,"Wake source unavailable; staying awake");return false;}
    // 不能仅依赖读清式扩展器中断：500ms复核PMU队列，不刷新屏幕，也不访问TF。
    // A read-cleared expander IRQ cannot be the only wake path: check the PMU queue every 500 ms without drawing or accessing TF.
    if(esp_sleep_enable_timer_wakeup(500000)!=ESP_OK){media_unchanged=false;wake_sources_close();ESP_LOGW(TAG,"Wake timer unavailable; staying awake");return false;}
    unsigned poll_failures=0;
    for(;;){
        // 先读PMU再清扩展器；清中断不代表PMU队列为空，首次入睡前的新按键也有效。
        // Read the PMU before clearing the expander: clearing an IRQ does not empty the PMU queue, and a new key before the first sleep is valid too.
        bool low=gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO)==0;
        bool key=read_pico_pmu_take_key_wakeup();
#if !PN_LOCK_SLEEP
        key=key || pn_device_test_console_take_power();
#endif
        bool valid=read_pico_pmu_poll()==ESP_OK && read_pico_pmu_get()->status_ok;
        if(key || (valid && (read_pico_pmu_get()->key_state&1u)))break;
        poll_failures=valid?0:poll_failures+1;
        if(poll_failures>=3){media_unchanged=false;wake_sources_close();ESP_LOGW(TAG,"PMU status unavailable; returning to awake key polling");return false;}
        if(low)media_unchanged=false;
        read_pico_clear_ioe_int();
        // 入睡前中断线仍为低：先处理积压事件，不带着挂起唤醒入睡。/ Line still low before sleeping: drain pending events instead of sleeping with a pending wake.
        if(gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO)==0){
            media_unchanged=false;vTaskDelay(pdMS_TO_TICKS(20));continue;
        }
#if PN_LOCK_SLEEP
        if(esp_light_sleep_start()!=ESP_OK){media_unchanged=false;wake_sources_close();ESP_LOGW(TAG,"Light sleep failed; staying awake");return false;}
        esp_sleep_wakeup_cause_t cause=esp_sleep_get_wakeup_cause();
#else
        // 诊断：以同周期延时代替浅睡，保留PMU复核与按键空闲判定。/ Diagnostic: a delay of the same period replaces light sleep, keeping PMU checks and key-idle rules.
        vTaskDelay(pdMS_TO_TICKS(500));esp_sleep_wakeup_cause_t cause=ESP_SLEEP_WAKEUP_TIMER;
#endif
        key=read_pico_pmu_take_key_wakeup();valid=read_pico_pmu_poll()==ESP_OK && read_pico_pmu_get()->status_ok;
        if(key || (valid && (read_pico_pmu_get()->key_state&1u)))break;
        poll_failures=valid?0:poll_failures+1;
        if(poll_failures>=3){media_unchanged=false;wake_sources_close();ESP_LOGW(TAG,"PMU status unavailable; returning to awake key polling");return false;}
        if(cause!=ESP_SLEEP_WAKEUP_TIMER)media_unchanged=false; // 定时复核不表示换卡，其他非电源唤醒仍禁用热恢复。/ Timer checks do not imply a card change; other non-key wakes still disable warm restore.
    }
    wake_sources_close();
    // KEY_DOWN可先唤醒，松开后才产生KEY_SHORT；完整消费这一按键周期，避免同一次按键再次锁屏。
    // KEY_DOWN can wake before KEY_SHORT arrives on release; consume the whole press so it cannot lock again.
    if(!wait_key_idle(800,false,NULL)){ESP_LOGW(TAG,"Wake key not idle; staying locked until release");return false;}
    ESP_LOGI(TAG,"Woken by power key");return true;
}
