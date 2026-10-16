/* 中文：只观察首次启动看门狗调用，不代表真实时间。 / English: observe boot-watchdog calls without representing real elapsed time. */
#pragma once
#include "esp_ota_ops.h"
typedef void *esp_timer_handle_t;
typedef struct {void (*callback)(void *);void *arg;const char *name;} esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t *,esp_timer_handle_t *);
esp_err_t esp_timer_start_once(esp_timer_handle_t,uint64_t);
esp_err_t esp_timer_stop(esp_timer_handle_t);
esp_err_t esp_timer_delete(esp_timer_handle_t);
