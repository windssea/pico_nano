#pragma once
#include "esp_err.h"
#include <stdint.h>
/* 主机替身：FAT容量查询。/ Host stand-in: FAT capacity query. */
esp_err_t esp_vfs_fat_info(const char *base_path,uint64_t *out_total_bytes,uint64_t *out_free_bytes);
