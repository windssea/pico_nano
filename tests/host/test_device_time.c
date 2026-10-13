/* 中文：真实无线状态机配SDK替身，覆盖对时确认/取消/资源清理。 / English: real radio state machine with SDK fakes covers confirmation, cancellation and cleanup. */
#define main pn_wifi_fixture_main
#include "test_device_wifi.c"
#undef main
#include "device_time.h"
#include "esp_netif_sntp.h"
#include <time.h>
static esp_err_t ntp_init=ESP_OK,ntp_wait=ESP_ERR_TIMEOUT;
static unsigned ntp_started,ntp_stopped;
static time_t utc=1780000000;
time_t time(time_t *out){if(out)*out=utc;return utc;}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c){assert(!strcmp(c->server,"pool.ntp.org"));if(ntp_init==ESP_OK)ntp_started++;return ntp_init;}
void esp_netif_sntp_deinit(void){ntp_stopped++;assert(ntp_stopped==ntp_started);}
esp_err_t esp_netif_sntp_sync_wait(unsigned ticks){assert(ticks==0);return ntp_wait;}
static pn_device_time_state_t snapshot(pn_device_time_t *t){pn_device_time_state_t s;assert(pn_device_time_state(t,&s)==PN_OK);return s;}
static void connect(pn_device_time_t *t){assert(pn_device_time_poll(t,now)==PN_OK);assert(initialized && !usb_pad);ip_event_got_ip_t got={0};got.ip_info.ip.addr=1;ip_handler(ip_context,IP_EVENT,IP_EVENT_STA_GOT_IP,&got);assert(pn_device_time_poll(t,now)==PN_OK);}
int main(void){
 pn_device_time_t t={0};pn_device_wifi_config_t c={.mode=PN_WIFI_STA};strcpy(c.ssid,"home");strcpy(c.password,"password123");
 assert(pn_device_time_open(&t,&c)==PN_OK && !initialized && usb_pad);assert(pn_device_time_close(&t)==PN_BUSY);connect(&t);
 assert(snapshot(&t).phase==PN_TIME_WAITING && !snapshot(&t).utc);ntp_wait=ESP_OK;fail_stop=true;assert(pn_device_time_poll(&t,now)==PN_OK);
 assert(snapshot(&t).phase==PN_TIME_STOPPING && !snapshot(&t).released && !usb_pad);assert(pn_device_time_close(&t)==PN_BUSY);
 fail_stop=false;assert(pn_device_time_poll(&t,++now)==PN_OK);assert(snapshot(&t).phase==PN_TIME_SUCCEEDED && snapshot(&t).released && snapshot(&t).utc==(uint32_t)utc && usb_pad);assert(pn_device_time_close(&t)==PN_OK);
 ntp_wait=ESP_ERR_NOT_FINISHED;assert(pn_device_time_open(&t,&c)==PN_OK);connect(&t);now+=12001;assert(pn_device_time_poll(&t,now)==PN_OK);assert(snapshot(&t).phase==PN_TIME_FAILED && snapshot(&t).sdk_error==ESP_ERR_TIMEOUT && !snapshot(&t).utc);assert(pn_device_time_close(&t)==PN_OK);
 unsigned starts=ntp_started;assert(pn_device_time_open(&t,&c)==PN_OK);assert(pn_device_time_request_stop(&t)==PN_OK);assert(pn_device_time_poll(&t,++now)==PN_OK);assert(snapshot(&t).phase==PN_TIME_CANCELLED && ntp_started==starts && usb_pad);assert(pn_device_time_close(&t)==PN_OK);
 assert(pn_device_time_open(&t,&c)==PN_OK);connect(&t);assert(pn_device_time_request_stop(&t)==PN_OK);assert(pn_device_time_poll(&t,++now)==PN_OK);assert(snapshot(&t).phase==PN_TIME_CANCELLED && !snapshot(&t).utc);assert(pn_device_time_close(&t)==PN_OK);
 ntp_init=ESP_ERR_INVALID_STATE;starts=ntp_stopped;assert(pn_device_time_open(&t,&c)==PN_OK);connect(&t);assert(snapshot(&t).phase==PN_TIME_FAILED && ntp_stopped==starts && usb_pad);assert(pn_device_time_close(&t)==PN_OK);
 ntp_init=ESP_OK;ntp_wait=ESP_OK;utc=123;assert(pn_device_time_open(&t,&c)==PN_OK);connect(&t);assert(snapshot(&t).phase==PN_TIME_FAILED && snapshot(&t).error==PN_CORRUPT);assert(pn_device_time_close(&t)==PN_OK);
 assert(!allocations && !initialized && !started && usb_pad && ntp_started==ntp_stopped);
 puts("Time sync: current confirmation, zero-wait, deadline, cancellation, cleanup retry and invalid UTC passed");return 0;
}
