#pragma once
#include "esp_err.h"
/* 中文：非阻塞SNTP的SDK替身。 / English: SDK stand-in for nonblocking SNTP. */
typedef struct {const char *server;} esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(name) ((esp_sntp_config_t){.server=(name)})
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *);
void esp_netif_sntp_deinit(void);
esp_err_t esp_netif_sntp_sync_wait(unsigned ticks);
