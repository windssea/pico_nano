/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：LocalSend v2 UDP发现，单socket/有界报文；不访问TF或HTTP会话。
 * English: LocalSend v2 UDP discovery with one socket and bounded packets; no TF or HTTP-session access.
 */
#include "device_ls_discovery.h"
#ifdef ESP_PLATFORM
#include "pn_brand.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "esp_http_client.h"
typedef struct {int socket;atomic_bool stop,running;char announce[512],reply[512],fingerprint[33];struct sockaddr_in group;} discovery_t;
/// 解码前限制嵌套，避免UDP报文消耗递归栈。/ Bound nesting before decoding so UDP packets cannot exhaust the recursive parser stack.
static bool bounded(const char *bytes,size_t n){
 unsigned depth=0;bool string=false,escaped=false;
 for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)bytes[i];if(string){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')string=false;else if(c<32)return false;}
  else if(c=='"')string=true;else if(c=='{' || c=='['){if(++depth>8)return false;}else if(c=='}' || c==']'){if(!depth)return false;depth--;}}
 return !depth && !string && !escaped;
}
static uint64_t now_ms(void){return (uint64_t)esp_timer_get_time()/1000;}
/// 对明文HTTP的对端直接POST注册（LocalSend首选应答方式）；加密对端需TLS，留给UDP应答。
/// POST a registration directly to plain-HTTP peers (LocalSend's preferred answer); encrypted peers need TLS and rely on the UDP answer.
static void register_peer(const discovery_t *s,const struct sockaddr_in *from,unsigned port){
 char url[64];char ip[16];if(!inet_ntop(AF_INET,&from->sin_addr,ip,sizeof ip))return;
 snprintf(url,sizeof url,"http://%s:%u/api/localsend/v2/register",ip,port);
 esp_http_client_config_t config={.url=url,.method=HTTP_METHOD_POST,.timeout_ms=1500,.disable_auto_redirect=true};
 esp_http_client_handle_t client=esp_http_client_init(&config);if(!client)return;
 (void)esp_http_client_set_header(client,"Content-Type","application/json");
 (void)esp_http_client_set_post_field(client,s->reply,(int)strlen(s->reply));
 (void)esp_http_client_perform(client);(void)esp_http_client_cleanup(client);
}
static void run(void *ctx){
 discovery_t *s=ctx;char bytes[1025];uint64_t advertised=0,answered=0,started=now_ms();
 while(!atomic_load(&s->stop)){
  // 开启后前30秒每3秒公告，之后15秒；电脑端刚打开LocalSend也能很快收到。/ Announce every 3 s for the first 30 s, then every 15 s, so a freshly opened LocalSend sees us quickly.
  uint64_t now=now_ms();uint64_t period=now-started<30000?3000:15000;
  if(!advertised || now-advertised>=period){(void)sendto(s->socket,s->announce,strlen(s->announce),0,(struct sockaddr *)&s->group,sizeof s->group);advertised=now;}
  struct sockaddr_in from={0};socklen_t from_size=sizeof from;
  ssize_t n=recvfrom(s->socket,bytes,sizeof bytes-1,0,(struct sockaddr *)&from,&from_size);if(n<=0 || n>1024 || atomic_load(&s->stop))continue;
  bytes[n]=0;if(!bounded(bytes,(size_t)n))continue;cJSON *json=cJSON_ParseWithLengthOpts(bytes,(size_t)n+1,NULL,true);
  cJSON *announce=cJSON_GetObjectItemCaseSensitive(json,"announce"),*fingerprint=cJSON_GetObjectItemCaseSensitive(json,"fingerprint");
  cJSON *protocol=cJSON_GetObjectItemCaseSensitive(json,"protocol"),*peer_port=cJSON_GetObjectItemCaseSensitive(json,"port");
  bool respond=cJSON_IsObject(json) && cJSON_IsTrue(announce) && cJSON_IsString(fingerprint) && strlen(fingerprint->valuestring)<=128 && strcmp(fingerprint->valuestring,s->fingerprint);
  bool http=respond && cJSON_IsString(protocol) && !strcmp(protocol->valuestring,"http") && cJSON_IsNumber(peer_port) && peer_port->valuedouble>=1 && peer_port->valuedouble<=65535;
  unsigned port=http?(unsigned)peer_port->valuedouble:0;
  cJSON_Delete(json);now=now_ms();
  if(respond && (!answered || now-answered>=750)){
   // 组播之外再单播回给发送方：部分路由器不转发无线客户端间组播，单播应答更可靠。
   // Besides multicast, answer the sender by unicast: some routers drop multicast between wireless clients, and unicast answers get through more reliably.
   (void)sendto(s->socket,s->reply,strlen(s->reply),0,(struct sockaddr *)&s->group,sizeof s->group);
   if(from.sin_family==AF_INET){struct sockaddr_in direct=from;direct.sin_port=htons(53317);(void)sendto(s->socket,s->reply,strlen(s->reply),0,(struct sockaddr *)&direct,sizeof direct);}
   if(http)register_peer(s,&from,port);
   answered=now;
  }
 }
 atomic_store(&s->running,false);vTaskDelete(NULL);
}
pn_status_t pn_device_ls_discovery_open(pn_device_ls_discovery_t *out,const char *address,uint16_t port,const char *fingerprint){
 if(!out || !address || !port || !fingerprint || strlen(fingerprint)!=32)return PN_INVALID;
 if(out->impl)return PN_BUSY;
 discovery_t *s=calloc(1,sizeof *s);if(!s)return PN_NO_MEMORY;s->socket=-1;
 struct in_addr interface;if(inet_pton(AF_INET,address,&interface)!=1){free(s);return PN_INVALID;}
 memcpy(s->fingerprint,fingerprint,33);
 const char *format="{\"alias\":\"" PN_BRAND_NAME "\",\"version\":\"2.1\",\"deviceModel\":\"" PN_BRAND_NAME "\",\"deviceType\":\"headless\",\"fingerprint\":\"%s\",\"port\":%u,\"protocol\":\"http\",\"download\":false,\"announce\":%s}";
 snprintf(s->announce,sizeof s->announce,format,fingerprint,(unsigned)port,"true");snprintf(s->reply,sizeof s->reply,format,fingerprint,(unsigned)port,"false");
 s->group=(struct sockaddr_in){.sin_family=AF_INET,.sin_port=htons(53317)};(void)inet_pton(AF_INET,"224.0.0.167",&s->group.sin_addr);
 s->socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s->socket<0){free(s);return PN_IO;}
 int reuse=1,ttl=1;struct timeval timeout={.tv_usec=500000};struct sockaddr_in local={.sin_family=AF_INET,.sin_port=htons(53317),.sin_addr={.s_addr=htonl(INADDR_ANY)}};
 struct ip_mreq member={.imr_multiaddr=s->group.sin_addr,.imr_interface=interface};
 bool failed=setsockopt(s->socket,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse) || setsockopt(s->socket,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout) || setsockopt(s->socket,IPPROTO_IP,IP_MULTICAST_TTL,&ttl,sizeof ttl) || setsockopt(s->socket,IPPROTO_IP,IP_MULTICAST_IF,&interface,sizeof interface) || bind(s->socket,(struct sockaddr *)&local,sizeof local) || setsockopt(s->socket,IPPROTO_IP,IP_ADD_MEMBERSHIP,&member,sizeof member);
 if(failed){close(s->socket);free(s);return PN_IO;}
 atomic_init(&s->stop,false);atomic_init(&s->running,true);
 // 只做UDP收发与JSON解析，栈放PSRAM，不占片内。/ UDP and JSON only, so the stack lives in PSRAM instead of internal RAM.
 if(xTaskCreatePinnedToCoreWithCaps(run,"pn_ls_discovery",8192,s,3,NULL,tskNO_AFFINITY,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){close(s->socket);free(s);return PN_NO_MEMORY;}
 out->impl=s;return PN_OK;
}
pn_status_t pn_device_ls_discovery_close(pn_device_ls_discovery_t *out){
 if(!out)return PN_INVALID;
 if(!out->impl)return PN_OK;
 discovery_t *s=out->impl;atomic_store(&s->stop,true);(void)shutdown(s->socket,SHUT_RD);
 uint64_t started=now_ms();while(atomic_load(&s->running)){if(now_ms()-started>=3000)return PN_BUSY;vTaskDelay(pdMS_TO_TICKS(10));}
 close(s->socket);free(s);out->impl=NULL;return PN_OK;
}
#else
/* 原生测试只替换无线socket边界，不宣称真机发现通过。/ Native tests replace only the radio/socket boundary without claiming hardware discovery. */
pn_status_t pn_device_ls_discovery_open(pn_device_ls_discovery_t *out,const char *address,uint16_t port,const char *fingerprint){return out && address && port && fingerprint?PN_OK:PN_INVALID;}
pn_status_t pn_device_ls_discovery_close(pn_device_ls_discovery_t *out){return out?PN_OK:PN_INVALID;}
#endif
