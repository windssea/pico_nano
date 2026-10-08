#pragma once
#include <stddef.h>
/* 启动测试熵替身；测试内定义确定值。/ Boot-test entropy stub; the test defines deterministic bytes. */
void esp_fill_random(void *,size_t);
