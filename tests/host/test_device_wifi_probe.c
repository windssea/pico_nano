/* 中文：真实无线状态机配SDK替身，覆盖保存前连接测试的成功、认证失败、找不到网络、超时、取消与资源归还。
 * English: the real radio state machine with SDK fakes covers the pre-save connection test: success, authentication failure, missing network, timeout, cancellation and resource release. */
#define main pn_wifi_fixture_main
#include "test_device_wifi.c"
#undef main
#include "device_wifi_probe.h"
static pn_device_wifi_probe_state_t snapshot(pn_device_wifi_probe_t *p){pn_device_wifi_probe_state_t s;assert(pn_device_wifi_probe_state(p,&s)==PN_OK);return s;}
/// 每轮推进时间并投递一次断开，直到测试结束。/ Advance time and deliver one disconnect per round until the test ends.
static pn_device_wifi_probe_state_t fail_with(pn_device_wifi_probe_t *p,uint8_t reason){
 wifi_event_sta_disconnected_t lost={.reason=reason};
 for(unsigned i=0;i<10 && !snapshot(p).released;i++){if(wifi_handler)wifi_handler(wifi_context,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&lost);now+=1000;assert(pn_device_wifi_probe_poll(p,now)==PN_OK);}
 return snapshot(p);
}
int main(void){
 pn_device_wifi_probe_t p={0};pn_device_wifi_config_t c={.mode=PN_WIFI_STA};strcpy(c.ssid,"home");strcpy(c.password,"password123");
 pn_device_wifi_config_t ap={.mode=PN_WIFI_AP},short_pw=c;strcpy(short_pw.password,"short");
 assert(pn_device_wifi_probe_open(&p,&ap)==PN_INVALID && pn_device_wifi_probe_open(&p,&short_pw)==PN_INVALID && !p.impl);
 /* 拿到IP即成功，并完整关闭无线、恢复USB。/ An IP address is success; the radio closes fully and USB is restored. */
 assert(pn_device_wifi_probe_open(&p,&c)==PN_OK && !initialized && pn_device_wifi_probe_open(&p,&c)==PN_BUSY);
 assert(pn_device_wifi_probe_poll(&p,now)==PN_OK && initialized && !usb_pad && !snapshot(&p).released && pn_device_wifi_probe_close(&p)==PN_BUSY);
 ip_event_got_ip_t got={0};got.ip_info.ip.addr=1;ip_handler(ip_context,IP_EVENT,IP_EVENT_STA_GOT_IP,&got);
 assert(pn_device_wifi_probe_poll(&p,now)==PN_OK && snapshot(&p).released && snapshot(&p).result==PN_OK && !initialized && usb_pad);
 assert(pn_device_wifi_probe_close(&p)==PN_OK && !p.impl && !allocations);
 /* 认证失败与找不到网络按断开原因归类。/ Authentication failures and missing networks are classified by disconnect reason. */
 assert(pn_device_wifi_probe_open(&p,&c)==PN_OK && pn_device_wifi_probe_poll(&p,now)==PN_OK);
 pn_device_wifi_probe_state_t s=fail_with(&p,WIFI_REASON_AUTH_FAIL);assert(s.released && s.result==PN_INVALID && s.reason==WIFI_REASON_AUTH_FAIL && !initialized);
 assert(pn_device_wifi_probe_close(&p)==PN_OK && !allocations);
 assert(pn_device_wifi_probe_open(&p,&c)==PN_OK && pn_device_wifi_probe_poll(&p,now)==PN_OK);
 s=fail_with(&p,WIFI_REASON_NO_AP_FOUND);assert(s.released && s.result==PN_EMPTY && pn_device_wifi_probe_close(&p)==PN_OK);
 assert(pn_device_wifi_probe_open(&p,&c)==PN_OK && pn_device_wifi_probe_poll(&p,now)==PN_OK);
 s=fail_with(&p,WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT);assert(s.result==PN_INVALID && pn_device_wifi_probe_close(&p)==PN_OK);
 /* 无断开原因的超时归为IO。/ A timeout without a disconnect reason is IO. */
 assert(pn_device_wifi_probe_open(&p,&c)==PN_OK && pn_device_wifi_probe_poll(&p,now)==PN_OK);
 now+=30000;assert(pn_device_wifi_probe_poll(&p,now)==PN_OK && snapshot(&p).released && snapshot(&p).result==PN_IO && pn_device_wifi_probe_close(&p)==PN_OK);
 /* 取消：未开始时不启动无线；连接中取消也完整关闭。/ Cancel: never start the radio before starting; cancelling mid-connect still closes fully. */
 unsigned before=connections;assert(pn_device_wifi_probe_open(&p,&c)==PN_OK && pn_device_wifi_probe_stop(&p)==PN_OK && pn_device_wifi_probe_poll(&p,now)==PN_OK);
 assert(snapshot(&p).released && snapshot(&p).result==PN_CANCELLED && connections==before && !initialized && pn_device_wifi_probe_close(&p)==PN_OK);
 assert(pn_device_wifi_probe_open(&p,&c)==PN_OK && pn_device_wifi_probe_poll(&p,now)==PN_OK && initialized && pn_device_wifi_probe_stop(&p)==PN_OK && pn_device_wifi_probe_poll(&p,++now)==PN_OK);
 assert(snapshot(&p).released && snapshot(&p).result==PN_CANCELLED && !initialized && usb_pad && pn_device_wifi_probe_close(&p)==PN_OK && !allocations);
 /* 关闭失败保留句柄重试。/ A failed close retains the handle for retry. */
 assert(pn_device_wifi_probe_open(&p,&c)==PN_OK && pn_device_wifi_probe_poll(&p,now)==PN_OK);fail_stop=true;assert(pn_device_wifi_probe_stop(&p)==PN_OK && pn_device_wifi_probe_poll(&p,++now)==PN_OK && !snapshot(&p).released && pn_device_wifi_probe_close(&p)==PN_BUSY);
 fail_stop=false;assert(pn_device_wifi_probe_poll(&p,++now)==PN_OK && snapshot(&p).released && pn_device_wifi_probe_close(&p)==PN_OK && !allocations && usb_suspend==usb_restore);
 assert(pn_device_wifi_probe_classify(WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY)==PN_EMPTY && pn_device_wifi_probe_classify(WIFI_REASON_CONNECTION_FAIL)==PN_IO && pn_device_wifi_probe_classify(0)==PN_IO);
 puts("WiFi probe: IP success, auth/missing/handshake classification, timeout, cancel before/during connect, close retry and USB restore passed");return 0;
}
