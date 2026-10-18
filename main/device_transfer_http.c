/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界设备HTTP协议、配对及资源服务，所有文件事务交给独立worker。
 * English: bounded device HTTP protocol, pairing and assets; a dedicated worker owns all file transactions.
 * 冻结：仅ESP-IDF6.1请求头布局，不放宽重复关键头，不记录令牌。
 * Frozen: ESP-IDF6.1 header layout only, never permit duplicate critical headers or log tokens.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_brand.h"
#include "device_transfer_http.h"
#include "pn_localsend_json.h"
#include "device_ls_discovery.h"
#include "transfer_assets.h"
#include "esp_http_server.h"
#include "esp_httpd_priv.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
typedef struct {
 pn_transfer_worker_t *worker;
 httpd_handle_t server;
 httpd_handle_t ls_server; ///< LocalSend标准端口53317的独立监听，客户端子网扫描只找这个端口 / Separate listener on LocalSend's standard port 53317, the only port client subnet scans probe
 SemaphoreHandle_t auth;
 SemaphoreHandle_t serial; ///< 两个监听任务共享会话与缓冲，请求串行处理 / Both listener tasks share sessions and buffers, so requests run serially
 char authority[64],origin[80],token[33];
 bool closing;
 struct {bool ready,rssi_known;int rssi;unsigned disconnects,reconnects,reason;} diagnostics; ///< 无口令快照 / Credential-free snapshot
 const pn_device_network_sink_t *network;
 const pn_device_transfer_install_sink_t *install; ///< 已提交书籍身份出口 / Committed-book identity outlet
 pn_pool_t ls_pool;pn_ls_session_t ls;char fingerprint[33];uint16_t port; ///< 单HTTP owner的LocalSend会话 / LocalSend session owned by the single HTTP task
 pn_device_ls_discovery_t discovery; ///< 生命周期先于HTTP上下文结束 / Ends before the HTTP context is released
 bool discovery_ready;uint64_t discovery_retry; ///< 发现故障不关闭网页，限频重试 / Discovery failures keep the web service alive with bounded retries
 pn_ls_request_t ls_request;pn_ls_snapshot_t ls_snapshot;pn_ls_grants_t ls_grants; ///< 串行复用PSRAM模型，避免撑满HTTP任务栈 / Serially reused PSRAM models avoid exhausting the HTTP task stack
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
 esp_err_t result=httpd_resp_send(r,data,(int)n);
#ifdef ESP_PLATFORM
 // 只记录响应结果与内存，不记录URL查询、令牌或用户内容。/ Record response results and memory only, never URL queries, tokens or user content.
 if(result!=ESP_OK){int socket_error=errno;ESP_LOGW("pn_http","response failed status=%u bytes=%zu result=%d errno=%d internal_free=%zu largest=%zu",status,n,(int)result,socket_error,heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));}
