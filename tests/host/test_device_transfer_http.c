/* 中文：真实设备handler、JSON和上传worker；SDK传输API显式stub。/ English: real device handler, JSON and upload worker with explicitly stubbed SDK transport APIs. */
#define _POSIX_C_SOURCE 200809L
#include "device_transfer_http.h"
#include "esp_http_server.h"
#include "esp_httpd_priv.h"
#include "cJSON.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
static httpd_uri_t route;
static char scratch[2048],response[4096];
static unsigned code,headers;
static const uint8_t *body;
static size_t body_at;
static uint64_t receive_delay;
static bool incomplete;
static uint64_t now=1000;
static size_t native_allocations;
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
esp_err_t httpd_start(httpd_handle_t *h,const httpd_config_t *cfg){assert(cfg->max_uri_handlers>=4);*h=&route;global_context=cfg->global_user_ctx;open_socket=cfg->open_fn;return ESP_OK;}
void *httpd_get_global_user_ctx(httpd_handle_t h){assert(h==&route);return global_context;}
esp_err_t httpd_sess_set_recv_override(httpd_handle_t h,int fd,httpd_recv_func_t callback){(void)fd;assert(h==&route);receive=callback;return ESP_OK;}
esp_err_t httpd_stop(httpd_handle_t h){assert(h==&route);return ESP_OK;}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *r){assert(h==&route);route=*r;return ESP_OK;}
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
 memset(scratch,0,sizeof scratch);headers=0;add("Host: 192.168.4.1");add("Origin: http://192.168.4.1");
 char line[256];if(token){snprintf(line,sizeof line,"Authorization: Bearer %s",token);add(line);}
 if(method==HTTP_POST || method==HTTP_PUT){snprintf(line,sizeof line,"Content-Length: %zu",n);add(line);if(method==HTTP_POST)add("Content-Type: application/json");}
 if(method==HTTP_PUT)add("X-Offset: 0");
 if(extra)add(extra);
 body=bytes;body_at=0;code=0;response[0]=0;
 struct httpd_req_aux aux={.scratch=scratch,.scratch_size_limit=sizeof scratch-1,.scratch_cur_size=sizeof scratch-1,.req_hdrs_count=headers};
 httpd_req_t r={.handle=&route,.method=method,.uri=uri,.content_len=n,.user_ctx=route.user_ctx,.aux=&aux};route.handler(&r);return cJSON_Parse(response);
}
int main(void){
 char root[]="/tmp/pn-device-http-XXXXXX";assert(mkdtemp(root));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,123)==PN_OK);
 pn_upload_files_options_t options={.root=root};pn_transfer_worker_t worker={0};assert(pn_transfer_worker_open(&worker,&media,&options,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
 pn_device_transfer_http_t server={0};assert(pn_device_transfer_http_open(&server,&worker,"192.168.4.1",80)==PN_OK);char pin[7];assert(pn_device_transfer_http_code(&server,pin)==PN_OK);
 int sockets[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);assert(open_socket(&route,sockets[0])==ESP_OK);assert(write(sockets[1],"a",1)==1);char received;assert(receive(&route,sockets[0],&received,1,0)==1 && received=='a');
 cJSON *json=request(HTTP_GET,"/api/v1/status",NULL,0,NULL,NULL);assert(code==200 && json);cJSON_Delete(json);
 json=request(HTTP_POST,"/api/v1/uploads","{}",2,NULL,NULL);assert(code==401);cJSON_Delete(json);
 char pair[64];snprintf(pair,sizeof pair,"{\"code\":\"%s\"}",pin);
 json=request(HTTP_POST,"/api/v1/pair",pair,strlen(pair),NULL,"Host: evil.test");assert(code==400);cJSON_Delete(json);
 json=request(HTTP_POST,"/api/v1/pair",pair,strlen(pair),NULL,NULL);assert(code==200);char token[33];strcpy(token,cJSON_GetObjectItemCaseSensitive(json,"token")->valuestring);cJSON_Delete(json);
 const uint8_t text[]="device HTTP\n";pn_book_id_t digest;assert(pn_identity_bytes(text,sizeof text-1,&digest)==PN_OK);char sha[65];for(unsigned i=0;i<32;i++)sprintf(sha+i*2,"%02x",digest.sha256[i]);
 char begin[512];snprintf(begin,sizeof begin,"{\"kind\":\"book\",\"name\":\"测试.txt\",\"size\":%zu,\"sha256\":\"%s\"}",sizeof text-1,sha);
 json=request(HTTP_POST,"/api/v1/uploads",begin,strlen(begin),token,NULL);assert(code==201);char id[33],uri[100];strcpy(id,cJSON_GetObjectItemCaseSensitive(json,"upload_id")->valuestring);cJSON_Delete(json);
 snprintf(uri,sizeof uri,"/api/v1/uploads/%s/chunks",id);
 char extra[100];snprintf(extra,sizeof extra,"X-Chunk-SHA256: %s",sha);
 memset(scratch,0,sizeof scratch);headers=0;add("Host: 192.168.4.1");add("Origin: http://192.168.4.1");char line[100];snprintf(line,sizeof line,"Authorization: Bearer %s",token);add(line);snprintf(line,sizeof line,"Content-Length: %zu",sizeof text-1);add(line);add("X-Offset: 0");add(extra);
 body=text;body_at=0;struct httpd_req_aux aux={.scratch=scratch,.scratch_size_limit=sizeof scratch-1,.scratch_cur_size=sizeof scratch-1,.req_hdrs_count=headers};httpd_req_t r={.handle=&route,.method=HTTP_PUT,.uri=uri,.content_len=sizeof text-1,.user_ctx=route.user_ctx,.aux=&aux};route.handler(&r);assert(code==200);
 snprintf(uri,sizeof uri,"/api/v1/uploads/%s/complete",id);char complete[100];snprintf(complete,sizeof complete,"{\"sha256\":\"%s\"}",sha);json=request(HTTP_POST,uri,complete,strlen(complete),token,NULL);assert(code==200 && cJSON_GetObjectItemCaseSensitive(json,"phase")->valueint==2);cJSON_Delete(json);
 json=request(HTTP_GET,"/api/v1/session",NULL,0,NULL,NULL);assert(code==401);cJSON_Delete(json);
 const char *bad="{\"kind\":\"book\",\"name\":\"../bad.txt\",\"size\":true,\"sha256\":\"bad\"}";json=request(HTTP_POST,"/api/v1/uploads",bad,strlen(bad),token,NULL);assert(code==400);cJSON_Delete(json);
 const char *nul="{\"code\":\"123456\\u0000suffix\"}";json=request(HTTP_POST,"/api/v1/pair",nul,strlen(nul),NULL,NULL);assert(code==400);cJSON_Delete(json);
 json=request(HTTP_POST,"/api/v1/pair",pair,strlen(pair),NULL,"Transfer-Encoding: chunked");assert(code==400);cJSON_Delete(json);
 json=request(HTTP_GET,"/api/v1/session",NULL,0,token,"Origin: http://evil.test");assert(code==400);cJSON_Delete(json);
 for(unsigned i=0;i<5;i++){const char *wrong=!strcmp(pin,"999999")?"{\"code\":\"000000\"}":"{\"code\":\"999999\"}";json=request(HTTP_POST,"/api/v1/pair",wrong,strlen(wrong),NULL,NULL);assert(code==401);cJSON_Delete(json);}
 json=request(HTTP_POST,"/api/v1/pair",pair,strlen(pair),NULL,NULL);assert(code==429);cJSON_Delete(json);now+=60001;
 json=request(HTTP_GET,"/api/v1/files/book?name=%E6%B5%8B%E8%AF%95.txt",NULL,0,token,NULL);assert(code==200 && cJSON_GetObjectItemCaseSensitive(json,"size")->valueint==(int)sizeof text-1 && !strcmp(cJSON_GetObjectItemCaseSensitive(json,"sha256")->valuestring,sha));cJSON_Delete(json);
 json=request(HTTP_POST,"/api/v1/uploads",begin,strlen(begin),token,NULL);assert(code==409);cJSON_Delete(json);
 char replace[700];snprintf(replace,sizeof replace,"{\"kind\":\"book\",\"name\":\"测试.txt\",\"size\":%zu,\"sha256\":\"%s\",\"replace\":{\"size\":%zu,\"sha256\":\"%s\"}}",sizeof text-1,sha,sizeof text-1,sha);
 json=request(HTTP_POST,"/api/v1/uploads",replace,strlen(replace),token,NULL);assert(code==201);snprintf(uri,sizeof uri,"/api/v1/uploads/%s",cJSON_GetObjectItemCaseSensitive(json,"upload_id")->valuestring);cJSON_Delete(json);
 json=request(HTTP_DELETE,uri,NULL,0,token,NULL);assert(code==200 && cJSON_GetObjectItemCaseSensitive(json,"phase")->valueint==3);cJSON_Delete(json);
 snprintf(begin,sizeof begin,"{\"kind\":\"font\",\"name\":\"bad.ttf\",\"size\":%zu,\"sha256\":\"%s\"}",sizeof text-1,sha);
 json=request(HTTP_POST,"/api/v1/uploads",begin,strlen(begin),token,NULL);assert(code==201);strcpy(id,cJSON_GetObjectItemCaseSensitive(json,"upload_id")->valuestring);cJSON_Delete(json);snprintf(uri,sizeof uri,"/api/v1/uploads/%s/chunks",id);
 json=request(HTTP_PUT,uri,text,sizeof text-1,token,extra);assert(code==200);cJSON_Delete(json);snprintf(uri,sizeof uri,"/api/v1/uploads/%s/complete",id);
 json=request(HTTP_POST,uri,complete,strlen(complete),token,NULL);assert(code==422);cJSON_Delete(json);
 snprintf(begin,sizeof begin,"{\"kind\":\"book\",\"name\":\"leading-zero.txt\",\"size\":01,\"sha256\":\"%s\"}",sha);json=request(HTTP_POST,"/api/v1/uploads",begin,strlen(begin),token,NULL);assert(code==400);cJSON_Delete(json);
 receive_delay=6000;json=request(HTTP_POST,"/api/v1/pair",pair,strlen(pair),NULL,NULL);assert(code==400);cJSON_Delete(json);receive_delay=0;
 incomplete=true;json=request(HTTP_POST,"/api/v1/pair",pair,strlen(pair),NULL,NULL);assert(code==400);cJSON_Delete(json);incomplete=false;
 const char *duplicate="{\"code\":\"1\",\"code\":\"2\"}";json=request(HTTP_POST,"/api/v1/pair",duplicate,strlen(duplicate),NULL,NULL);assert(code==400);cJSON_Delete(json);
 now+=300001;json=request(HTTP_POST,"/api/v1/pair",pair,strlen(pair),NULL,NULL);assert(code==401);cJSON_Delete(json);
 assert(pn_device_transfer_http_request_stop(&server)==PN_OK);json=request(HTTP_GET,"/api/v1/session",NULL,0,token,NULL);assert(code==503);cJSON_Delete(json);
 assert(receive(&route,sockets[0],&received,1,0)==HTTPD_SOCK_ERR_FAIL);close(sockets[0]);close(sockets[1]);
 assert(pn_device_transfer_http_close(&server)==PN_OK && !native_allocations);assert(pn_transfer_worker_close(&worker)==PN_OK && !pn_media_active(&media));
 puts("Device HTTP handler: SDK stubs, real JSON/worker/files, pairing, duplicates, expiry and stopping passed");return 0;
}
