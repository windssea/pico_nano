/* 中文：真实设备handler、JSON和上传worker；SDK传输API显式stub。/ English: real device handler, JSON and upload worker with explicitly stubbed SDK transport APIs. */
#define _POSIX_C_SOURCE 200809L
#include "device_transfer_http.h"
#include "device_ls_discovery.h"
#include "esp_http_server.h"
#include "esp_httpd_priv.h"
#include "cJSON.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
static httpd_uri_t route,ls_route; ///< 网页80与LocalSend53317两个监听 / Web 80 and LocalSend 53317 listeners
static unsigned ls_routes;
static char scratch[2048],response[4096];
static unsigned code,headers;
static const uint8_t *body;
static size_t body_at;
static uint64_t receive_delay;
static bool incomplete;
static bool wrong_peer;
int httpd_req_to_sockfd(httpd_req_t *r){(void)r;return 50;}
int getpeername(int fd,struct sockaddr *address,socklen_t *length){assert(fd==50 && *length>=sizeof(struct sockaddr_in));struct sockaddr_in peer={.sin_family=AF_INET};uint8_t *ip=(uint8_t *)&peer.sin_addr;ip[0]=192;ip[1]=168;ip[2]=4;ip[3]=wrong_peer?3:2;memcpy(address,&peer,sizeof peer);*length=sizeof peer;return 0;}
static _Atomic uint64_t now=1000;
static size_t native_allocations;
static _Atomic bool http_stop_fails;
static _Atomic bool discovery_fails;
static _Atomic unsigned discovery_attempts;
pn_status_t __real_pn_device_ls_discovery_open(pn_device_ls_discovery_t *,const char *,uint16_t,const char *);
pn_status_t __wrap_pn_device_ls_discovery_open(pn_device_ls_discovery_t *out,const char *address,uint16_t port,const char *fingerprint){discovery_attempts++;return discovery_fails?PN_IO:__real_pn_device_ls_discovery_open(out,address,port,fingerprint);}
static void *global_context;
static httpd_recv_func_t receive;
static esp_err_t (*open_socket)(httpd_handle_t,int);
void *httpd_uri_match_wildcard;
int64_t esp_timer_get_time(void){return (int64_t)now*1000;}
void esp_fill_random(void *p,size_t n){static unsigned seed=7;uint8_t *b=p;while(n--)*b++=(uint8_t)++seed;}
void *heap_caps_malloc(size_t n,unsigned caps){(void)caps;void *p=malloc(n);if(p)native_allocations++;return p;}
void heap_caps_free(void *p){if(p){native_allocations--;free(p);}}
void *xSemaphoreCreateMutex(void){pthread_mutex_t *p=malloc(sizeof *p);if(p)pthread_mutex_init(p,NULL);return p;}
int xSemaphoreTake(void *p,unsigned timeout){(void)timeout;return pthread_mutex_lock(p)==0;}
int xSemaphoreGive(void *p){return pthread_mutex_unlock(p)==0;}
void vSemaphoreDelete(void *p){pthread_mutex_destroy(p);free(p);}
esp_err_t httpd_start(httpd_handle_t *h,const httpd_config_t *cfg){
    assert(cfg->max_uri_handlers>=4 && cfg->lru_purge_enable && cfg->global_user_ctx);
    // LocalSend监听在独立控制端口、栈放PSRAM，连接上限不挤占网页。/ The LocalSend listener uses its own control port and a PSRAM stack, with a socket cap separate from the web page.
    if(cfg->server_port==53317){assert(cfg->ctrl_port!=32768 && cfg->max_open_sockets==2 && cfg->task_caps && global_context==cfg->global_user_ctx);*h=&ls_route;ls_routes=0;return ESP_OK;}
    assert(cfg->max_open_sockets==4);*h=&route;global_context=cfg->global_user_ctx;open_socket=cfg->open_fn;return ESP_OK;}