#endif
 // 错误会关闭连接；先读掉不超过64KiB的未读正文，否则对端在发送中收到RST，只看到“发送请求失败”而不是状态码。
 // Errors close the connection; drain up to 64 KiB of unread body first, otherwise the peer gets a reset mid-send and sees "error sending request" instead of the status.
 if(status>=400){char sink[256];size_t drained=0;while(drained<64u*1024u){int got=httpd_req_recv(r,sink,sizeof sink);if(got<=0)break;drained+=(size_t)got;}}
 return status>=400?ESP_FAIL:result;
}
static esp_err_t error(httpd_req_t *r,unsigned status,const char *message){char bytes[384];int n=snprintf(bytes,sizeof bytes,"{\"message\":\"%s\"}",message);return send_data(r,status,"application/json; charset=utf-8",bytes,(size_t)n);}
static esp_err_t json_send(httpd_req_t *r,unsigned status,cJSON *json,bool valid){
 // 4608足够八条最长字体名的列表。/ 4608 bytes fit a listing of eight maximum-length font names.
 char bytes[4608];bool okay=valid && json && cJSON_PrintPreallocated(json,bytes,sizeof bytes,false);cJSON_Delete(json);
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
/// 精确资源路由，拒绝同前缀的其他路径。/ Match exact resource routes, rejecting other paths with the same prefix.
static pn_upload_kind_t resource_route(const char *uri,const char **query){
 const char *paths[]={"/api/v1/fonts","/api/v1/covers","/api/v1/wallpapers"};
 const pn_upload_kind_t kinds[]={PN_UPLOAD_FONT,PN_UPLOAD_COVER,PN_UPLOAD_WALLPAPER};
 for(unsigned i=0;i<3;i++){size_t n=strlen(paths[i]);if(!strncmp(uri,paths[i],n) && (!uri[n] || uri[n]=='?')){*query=uri[n]?uri+n:NULL;return kinds[i];}}
 return 0;
}
static const char *kind_name(pn_upload_kind_t value){return value==PN_UPLOAD_BOOK?"book":value==PN_UPLOAD_FONT?"font":value==PN_UPLOAD_COVER?"cover":"wallpaper";}
static esp_err_t execute(http_t *s,httpd_req_t *r,pn_transfer_command_t *command,unsigned success){
 pn_transfer_reply_t reply;pn_status_t status=pn_transfer_worker_execute(s->worker,command,&reply);
 if(status!=PN_OK)return error(r,status<=PN_IO?statuses[status]:503,status<=PN_IO?messages[status]:"传输服务不可用");
 // 安装事务已确认后才通知主任务；只复制身份，不在此写内部存储。/ Notify the main task only after the install commits; copy identity only, never write internal storage here.
 if(command->operation==PN_TRANSFER_COMPLETE && reply.request.has_digest){
  const pn_device_transfer_install_sink_t *sink=NULL;lock(s);sink=s->install;unlock(s);
  if(sink && sink->installed)sink->installed(sink->ctx,reply.request.kind,reply.request.name,&reply.request.digest);
 }
 cJSON *json=cJSON_CreateObject();bool valid=json!=NULL;
 if(reply.has_file){char sha[65];encode(reply.file_digest.sha256,32,sha);valid&=number(json,"size",reply.file_size);valid&=string(json,"sha256",sha);}
 if(reply.has_upload){char sha[65],id[33];encode(reply.request.digest.sha256,32,sha);encode(reply.request.id,16,id);valid&=string(json,"upload_id",id);valid&=string(json,"name",reply.request.name);valid&=string(json,"kind",kind_name(reply.request.kind));valid&=string(json,"sha256",sha);valid&=number(json,"size",reply.state.size);valid&=number(json,"next_offset",reply.state.offset);valid&=number(json,"phase",reply.state.phase);valid&=number(json,"chunk_size",PN_UPLOAD_CHUNK);valid&=boolean(json,"cleanup_pending",reply.cleanup_pending);}
 if(reply.has_list){
  cJSON *items=json?cJSON_AddArrayToObject(json,"items"):NULL;valid&=items!=NULL;valid&=boolean(json,"more",reply.more);
  for(size_t i=0;valid && i<reply.count;i++){cJSON *item=cJSON_CreateObject();bool good=item && string(item,"name",reply.list[i].name) && number(item,"size",reply.list[i].size);
   if(good)cJSON_AddItemToArray(items,item);else{cJSON_Delete(item);valid=false;}}
 }
 if(reply.deleted){valid&=boolean(json,"deleted",true);valid&=string(json,"message","字体已删除");}
 return json_send(r,success,json,valid);
}
/// 严格解析 ?k=v&k=v：键须在允许集合内且各至多一次，值做百分号解码，拒绝#、NUL与超长。
/// Strictly parse ?k=v&k=v: keys must be allowed and unique, values are percent-decoded, and '#', NUL and overlong values are rejected.
static bool query_parse(const char *query,const char *const *keys,size_t n,char values[][PN_UPLOAD_NAME_MAX],unsigned *seen){
 *seen=0;
 if(!query)return true;
 if(*query!='?')return false;
 const char *at=query+1;
 while(*at){
  const char *eq=strchr(at,'=');const char *amp=strchr(at,'&');
  if(!eq || (amp && amp<eq))return false;
  size_t key_length=(size_t)(eq-at),i=0;
  while(i<n && (strlen(keys[i])!=key_length || strncmp(keys[i],at,key_length)))i++;
  if(i==n || (*seen&(1u<<i)))return false;
  *seen|=1u<<i;
  const char *in=eq+1;size_t out=0;
  while(*in && *in!='&'){
   unsigned char c=(unsigned char)*in++;
   if(c=='%'){int a=in[0]?digit(in[0]):-1,b=a>=0 && in[1]?digit(in[1]):-1;if(a<0 || b<0)return false;c=(unsigned char)(a*16+b);in+=2;}
   else if(c=='+')c=' ';
   else if(c=='#')return false;
   if(!c || out>=PN_UPLOAD_NAME_MAX-1)return false;
   values[i][out++]=(char)c;
  }
  values[i][out]=0;
  at=*in=='&'?in+1:in;
 }
 return true;
}
static pn_status_t ls_entropy(void *ctx,uint8_t *out,size_t n){(void)ctx;esp_fill_random(out,n);return PN_OK;}
static void *ls_allocate(void *ctx,size_t n){(void)ctx;return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
static void ls_release(void *ctx,void *p){(void)ctx;heap_caps_free(p);}
static bool ls_peer(httpd_req_t *r,pn_ls_peer_t *peer){
 struct sockaddr_storage address;socklen_t length=sizeof address;memset(peer,0,sizeof *peer);
 if(getpeername(httpd_req_to_sockfd(r),(struct sockaddr *)&address,&length))return false;
 if(address.ss_family==AF_INET){peer->family=4;memcpy(peer->address,&((struct sockaddr_in *)&address)->sin_addr,4);return true;}
 if(address.ss_family==AF_INET6){peer->family=6;memcpy(peer->address,&((struct sockaddr_in6 *)&address)->sin6_addr,16);return true;}return false;
}
static esp_err_t ls_info(http_t *s,httpd_req_t *r){
 cJSON *json=cJSON_CreateObject();bool valid=string(json,"alias",PN_BRAND_NAME);valid&=string(json,"version","2.1");valid&=string(json,"deviceModel",PN_BRAND_NAME);valid&=string(json,"deviceType","headless");
 valid&=string(json,"fingerprint",s->fingerprint);valid&=string(json,"protocol","http");valid&=number(json,"port",s->ls_server?PN_DEVICE_LS_PORT:s->port);valid&=boolean(json,"download",false);return json_send(r,200,json,valid);
}
typedef struct {http_t *server;httpd_req_t *request;uint64_t received,started;} ls_stream_t;
static pn_status_t ls_execute(void *ctx,const pn_transfer_command_t *command,pn_transfer_reply_t *reply){return pn_transfer_worker_execute(((http_t *)ctx)->worker,command,reply);}
static pn_status_t ls_read(void *ctx,uint8_t *out,size_t cap,size_t *n){
 ls_stream_t *stream=ctx;http_t *s=stream->server;*n=0;
 if(is_closing(s))return PN_CANCELLED;
 if(stream->received==stream->request->content_len){pn_status_t done=pn_ls_body_done(&s->ls,now_ms());return done==PN_OK?PN_EMPTY:done;}
 uint64_t now=now_ms();if(now<stream->started || now-stream->started>=PN_LS_TRANSFER_MS)return PN_CANCELLED;
 size_t remaining=stream->request->content_len-(size_t)stream->received;if(cap>remaining)cap=remaining;
 int got=httpd_req_recv(stream->request,(char *)out,cap);if(got<=0 || (size_t)got>cap)return PN_IO;
 stream->received+=(size_t)got;*n=(size_t)got;return pn_ls_progress(&s->ls,stream->received,now_ms());
}
/// uri为规范化后的v2路由（v3客户端路径映射到同一处理）。/ uri is the normalized v2 route (v3 client paths map to the same handlers).
static esp_err_t ls_dispatch(http_t *s,httpd_req_t *r,unsigned presence,const char *uri){
 // Native LocalSend uses peer/session grants; browser requests retain their separate pairing contract.
 // 原生LocalSend使用连接来源与会话授权；浏览器仍走独立配对契约。
 if(is_closing(s))return error(r,503,"传输服务已停止");
 if(presence&2u)return error(r,403,"LocalSend仅接受原生客户端");
 pn_ls_peer_t peer;if(!ls_peer(r,&peer))return error(r,403,"无法确认连接来源");
 (void)pn_ls_poll(&s->ls,now_ms());
 bool info=r->method==HTTP_GET && !strcmp(uri,"/api/localsend/v2/info");
 bool registration=r->method==HTTP_POST && !strcmp(uri,"/api/localsend/v2/register");
 bool prepare=r->method==HTTP_POST && !strcmp(uri,"/api/localsend/v2/prepare-upload");
 if(info){if(r->content_len)return error(r,400,"无效正文");return ls_info(s,r);}
 if(registration || prepare){
  char length[32],type[64];uint64_t parsed;
  if(!header(r,"Content-Length",length,sizeof length) || !decimal(length,PN_LS_JSON_MAX,&parsed) || parsed!=r->content_len || !parsed || !header(r,"Content-Type",type,sizeof type) || (strcasecmp(type,"application/json") && strcasecmp(type,"application/json; charset=utf-8")))return error(r,400,"无效元数据");
  uint64_t started=now_ms();size_t at=0;
  while(at<r->content_len){if(is_closing(s) || now_ms()-started>=PN_LS_IDLE_MS)return error(r,408,"元数据接收超时");int got=httpd_req_recv(r,(char *)s->bytes+at,r->content_len-at);if(got<=0 || (size_t)got>r->content_len-at)return error(r,400,"元数据不完整");at+=(size_t)got;}
  if(registration){s->bytes[at]=0;if(!json_guard(s->bytes,at))return error(r,400,"无效元数据");cJSON *j=cJSON_ParseWithLengthOpts((char *)s->bytes,at+1,NULL,true);bool valid=cJSON_IsObject(j) && get_string(j,"fingerprint") && get_string(j,"version");cJSON_Delete(j);return valid?ls_info(s,r):error(r,400,"无效设备信息");}
  pn_ls_request_t *request=&s->ls_request;pn_status_t status=pn_ls_parse_prepare(&s->ls_pool,s->bytes,at,request);
  if(status==PN_OK)status=pn_ls_prepare(&s->ls,&peer,request,now_ms());
  if(status!=PN_OK)return error(r,status==PN_BUSY?409:400,"接收请求未能建立");
  pn_ls_snapshot_t *snapshot=&s->ls_snapshot;status=pn_ls_snapshot(&s->ls,snapshot);pn_ls_choice_t choices[PN_LS_FILES_MAX]={0};size_t accepted=0;
  if(status==PN_OK)for(size_t i=0;i<request->count;i++){
   // 用户开启接收模式即授权书籍/字体自动接收；图片须通过网页明确指定封面或壁纸。
   // Entering receive mode authorizes book/font auto-acceptance; images require an explicit cover/wallpaper category on the web page.
   choices[i].accept=snapshot->request.files[i].kind==PN_UPLOAD_BOOK || snapshot->request.files[i].kind==PN_UPLOAD_FONT;accepted+=choices[i].accept;
  }
  if(status==PN_OK)status=pn_ls_decide(&s->ls,snapshot->generation,choices,now_ms());
  if(status!=PN_OK || !accepted)return error(r,403,"图片请在网页选择封面或壁纸");
  char reply[PN_LS_REPLY_MAX];size_t written=0;
  status=pn_ls_grants(&s->ls,&s->ls_grants,now_ms());if(status==PN_OK)status=pn_ls_encode_grants(&s->ls_pool,&s->ls_grants,reply,sizeof reply,&written);
  return status==PN_OK?send_data(r,200,"application/json",reply,written):error(r,503,"接收授权不可用");
 }
 if(r->method!=HTTP_POST)return error(r,404,"未知LocalSend操作");
 const char *query=strchr(uri,'?');size_t route_len=query?(size_t)(query-uri):strlen(uri);char values[3][PN_UPLOAD_NAME_MAX];unsigned seen=0;
 const char *keys[]={"sessionId","fileId","token"};
 if(!query_parse(query,keys,3,values,&seen))return error(r,400,"无效会话参数");
 if(route_len==strlen("/api/localsend/v2/cancel") && !strncmp(uri,"/api/localsend/v2/cancel",route_len)){
  if(seen!=1u || r->content_len)return error(r,400,"无效取消请求");
  pn_status_t status=pn_ls_cancel_peer(&s->ls,&peer,values[0],now_ms());return status==PN_OK?send_data(r,200,"text/plain","",0):error(r,403,"无效会话来源");
 }
 if(route_len!=strlen("/api/localsend/v2/upload") || strncmp(uri,"/api/localsend/v2/upload",route_len))return error(r,404,"未知LocalSend操作");
 if(seen!=7u)return error(r,400,"缺少会话参数");
 pn_upload_request_t request;pn_status_t status=pn_ls_authorize(&s->ls,&peer,values[0],values[1],values[2],now_ms(),&request);
 if(status!=PN_OK)return error(r,status==PN_BUSY?409:403,"无效文件授权");
 char length[32];uint64_t parsed;
 if(!header(r,"Content-Length",length,sizeof length) || !decimal(length,512u*1024u*1024u,&parsed) || parsed!=request.size || parsed!=r->content_len){(void)pn_ls_cancel(&s->ls,now_ms());return error(r,400,"文件长度不匹配");}
 ls_stream_t stream={s,r,0,now_ms()};pn_transfer_receive_result_t receipt;
 status=pn_transfer_receive(&s->ls_pool,ls_execute,s,&request,ls_read,&stream,&receipt);pn_status_t finished=pn_ls_finish(&s->ls,&receipt,now_ms());
 if(status==PN_OK && finished==PN_OK && receipt.committed){
  const pn_device_transfer_install_sink_t *sink=NULL;lock(s);sink=s->install;unlock(s);
  if(sink && sink->installed)sink->installed(sink->ctx,request.kind,request.name,&receipt.digest);
  return send_data(r,200,"text/plain","",0);
 }
 return error(r,status==PN_CORRUPT?422:status==PN_BUSY?409:503,"文件未确认安装，请重试或关闭传输");
}
static esp_err_t dispatch_serial(httpd_req_t *r){
 http_t *s=r->user_ctx;char host[64],origin[80],authorization[64];
 if(strlen(r->uri)>1024)return error(r,400,"请求地址过长");
 unsigned presence=0;
 if(!unique_headers(r,&presence))return error(r,400,"请求头重复或无效");
 // 新版LocalSend客户端先请求v3路径；其注册/准备/上传与v2同形，映射到同一处理，避免被网页路由拒绝。
 // Newer LocalSend clients request v3 paths first; register/prepare/upload keep the v2 shape, so map them onto the same handlers instead of letting web routing reject them.
 bool localsend=!strncmp(r->uri,"/api/localsend/v2/",18) || !strncmp(r->uri,"/api/localsend/v3/",18);
 char ls_route[1032];if(localsend){snprintf(ls_route,sizeof ls_route,"%s",r->uri);ls_route[16]='2';}char host_port[72];snprintf(host_port,sizeof host_port,"%s:%u",s->authority,s->port);
 char ls_host[72];snprintf(ls_host,sizeof ls_host,"%s:%u",s->authority,(unsigned)PN_DEVICE_LS_PORT);
 if(!header(r,"Host",host,sizeof host) || (strcmp(host,s->authority) && (!localsend || (strcmp(host,host_port) && strcmp(host,ls_host)))))return error(r,403,"传输地址不匹配");
 if(localsend)return ls_dispatch(s,r,presence,ls_route);
 bool has_origin=(presence&2u)!=0;bool valid_origin=header(r,"Origin",origin,sizeof origin);
 if((has_origin && (!valid_origin || strcmp(origin,s->origin))) || (r->method!=HTTP_GET && !has_origin))return error(r,403,"请求来源不匹配");
 lock(s);bool closing=s->closing;bool authorized=false;
 if(header(r,"Authorization",authorization,sizeof authorization) && strlen(authorization)==39 && !memcmp(authorization,"Bearer ",7)){unsigned diff=0;for(unsigned i=0;i<32;i++)diff|=(unsigned char)authorization[i+7]^(unsigned char)s->token[i];authorized=diff==0;}
 unlock(s);if(closing)return error(r,503,"传输服务已停止");
 const char *uri=r->uri;if(!uri || strlen(uri)>1024)return error(r,400,"请求地址过长");
 if((r->method==HTTP_GET || r->method==HTTP_DELETE) && r->content_len)return error(r,400,"该操作不接受请求正文");
 if(r->method==HTTP_GET){
  const pn_transfer_asset_t *asset=pn_transfer_asset(uri);if(asset)return send_data(r,200,asset->mime,(const char *)asset->bytes,asset->size);
  if(!strcmp(uri,"/api/v1/status")){cJSON *json=cJSON_CreateObject();bool v=string(json,"name",PN_BRAND_NAME);v&=string(json,"version",PN_PRODUCT_VERSION);v&=number(json,"chunk_size",PN_UPLOAD_CHUNK);v&=boolean(json,"preview",false);
   lock(s);bool ready=s->diagnostics.ready,known=s->diagnostics.rssi_known;int rssi=s->diagnostics.rssi;unsigned drops=s->diagnostics.disconnects,recoveries=s->diagnostics.reconnects,reason=s->diagnostics.reason;unlock(s);
   v&=boolean(json,"wifi_ready",ready);v&=boolean(json,"rssi_known",known);if(known)v&=json && cJSON_AddNumberToObject(json,"rssi_dbm",(double)rssi)!=NULL;v&=number(json,"disconnects",drops);v&=number(json,"reconnects",recoveries);v&=number(json,"last_disconnect_reason",reason);return json_send(r,200,json,v);}
 }
 // 同源页面自动取得短生命周期令牌，不再输入配对码。 / Same-origin pages obtain a session token automatically without a pairing code.
 if(r->method==HTTP_GET && !strcmp(uri,"/api/v1/session")){
  char token[33];lock(s);closing=s->closing;if(!closing)memcpy(token,s->token,sizeof token);unlock(s);
  if(closing)return error(r,503,"传输服务已停止");
  cJSON *result=cJSON_CreateObject();return json_send(r,200,result,boolean(result,"ready",true) && string(result,"token",token));
 }
 if(!authorized)return error(r,401,"会话已失效，请刷新页面重新连接");
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
 if(!strcmp(uri,"/api/v1/network")){
  // 配网：只排队交给主任务保存，口令不回显。/ Provisioning: queue for the main task to save; passwords are never echoed.
  lock(s);const pn_device_network_sink_t *sink=s->network;unlock(s);
  if(!sink){cJSON_Delete(json);return error(r,404,"当前模式不支持保存家庭网络");}
  if(r->method==HTTP_GET){char ssid[PN_NETWORK_SSID_MAX+1]={0};bool pending=false;pn_status_t last=PN_EMPTY;pn_status_t got=sink->status(sink->ctx,ssid,&pending,&last);cJSON_Delete(json);
   if(got!=PN_OK)return error(r,500,"无法读取网络状态");
   cJSON *result=cJSON_CreateObject();bool v=string(result,"ssid",ssid);v&=boolean(result,"saved",ssid[0]!=0);v&=boolean(result,"pending",pending);v&=string(result,"result",last==PN_EMPTY?"none":last==PN_OK?"ok":"failed");return json_send(r,200,result,v);}
  pn_network_credentials_t credentials={0};bool forget=r->method==HTTP_DELETE;pn_status_t queued=PN_INVALID;
  if(r->method==HTTP_POST){const char *keys[]={"ssid","password"};const char *ssid=get_string(json,"ssid"),*password=get_string(json,"password");
   if(fields(json,keys,2,3) && ssid && password && strlen(ssid)<=PN_NETWORK_SSID_MAX && strlen(password)<=PN_NETWORK_PASSWORD_MAX){strcpy(credentials.ssid,ssid);strcpy(credentials.password,password);
    queued=pn_network_validate(&credentials,false)==PN_OK?sink->submit(sink->ctx,&credentials,false):PN_INVALID;}}
  else if(forget && !json)queued=sink->submit(sink->ctx,&credentials,true);
  pn_network_wipe(&credentials);cJSON_Delete(json);
  if(queued==PN_BUSY)return error(r,409,"上一个网络设置还在保存");
  if(queued!=PN_OK)return error(r,400,"网络名称或口令格式无效（口令8–63位或留空）");
  cJSON *result=cJSON_CreateObject();return json_send(r,202,result,boolean(result,"pending",true));
 }
 pn_transfer_command_t command={0};bool valid=false;unsigned success=200;const char *resource_query=NULL;pn_upload_kind_t resource=resource_route(uri,&resource_query);
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
 }else if((r->method==HTTP_GET || r->method==HTTP_DELETE) && resource){
  // 字体/图片原图：分页与删除复用owner，删除不会修改内部已应用壁纸。/ Font/image sources: paging and deletion share the owner; deleting never modifies the internally applied wallpaper.
  const char *query=resource_query;unsigned seen=0;static const char *const list_keys[]={"after"},*const delete_keys[]={"name","size","sha256"};
  char values[3][PN_UPLOAD_NAME_MAX];command.request.kind=resource;
  if(r->method==HTTP_GET){
   valid=query_parse(query,list_keys,1,values,&seen);command.operation=PN_TRANSFER_LIST;
   if(valid && (seen&1u))snprintf(command.request.name,sizeof command.request.name,"%s",values[0]);
  }else{
   valid=query_parse(query,delete_keys,3,values,&seen) && seen==7u;command.operation=PN_TRANSFER_DELETE;
   if(valid){snprintf(command.request.name,sizeof command.request.name,"%s",values[0]);valid=decimal(values[1],512u*1024u*1024u,&command.old_size) && command.old_size>0 && decode(values[2],command.old_digest.sha256,32);}
  }
 }else if(r->method==HTTP_GET && !strncmp(uri,"/api/v1/files/",14)){
  const char *query=strchr(uri+14,'?');if(query && !strncmp(query,"?name=",6)){char name[16];size_t n=(size_t)(query-(uri+14));if(n<sizeof name){memcpy(name,uri+14,n);name[n]=0;command.request.kind=kind(name);command.operation=PN_TRANSFER_FILE;const char *in=query+6;size_t at=0;valid=command.request.kind!=0;
    while(*in && valid){unsigned char c=(unsigned char)*in++;if(c=='%' && in[0] && in[1]){int a=digit(in[0]),b=digit(in[1]);if(a<0 || b<0){valid=false;break;}c=(uint8_t)(a*16+b);in+=2;}else if(c=='%'){valid=false;break;}else if(c=='+')c=' ';else if(c=='&' || c=='#'){valid=false;break;}if(!c || at>=PN_UPLOAD_NAME_MAX-1){valid=false;break;}command.request.name[at++]=(char)c;}
   }
  }
 }
 cJSON_Delete(json);if(!valid)return error(r,400,"请求操作或参数无效");return execute(s,r,&command,success);
}
pn_status_t pn_device_transfer_http_network(pn_device_transfer_http_t *http,const pn_device_network_sink_t *sink){
 if(!http || !http->impl || (sink && (!sink->submit || !sink->status)))return PN_INVALID;
 http_t *s=http->impl;lock(s);s->network=sink;unlock(s);return PN_OK;
}
pn_status_t pn_device_transfer_http_install(pn_device_transfer_http_t *http,const pn_device_transfer_install_sink_t *sink){
 if(!http || !http->impl || (sink && !sink->installed))return PN_INVALID;
 http_t *s=http->impl;lock(s);s->install=sink;unlock(s);return PN_OK;
}
/// 网页(80)与LocalSend(53317)两个监听共用一个会话：串行处理，长上传时另一端短暂返回503而不是争用缓冲。
/// Web (80) and LocalSend (53317) listeners share one session: serialize requests so the other side briefly gets 503 during long uploads instead of racing buffers.
static esp_err_t dispatch(httpd_req_t *r){
 http_t *s=r->user_ctx;
 if(xSemaphoreTake(s->serial,pdMS_TO_TICKS(3000))!=pdTRUE)return error(r,503,"正在接收其他传输，请稍后重试");
 esp_err_t result=dispatch_serial(r);xSemaphoreGive(s->serial);return result;
}
pn_status_t pn_device_transfer_http_open(pn_device_transfer_http_t *out,pn_transfer_worker_t *worker,const char *authority,uint16_t port){
 if(!out || !worker || !authority || !*authority || strlen(authority)>=64 || !port)return PN_INVALID;
 if(out->impl)return PN_BUSY;
 for(const char *p=authority;*p;p++)if((*p<'0' || *p>'9') && *p!='.' && *p!=':')return PN_INVALID;
 pn_transfer_worker_state_t state;if(pn_transfer_worker_state(worker,&state)!=PN_OK || state.phase!=PN_TWORK_ACTIVE)return PN_BUSY;
 http_t *s=heap_caps_malloc(sizeof *s,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!s)return PN_NO_MEMORY;memset(s,0,sizeof *s);s->worker=worker;strcpy(s->authority,authority);snprintf(s->origin,sizeof s->origin,"http://%s",authority);
 s->auth=xSemaphoreCreateMutex();s->serial=xSemaphoreCreateMutex();if(!s->auth || !s->serial){if(s->auth)vSemaphoreDelete(s->auth);if(s->serial)vSemaphoreDelete(s->serial);heap_caps_free(s);return PN_NO_MEMORY;}
 s->port=port;uint8_t fingerprint[16];esp_fill_random(fingerprint,sizeof fingerprint);encode(fingerprint,16,s->fingerprint);
 if(pn_pool_init(&s->ls_pool,128u*1024u,ls_allocate,ls_release,NULL)!=0 || pn_ls_session_open(&s->ls,&s->ls_pool,ls_entropy,NULL)!=PN_OK){vSemaphoreDelete(s->auth);vSemaphoreDelete(s->serial);heap_caps_free(s);return PN_NO_MEMORY;}
 uint8_t token[16];esp_fill_random(token,16);encode(token,16,s->token);
 // 接收owner串行处理文件；浏览器上传与状态轮询会并行开连接，两条上限加LRU回收会掐断进行中的上传，放宽到四条。
 // The receiving owner serializes files; browsers open parallel connections for uploads and status polls, and a two-socket cap with LRU purging cut in-flight uploads, so allow four.
 httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.server_port=port;cfg.ctrl_port=32768;cfg.stack_size=16384;cfg.max_req_hdr_len=2048;cfg.max_uri_len=1024;cfg.max_open_sockets=4;
 // 处理函数不直接写片内flash（存储写入交主任务/接收任务），栈放PSRAM，16KiB片内不足曾让“接收服务”启动失败（阶段4、PN_IO）。
 // Handlers never write internal flash directly (storage writes go to the main/receiving tasks), so the stack lives in PSRAM; a 16 KiB internal shortage made the receiving service fail to start (stage 4, PN_IO).
 cfg.task_caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;cfg.lru_purge_enable=true;cfg.max_uri_handlers=4;cfg.max_resp_headers=6;cfg.recv_wait_timeout=10;cfg.send_wait_timeout=10;cfg.uri_match_fn=httpd_uri_match_wildcard;
 cfg.global_user_ctx=s;cfg.global_user_ctx_free_fn=keep_context;cfg.open_fn=opened;
 esp_err_t result=httpd_start(&s->server,&cfg);out->impl=s;
 if(result==ESP_OK){const int methods[]={HTTP_GET,HTTP_POST,HTTP_PUT,HTTP_DELETE};for(unsigned i=0;i<4;i++){httpd_uri_t uri={.uri="/*",.method=methods[i],.handler=dispatch,.user_ctx=s};result=httpd_register_uri_handler(s->server,&uri);if(result!=ESP_OK)break;}}
 if(result!=ESP_OK){(void)pn_device_transfer_http_request_stop(out);if(s->server && httpd_stop(s->server)!=ESP_OK)return PN_IO;(void)pn_ls_stop_after_join(&s->ls);(void)pn_ls_session_close(&s->ls);vSemaphoreDelete(s->auth);vSemaphoreDelete(s->serial);heap_caps_free(s);out->impl=NULL;return PN_IO;}
 // LocalSend标准端口另开监听，栈放PSRAM不占内部RAM；失败时仍可在80端口按原公告工作。
 // Open LocalSend's standard port as a second listener with its stack in PSRAM; on failure LocalSend keeps working on port 80 with the original announcement.
 if(port!=PN_DEVICE_LS_PORT){httpd_config_t ls=cfg;ls.server_port=PN_DEVICE_LS_PORT;ls.ctrl_port=32769;ls.max_open_sockets=2;ls.task_caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
  esp_err_t opened_ls=httpd_start(&s->ls_server,&ls);
  if(opened_ls==ESP_OK){const int methods[]={HTTP_GET,HTTP_POST};for(unsigned i=0;i<2 && opened_ls==ESP_OK;i++){httpd_uri_t uri={.uri="/*",.method=methods[i],.handler=dispatch,.user_ctx=s};opened_ls=httpd_register_uri_handler(s->ls_server,&uri);}}
  if(opened_ls!=ESP_OK){
#ifdef ESP_PLATFORM
   ESP_LOGW("pn_transfer","LocalSend port %u unavailable: %s",(unsigned)PN_DEVICE_LS_PORT,esp_err_to_name(opened_ls));
#endif
   if(s->ls_server)(void)httpd_stop(s->ls_server);
   s->ls_server=NULL;}}
 char address[64];snprintf(address,sizeof address,"%s",authority);char *colon=strchr(address,':');if(colon)*colon=0;
 pn_status_t discovery=pn_device_ls_discovery_open(&s->discovery,address,s->ls_server?PN_DEVICE_LS_PORT:port,s->fingerprint);s->discovery_ready=discovery==PN_OK;s->discovery_retry=now_ms()+2000;
 return PN_OK;
}
bool pn_device_transfer_http_localsend(pn_device_transfer_http_t *out){
 if(!out || !out->impl)return false;
 http_t *s=out->impl;
 if(!s->discovery_ready && !is_closing(s) && now_ms()>=s->discovery_retry){char address[64];strcpy(address,s->authority);char *colon=strchr(address,':');if(colon)*colon=0;s->discovery_ready=pn_device_ls_discovery_open(&s->discovery,address,s->ls_server?PN_DEVICE_LS_PORT:s->port,s->fingerprint)==PN_OK;s->discovery_retry=now_ms()+2000;}
 return s->discovery_ready;
}
pn_status_t pn_device_transfer_http_request_stop(pn_device_transfer_http_t *out){if(!out || !out->impl)return PN_INVALID;http_t *s=out->impl;lock(s);s->closing=true;memset(s->token,0,sizeof s->token);unlock(s);return pn_transfer_worker_request_stop(s->worker);}
pn_status_t pn_device_transfer_http_diagnostics(pn_device_transfer_http_t *out,const pn_device_wifi_state_t *state){if(!out || !out->impl || !state)return PN_INVALID;http_t *s=out->impl;lock(s);s->diagnostics.ready=state->phase==PN_WIFI_READY;s->diagnostics.rssi_known=state->rssi_known;s->diagnostics.rssi=state->rssi;s->diagnostics.disconnects=state->disconnects;s->diagnostics.reconnects=state->reconnects;s->diagnostics.reason=state->last_disconnect_reason;unlock(s);return PN_OK;}
pn_status_t pn_device_transfer_http_close(pn_device_transfer_http_t *out){if(!out)return PN_INVALID;if(!out->impl)return PN_OK;http_t *s=out->impl;(void)pn_device_transfer_http_request_stop(out);pn_status_t discovery=pn_device_ls_discovery_close(&s->discovery);if(discovery!=PN_OK)return discovery;if(s->ls_server){if(httpd_stop(s->ls_server)!=ESP_OK)return PN_IO;s->ls_server=NULL;}if(s->server && httpd_stop(s->server)!=ESP_OK)return PN_IO;(void)pn_ls_stop_after_join(&s->ls);(void)pn_ls_session_close(&s->ls);vSemaphoreDelete(s->auth);vSemaphoreDelete(s->serial);heap_caps_free(s);out->impl=NULL;return PN_OK;}
