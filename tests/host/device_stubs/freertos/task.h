#pragma once
typedef void *TaskHandle_t; ///< 仅主机任务句柄替身 / Host-only task handle substitute
void vTaskDelete(void *);void vTaskDelay(unsigned);int xTaskCreatePinnedToCore(void (*)(void *),const char *,unsigned,void *,unsigned,void *,int);
/// 任务通知替身 / Task-notification doubles
void xTaskNotifyGive(TaskHandle_t);unsigned ulTaskNotifyTake(int,unsigned);TaskHandle_t xTaskGetCurrentTaskHandle(void);
