/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：按参考固件已验证的顺序：等按键空闲→配置IOE_INT低电平唤醒→循环浅睡直到PMU确认按键。
 * English: following the reference firmware's validated order: wait for key idle → arm IOE_INT low-level wake → loop light sleep until the PMU confirms a key.
 * 冻结：不支持拿起唤醒；不改变PMU设置。/ Frozen: no pickup wake; PMU settings untouched.
 */
#include "device_sleep.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "read_pico_pmu.h"
static const char *TAG="pn_sleep";
static int64_t now_ms(void){return esp_timer_get_time()/1000;}
/* 电源键松开且中断线保持高电平150ms才算空闲，最多等800ms。/ Idle means the key is released and the interrupt line stays high for 150 ms, waiting at most 800 ms. */
static void wait_key_idle(int timeout_ms){
    int64_t start=now_ms(),high_from=0;
    while(now_ms()-start<timeout_ms){
        read_pico_pmu_drain_events();read_pico_clear_ioe_int();
        bool held=read_pico_pmu_poll()==ESP_OK && (read_pico_pmu_get()->key_state&0x01)!=0;
        bool high=gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO)!=0;
        if(!held && high){if(!high_from)high_from=now_ms();if(now_ms()-high_from>=150)return;}
        else high_from=0;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
bool pn_device_sleep_until_key(void){
    wait_key_idle(800);
    gpio_config_t io={.pin_bit_mask=1ULL<<READ_PICO_IOE_INT_GPIO,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    if(gpio_config(&io)!=ESP_OK){ESP_LOGW(TAG,"IOE_INT config failed; staying awake");return false;}
    read_pico_clear_ioe_int();
    if(gpio_wakeup_enable((gpio_num_t)READ_PICO_IOE_INT_GPIO,GPIO_INTR_LOW_LEVEL)!=ESP_OK || esp_sleep_enable_gpio_wakeup()!=ESP_OK){
        (void)gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);ESP_LOGW(TAG,"Wake source unavailable; staying awake");return false;}
    bool slept=false;
    for(;;){
        read_pico_clear_ioe_int();
        // 入睡前中断线仍为低：先处理积压事件，不带着挂起唤醒入睡。/ Line still low before sleeping: drain pending events instead of sleeping with a pending wake.
        if(gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO)==0){
            if(slept && read_pico_pmu_take_key_wakeup())break;
            read_pico_pmu_drain_events();read_pico_clear_ioe_int();vTaskDelay(pdMS_TO_TICKS(20));continue;
        }
        (void)esp_light_sleep_start();slept=true;
        read_pico_clear_ioe_int();
        if(read_pico_pmu_take_key_wakeup())break;
    }
    (void)gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);
    ESP_LOGI(TAG,"Woken by power key");return true;
}
