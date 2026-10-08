/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界设备HTTP协议、配对及资源服务，所有文件事务交给独立worker。
 * English: bounded device HTTP protocol, pairing and assets; a dedicated worker owns all file transactions.
 * 冻结：仅ESP-IDF6.1请求头布局，不放宽重复关键头，不记录令牌。
 * Frozen: ESP-IDF6.1 header layout only, never permit duplicate critical headers or log tokens.
 */
#define _POSIX_C_SOURCE 200809L
#include "device_transfer_http.h"
#include "transfer_assets.h"
#include "esp_http_server.h"
#include "esp_httpd_priv.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <sys/socket.h>
typedef struct {
 pn_transfer_worker_t *worker;
 httpd_handle_t server;
 SemaphoreHandle_t auth;
 char authority[64],origin[80],pin[7],token[33];
 uint64_t created,locked_until;
 unsigned failures;
 bool closing;
 uint8_t bytes[PN_UPLOAD_CHUNK+1];
} http_t;
static const char *const messages[]={"成功","输入参数无效","资源正在使用或同名文件已存在","没有找到该文件或上传会话","空间、配额或大小超过限制","内存不足，请稍后重试","存储介质已失效","文件身份已经变化","上传已取消","暂不支持此格式","摘要或文件格式校验失败","读写失败，请重新查询后续传"};
static const unsigned statuses[]={200,400,409,404,413,503,503,409,409,422,422,503};
static uint64_t now_ms(void){return (uint64_t)(esp_timer_get_time()/1000);}
static void lock(http_t *s){xSemaphoreTake(s->auth,portMAX_DELAY);}
static void unlock(http_t *s){xSemaphoreGive(s->auth);}
static bool is_closing(http_t *s){lock(s);bool result=s->closing;unlock(s);return result;}
static int receive(httpd_handle_t server,int socket,char *out,size_t cap,int flags){
 http_t *s=httpd_get_global_user_ctx(server);
 if(!s || is_closing(s))return HTTPD_SOCK_ERR_FAIL;
 int n=(int)recv(socket,out,cap,flags);
 if(is_closing(s))return HTTPD_SOCK_ERR_FAIL;
 return n<0?(errno==EAGAIN || errno==EWOULDBLOCK?HTTPD_SOCK_ERR_TIMEOUT:HTTPD_SOCK_ERR_FAIL):n;
}
static esp_err_t opened(httpd_handle_t server,int socket){return httpd_sess_set_recv_override(server,socket,receive);}
static void keep_context(void *ctx){(void)ctx;}
static bool header(httpd_req_t *r,const char *key,char *out,size_t cap){size_t n=httpd_req_get_hdr_value_len(r,key);out[0]=0;return n && n<cap && httpd_req_get_hdr_value_str(r,key,out,cap)==ESP_OK;}
static bool unique_headers(httpd_req_t *r,unsigned *presence){
 const struct httpd_req_aux *a=r->aux;
 if(!a || !a->scratch || !a->scratch_cur_size || a->scratch_cur_size>4096 || a->scratch_size_limit>4096 || a->req_hdrs_count>64)return false;
 size_t cap=a->scratch_cur_size<a->scratch_size_limit?a->scratch_cur_size:a->scratch_size_limit;
 const char *at=a->scratch,*end=at+cap+1;unsigned seen=0;
 const char *const keys[]={"Host","Origin","Authorization","Content-Length","Transfer-Encoding","Content-Type","X-Offset","X-Chunk-SHA256"};
 for(unsigned i=0;i<a->req_hdrs_count;i++){
  while(at<end && !*at)at++;
  if(at==end)return false;
  const char *stop=memchr(at,0,(size_t)(end-at));if(!stop)return false;
  const char *colon=memchr(at,':',(size_t)(stop-at));if(!colon)return false;
  for(unsigned j=0;j<sizeof keys/sizeof keys[0];j++)if((size_t)(colon-at)==strlen(keys[j]) && !strncasecmp(at,keys[j],strlen(keys[j]))){if((seen&(1u<<j)) || j==4)return false;seen|=1u<<j;}
  at=stop+1;
 }
 *presence=seen;return true;
}
static esp_err_t send_data(httpd_req_t *r,unsigned status,const char *mime,const char *data,size_t n){
 char value[40];snprintf(value,sizeof value,"%u %s",status,status<400?"OK":"Error");
 httpd_resp_set_status(r,value);httpd_resp_set_type(r,mime);
 httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");httpd_resp_set_hdr(r,"Connection","close");
 httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; script-src 'self'; style-src 'self'; worker-src 'self'; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
 esp_err_t result=httpd_resp_send(r,data,(int)n);return status>=400?ESP_FAIL:result;
}
static esp_err_t error(httpd_req_t *r,unsigned status,const char *message){char bytes[384];int n=snprintf(bytes,sizeof bytes,"{\"message\":\"%s\"}",message);return send_data(r,status,"application/json; charset=utf-8",bytes,(size_t)n);}
static esp_err_t json_send(httpd_req_t *r,unsigned status,cJSON *json,bool valid){
 char bytes[2048];bool okay=valid && json && cJSON_PrintPreallocated(json,bytes,sizeof bytes,false);cJSON_Delete(json);
 return okay?send_data(r,status,"application/json; charset=utf-8",bytes,strlen(bytes)):error(r,503,"内存不足，请稍后重试");
}
static bool string(cJSON *o,const char *key,const char *value){return o && cJSON_AddStringToObject(o,key,value)!=NULL;}
static bool number(cJSON *o,const char *key,uint64_t value){return o && cJSON_AddNumberToObject(o,key,(double)value)!=NULL;}
static bool boolean(cJSON *o,const char *key,bool value){return o && cJSON_AddBoolToObject(o,key,value)!=NULL;}
static bool decimal(const char *s,uint64_t limit,uint64_t *out){if(!s || !*s)return false;uint64_t n=0;while(*s){unsigned c=(unsigned char)*s++;if(c<'0' || c>'9' || n>limit/10 || (n==limit/10 && c-'0'>limit%10))return false;n=n*10+c-'0';}*out=n;return true;}
static int digit(char c){return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;}
static bool decode(const char *s,uint8_t *out,size_t n){if(!s || strlen(s)!=n*2)return false;for(size_t i=0;i<n;i++){int a=digit(s[i*2]),b=digit(s[i*2+1]);if(a<0 || b<0)return false;out[i]=(uint8_t)(a*16+b);}return true;}
static void encode(const uint8_t *bytes,size_t n,char *out){for(size_t i=0;i<n;i++)sprintf(out+i*2,"%02x",bytes[i]);}
static const char *get_string(cJSON *o,const char *key){cJSON *value=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(value)?value->valuestring:NULL;}
static bool integer(cJSON *o,const char *key,uint64_t *out){cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);if(!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<0 || v->valuedouble>512u*1024u*1024u || floor(v->valuedouble)!=v->valuedouble)return false;*out=(uint64_t)v->valuedouble;return true;}
static bool fields(cJSON *o,const char *const *keys,size_t n,unsigned required){
 if(!cJSON_IsObject(o))return false;
 unsigned seen=0;
 for(cJSON *v=o->child;v;v=v->next){size_t i=0;while(i<n && strcmp(keys[i],v->string))i++;if(i==n || (seen&(1u<<i)))return false;seen|=1u<<i;}
 return (seen&required)==required;
}
static bool json_guard(const uint8_t *bytes,size_t n){
 bool quoted=false,escaped=false;unsigned tokens=0,depth=0;
 for(size_t i=0;i<n;i++){
  uint8_t c=bytes[i];if(!c)return false;
  if(quoted){
   if(escaped){if(c=='u' && i+4<n && !memcmp(bytes+i+1,"0000",4))return false;escaped=false;}
   else if(c=='\\')escaped=true;
   else if(c=='"')quoted=false;
   else if(c<32)return false;
  }else if(c=='"'){quoted=true;tokens++;}
  else if(c=='{' || c=='['){if(++depth>16)return false;tokens++;}
  else if(c=='}' || c==']'){if(!depth)return false;depth--;}
  else if(c==',')tokens++;
  else if(c>='0' && c<='9'){
   size_t end=i+1;while(end<n && bytes[end]>='0' && bytes[end]<='9')end++;
   if(c=='0' && end>i+1)return false;
   i=end-1;
  }else if(c!=':' && c!=' ' && c!='\t' && c!='\r' && c!='\n')return false;
  if(tokens>64)return false;
 }
 return !quoted && !depth;
}
static pn_upload_kind_t kind(const char *s){if(!s)return 0;return !strcmp(s,"book")?PN_UPLOAD_BOOK:!strcmp(s,"font")?PN_UPLOAD_FONT:!strcmp(s,"cover")?PN_UPLOAD_COVER:!strcmp(s,"wallpaper")?PN_UPLOAD_WALLPAPER:0;}
static const char *kind_name(pn_upload_kind_t value){return value==PN_UPLOAD_BOOK?"book":value==PN_UPLOAD_FONT?"font":value==PN_UPLOAD_COVER?"cover":"wallpaper";}
static esp_err_t execute(http_t *s,httpd_req_t *r,pn_transfer_command_t *command,unsigned success){
 pn_transfer_reply_t reply;pn_status_t status=pn_transfer_worker_execute(s->worker,command,&reply);
 if(status!=PN_OK)return error(r,status<=PN_IO?statuses[status]:503,status<=PN_IO?messages[status]:"传输服务不可用");
 cJSON *json=cJSON_CreateObject();bool valid=json!=NULL;
 if(reply.has_file){char sha[65];encode(reply.file_digest.sha256,32,sha);valid&=number(json,"size",reply.file_size);valid&=string(json,"sha256",sha);}
 if(reply.has_upload){char sha[65],id[33];encode(reply.request.digest.sha256,32,sha);encode(reply.request.id,16,id);valid&=string(json,"upload_id",id);valid&=string(json,"name",reply.request.name);valid&=string(json,"kind",kind_name(reply.request.kind));valid&=string(json,"sha256",sha);valid&=number(json,"size",reply.state.size);valid&=number(json,"next_offset",reply.state.offset);valid&=number(json,"phase",reply.state.phase);valid&=number(json,"chunk_size",PN_UPLOAD_CHUNK);valid&=boolean(json,"cleanup_pending",reply.cleanup_pending);}
 return json_send(r,success,json,valid);
}
static esp_err_t dispatch(httpd_req_t *r){
 http_t *s=r->user_ctx;char host[64],origin[80],authorization[64];
 unsigned presence=0;
 if(!unique_headers(r,&presence))return error(r,400,"请求头重复或无效");
 if(!header(r,"Host",host,sizeof host) || strcmp(host,s->authority))return error(r,403,"传输地址不匹配");
 bool has_origin=(presence&2u)!=0;bool valid_origin=header(r,"Origin",origin,sizeof origin);
 if((has_origin && (!valid_origin || strcmp(origin,s->origin))) || (r->method!=HTTP_GET && !has_origin))return error(r,403,"请求来源不匹配");
 lock(s);bool closing=s->closing;bool authorized=false;
 if(header(r,"Authorization",authorization,sizeof authorization) && strlen(authorization)==39 && !memcmp(authorization,"Bearer ",7)){unsigned diff=0;for(unsigned i=0;i<32;i++)diff|=(unsigned char)authorization[i+7]^(unsigned char)s->token[i];authorized=diff==0;}
 unlock(s);if(closing)return error(r,503,"传输服务已停止");
 const char *uri=r->uri;if(!uri || strlen(uri)>1024)return error(r,400,"请求地址过长");
 if((r->method==HTTP_GET || r->method==HTTP_DELETE) && r->content_len)return error(r,400,"该操作不接受请求正文");
 if(r->method==HTTP_GET){
  const pn_transfer_asset_t *asset=pn_transfer_asset(uri);if(asset)return send_data(r,200,asset->mime,(const char *)asset->bytes,asset->size);
  if(!strcmp(uri,"/api/v1/status")){cJSON *json=cJSON_CreateObject();bool v=string(json,"name","小纸 Pico");v&=string(json,"version","0.0.50");v&=number(json,"chunk_size",PN_UPLOAD_CHUNK);v&=boolean(json,"preview",false);return json_send(r,200,json,v);}
 }
 bool pairing=r->method==HTTP_POST && !strcmp(uri,"/api/v1/pair");
 if(!pairing && !authorized)return error(r,401,"请先输入配对码");
 cJSON *json=NULL;
 if(r->method==HTTP_POST || r->method==HTTP_PUT){
  size_t limit=r->method==HTTP_POST?8192:PN_UPLOAD_CHUNK;char length[32];uint64_t parsed=0;
  if(!header(r,"Content-Length",length,sizeof length) || !decimal(length,limit,&parsed) || parsed!=r->content_len || !parsed)return error(r,400,"请求长度无效");
  if(r->method==HTTP_POST){char type[64];if(!header(r,"Content-Type",type,sizeof type) || (strcasecmp(type,"application/json") && strcasecmp(type,"application/json; charset=utf-8")))return error(r,400,"请求正文格式无效");}
  uint64_t started=now_ms();size_t at=0;
  while(at<r->content_len){
   if(is_closing(s))return error(r,503,"传输服务已停止");
   uint64_t now=now_ms();if(now<started || now-started>=10000)return error(r,400,"请求正文接收超时");
   int got=httpd_req_recv(r,(char *)s->bytes+at,r->content_len-at);if(got<=0)return error(r,400,"请求正文未完整收到");at+=(size_t)got;
  }
  if(now_ms()-started>=10000)return error(r,400,"请求正文接收超时");
  if(r->method==HTTP_POST){s->bytes[at]=0;if(!json_guard(s->bytes,at))return error(r,400,"请求JSON无效");json=cJSON_ParseWithLengthOpts((char *)s->bytes,at+1,NULL,true);if(!json)return error(r,400,"请求JSON无效");}
 }
 if(pairing){
  const char *keys[]={"code"};const char *pin=get_string(json,"code");bool valid=fields(json,keys,1,1) && pin && strlen(pin)==6;
  if(valid)for(unsigned i=0;i<6;i++)if(pin[i]<'0' || pin[i]>'9')valid=false;
  if(!valid){cJSON_Delete(json);return error(r,400,"配对码格式无效");}
  unsigned status=200;const char *message=NULL;char token[33];uint64_t now=now_ms();lock(s);
  if(s->closing){status=503;message="传输服务已停止";}else if(now<s->locked_until){status=429;message="尝试过于频繁，请稍后再试";}else if(now<s->created || now-s->created>=300000){status=401;message="配对码已过期，请重新开启传输";}else{
   if(s->failures>=5)s->failures=0;
   unsigned diff=0;for(unsigned i=0;i<6;i++)diff|=(unsigned char)pin[i]^(unsigned char)s->pin[i];
   if(diff){status=401;message="配对码不正确";if(++s->failures>=5)s->locked_until=now+60000;}else{s->failures=0;memcpy(token,s->token,sizeof token);}
  }
  unlock(s);cJSON_Delete(json);if(message)return error(r,status,message);
  cJSON *result=cJSON_CreateObject();return json_send(r,200,result,string(result,"token",token));
 }
 if(r->method==HTTP_GET && !strcmp(uri,"/api/v1/session")){cJSON *result=cJSON_CreateObject();return json_send(r,200,result,boolean(result,"paired",true));}
 pn_transfer_command_t command={0};bool valid=false;unsigned success=200;
 if(r->method==HTTP_POST && !strcmp(uri,"/api/v1/uploads")){
  const char *keys[]={"kind","name","size","sha256","replace"};const char *name=get_string(json,"name");command.operation=PN_TRANSFER_BEGIN;command.request.kind=kind(get_string(json,"kind"));command.request.has_digest=true;
  valid=fields(json,keys,5,15) && name && strlen(name)<PN_UPLOAD_NAME_MAX && integer(json,"size",&command.request.size) && decode(get_string(json,"sha256"),command.request.digest.sha256,32);
  if(valid)strcpy(command.request.name,name);
  cJSON *replace=cJSON_GetObjectItemCaseSensitive(json,"replace");if(replace){const char *old_keys[]={"size","sha256"};command.replace=true;valid=valid && fields(replace,old_keys,2,3) && integer(replace,"size",&command.old_size) && decode(get_string(replace,"sha256"),command.old_digest.sha256,32);}
  if(valid){esp_fill_random(command.request.id,16);valid=pn_upload_request_validate(&command.request)==PN_OK;}
  success=201;
 }else if(!strncmp(uri,"/api/v1/uploads/",16)){
  const char *id=uri+16;size_t n=strlen(id);char text[33];if(n>=32){memcpy(text,id,32);text[32]=0;valid=decode(text,command.id,16);const char *tail=id+32;
   if(r->method==HTTP_GET && !*tail)command.operation=PN_TRANSFER_OPEN;
   else if(r->method==HTTP_DELETE && !*tail)command.operation=PN_TRANSFER_CANCEL;
   else if(r->method==HTTP_POST && !strcmp(tail,"/complete")){const char *keys[]={"sha256"};command.operation=PN_TRANSFER_COMPLETE;valid=valid && fields(json,keys,1,1) && decode(get_string(json,"sha256"),command.digest.sha256,32);}
   else if(r->method==HTTP_PUT && !strcmp(tail,"/chunks")){char offset[32],sha[65];command.operation=PN_TRANSFER_CHUNK;command.bytes=s->bytes;command.length=r->content_len;valid=valid && header(r,"X-Offset",offset,sizeof offset) && decimal(offset,512u*1024u*1024u,&command.offset) && header(r,"X-Chunk-SHA256",sha,sizeof sha) && decode(sha,command.digest.sha256,32);}
   else valid=false;
  }
 }else if(r->method==HTTP_GET && !strncmp(uri,"/api/v1/files/",14)){
  const char *query=strchr(uri+14,'?');if(query && !strncmp(query,"?name=",6)){char name[16];size_t n=(size_t)(query-(uri+14));if(n<sizeof name){memcpy(name,uri+14,n);name[n]=0;command.request.kind=kind(name);command.operation=PN_TRANSFER_FILE;const char *in=query+6;size_t at=0;valid=command.request.kind!=0;
    while(*in && valid){unsigned char c=(unsigned char)*in++;if(c=='%' && in[0] && in[1]){int a=digit(in[0]),b=digit(in[1]);if(a<0 || b<0){valid=false;break;}c=(uint8_t)(a*16+b);in+=2;}else if(c=='%'){valid=false;break;}else if(c=='+')c=' ';else if(c=='&' || c=='#'){valid=false;break;}if(!c || at>=PN_UPLOAD_NAME_MAX-1){valid=false;break;}command.request.name[at++]=(char)c;}
   }
  }
 }
 cJSON_Delete(json);if(!valid)return error(r,400,"请求操作或参数无效");return execute(s,r,&command,success);
}
pn_status_t pn_device_transfer_http_open(pn_device_transfer_http_t *out,pn_transfer_worker_t *worker,const char *authority,uint16_t port){
 if(!out || !worker || !authority || !*authority || strlen(authority)>=64 || !port)return PN_INVALID;
 if(out->impl)return PN_BUSY;
 for(const char *p=authority;*p;p++)if((*p<'0' || *p>'9') && *p!='.' && *p!=':')return PN_INVALID;
 pn_transfer_worker_state_t state;if(pn_transfer_worker_state(worker,&state)!=PN_OK || state.phase!=PN_TWORK_ACTIVE)return PN_BUSY;
 http_t *s=heap_caps_malloc(sizeof *s,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!s)return PN_NO_MEMORY;memset(s,0,sizeof *s);s->worker=worker;strcpy(s->authority,authority);snprintf(s->origin,sizeof s->origin,"http://%s",authority);
 s->auth=xSemaphoreCreateMutex();if(!s->auth){heap_caps_free(s);return PN_NO_MEMORY;}
 uint32_t value;do{esp_fill_random(&value,sizeof value);}while(value>=UINT32_MAX-UINT32_MAX%1000000u);snprintf(s->pin,sizeof s->pin,"%06u",(unsigned)(value%1000000u));uint8_t token[16];esp_fill_random(token,16);encode(token,16,s->token);s->created=now_ms();
 httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.server_port=port;cfg.ctrl_port=32768;cfg.stack_size=16384;cfg.max_req_hdr_len=2048;cfg.max_uri_len=1024;cfg.max_open_sockets=4;cfg.max_uri_handlers=4;cfg.max_resp_headers=6;cfg.recv_wait_timeout=10;cfg.send_wait_timeout=10;cfg.uri_match_fn=httpd_uri_match_wildcard;
 cfg.global_user_ctx=s;cfg.global_user_ctx_free_fn=keep_context;cfg.open_fn=opened;
 esp_err_t result=httpd_start(&s->server,&cfg);out->impl=s;
 if(result==ESP_OK){const int methods[]={HTTP_GET,HTTP_POST,HTTP_PUT,HTTP_DELETE};for(unsigned i=0;i<4;i++){httpd_uri_t uri={.uri="/*",.method=methods[i],.handler=dispatch,.user_ctx=s};result=httpd_register_uri_handler(s->server,&uri);if(result!=ESP_OK)break;}}
 if(result!=ESP_OK){(void)pn_device_transfer_http_request_stop(out);if(s->server && httpd_stop(s->server)!=ESP_OK)return PN_IO;vSemaphoreDelete(s->auth);heap_caps_free(s);out->impl=NULL;return PN_IO;}
 return PN_OK;
}
pn_status_t pn_device_transfer_http_code(pn_device_transfer_http_t *out,char code[7]){if(!out || !out->impl || !code)return PN_INVALID;http_t *s=out->impl;lock(s);pn_status_t status=s->closing?PN_CANCELLED:PN_OK;if(status==PN_OK)memcpy(code,s->pin,7);unlock(s);return status;}
pn_status_t pn_device_transfer_http_request_stop(pn_device_transfer_http_t *out){if(!out || !out->impl)return PN_INVALID;http_t *s=out->impl;lock(s);s->closing=true;memset(s->token,0,sizeof s->token);unlock(s);return pn_transfer_worker_request_stop(s->worker);}
pn_status_t pn_device_transfer_http_close(pn_device_transfer_http_t *out){if(!out)return PN_INVALID;if(!out->impl)return PN_OK;http_t *s=out->impl;(void)pn_device_transfer_http_request_stop(out);if(s->server && httpd_stop(s->server)!=ESP_OK)return PN_IO;vSemaphoreDelete(s->auth);heap_caps_free(s);out->impl=NULL;return PN_OK;}
