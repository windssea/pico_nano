#pragma once
typedef void *TaskHandle_t; ///< 仅主机任务句柄替身 / Host-only task handle substitute
void vTaskDelete(void *);void vTaskDelay(unsigned);int xTaskCreatePinnedToCore(void (*)(void *),const char *,unsigned,void *,unsigned,void *,int);
