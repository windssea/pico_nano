#pragma once
#include <stddef.h>
void *xQueueCreate(unsigned,size_t);
int xQueueSend(void *,const void *,unsigned);
int xQueueReceive(void *,void *,unsigned);
void vQueueDelete(void *);
