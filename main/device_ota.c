/* SPDX-License-Identifier: Apache-2.0
 * 中文：Gitee匿名HTTPS、PSA验签、非活动Flash槽和启动回滚的设备端口。
 * English: device ports for anonymous Gitee HTTPS, PSA signatures, inactive flash slots and boot rollback. */
#include "device_ota.h"
#include "pn_brand.h"
#include "pn_network_store.h"
#include "sdkconfig.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "cJSON.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#define OTA_HTTP_MAX 8192u
#define OTA_CHUNK 4096u
#define OTA_TIMEOUT_MS 10000 ///< 单次连接/读取等待 / Per connect/read wait
#define OTA_READ_STALLS 3u ///< 同一连接连续超时上限，约30秒无数据 / Consecutive timeouts per connection, about 30 s without data
#define OTA_RESUMES 4u ///< 下载断线后的Range续传次数 / Range resumptions after a dropped download
typedef struct {
    pn_device_ota_state_t state;
    pn_device_wifi_t wifi;
    pn_device_wifi_config_t config;
    pn_update_t update;
    pn_update_target_t target;
    const esp_partition_t *running,*destination;
    esp_ota_handle_t handle;
    bool writing,started,installing,worker_active,network_failed;
    char location[512];
    char range[96]; ///< 最近响应的Content-Range / Content-Range of the latest response
    _Atomic bool stop,worker_done;
    _Atomic uint32_t progress;
    pn_status_t result;
    uint8_t *scratch;
} ota_t;
static int hex(char c){return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;}
static bool public_key(uint8_t key[65]){
#if defined(CONFIG_PN_DEVICE_OTA) && CONFIG_PN_DEVICE_OTA && defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) && CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const char *text=CONFIG_PN_OTA_PUBLIC_KEY_HEX;if(strlen(text)!=130)return false;
    for(unsigned i=0;i<65;i++){int a=hex(text[i*2]),b=hex(text[i*2+1]);if(a<0 || b<0)return false;key[i]=(uint8_t)(a*16+b);}
    return key[0]==4;
#else
    (void)key;(void)hex;return false;
#endif
}
bool pn_device_ota_configured(void){uint8_t key[65];return public_key(key);}
static void wipe_config(ota_t *s){volatile uint8_t *p=(volatile uint8_t *)&s->config;for(size_t i=0;i<sizeof s->config;i++)p[i]=0;}
static pn_status_t signature(void *ctx,const uint8_t *data,size_t n,const uint8_t *sig){
    (void)ctx;uint8_t key[65],digest[32];size_t got=0;psa_key_id_t id=0;psa_key_attributes_t a=PSA_KEY_ATTRIBUTES_INIT;
    if(!public_key(key))return PN_UNSUPPORTED;
    if(psa_crypto_init()!=PSA_SUCCESS || psa_hash_compute(PSA_ALG_SHA_256,data,n,digest,sizeof digest,&got)!=PSA_SUCCESS || got!=32)return PN_IO;
    psa_set_key_type(&a,PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));psa_set_key_bits(&a,256);psa_set_key_usage_flags(&a,PSA_KEY_USAGE_VERIFY_HASH);psa_set_key_algorithm(&a,PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_status_t result=psa_import_key(&a,key,sizeof key,&id);psa_reset_key_attributes(&a);
    if(result==PSA_SUCCESS)result=psa_verify_hash(id,PSA_ALG_ECDSA(PSA_ALG_SHA_256),digest,sizeof digest,sig,64);
    if(id)(void)psa_destroy_key(id);
    return result==PSA_SUCCESS?PN_OK:PN_CORRUPT;
}
static pn_status_t flash_digest(ota_t *s,const esp_partition_t *part,uint32_t bytes,const uint8_t *expected){
    psa_hash_operation_t op=PSA_HASH_OPERATION_INIT;pn_status_t status=PN_OK;
    if(psa_hash_setup(&op,PSA_ALG_SHA_256)!=PSA_SUCCESS)return PN_IO;
    for(uint32_t at=0;at<bytes;){
        if(atomic_load(&s->stop)){status=PN_CANCELLED;break;}
        size_t n=bytes-at;if(n>OTA_CHUNK)n=OTA_CHUNK;
        esp_err_t e=part?esp_partition_read(part,at,s->scratch,n):esp_flash_read(NULL,s->scratch,at,n);
        if(e!=ESP_OK || psa_hash_update(&op,s->scratch,n)!=PSA_SUCCESS){status=PN_IO;break;}
        at+=(uint32_t)n;vTaskDelay(1);
    }
    uint8_t digest[32];size_t got=0;
    if(status==PN_OK && (psa_hash_finish(&op,digest,sizeof digest,&got)!=PSA_SUCCESS || got!=32 || memcmp(digest,expected,32)))status=PN_CORRUPT;
    (void)psa_hash_abort(&op);return status;
}
static pn_status_t bootloader(void *ctx,uint32_t size,const uint8_t *sha){pn_status_t e=flash_digest(ctx,NULL,size,sha);return e==PN_CORRUPT?PN_UNSUPPORTED:e;}
static pn_status_t begin(void *ctx,unsigned slot,uint32_t size){
    ota_t *s=ctx;
    if(atomic_load(&s->stop))return PN_CANCELLED;
    s->destination=esp_partition_find_first(ESP_PARTITION_TYPE_APP,slot?ESP_PARTITION_SUBTYPE_APP_OTA_1:ESP_PARTITION_SUBTYPE_APP_OTA_0,NULL);
    if(!s->destination || s->destination==esp_ota_get_running_partition() || size>s->destination->size)return PN_UNSUPPORTED;
    // 首次USB迁移可能留下空otadata，先将当前同一应用登记为VALID，给首轮失败回滚保留有效记录。
    // A USB migration can leave blank otadata: register this same running app as VALID to retain a valid first-OTA rollback record.
    esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;esp_err_t e=esp_ota_get_state_partition(s->running,&state);
    const esp_partition_t *selected=esp_ota_get_boot_partition();
    if(!selected || selected->address!=s->running->address)return PN_STALE_JOB;
    if(e==ESP_ERR_NOT_FOUND || (e==ESP_OK && state==ESP_OTA_IMG_UNDEFINED)){
        if(esp_ota_set_boot_partition(s->running)!=ESP_OK || esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK)return PN_IO;
    }else if(e!=ESP_OK || state!=ESP_OTA_IMG_VALID)return PN_UNSUPPORTED;
    e=esp_ota_begin(s->destination,OTA_WITH_SEQUENTIAL_WRITES,&s->handle);s->writing=e==ESP_OK;return e==ESP_OK?PN_OK:PN_IO;
}
static pn_status_t write_bytes(void *ctx,const uint8_t *data,size_t size){ota_t *s=ctx;if(atomic_load(&s->stop))return PN_CANCELLED;return esp_ota_write(s->handle,data,size)==ESP_OK?PN_OK:PN_IO;}
static pn_status_t seal(void *ctx){ota_t *s=ctx;esp_err_t e=esp_ota_end(s->handle);s->writing=false;return e==ESP_OK?PN_OK:PN_CORRUPT;}
static pn_status_t verify_flash(void *ctx,uint32_t size,const uint8_t *sha){ota_t *s=ctx;return flash_digest(s,s->destination,size,sha);}
static pn_status_t activate(void *ctx,unsigned slot){
    ota_t *s=ctx;const esp_partition_t *running=esp_ota_get_running_partition();
    if(atomic_load(&s->stop) || !s->destination || running!=s->running || running==s->destination || s->destination->subtype!=(slot?ESP_PARTITION_SUBTYPE_APP_OTA_1:ESP_PARTITION_SUBTYPE_APP_OTA_0))return PN_STALE_JOB;
    esp_ota_img_states_t state;const esp_partition_t *selected=esp_ota_get_boot_partition();
    if(!selected || selected->address!=running->address || esp_ota_get_state_partition(running,&state)!=ESP_OK || state!=ESP_OTA_IMG_VALID)return PN_STALE_JOB;
    return esp_ota_set_boot_partition(s->destination)==ESP_OK?PN_OK:PN_IO;
}
static void abort_write(void *ctx){ota_t *s=ctx;if(s->writing){(void)esp_ota_abort(s->handle);s->writing=false;}}
static const pn_update_port_t port={signature,bootloader,begin,write_bytes,seal,verify_flash,activate,abort_write};
static bool partition_ok(esp_partition_type_t type,esp_partition_subtype_t subtype,const char *name,uint32_t at,uint32_t size){const esp_partition_t *p=esp_partition_find_first(type,subtype,name);return p && p->address==at && p->size==size;}
static pn_status_t target(ota_t *s){
    s->running=esp_ota_get_running_partition();if(!s->running)return PN_UNSUPPORTED;
    s->target.running_slot=s->running->subtype==ESP_PARTITION_SUBTYPE_APP_OTA_0?0:s->running->subtype==ESP_PARTITION_SUBTYPE_APP_OTA_1?1:2;
    uint32_t size=0;
    s->target.layout_compatible=esp_flash_get_size(NULL,&size)==ESP_OK && size==0x1000000 &&
        partition_ok(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,NULL,0x20000,0x600000) &&
        partition_ok(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_1,NULL,0x620000,0x600000) &&
        partition_ok(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,NULL,0xf000,0x2000) &&
        partition_ok(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"data",0xc20000,0x100000) &&
        partition_ok(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"wallpaper",0xf00000,0x100000);
    esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;esp_err_t e=esp_ota_get_state_partition(s->running,&state);
    s->target.running_confirmed=(e==ESP_OK && (state==ESP_OTA_IMG_VALID || state==ESP_OTA_IMG_UNDEFINED)) || e==ESP_ERR_NOT_FOUND;
    const esp_partition_t *selected=esp_ota_get_boot_partition();
    s->target.running_confirmed=s->target.running_confirmed && selected && selected->address==s->running->address;
    return s->target.layout_compatible && s->target.running_confirmed && s->target.running_slot<2?PN_OK:PN_UNSUPPORTED;
}
static esp_err_t http_event(esp_http_client_event_t *event){
    ota_t *s=event->user_data;
    if(event->event_id==HTTP_EVENT_ON_HEADER && event->header_key && event->header_value && !strcasecmp(event->header_key,"Location")){
        size_t n=strlen(event->header_value);
        if(n<sizeof s->location && !s->location[0])memcpy(s->location,event->header_value,n+1);
        else s->location[0]=0;
    }
    if(event->event_id==HTTP_EVENT_ON_HEADER && event->header_key && event->header_value && !strcasecmp(event->header_key,"Content-Range")){
        size_t n=strlen(event->header_value);
        if(n<sizeof s->range && !s->range[0])memcpy(s->range,event->header_value,n+1);
        else s->range[0]=0;
    }
    return ESP_OK;
}
/// 解析"bytes a-b/total"，其他形式（含*）拒绝。 / Parse "bytes a-b/total", rejecting any other form including "*".
static bool content_range(const char *text,uint64_t *first,uint64_t *last,uint64_t *total){
    if(strncasecmp(text,"bytes ",6))return false;
    const char *p=text+6;uint64_t v[3];
    for(unsigned i=0;i<3;i++){
        if(*p<'0' || *p>'9')return false;
        uint64_t x=0;while(*p>='0' && *p<='9'){if(x>UINT32_MAX)return false;x=x*10u+(uint64_t)(*p++-'0');}
        v[i]=x;if(i<2 && *p++!=(i?'/':'-'))return false;
    }
    if(*p || v[0]>v[1] || v[1]>=v[2])return false;
    *first=v[0];*last=v[1];*total=v[2];return true;
}
/// 按协作取消分片等待，避免重连间隔阻塞stop。 / Wait in slices that honour cooperative stop between reconnections.
static bool pause_ms(ota_t *s,unsigned ms){for(unsigned t=0;t<ms;t+=100){if(atomic_load(&s->stop))return false;vTaskDelay(pdMS_TO_TICKS(100));}return !atomic_load(&s->stop);}
/// from>0时每跳都带Range，只接受200或206；其余重定向限于HTTPS。 / With from>0 every hop carries Range and only 200 or 206 is accepted; redirects stay on HTTPS.
static esp_http_client_handle_t http_open(ota_t *s,const char *url,uint32_t from,pn_status_t *status){
    *status=PN_IO;char current[512];if(strlen(url)>=sizeof current)return NULL;strcpy(current,url);
    for(unsigned redirects=0;redirects<4;redirects++){
        if(atomic_load(&s->stop)){*status=PN_CANCELLED;return NULL;}
        s->location[0]=0;s->range[0]=0;
        esp_http_client_config_t config={.url=current,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=OTA_TIMEOUT_MS,.buffer_size=4096,.buffer_size_tx=1024,.disable_auto_redirect=true,.user_agent="PicoNano/OTA",.event_handler=http_event,.user_data=s};
        esp_http_client_handle_t h=esp_http_client_init(&config);if(!h)return NULL;
        if(from){char value[32];snprintf(value,sizeof value,"bytes=%lu-",(unsigned long)from);if(esp_http_client_set_header(h,"Range",value)!=ESP_OK){esp_http_client_cleanup(h);return NULL;}}
        if(esp_http_client_open(h,0)!=ESP_OK || esp_http_client_fetch_headers(h)<0){esp_http_client_cleanup(h);return NULL;}
        int code=esp_http_client_get_status_code(h);
        if(code==200 || (from && code==206)){*status=PN_OK;return h;}
        if((code==301 || code==302 || code==303 || code==307 || code==308) && !strncmp(s->location,"https://",8)){strcpy(current,s->location);esp_http_client_cleanup(h);continue;}
        esp_http_client_cleanup(h);return NULL;
    }
    return NULL;
}
static pn_status_t fetch(ota_t *s,const char *url,uint8_t *out,size_t capacity,size_t *received){
    pn_status_t status;esp_http_client_handle_t h=http_open(s,url,0,&status);if(!h)return status;
    size_t used=0;int n=0;unsigned stalls=0;
    while(!atomic_load(&s->stop) && used<capacity){
        n=esp_http_client_read(h,(char *)out+used,(int)(capacity-used));
        if(n==-ESP_ERR_HTTP_EAGAIN && ++stalls<=OTA_READ_STALLS)continue;
        if(n<=0)break;
        used+=(size_t)n;stalls=0;
    }
    if(atomic_load(&s->stop))status=PN_CANCELLED;
    else if(n<0 || !esp_http_client_is_complete_data_received(h))status=PN_IO;
    esp_http_client_close(h);esp_http_client_cleanup(h);if(status==PN_OK)*received=used;return status;
}
static pn_status_t discover(ota_t *s,char url[256]){
    char *json=heap_caps_malloc(OTA_HTTP_MAX+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!json)return PN_NO_MEMORY;
    size_t bytes=0;pn_status_t status=fetch(s,PN_OTA_LATEST_URL,(uint8_t *)json,OTA_HTTP_MAX,&bytes);
    if(status!=PN_OK){heap_caps_free(json);return status;}
    json[bytes]=0;const char *end=NULL;cJSON *root=cJSON_ParseWithLengthOpts(json,bytes+1,&end,true);
    status=PN_CORRUPT;
    if(root && end==json+bytes){
        const cJSON *pre=cJSON_GetObjectItemCaseSensitive(root,"prerelease"),*tag=cJSON_GetObjectItemCaseSensitive(root,"tag_name"),*assets=cJSON_GetObjectItemCaseSensitive(root,"assets");
        uint32_t version=0;
        if(cJSON_IsTrue(pre))status=PN_EMPTY;
        else if(cJSON_IsFalse(pre) && cJSON_IsString(tag) && tag->valuestring[0]=='v' && pn_update_version(tag->valuestring+1,&version)==PN_OK && cJSON_IsArray(assets) && cJSON_GetArraySize(assets)<=16){
            int n=snprintf(url,256,PN_OTA_GITEE_ROOT "%s/PicoNano-ota-manifest.bin",tag->valuestring);unsigned found=0;
            const cJSON *a=NULL;cJSON_ArrayForEach(a,assets){const cJSON *name=cJSON_GetObjectItemCaseSensitive(a,"name"),*link=cJSON_GetObjectItemCaseSensitive(a,"browser_download_url");if(cJSON_IsString(name) && !strcmp(name->valuestring,"PicoNano-ota-manifest.bin") && cJSON_IsString(link) && !strcmp(link->valuestring,url))found++;}
            status=n>0 && n<256 && found==1?PN_OK:PN_EMPTY;
        }
    }
    cJSON_Delete(root);heap_caps_free(json);return status;
}
/// 一次连接：校验206范围或200全长，忽略Range的200丢弃已写前缀；*retry只在网络中断时置位。
/// One connection: verify the 206 range or full 200 length, discard the written prefix when a 200 ignores Range; *retry is set only for network drops.
static pn_status_t download_once(ota_t *s,bool *retry){
    *retry=false;uint32_t from=s->update.received,total=s->update.manifest.image_bytes,skip=0;pn_status_t status;
    esp_http_client_handle_t h=http_open(s,s->update.manifest.url,from,&status);
    if(!h){*retry=status==PN_IO;return status;}
    int64_t length=esp_http_client_get_content_length(h);
    if(esp_http_client_get_status_code(h)==206){
        uint64_t first=0,last=0,size=0;
        if(!content_range(s->range,&first,&last,&size) || first!=from || last+1u!=total || size!=total || (length>=0 && length!=(int64_t)(total-from)))status=PN_CORRUPT;
    }else{skip=from;if(length>=0 && length!=(int64_t)total)status=PN_CORRUPT;}
    unsigned stalls=0;bool done=false;
    while(status==PN_OK && !done){
        if(atomic_load(&s->stop)){status=PN_CANCELLED;break;}
        int n=esp_http_client_read(h,(char *)s->scratch,OTA_CHUNK);
        if(n==-ESP_ERR_HTTP_EAGAIN){if(++stalls>OTA_READ_STALLS){status=PN_IO;*retry=true;}continue;}
        if(n<0){status=PN_IO;*retry=true;break;}
        if(!n){if(esp_http_client_is_complete_data_received(h))done=true;else{status=PN_IO;*retry=true;}break;}
        stalls=0;const uint8_t *data=s->scratch;size_t used=(size_t)n;
        if(skip){size_t drop=used<skip?used:skip;skip-=(uint32_t)drop;data+=drop;used-=drop;}
        // 写入或校验失败已由状态机终止事务，不能续传。 / Write or validation failures already ended the transaction and must not resume.
        if(used){status=pn_update_feed(&s->update,data,used);atomic_store(&s->progress,s->update.received);}
        vTaskDelay(1);
    }
    esp_http_client_close(h);esp_http_client_cleanup(h);
    if(atomic_load(&s->stop))return PN_CANCELLED;
    if(status==PN_OK && skip)status=PN_CORRUPT;
    return status;
}
/// 有界续传；全部字节写入后才做SDK封口与读回校验。 / Bounded resumption; SDK sealing and read-back verification only after every byte is written.
static pn_status_t download(ota_t *s){
    pn_status_t status=PN_IO;
    for(unsigned attempt=0;attempt<=OTA_RESUMES;attempt++){
        if(attempt && !pause_ms(s,1000))return PN_CANCELLED;
        bool retry=false;uint32_t before=s->update.received;status=download_once(s,&retry);
        if(status==PN_OK)return pn_update_finish(&s->update);
        if(!retry)return status;
        // 断线前有进展则重置续传次数；无进展的重连仍计数，避免无限重连。 / A drop after progress resets the resume budget; reconnects without progress still count, preventing endless retries.
        if(s->update.received>before)attempt=0;
    }
    return status;
}
static void worker(void *ctx){
    ota_t *s=ctx;pn_status_t status=PN_OK;
    if(!s->installing){
        char url[256];uint8_t envelope[PN_OTA_MANIFEST_BYTES+1];size_t n=0;
        status=discover(s,url);
        if(status==PN_OK)status=fetch(s,url,envelope,sizeof envelope,&n);
        if(status==PN_OK)status=target(s);
        if(status==PN_OK)status=pn_update_prepare(&s->update,envelope,n,PN_PRODUCT_VERSION,&s->target,&port,s);
    }else status=download(s);
    if(status!=PN_OK)(void)pn_update_cancel(&s->update);
    s->result=status;atomic_store_explicit(&s->worker_done,true,memory_order_release);vTaskDelete(NULL);
}
static pn_status_t copy_config(ota_t *s,const pn_device_wifi_config_t *config){
    if(!config || config->mode!=PN_WIFI_STA || !memchr(config->ssid,0,sizeof config->ssid) || !memchr(config->password,0,sizeof config->password))return PN_INVALID;
    pn_network_credentials_t c;memcpy(c.ssid,config->ssid,sizeof c.ssid);memcpy(c.password,config->password,sizeof c.password);pn_status_t status=pn_network_validate(&c,false);pn_network_wipe(&c);if(status==PN_OK)s->config=*config;return status;
}
pn_status_t pn_device_ota_open(pn_device_ota_t *out,const pn_device_wifi_config_t *config){
    if(!out)return PN_INVALID;
    if(out->impl)return PN_BUSY;
    if(!pn_device_ota_configured())return PN_UNSUPPORTED;
    ota_t *s=heap_caps_calloc(1,sizeof *s,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!s)return PN_NO_MEMORY;
    pn_status_t status=copy_config(s,config);if(status!=PN_OK){heap_caps_free(s);return status;}
    s->scratch=heap_caps_malloc(OTA_CHUNK,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);if(!s->scratch){wipe_config(s);heap_caps_free(s);return PN_NO_MEMORY;}
    s->state.phase=PN_DEVICE_OTA_CONNECTING;out->impl=s;return PN_OK;
}
pn_status_t pn_device_ota_install(pn_device_ota_t *out,const pn_device_wifi_config_t *config,bool power_safe){
    if(!out || !out->impl)return PN_INVALID;
    ota_t *s=out->impl;
    if(s->state.phase!=PN_DEVICE_OTA_AVAILABLE || !s->state.radio_released || !power_safe)return PN_BUSY;
    pn_status_t status=copy_config(s,config);if(status!=PN_OK)return status;
    s->installing=true;s->started=false;s->worker_active=false;atomic_store(&s->worker_done,false);s->state.radio_released=false;s->state.phase=PN_DEVICE_OTA_CONNECTING;return PN_OK;
}
pn_status_t pn_device_ota_poll(pn_device_ota_t *out){
    if(!out || !out->impl)return PN_INVALID;
    ota_t *s=out->impl;
    if(s->state.radio_released)return PN_OK;
    if(atomic_load(&s->stop) && !s->worker_active)s->state.phase=PN_DEVICE_OTA_STOPPING;
    if(!s->started && !atomic_load(&s->stop)){s->started=true;pn_status_t e=pn_device_wifi_open(&s->wifi,&s->config);wipe_config(s);if(e!=PN_OK){s->result=e;s->state.phase=PN_DEVICE_OTA_STOPPING;}}
    if(s->state.phase!=PN_DEVICE_OTA_STOPPING){
        pn_status_t e=pn_device_wifi_poll(&s->wifi);pn_device_wifi_state_t wifi={0};(void)pn_device_wifi_state(&s->wifi,&wifi);
        if(e!=PN_OK || wifi.phase==PN_WIFI_FAILED){s->network_failed=true;atomic_store(&s->stop,true);}
        if(!atomic_load(&s->stop) && !s->worker_active && wifi.phase==PN_WIFI_READY){
            s->state.phase=s->installing?PN_DEVICE_OTA_DOWNLOADING:PN_DEVICE_OTA_CHECKING;s->worker_active=true;
            if(xTaskCreate(worker,"pn-ota",16384,s,4,NULL)!=pdPASS){s->worker_active=false;s->result=PN_NO_MEMORY;s->state.phase=PN_DEVICE_OTA_STOPPING;}
        }
        if(atomic_load(&s->stop) || (s->worker_active && atomic_load_explicit(&s->worker_done,memory_order_acquire)))s->state.phase=PN_DEVICE_OTA_STOPPING;
    }
    if(s->state.phase==PN_DEVICE_OTA_STOPPING){
        if(s->worker_active && !atomic_load_explicit(&s->worker_done,memory_order_acquire))return PN_OK;
        if(pn_device_wifi_close(&s->wifi)!=PN_OK)return PN_OK;
        wipe_config(s);s->state.radio_released=true;s->state.error=s->network_failed?PN_IO:atomic_load(&s->stop)?PN_CANCELLED:s->result;
        if(s->state.error==PN_OK){s->state.phase=s->installing?PN_DEVICE_OTA_READY:PN_DEVICE_OTA_AVAILABLE;memcpy(s->state.version,s->update.manifest.version,sizeof s->state.version);s->state.total=s->update.manifest.image_bytes;}
        else s->state.phase=PN_DEVICE_OTA_ERROR;
    }
    return PN_OK;
}
pn_status_t pn_device_ota_state(const pn_device_ota_t *out,pn_device_ota_state_t *state){if(!out || !out->impl || !state)return PN_INVALID;ota_t *s=out->impl;*state=s->state;state->bytes=atomic_load(&s->progress);return PN_OK;}
pn_status_t pn_device_ota_commit(pn_device_ota_t *out){if(!out || !out->impl)return PN_INVALID;ota_t *s=out->impl;if(s->state.phase!=PN_DEVICE_OTA_READY || !s->state.radio_released)return PN_BUSY;pn_status_t e=pn_update_commit(&s->update);s->state.error=e;s->state.phase=e==PN_OK?PN_DEVICE_OTA_DONE:PN_DEVICE_OTA_ERROR;return e;}
pn_status_t pn_device_ota_stop(pn_device_ota_t *out){if(!out || !out->impl)return PN_INVALID;ota_t *s=out->impl;if(s->state.phase==PN_DEVICE_OTA_DONE)return PN_BUSY;atomic_store(&s->stop,true);if(s->state.radio_released){(void)pn_update_cancel(&s->update);s->state.phase=PN_DEVICE_OTA_ERROR;s->state.error=PN_CANCELLED;}return PN_OK;}
pn_status_t pn_device_ota_close(pn_device_ota_t *out){if(!out)return PN_INVALID;if(!out->impl)return PN_OK;ota_t *s=out->impl;if(!s->state.radio_released)return PN_BUSY;if(s->state.phase!=PN_DEVICE_OTA_DONE)(void)pn_update_cancel(&s->update);wipe_config(s);heap_caps_free(s->scratch);heap_caps_free(s);out->impl=NULL;return PN_OK;}
