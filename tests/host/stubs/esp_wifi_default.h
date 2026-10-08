#pragma once
#include "esp_netif.h"
esp_netif_t *esp_netif_create_default_wifi_ap(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
void esp_netif_destroy_default_wifi(esp_netif_t *);
esp_err_t esp_netif_attach_wifi_ap(esp_netif_t *);
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *);
esp_err_t esp_wifi_set_default_wifi_ap_handlers(void);
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void);
esp_err_t esp_wifi_clear_default_wifi_driver_and_handlers(void *);
