#pragma once
#include "esp_netif.h"
extern const char *pn_stub_wifi_event;
#define WIFI_EVENT pn_stub_wifi_event
#define WIFI_EVENT_AP_START 1
#define WIFI_EVENT_AP_STOP 2
#define WIFI_EVENT_STA_START 3
#define WIFI_EVENT_STA_DISCONNECTED 4
#define WIFI_EVENT_AP_STACONNECTED 5
#define WIFI_EVENT_AP_STADISCONNECTED 6
#define ESP_ERR_WIFI_NOT_INIT 101
#define ESP_ERR_WIFI_NOT_STARTED 102
#define WIFI_STORAGE_RAM 0
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 1
#define WIFI_CIPHER_TYPE_CCMP 1
#define WPA3_SAE_PWE_BOTH 3
typedef enum {WIFI_MODE_STA=1,WIFI_MODE_AP} wifi_mode_t;
typedef enum {WIFI_IF_AP,WIFI_IF_STA} wifi_interface_t;
typedef struct {int nvs_enable;} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){.nvs_enable=1})
typedef union {struct {uint8_t ssid[32],password[64];unsigned ssid_len,channel,max_connection,authmode,pairwise_cipher;} ap;struct {uint8_t ssid[32],password[64];struct {int authmode;} threshold;struct {bool capable,required;} pmf_cfg;int sae_pwe_h2e;} sta;} wifi_config_t;
esp_err_t esp_wifi_get_mode(wifi_mode_t *);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(wifi_mode_t);
esp_err_t esp_wifi_set_config(wifi_interface_t,const wifi_config_t *);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_deinit(void);