void *httpd_get_global_user_ctx(httpd_handle_t h){assert(h==&route || h==&ls_route);return global_context;}
esp_err_t httpd_sess_set_recv_override(httpd_handle_t h,int fd,httpd_recv_func_t callback){(void)fd;assert(h==&route || h==&ls_route);receive=callback;return ESP_OK;}
esp_err_t httpd_stop(httpd_handle_t h){assert(h==&route || h==&ls_route);return http_stop_fails?ESP_FAIL:ESP_OK;}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *r){assert(h==&route || h==&ls_route);if(h==&ls_route){ls_route=*r;ls_routes++;}else route=*r;return ESP_OK;}
static const char *header(const char *name){const char *at=scratch;for(unsigned i=0;i<headers;i++){const char *colon=strchr(at,':');if(colon && (size_t)(colon-at)==strlen(name) && !strncasecmp(at,name,strlen(name))){while(*++colon==' ');return colon;}if(i+1<headers){at+=strlen(at)+1;while(!*at)at++;}}return NULL;}
size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *name){(void)r;const char *value=header(name);return value?strlen(value):0;}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *name,char *out,size_t cap){(void)r;const char *value=header(name);if(!value || strlen(value)>=cap)return ESP_FAIL;strcpy(out,value);return ESP_OK;}
int httpd_req_recv(httpd_req_t *r,char *out,size_t cap){now+=receive_delay;if(incomplete && body_at)return -1;size_t n=r->content_len-body_at;if(n>cap)n=cap;if(n>13)n=13;if(incomplete)n=1;memcpy(out,body+body_at,n);body_at+=n;return (int)n;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *value){(void)r;code=(unsigned)atoi(value);return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *value){(void)r;assert(value);return ESP_OK;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *value){(void)r;assert(key && value);return ESP_OK;}
esp_err_t httpd_resp_send(httpd_req_t *r,const char *data,int n){(void)r;if(n<0)n=(int)strlen(data);if(n<(int)sizeof response){memcpy(response,data,(size_t)n);response[n]=0;}return ESP_OK;}
static void add(const char *line){size_t at=0;for(unsigned i=0;i<headers;i++)at+=strlen(scratch+at)+1;strcpy(scratch+at,line);headers++;}
static cJSON *request(int method,const char *uri,const void *bytes,size_t n,const char *token,const char *extra){
 memset(scratch,0,sizeof scratch);headers=0;add("Host: 192.168.4.1");if(strncmp(uri,"/api/localsend/",15))add("Origin: http://192.168.4.1");
 char line[256];if(token){snprintf(line,sizeof line,"Authorization: Bearer %s",token);add(line);}
 if(method==HTTP_POST || method==HTTP_PUT){snprintf(line,sizeof line,"Content-Length: %zu",n);add(line);if(method==HTTP_POST)add("Content-Type: application/json");}
 if(method==HTTP_PUT)add("X-Offset: 0");
 if(extra)add(extra);
 body=bytes;body_at=0;code=0;response[0]=0;
 struct httpd_req_aux aux={.scratch=scratch,.scratch_size_limit=sizeof scratch-1,.scratch_cur_size=sizeof scratch-1,.req_hdrs_count=headers};
 httpd_req_t r={.handle=&route,.method=method,.uri=uri,.content_len=n,.user_ctx=route.user_ctx,.aux=&aux};route.handler(&r);return cJSON_Parse(response);
}
static pn_network_credentials_t sink_last;static bool sink_forget,sink_busy;static unsigned sink_calls;
static pn_status_t sink_submit(void *ctx,const pn_network_credentials_t *c,bool forget){(void)ctx;if(sink_busy)return PN_BUSY;sink_calls++;sink_forget=forget;if(!forget)sink_last=*c;return PN_OK;}
static pn_status_t sink_status(void *ctx,char ssid[PN_NETWORK_SSID_MAX+1],bool *pending,pn_status_t *last){(void)ctx;strcpy(ssid,sink_last.ssid);*pending=false;*last=PN_OK;return PN_OK;}
static const pn_device_network_sink_t net_sink={NULL,sink_submit,sink_status};
int main(void){
 char root[]="/tmp/pn-device-http-XXXXXX";assert(mkdtemp(root));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,123)==PN_OK);
 pn_upload_files_options_t options={.root=root};pn_transfer_worker_t worker={0};assert(pn_transfer_worker_open(&worker,&media,&options,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
 pn_device_transfer_http_t server={0};discovery_fails=true;discovery_attempts=0;assert(pn_device_transfer_http_open(&server,&worker,"192.168.4.1",80)==PN_OK && server.impl);
 assert(discovery_attempts==1 && !pn_device_transfer_http_localsend(&server) && discovery_attempts==1);now+=2000;assert(!pn_device_transfer_http_localsend(&server) && discovery_attempts==2);discovery_fails=false;now+=2000;assert(pn_device_transfer_http_localsend(&server) && discovery_attempts==3);
 int sockets[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);assert(open_socket(&route,sockets[0])==ESP_OK);assert(write(sockets[1],"a",1)==1);char received;assert(receive(&route,sockets[0],&received,1,0)==1 && received=='a');
 cJSON *json=request(HTTP_GET,"/api/v1/status",NULL,0,NULL,NULL);assert(code==200 && json && !strcmp(cJSON_GetObjectItemCaseSensitive(json,"name")->valuestring,"PicoNano"));cJSON_Delete(json);
 pn_device_wifi_state_t radio={.phase=PN_WIFI_READY,.rssi_known=true,.rssi=-61,.disconnects=2,.reconnects=1,.last_disconnect_reason=201};strcpy(radio.ap_password,"private");
 assert(pn_device_transfer_http_diagnostics(&server,&radio)==PN_OK);json=request(HTTP_GET,"/api/v1/status",NULL,0,NULL,NULL);
 assert(code==200 && cJSON_GetObjectItemCaseSensitive(json,"rssi_dbm")->valueint==-61 && cJSON_GetObjectItemCaseSensitive(json,"disconnects")->valueint==2 && cJSON_GetObjectItemCaseSensitive(json,"last_disconnect_reason")->valueint==201 && !strstr(response,"private"));cJSON_Delete(json);
 radio.rssi_known=false;assert(pn_device_transfer_http_diagnostics(&server,&radio)==PN_OK);json=request(HTTP_GET,"/api/v1/status",NULL,0,NULL,NULL);assert(!cJSON_GetObjectItemCaseSensitive(json,"rssi_dbm"));cJSON_Delete(json);
 json=request(HTTP_POST,"/api/v1/uploads","{}",2,NULL,NULL);assert(code==401);cJSON_Delete(json);
 json=request(HTTP_GET,"/api/v1/session",NULL,0,NULL,"Host: evil.test");assert(code==400);cJSON_Delete(json);
 json=request(HTTP_GET,"/api/v1/session",NULL,0,NULL,NULL);assert(code==200 && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json,"ready")));char token[33];strcpy(token,cJSON_GetObjectItemCaseSensitive(json,"token")->valuestring);cJSON_Delete(json);
 const char *request_body="{\"ssid\":\"test\",\"password\":\"\"}";
 const uint8_t text[]="device HTTP\n";pn_book_id_t digest;assert(pn_identity_bytes(text,sizeof text-1,&digest)==PN_OK);char sha[65];for(unsigned i=0;i<32;i++)sprintf(sha+i*2,"%02x",digest.sha256[i]);
 /* LocalSend自动书籍接收使用真实共享安装器，连接来源和一次性令牌必须匹配。/ LocalSend book auto-reception uses the real shared installer with matching connection origin and single-use grants. */
 json=request(HTTP_GET,"/api/localsend/v2/info",NULL,0,NULL,NULL);assert(code==200 && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(json,"fingerprint")) && cJSON_GetObjectItemCaseSensitive(json,"port")->valueint==53317 && ls_routes==2 && ls_route.handler==route.handler && ls_route.user_ctx==route.user_ctx);cJSON_Delete(json);
 // 新版客户端的v3路径映射到同一处理。/ Newer clients' v3 paths map to the same handlers.
 json=request(HTTP_GET,"/api/localsend/v3/info",NULL,0,NULL,NULL);assert(code==200 && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(json,"fingerprint")));cJSON_Delete(json);
 json=request(HTTP_GET,"/api/localsend/v2/info",NULL,0,NULL,"Origin: http://192.168.4.1");assert(code==403);cJSON_Delete(json);
 char metadata[1200];snprintf(metadata,sizeof metadata,"{\"info\":{\"alias\":\"Sender\",\"version\":\"2.2\",\"fingerprint\":\"sender\"},\"files\":{\"native\":{\"id\":\"native\",\"fileName\":\"native.txt\",\"size\":%zu,\"fileType\":\"text/plain\",\"sha256\":\"%s\"}}}",sizeof text-1,sha);
 json=request(HTTP_POST,"/api/localsend/v2/prepare-upload",metadata,strlen(metadata),NULL,NULL);assert(code==200 && json);char ls_id[33],ls_token[33];strcpy(ls_id,cJSON_GetObjectItemCaseSensitive(json,"sessionId")->valuestring);strcpy(ls_token,cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(json,"files"),"native")->valuestring);cJSON_Delete(json);
 json=request(HTTP_POST,"/api/localsend/v2/prepare-upload",metadata,strlen(metadata),NULL,NULL);assert(code==409);cJSON_Delete(json);
 char ls_uri[300];snprintf(ls_uri,sizeof ls_uri,"/api/localsend/v2/upload?sessionId=%s&fileId=native&token=%s",ls_id,ls_token);
 wrong_peer=true;json=request(HTTP_POST,ls_uri,text,sizeof text-1,NULL,NULL);assert(code==403);cJSON_Delete(json);wrong_peer=false;
 json=request(HTTP_POST,ls_uri,text,sizeof text-1,NULL,NULL);assert(code==200 && !json);
 char native_path[512];snprintf(native_path,sizeof native_path,"%s/piconano/books/native.txt",root);FILE *native=fopen(native_path,"rb");assert(native);uint8_t native_bytes[64];assert(fread(native_bytes,1,sizeof native_bytes,native)==sizeof text-1 && !memcmp(native_bytes,text,sizeof text-1));assert(!fclose(native));
 json=request(HTTP_POST,ls_uri,text,sizeof text-1,NULL,NULL);assert(code==409);cJSON_Delete(json);
 const char *image_meta="{\"info\":{\"alias\":\"Sender\",\"version\":\"2.2\",\"fingerprint\":\"sender\"},\"files\":{\"image\":{\"id\":\"image\",\"fileName\":\"image.png\",\"size\":100,\"fileType\":\"image/png\"}}}";
 json=request(HTTP_POST,"/api/localsend/v2/prepare-upload",image_meta,strlen(image_meta),NULL,NULL);assert(code==403);cJSON_Delete(json);
 char bad_sha[65];memset(bad_sha,'0',64);bad_sha[64]=0;
 snprintf(metadata,sizeof metadata,"{\"info\":{\"alias\":\"Sender\",\"version\":\"2.2\",\"fingerprint\":\"sender\"},\"files\":{\"native\":{\"id\":\"native\",\"fileName\":\"bad-native.txt\",\"size\":%zu,\"sha256\":\"%s\"}}}",sizeof text-1,bad_sha);
 json=request(HTTP_POST,"/api/localsend/v2/prepare-upload",metadata,strlen(metadata),NULL,NULL);assert(code==200 && json);strcpy(ls_id,cJSON_GetObjectItemCaseSensitive(json,"sessionId")->valuestring);strcpy(ls_token,cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(json,"files"),"native")->valuestring);cJSON_Delete(json);
 snprintf(ls_uri,sizeof ls_uri,"/api/localsend/v2/upload?sessionId=%s&fileId=native&token=%s",ls_id,ls_token);json=request(HTTP_POST,ls_uri,text,sizeof text-1,NULL,NULL);assert(code==422);cJSON_Delete(json);
 snprintf(native_path,sizeof native_path,"%s/piconano/books/bad-native.txt",root);assert(access(native_path,F_OK)!=0);
 char begin[512];snprintf(begin,sizeof begin,"{\"kind\":\"book\",\"name\":\"测试.txt\",\"size\":%zu,\"sha256\":\"%s\"}",sizeof text-1,sha);
 json=request(HTTP_POST,"/api/v1/uploads",begin,strlen(begin),token,NULL);assert(code==201);char id[33],uri[100];strcpy(id,cJSON_GetObjectItemCaseSensitive(json,"upload_id")->valuestring);cJSON_Delete(json);
 snprintf(uri,sizeof uri,"/api/v1/uploads/%s/chunks",id);
 char extra[100];snprintf(extra,sizeof extra,"X-Chunk-SHA256: %s",sha);
 memset(scratch,0,sizeof scratch);headers=0;add("Host: 192.168.4.1");add("Origin: http://192.168.4.1");char line[100];snprintf(line,sizeof line,"Authorization: Bearer %s",token);add(line);snprintf(line,sizeof line,"Content-Length: %zu",sizeof text-1);add(line);add("X-Offset: 0");add(extra);
 body=text;body_at=0;struct httpd_req_aux aux={.scratch=scratch,.scratch_size_limit=sizeof scratch-1,.scratch_cur_size=sizeof scratch-1,.req_hdrs_count=headers};httpd_req_t r={.handle=&route,.method=HTTP_PUT,.uri=uri,.content_len=sizeof text-1,.user_ctx=route.user_ctx,.aux=&aux};route.handler(&r);assert(code==200);
 snprintf(uri,sizeof uri,"/api/v1/uploads/%s/complete",id);char complete[100];snprintf(complete,sizeof complete,"{\"sha256\":\"%s\"}",sha);json=request(HTTP_POST,uri,complete,strlen(complete),token,NULL);assert(code==200 && cJSON_GetObjectItemCaseSensitive(json,"phase")->valueint==2);cJSON_Delete(json);
 json=request(HTTP_GET,"/api/v1/session",NULL,0,NULL,NULL);assert(code==200);cJSON_Delete(json);
 const char *bad="{\"kind\":\"book\",\"name\":\"../bad.txt\",\"size\":true,\"sha256\":\"bad\"}";json=request(HTTP_POST,"/api/v1/uploads",bad,strlen(bad),token,NULL);assert(code==400);cJSON_Delete(json);
 const char *nul="{\"code\":\"123456\\u0000suffix\"}";json=request(HTTP_POST,"/api/v1/network",nul,strlen(nul),token,NULL);assert(code==400);cJSON_Delete(json);
 json=request(HTTP_POST,"/api/v1/network",request_body,strlen(request_body),token,"Transfer-Encoding: chunked");assert(code==400);cJSON_Delete(json);
 json=request(HTTP_GET,"/api/v1/session",NULL,0,token,"Origin: http://evil.test");assert(code==400);cJSON_Delete(json);
 json=request(HTTP_GET,"/api/v1/files/book?name=%E6%B5%8B%E8%AF%95.txt",NULL,0,token,NULL);assert(code==200 && cJSON_GetObjectItemCaseSensitive(json,"size")->valueint==(int)sizeof text-1 && !strcmp(cJSON_GetObjectItemCaseSensitive(json,"sha256")->valuestring,sha));cJSON_Delete(json);
 json=request(HTTP_POST,"/api/v1/uploads",begin,strlen(begin),token,NULL);assert(code==409);cJSON_Delete(json);
 char replace[700];snprintf(replace,sizeof replace,"{\"kind\":\"book\",\"name\":\"测试.txt\",\"size\":%zu,\"sha256\":\"%s\",\"replace\":{\"size\":%zu,\"sha256\":\"%s\"}}",sizeof text-1,sha,sizeof text-1,sha);
 json=request(HTTP_POST,"/api/v1/uploads",replace,strlen(replace),token,NULL);assert(code==201);snprintf(uri,sizeof uri,"/api/v1/uploads/%s",cJSON_GetObjectItemCaseSensitive(json,"upload_id")->valuestring);cJSON_Delete(json);
 json=request(HTTP_DELETE,uri,NULL,0,token,NULL);assert(code==200 && cJSON_GetObjectItemCaseSensitive(json,"phase")->valueint==3);cJSON_Delete(json);
 snprintf(begin,sizeof begin,"{\"kind\":\"font\",\"name\":\"bad.ttf\",\"size\":%zu,\"sha256\":\"%s\"}",sizeof text-1,sha);
 json=request(HTTP_POST,"/api/v1/uploads",begin,strlen(begin),token,NULL);assert(code==201);strcpy(id,cJSON_GetObjectItemCaseSensitive(json,"upload_id")->valuestring);cJSON_Delete(json);snprintf(uri,sizeof uri,"/api/v1/uploads/%s/chunks",id);
 json=request(HTTP_PUT,uri,text,sizeof text-1,token,extra);assert(code==200);cJSON_Delete(json);snprintf(uri,sizeof uri,"/api/v1/uploads/%s/complete",id);
 json=request(HTTP_POST,uri,complete,strlen(complete),token,NULL);assert(code==422);cJSON_Delete(json);
 /* 图片资源路由：各自目录、严格查询、身份核对与删除。/ Image resource routes: separate directories, strict queries, identity checking and deletion. */
 for(unsigned i=0;i<2;i++){
  const char *dir=i?"piconano/wallpapers":"piconano/covers",*route=i?"/api/v1/wallpapers":"/api/v1/covers";
  char folder[256],path[300],uri[400];snprintf(folder,sizeof folder,"%s/%s",root,dir);assert(mkdir(folder,0700)==0 || errno==EEXIST);
  snprintf(path,sizeof path,"%s/x.png",folder);FILE *image=fopen(path,"wb");assert(image && fwrite(text,1,sizeof text-1,image)==sizeof text-1 && fclose(image)==0);
  json=request(HTTP_GET,route,NULL,0,token,NULL);assert(code==200 && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(json,"items"))==1);cJSON_Delete(json);
  snprintf(uri,sizeof uri,"%s?after=x.png&after=x.png",route);json=request(HTTP_GET,uri,NULL,0,token,NULL);assert(code==400);cJSON_Delete(json);
  snprintf(uri,sizeof uri,"%s?name=x.png&size=%zu&sha256=%s",route,sizeof text,sha);json=request(HTTP_DELETE,uri,NULL,0,token,NULL);assert(code==409 && access(path,F_OK)==0);cJSON_Delete(json);
  snprintf(uri,sizeof uri,"%s?name=x.png&size=%zu&sha256=%s",route,sizeof text-1,sha);json=request(HTTP_DELETE,uri,NULL,0,token,NULL);assert(code==200 && access(path,F_OK)!=0);cJSON_Delete(json);
 }
 /* 已安装字体：授权列表、游标、严格查询、身份不符拒绝删除、成功删除。/ Installed fonts: authorized listing, cursor, strict queries, refusal on identity mismatch, and successful deletion. */
 {
  char folder[256],path[300];snprintf(folder,sizeof folder,"%s/piconano/fonts",root);snprintf(path,sizeof path,"%s/x.ttf",folder);FILE *font=fopen(path,"wb");assert(font && fwrite(text,1,sizeof text-1,font)==sizeof text-1 && fclose(font)==0);
  snprintf(path,sizeof path,"%s/字体.ttf",folder);font=fopen(path,"wb");assert(font && fwrite(text,1,sizeof text-1,font)==sizeof text-1 && fclose(font)==0);
  json=request(HTTP_GET,"/api/v1/fonts",NULL,0,NULL,NULL);assert(code==401);cJSON_Delete(json);
  json=request(HTTP_GET,"/api/v1/fonts",NULL,0,token,NULL);assert(code==200 && cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(json,"more")));
  cJSON *items=cJSON_GetObjectItemCaseSensitive(json,"items");assert(cJSON_GetArraySize(items)==2);
  assert(!strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(items,0),"name")->valuestring,"x.ttf") && cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(items,0),"size")->valueint==(int)sizeof text-1);
  assert(!strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(items,1),"name")->valuestring,"字体.ttf"));cJSON_Delete(json);
  json=request(HTTP_GET,"/api/v1/fonts?after=x.ttf",NULL,0,token,NULL);assert(code==200 && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(json,"items"))==1);cJSON_Delete(json);
  json=request(HTTP_GET,"/api/v1/fonts?after=%E5%AD%97%E4%BD%93.ttf",NULL,0,token,NULL);assert(code==200 && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(json,"items"))==0);cJSON_Delete(json);
  const char *bad_queries[]={"/api/v1/fonts?bogus=1","/api/v1/fonts?after=a&after=b","/api/v1/fonts?after=%zz","/api/v1/fonts?after","/api/v1/fonts?after=..%2Fx.ttf","/api/v1/fontsx"};
  for(unsigned i=0;i<sizeof bad_queries/sizeof bad_queries[0];i++){json=request(HTTP_GET,bad_queries[i],NULL,0,token,NULL);assert(code==400 || code==404);cJSON_Delete(json);}
  char remove[400];snprintf(remove,sizeof remove,"/api/v1/fonts?name=x.ttf&size=%zu&sha256=%s",sizeof text-1,sha);
  json=request(HTTP_DELETE,remove,NULL,0,NULL,NULL);assert(code==401);cJSON_Delete(json);
  json=request(HTTP_DELETE,remove,NULL,0,token,"Origin: http://evil.test");assert(code==400);cJSON_Delete(json);
  char query[400];
  snprintf(query,sizeof query,"/api/v1/fonts?name=x.ttf&size=%zu",sizeof text-1);json=request(HTTP_DELETE,query,NULL,0,token,NULL);assert(code==400);cJSON_Delete(json);
  snprintf(query,sizeof query,"/api/v1/fonts?name=x.ttf&size=0&sha256=%s",sha);json=request(HTTP_DELETE,query,NULL,0,token,NULL);assert(code==400);cJSON_Delete(json);
  snprintf(query,sizeof query,"/api/v1/fonts?name=x.ttf&size=%zu&sha256=%s",sizeof text-1+1,sha);json=request(HTTP_DELETE,query,NULL,0,token,NULL);assert(code==409);cJSON_Delete(json);
  snprintf(path,sizeof path,"%s/x.ttf",folder);assert(access(path,F_OK)==0);
  json=request(HTTP_DELETE,remove,NULL,0,token,NULL);assert(code==200 && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json,"deleted")));cJSON_Delete(json);assert(access(path,F_OK)!=0);
  json=request(HTTP_DELETE,remove,NULL,0,token,NULL);assert(code==404);cJSON_Delete(json);
  json=request(HTTP_GET,"/api/v1/fonts",NULL,0,token,NULL);assert(code==200 && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(json,"items"))==1);cJSON_Delete(json);
 }
 /* 配网接口：未启用404；保存只交给sink且不回显口令。/ Provisioning endpoints: 404 when disabled; saves go only to the sink and never echo passwords. */
 json=request(HTTP_GET,"/api/v1/network",NULL,0,token,NULL);assert(code==404);cJSON_Delete(json);
 assert(pn_device_transfer_http_network(&server,&net_sink)==PN_OK);
 const char *home="{\"ssid\":\"家里的WiFi\",\"password\":\"secret-pass\"}";
 json=request(HTTP_POST,"/api/v1/network",home,strlen(home),NULL,NULL);assert(code==401 && !sink_calls);cJSON_Delete(json);
 json=request(HTTP_POST,"/api/v1/network",home,strlen(home),token,NULL);assert(code==202 && sink_calls==1 && !sink_forget && !strcmp(sink_last.ssid,"家里的WiFi") && !strcmp(sink_last.password,"secret-pass") && !strstr(response,"secret"));cJSON_Delete(json);
 sink_busy=true;json=request(HTTP_POST,"/api/v1/network",home,strlen(home),token,NULL);assert(code==409);cJSON_Delete(json);sink_busy=false;
 const char *invalid[]={"{\"ssid\":\"x\",\"password\":\"1234567\"}","{\"ssid\":\"x\"}","{\"ssid\":\"\",\"password\":\"\"}","{\"ssid\":\"x\",\"password\":\"12345678\",\"extra\":1}","{\"ssid\":\"123456789012345678901234567890123\",\"password\":\"\"}","{\"ssid\":\"x\",\"password\":\"badépassword\"}"};
 for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++){json=request(HTTP_POST,"/api/v1/network",invalid[i],strlen(invalid[i]),token,NULL);assert(code==400 && sink_calls==1);cJSON_Delete(json);}
 const char *open="{\"ssid\":\"开放网络\",\"password\":\"\"}";json=request(HTTP_POST,"/api/v1/network",open,strlen(open),token,NULL);assert(code==202 && sink_calls==2 && !sink_last.password[0]);cJSON_Delete(json);
 json=request(HTTP_GET,"/api/v1/network",NULL,0,token,NULL);assert(code==200 && !strcmp(cJSON_GetObjectItemCaseSensitive(json,"ssid")->valuestring,"开放网络") && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json,"saved")) && !cJSON_GetObjectItemCaseSensitive(json,"password"));cJSON_Delete(json);
 json=request(HTTP_DELETE,"/api/v1/network",NULL,0,token,NULL);assert(code==202 && sink_calls==3 && sink_forget);cJSON_Delete(json);
 assert(pn_device_transfer_http_network(&server,NULL)==PN_OK);json=request(HTTP_GET,"/api/v1/network",NULL,0,token,NULL);assert(code==404);cJSON_Delete(json);
 snprintf(begin,sizeof begin,"{\"kind\":\"book\",\"name\":\"leading-zero.txt\",\"size\":01,\"sha256\":\"%s\"}",sha);json=request(HTTP_POST,"/api/v1/uploads",begin,strlen(begin),token,NULL);assert(code==400);cJSON_Delete(json);
 receive_delay=6000;json=request(HTTP_POST,"/api/v1/network",request_body,strlen(request_body),token,NULL);assert(code==400);cJSON_Delete(json);receive_delay=0;
 incomplete=true;json=request(HTTP_POST,"/api/v1/network",request_body,strlen(request_body),token,NULL);assert(code==400);cJSON_Delete(json);incomplete=false;
 const char *duplicate="{\"kind\":\"book\",\"kind\":\"font\",\"name\":\"dup.txt\",\"size\":1,\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}";json=request(HTTP_POST,"/api/v1/uploads",duplicate,strlen(duplicate),token,NULL);assert(code==400);cJSON_Delete(json);
 now+=300001;json=request(HTTP_GET,"/api/v1/session",NULL,0,NULL,NULL);assert(code==200);cJSON_Delete(json);
 assert(pn_device_transfer_http_request_stop(&server)==PN_OK);json=request(HTTP_GET,"/api/v1/session",NULL,0,token,NULL);assert(code==503);cJSON_Delete(json);
 assert(receive(&route,sockets[0],&received,1,0)==HTTPD_SOCK_ERR_FAIL);close(sockets[0]);close(sockets[1]);
 assert(pn_device_transfer_http_close(&server)==PN_OK && !native_allocations);assert(pn_transfer_worker_close(&worker)==PN_OK && !pn_media_active(&media));
 puts("Device HTTP handler: SDK stubs, real JSON/worker/files, pairing, duplicates, expiry and stopping passed");return 0;
}
