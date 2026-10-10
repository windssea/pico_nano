#pragma once
/* 主机替身：应用描述。/ Host stand-in: application description. */
typedef struct {char version[32];} esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);
