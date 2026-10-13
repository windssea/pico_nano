#pragma once
void *xSemaphoreCreateBinary(void);
int xSemaphoreTake(void *,unsigned);
int xSemaphoreGive(void *);
void vSemaphoreDelete(void *);
