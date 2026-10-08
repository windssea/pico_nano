#pragma once
#include "esp_event.h"
typedef struct {uint32_t addr;} esp_ip4_addr_t;
typedef struct {esp_ip4_addr_t ip;} esp_netif_ip_info_t;
typedef struct {esp_netif_ip_info_t ip_info;} ip_event_got_ip_t;
typedef struct {int value;} esp_netif_t;
typedef struct {int mode;} esp_netif_config_t;
esp_netif_t *esp_netif_new(const esp_netif_config_t *);
void esp_netif_destroy(esp_netif_t *);
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *);
extern const char *pn_stub_ip_event;
#define IP_EVENT pn_stub_ip_event
#define IP_EVENT_STA_GOT_IP 1
#define IP_EVENT_STA_LOST_IP 2
esp_err_t esp_netif_init(void);
esp_err_t esp_netif_get_ip_info(esp_netif_t *,esp_netif_ip_info_t *);
