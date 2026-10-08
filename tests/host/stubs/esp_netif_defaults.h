#pragma once
#include "esp_netif.h"
#define ESP_NETIF_DEFAULT_WIFI_AP() ((esp_netif_config_t){.mode=1})
#define ESP_NETIF_DEFAULT_WIFI_STA() ((esp_netif_config_t){.mode=2})
