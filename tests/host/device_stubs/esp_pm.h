#pragma once
#include "esp_err.h"
typedef void *esp_pm_lock_handle_t;
typedef enum {ESP_PM_NO_LIGHT_SLEEP} esp_pm_lock_type_t;
esp_err_t esp_pm_lock_create(esp_pm_lock_type_t type,int arg,const char *name,esp_pm_lock_handle_t *out);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t lock);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t lock);
