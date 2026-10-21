/* 中文：同源监督任务+真实阅读/文件worker；SDK无线/HTTP传输显式stub。/ English: shared supervisor and real reader/file worker with explicitly stubbed SDK wireless/HTTP transports. */
#define main http_fixture_main
#include "test_device_transfer_http.c"
#undef main
#include "device_transfer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "read_pico_sd.h"
#include "esp_vfs_fat.h"
#include "pn_reader_app.h"
#include <stdatomic.h>
#include <time.h>
#include <sys/stat.h>
const char *pn_stub_wifi_event="wifi",*pn_stub_ip_event="ip";
static esp_event_handler_t wifi_callback,ip_callback;
static void *wifi_ctx,*ip_ctx;
static wifi_mode_t network_mode;
static unsigned connection_calls;
static bool radio;
static bool radio_init_fails;
static _Atomic bool card_present=true;
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out){bool present=card_present;*out=(read_pico_sd_info_t){.present=present,.mounted=present};return present?ESP_OK:ESP_ERR_NOT_FOUND;}
esp_err_t esp_vfs_fat_info(const char *root,uint64_t *total,uint64_t *free_bytes){assert(root);*total=128u*1024u*1024u;*free_bytes=64u*1024u*1024u;return ESP_OK;}
void bootloader_random_enable(void){assert(!radio);}
void bootloader_random_disable(void){}
esp_err_t esp_read_mac(uint8_t *mac,int kind){(void)kind;memset(mac,0x12,6);return ESP_OK;}
esp_err_t esp_wifi_get_mode(wifi_mode_t *out){if(!radio)return ESP_ERR_WIFI_NOT_INIT;*out=WIFI_MODE_AP;return ESP_OK;}
esp_err_t esp_netif_init(void){return ESP_OK;}
esp_err_t esp_event_loop_create_default(void){return ESP_OK;}
esp_err_t esp_event_loop_delete_default(void){return ESP_OK;}
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key){(void)key;return NULL;}
esp_netif_t *esp_netif_new(const esp_netif_config_t *cfg){(void)cfg;return malloc(sizeof(esp_netif_t));}
void esp_netif_destroy(esp_netif_t *netif){assert(!radio);free(netif);}
esp_err_t esp_netif_attach_wifi_ap(esp_netif_t *netif){assert(netif);return ESP_OK;}
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *netif){assert(netif);return ESP_OK;}
esp_err_t esp_wifi_set_default_wifi_ap_handlers(void){return ESP_OK;}
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void){return ESP_OK;}
esp_err_t esp_wifi_clear_default_wifi_driver_and_handlers(void *netif){assert(netif);return ESP_OK;}
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif,esp_netif_ip_info_t *out){assert(netif);uint8_t *ip=(uint8_t *)&out->ip.addr;ip[0]=192;ip[1]=168;ip[2]=4;ip[3]=1;return ESP_OK;}
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int32_t id,esp_event_handler_t fn,void *ctx,esp_event_handler_instance_t *instance){(void)id;if(base==WIFI_EVENT){wifi_callback=fn;wifi_ctx=ctx;}else{ip_callback=fn;ip_ctx=ctx;}*instance=(void *)1;return ESP_OK;}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,int32_t id,esp_event_handler_instance_t instance){(void)id;(void)instance;if(base==WIFI_EVENT)wifi_callback=NULL;else ip_callback=NULL;return ESP_OK;}
esp_err_t esp_wifi_init(const wifi_init_config_t *config){assert(!config->nvs_enable);if(radio_init_fails)return 0x101;radio=true;return ESP_OK;}
esp_err_t esp_wifi_set_storage(int storage){assert(storage==WIFI_STORAGE_RAM);return ESP_OK;}
esp_err_t esp_wifi_set_mode(wifi_mode_t value){assert(value==WIFI_MODE_AP || value==WIFI_MODE_STA);network_mode=value;return ESP_OK;}
esp_err_t esp_wifi_set_config(wifi_interface_t iface,const wifi_config_t *config){assert(iface==WIFI_IF_AP?config->ap.authmode==WIFI_AUTH_WPA2_PSK:iface==WIFI_IF_STA);return ESP_OK;}
esp_err_t esp_wifi_start(void){wifi_callback(wifi_ctx,WIFI_EVENT,network_mode==WIFI_MODE_AP?WIFI_EVENT_AP_START:WIFI_EVENT_STA_START,NULL);return ESP_OK;}
esp_err_t esp_wifi_set_ps(wifi_ps_type_t ps){assert(ps==WIFI_PS_NONE);return ESP_OK;}
esp_err_t esp_wifi_connect(void){
 connection_calls++;ip_event_got_ip_t got={0};uint8_t *ip=(uint8_t *)&got.ip_info.ip.addr;ip[0]=192;ip[1]=168;ip[2]=4;ip[3]=1;
 ip_callback(ip_ctx,IP_EVENT,IP_EVENT_STA_GOT_IP,&got);return ESP_OK;
}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *out){out->rssi=-61;return ESP_OK;}
esp_err_t esp_wifi_stop(void){return ESP_OK;}
esp_err_t esp_wifi_deinit(void){radio=false;return ESP_OK;}
static pn_status_t presented(void *ctx,const pn_frame_t *frame,pn_refresh_t mode){(void)ctx;(void)mode;return frame && frame->pixels?PN_OK:PN_INVALID;}
static pn_device_transfer_state_t wait_for(pn_device_transfer_t *session,int target){pn_device_transfer_state_t state={0};for(unsigned i=0;i<250;i++){assert(pn_device_transfer_state(session,&state)==PN_OK);if(target==1?state.phase==PN_DTRANSFER_READY:target==2?(state.phase==PN_DTRANSFER_FAILED && !state.released):target==3?state.phase==PN_DTRANSFER_STARTING:state.released)return state;struct timespec t={.tv_nsec=20000000};nanosleep(&t,NULL);}fprintf(stderr,"wait target=%d phase=%d released=%d error=%d wifi=%d attempts=%u connections=%u now=%llu\n",target,state.phase,state.released,state.error,state.network.phase,state.network.attempts,connection_calls,(unsigned long long)now);assert(!"transfer supervisor did not reach expected state");return state;}
int main(void){
 char root[]="/tmp/pn-transfer-control-XXXXXX";assert(mkdtemp(root));char book[256],progress[256];snprintf(book,sizeof book,"%s/seed.txt",root);snprintf(progress,sizeof progress,"%s/progress",root);FILE *file=fopen(book,"wb");assert(file);for(unsigned i=0;i<1000;i++)fputs("Reading position survives a transfer session.\n",file);assert(!fclose(file));
 pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,123)==PN_OK);pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_reader_app_t reader={0};
 assert(pn_reader_app_open_on_media(&reader,&pool,&media,book,NULL,progress,44,1000)==PN_OK);assert(pn_reader_app_step(&reader,PN_APP_OPEN,1000,presented,NULL)==PN_OK);assert(pn_reader_app_step(&reader,PN_APP_NEXT,1001,presented,NULL)==PN_OK);pn_txt_progress_t before,after;assert(pn_reader_app_progress(&reader,&before)==PN_OK);
 assert(before.source_offset>0);
 pn_device_transfer_t session={0};pn_device_wifi_config_t config={.mode=PN_WIFI_AP};assert(pn_device_transfer_open(&session,&media,root,&config)==PN_BUSY);assert(pn_reader_app_close(&reader,1002)==PN_OK);
 assert(pn_device_transfer_open(&session,&media,root,&config)==PN_OK);pn_device_transfer_state_t state=wait_for(&session,true);assert(!state.released && state.network.ap_password[0]);assert(pn_device_transfer_close(&session)==PN_BUSY);
 /* 配网信箱：无请求EMPTY，回报校验名称长度。/ Provisioning mailbox: EMPTY without requests; results validate the name length. */
 {pn_network_credentials_t c;bool forget;assert(pn_device_transfer_take_network(&session,&c,&forget)==PN_EMPTY);assert(pn_device_transfer_network_result(&session,PN_OK,"123456789012345678901234567890123")==PN_INVALID && pn_device_transfer_network_result(&session,PN_OK,"家")==PN_OK);}
 {pn_upload_kind_t kind;char name[PN_UPLOAD_NAME_MAX];pn_book_id_t digest;assert(pn_device_transfer_take_install(&session,&kind,name,sizeof name,&digest)==PN_EMPTY);}
 cJSON *json=request(HTTP_GET,"/api/v1/session",NULL,0,NULL,NULL);assert(code==200 && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(json,"token")));cJSON_Delete(json);
 assert(pn_device_transfer_request_stop(&session)==PN_OK);state=wait_for(&session,false);assert(state.phase==PN_DTRANSFER_STOPPED);assert(pn_device_transfer_close(&session)==PN_OK && !native_allocations && !radio && !pn_media_active(&media));
 assert(pn_reader_app_open_on_media(&reader,&pool,&media,book,NULL,progress,44,2000)==PN_OK);assert(pn_reader_app_step(&reader,PN_APP_OPEN,2000,presented,NULL)==PN_OK);assert(pn_reader_app_progress(&reader,&after)==PN_OK);assert(before.source_offset==after.source_offset);assert(pn_reader_app_close(&reader,2001)==PN_OK);
 assert(pn_device_transfer_open(&session,&media,root,&config)==PN_OK);wait_for(&session,true);http_stop_fails=true;assert(pn_device_transfer_request_stop(&session)==PN_OK);state=wait_for(&session,2);assert(state.error==PN_IO && pn_device_transfer_close(&session)==PN_BUSY);http_stop_fails=false;assert(pn_device_transfer_request_stop(&session)==PN_OK);wait_for(&session,false);assert(pn_device_transfer_close(&session)==PN_OK && !native_allocations);
 config.mode=PN_WIFI_STA;strcpy(config.ssid,"test-network");strcpy(config.password,"reader-password");
 assert(pn_device_transfer_open(&session,&media,root,&config)==PN_OK);wait_for(&session,1);
 unsigned connected_before=connection_calls;void *http_before=route.user_ctx;
 json=request(HTTP_GET,"/api/v1/session",NULL,0,NULL,NULL);assert(code==200);char automatic[33];strcpy(automatic,cJSON_GetObjectItemCaseSensitive(json,"token")->valuestring);cJSON_Delete(json);
 const char *unfinished="{\"kind\":\"book\",\"name\":\"retry.txt\",\"size\":65539,\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}";
 json=request(HTTP_POST,"/api/v1/uploads",unfinished,strlen(unfinished),automatic,NULL);assert(code==201);char retained[80];snprintf(retained,sizeof retained,"/api/v1/uploads/%s",cJSON_GetObjectItemCaseSensitive(json,"upload_id")->valuestring);cJSON_Delete(json);
 uint8_t partial[PN_UPLOAD_CHUNK];memset(partial,'r',sizeof partial);pn_book_id_t partial_hash;assert(pn_identity_bytes(partial,sizeof partial,&partial_hash)==PN_OK);
 char chunk_header[96]="X-Chunk-SHA256: ",chunk_uri[96];for(unsigned i=0;i<32;i++)sprintf(chunk_header+sizeof("X-Chunk-SHA256: ")-1+i*2,"%02x",partial_hash.sha256[i]);snprintf(chunk_uri,sizeof chunk_uri,"%s/chunks",retained);
 json=request(HTTP_PUT,chunk_uri,partial,sizeof partial,automatic,chunk_header);assert(code==200 && cJSON_GetObjectItemCaseSensitive(json,"next_offset")->valueint==PN_UPLOAD_CHUNK);cJSON_Delete(json);
 wifi_callback(wifi_ctx,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);state=wait_for(&session,3);assert(!state.released);now+=1000;
 state=wait_for(&session,1);assert(connection_calls==connected_before+1 && route.user_ctx==http_before && !state.released);
 json=request(HTTP_GET,retained,NULL,0,automatic,NULL);assert(code==200 && cJSON_GetObjectItemCaseSensitive(json,"next_offset")->valueint==PN_UPLOAD_CHUNK);cJSON_Delete(json);
 // 长时间断网必须退出接收态、释放租约，已确认分块仍可跨会话续传。/ A sustained outage must leave receiving state, release leases and retain acknowledged chunks across sessions.
 wifi_callback(wifi_ctx,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);state=wait_for(&session,3);assert(state.phase!=PN_DTRANSFER_READY && !state.network.address[0]);now+=20001;
 state=wait_for(&session,0);assert(state.phase==PN_DTRANSFER_FAILED && state.released && !state.work.busy && !pn_media_active(&media));assert(pn_device_transfer_close(&session)==PN_OK && !native_allocations && !radio);
 assert(pn_device_transfer_open(&session,&media,root,&config)==PN_OK);wait_for(&session,1);
 json=request(HTTP_GET,"/api/v1/session",NULL,0,NULL,NULL);assert(code==200);strcpy(automatic,cJSON_GetObjectItemCaseSensitive(json,"token")->valuestring);cJSON_Delete(json);
 json=request(HTTP_GET,retained,NULL,0,automatic,NULL);assert(code==200 && cJSON_GetObjectItemCaseSensitive(json,"next_offset")->valueint==PN_UPLOAD_CHUNK);cJSON_Delete(json);
 assert(pn_device_transfer_request_stop(&session)==PN_OK);wait_for(&session,0);assert(pn_device_transfer_close(&session)==PN_OK && !native_allocations);
 config.mode=PN_WIFI_AP;
 radio_init_fails=true;assert(pn_device_transfer_open(&session,&media,root,&config)==PN_OK);state=wait_for(&session,0);assert(state.phase==PN_DTRANSFER_FAILED && state.stage==2 && state.error==PN_IO && state.network.error==0x101 && state.network.mode==PN_WIFI_AP);assert(pn_device_transfer_close(&session)==PN_OK && !native_allocations && !radio && !pn_media_active(&media));radio_init_fails=false;
 assert(pn_device_transfer_open(&session,&media,root,&config)==PN_OK);wait_for(&session,true);card_present=false;state=wait_for(&session,false);assert(state.phase==PN_DTRANSFER_FAILED && state.error==PN_STALE_MEDIA);assert(pn_device_transfer_close(&session)==PN_OK && !native_allocations && !pn_media_active(&media));
 assert(!pool.used && !pool.live);puts("Transfer supervisor: reader progress, TF handoff, automatic session, STA reconnect with persisted chunk, stop return and card-loss drain passed");return 0;
}
