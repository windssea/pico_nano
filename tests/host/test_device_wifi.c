/* 中文：同源WiFi生命周期，SDK驱动/事件/RNG显式stub。/ English: shared WiFi lifecycle with explicitly stubbed SDK drivers, events and RNG. */
#include "device_wifi.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
const char *pn_stub_wifi_event="wifi",*pn_stub_ip_event="ip";
static bool initialized,started,entropy,shared_loop,fail_stop,fail_start,fail_netif;
static unsigned connections,deleted,allocations;
static uint64_t now=1000;
static wifi_mode_t mode;
static wifi_config_t config;
static esp_event_handler_t wifi_handler,ip_handler;
static void *wifi_context,*ip_context;
static bool foreign_netif;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key){(void)key;static esp_netif_t foreign;return foreign_netif?&foreign:NULL;}
esp_netif_t *esp_netif_new(const esp_netif_config_t *cfg){(void)cfg;return fail_netif?NULL:malloc(sizeof(esp_netif_t));}
void esp_netif_destroy(esp_netif_t *netif){assert(!initialized && !started);free(netif);}
esp_err_t esp_netif_attach_wifi_ap(esp_netif_t *netif){assert(netif);return ESP_OK;}
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *netif){assert(netif);return ESP_OK;}
esp_err_t esp_wifi_set_default_wifi_ap_handlers(void){return ESP_OK;}
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void){return ESP_OK;}
esp_err_t esp_wifi_clear_default_wifi_driver_and_handlers(void *netif){assert(netif);return ESP_OK;}
void *heap_caps_malloc(size_t n,unsigned caps){(void)caps;void *p=malloc(n);if(p)allocations++;return p;}
void heap_caps_free(void *p){if(p){allocations--;free(p);}}
void *xSemaphoreCreateMutex(void){pthread_mutex_t *p=malloc(sizeof *p);if(p)pthread_mutex_init(p,NULL);return p;}
int xSemaphoreTake(void *p,unsigned delay){(void)delay;return pthread_mutex_lock(p)==0;}
int xSemaphoreGive(void *p){return pthread_mutex_unlock(p)==0;}
void vSemaphoreDelete(void *p){pthread_mutex_destroy(p);free(p);}
int64_t esp_timer_get_time(void){return (int64_t)now*1000;}
void bootloader_random_enable(void){assert(!initialized);entropy=true;}
void bootloader_random_disable(void){entropy=false;}
void esp_fill_random(void *out,size_t n){assert(entropy && !initialized);static unsigned seed;uint8_t *p=out;while(n--)*p++=(uint8_t)++seed;}
esp_err_t esp_read_mac(uint8_t *mac,int kind){(void)kind;memset(mac,0x12,6);return ESP_OK;}
esp_err_t esp_netif_init(void){return ESP_OK;}
esp_err_t esp_event_loop_create_default(void){return shared_loop?ESP_ERR_INVALID_STATE:ESP_OK;}
esp_err_t esp_event_loop_delete_default(void){assert(!initialized);deleted++;return ESP_OK;}
esp_netif_t *esp_netif_create_default_wifi_ap(void){if(fail_netif)return NULL;assert(!"asserting SDK constructor is forbidden");return NULL;}
esp_netif_t *esp_netif_create_default_wifi_sta(void){if(fail_netif)return NULL;assert(!"asserting SDK constructor is forbidden");return NULL;}
void esp_netif_destroy_default_wifi(esp_netif_t *netif){assert(!initialized && !started);free(netif);}
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif,esp_netif_ip_info_t *out){assert(netif);uint8_t *ip=(uint8_t *)&out->ip.addr;ip[0]=192;ip[1]=168;ip[2]=4;ip[3]=1;return ESP_OK;}
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int32_t id,esp_event_handler_t handler,void *ctx,esp_event_handler_instance_t *instance){(void)id;if(!strcmp(base,WIFI_EVENT)){wifi_handler=handler;wifi_context=ctx;*instance=&wifi_handler;}else{ip_handler=handler;ip_context=ctx;*instance=&ip_handler;}return ESP_OK;}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,int32_t id,esp_event_handler_instance_t instance){(void)id;(void)instance;if(!strcmp(base,WIFI_EVENT))wifi_handler=NULL;else ip_handler=NULL;return ESP_OK;}
esp_err_t esp_wifi_get_mode(wifi_mode_t *out){if(!initialized)return ESP_ERR_WIFI_NOT_INIT;*out=mode;return ESP_OK;}
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg){assert(!entropy && !cfg->nvs_enable);initialized=true;return ESP_OK;}
esp_err_t esp_wifi_set_storage(int storage){assert(storage==WIFI_STORAGE_RAM);return ESP_OK;}
esp_err_t esp_wifi_set_mode(wifi_mode_t value){mode=value;return ESP_OK;}
esp_err_t esp_wifi_set_config(wifi_interface_t iface,const wifi_config_t *value){(void)iface;config=*value;return ESP_OK;}
esp_err_t esp_wifi_start(void){started=true;wifi_handler(wifi_context,WIFI_EVENT,mode==WIFI_MODE_AP?WIFI_EVENT_AP_START:WIFI_EVENT_STA_START,NULL);return fail_start?ESP_FAIL:ESP_OK;}
esp_err_t esp_wifi_connect(void){connections++;return ESP_OK;}
esp_err_t esp_wifi_stop(void){if(fail_stop)return ESP_FAIL;started=false;return ESP_OK;}
esp_err_t esp_wifi_deinit(void){assert(!started);initialized=false;return ESP_OK;}
int main(void){
 pn_device_wifi_t wifi={0};pn_device_wifi_config_t settings={.mode=PN_WIFI_AP};pn_device_wifi_state_t state;
 assert(pn_device_wifi_open(&wifi,&settings)==PN_OK);assert(config.ap.authmode==WIFI_AUTH_WPA2_PSK && strlen((char *)config.ap.password)==12 && !entropy);
 assert(pn_device_wifi_poll(&wifi)==PN_OK);assert(pn_device_wifi_state(&wifi,&state)==PN_OK && state.phase==PN_WIFI_READY && !strcmp(state.address,"192.168.4.1"));char old[13];strcpy(old,state.ap_password);
 fail_stop=true;assert(pn_device_wifi_close(&wifi)==PN_IO && wifi.impl && wifi_handler);fail_stop=false;assert(pn_device_wifi_close(&wifi)==PN_OK && !wifi.impl && !allocations && deleted==1);
 fail_start=true;assert(pn_device_wifi_open(&wifi,&settings)==PN_IO && !wifi.impl && !initialized && !started && !allocations);fail_start=false;
 shared_loop=true;assert(pn_device_wifi_open(&wifi,&settings)==PN_OK);assert(pn_device_wifi_poll(&wifi)==PN_OK);assert(pn_device_wifi_state(&wifi,&state)==PN_OK && strcmp(old,state.ap_password));assert(pn_device_wifi_close(&wifi)==PN_OK && deleted==2);
 settings.mode=PN_WIFI_STA;strcpy(settings.ssid,"家里的网络");strcpy(settings.password,"reader-password");assert(pn_device_wifi_open(&wifi,&settings)==PN_OK);
 assert(pn_device_wifi_poll(&wifi)==PN_OK && connections==1);assert(pn_device_wifi_state(&wifi,&state)==PN_OK && !state.ap_password[0]);
 ip_event_got_ip_t got={0};uint8_t *ip=(uint8_t *)&got.ip_info.ip.addr;ip[0]=10;ip[3]=2;ip_handler(ip_context,IP_EVENT,IP_EVENT_STA_GOT_IP,&got);assert(pn_device_wifi_state(&wifi,&state)==PN_OK && state.phase==PN_WIFI_READY && !strcmp(state.address,"10.0.0.2"));
 wifi_handler(wifi_context,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);for(unsigned i=0;i<3;i++){now+=1000;assert(pn_device_wifi_poll(&wifi)==PN_OK);wifi_handler(wifi_context,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);}
 now+=1000;assert(pn_device_wifi_poll(&wifi)==PN_IO);assert(pn_device_wifi_state(&wifi,&state)==PN_OK && state.phase==PN_WIFI_FAILED && state.mode==PN_WIFI_STA);
 assert(pn_device_wifi_close(&wifi)==PN_OK && !allocations && !wifi_handler && !ip_handler);
 initialized=true;assert(pn_device_wifi_open(&wifi,&settings)==PN_BUSY && !wifi.impl);initialized=false;
 foreign_netif=true;assert(pn_device_wifi_open(&wifi,&settings)==PN_BUSY && !wifi.impl);foreign_netif=false;
 fail_netif=true;assert(pn_device_wifi_open(&wifi,&settings)==PN_IO && !wifi.impl && !allocations);fail_netif=false;
 assert(pn_device_wifi_open(&wifi,&settings)==PN_OK);now+=20000;assert(pn_device_wifi_poll(&wifi)==PN_IO);assert(pn_device_wifi_close(&wifi)==PN_OK && !allocations);
 puts("Device WiFi: SDK stubs, fresh AP credentials, entropy ordering, RAM config, STA retry bounds, stop retry and shared-loop preservation passed");return 0;
}
