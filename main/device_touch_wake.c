/* SPDX-License-Identifier: Apache-2.0
 * 中文：只在自有端口配置触摸INT中断，不修改复用触摸驱动；ISR只释放信号量。
 * English: configure the touch INT interrupt in our own port without modifying the reused touch driver; the ISR only gives a semaphore. */
#include "device_touch_wake.h"
#include "read_pico_board.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static const char *TAG="pn_touch_wake";
static SemaphoreHandle_t wake;
static void IRAM_ATTR touch_edge(void *arg){(void)arg;BaseType_t woken=pdFALSE;xSemaphoreGiveFromISR(wake,&woken);if(woken)portYIELD_FROM_ISR();}
bool pn_device_touch_wake_init(void){
    if(wake)return true;
    SemaphoreHandle_t semaphore=xSemaphoreCreateBinary();if(!semaphore)return false;
    const gpio_num_t pin=(gpio_num_t)READ_PICO_TP_INT_GPIO;
    esp_err_t error=gpio_install_isr_service(0);
    if(error==ESP_ERR_INVALID_STATE)error=ESP_OK; // 服务已由其他组件安装 / Service already installed elsewhere
    wake=semaphore;
    if(error==ESP_OK)error=gpio_set_intr_type(pin,GPIO_INTR_ANYEDGE);
    if(error==ESP_OK)error=gpio_isr_handler_add(pin,touch_edge,NULL);
    if(error==ESP_OK)error=gpio_intr_enable(pin);
    if(error!=ESP_OK){(void)gpio_isr_handler_remove(pin);(void)gpio_set_intr_type(pin,GPIO_INTR_DISABLE);wake=NULL;vSemaphoreDelete(semaphore);}
    ESP_LOGI(TAG,"Touch INT wake gpio=%d status=%s",(int)pin,esp_err_to_name(error));
    return error==ESP_OK;
}
bool pn_device_touch_wake_wait(unsigned timeout_ms){
    if(!wake){vTaskDelay(pdMS_TO_TICKS(timeout_ms));return false;}
    return xSemaphoreTake(wake,pdMS_TO_TICKS(timeout_ms))==pdTRUE;
}
