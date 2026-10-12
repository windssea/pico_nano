/* 协议元数据边界与歧义拒绝。/ Protocol metadata bounds and ambiguity rejection. */
#include "pn_localsend_json.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "cJSON.h"
static const char *valid="{\"info\":{\"alias\":\"手机\",\"version\":\"2.2\",\"fingerprint\":\"abc\",\"port\":53317,\"protocol\":\"https\",\"deviceType\":\"mobile\"},\"files\":{\"file id\":{\"id\":\"file id\",\"fileName\":\"小说.epub\",\"size\":123,\"fileType\":\"application/epub+zip\",\"sha256\":null}}}";
static void reject(pn_pool_t *pool,const char *text){pn_ls_request_t r;memset(&r,0x5a,sizeof r);pn_ls_request_t old=r;assert(pn_ls_parse_prepare(pool,(const uint8_t *)text,strlen(text),&r)!=PN_OK && !memcmp(&old,&r,sizeof r));}
int main(void){
    pn_pool_t pool;assert(!pn_pool_init(&pool,64u*1024u,NULL,NULL,NULL));pn_ls_request_t r;
    assert(pn_ls_parse_prepare(&pool,(const uint8_t *)valid,strlen(valid),&r)==PN_OK && r.count==1 && r.files[0].size==123 && !r.files[0].has_digest && !strcmp(r.files[0].id,"file id"));
    reject(&pool,"{\"info\":{},\"info\":{},\"files\":{}}");
    reject(&pool,"{\"info\":{\"alias\":\"a\",\"version\":\"2.2\",\"fingerprint\":\"b\"},\"files\":{\"x\":{\"id\":\"y\",\"fileName\":\"book.txt\",\"size\":1}}}");
    reject(&pool,"{\"info\":{\"alias\":\"a\",\"version\":\"2.2\",\"fingerprint\":\"b\"},\"files\":{\"x\":{\"id\":\"x\",\"fileName\":\"book.txt\",\"size\":01}}}");
    reject(&pool,"{\"info\":{\"alias\":\"a\\u0000b\",\"version\":\"2.2\",\"fingerprint\":\"b\"},\"files\":{}}");
    reject(&pool,"{\"info\":{\"alias\":\"a\",\"version\":\"2.2\",\"fingerprint\":\"b\"},\"files\":{\"x\":{\"id\":\"x\",\"fileName\":\"a.txt\",\"size\":1},\"x\":{\"id\":\"x\",\"fileName\":\"b.txt\",\"size\":1}}}");
    reject(&pool,"{\"info\":{\"alias\":\"a\",\"version\":\"2.2\",\"fingerprint\":\"b\"},\"files\":{\"x\":{\"id\":\"x\",\"fileName\":\"a.txt\",\"size\":1.5}}}");
    reject(&pool,"{\"info\":{\"alias\":\"a\",\"version\":\"2.2\",\"fingerprint\":\"b\"},\"files\":{\"x\":{\"id\":\"x\",\"fileName\":\"a.txt\",\"fileName\":\"b.txt\",\"size\":1}}}");
    for(size_t n=0;n<strlen(valid);n++)assert(pn_ls_parse_prepare(&pool,(const uint8_t *)valid,n,&r)!=PN_OK);
    pool.fail_at=pool.attempts+1;assert(pn_ls_parse_prepare(&pool,(const uint8_t *)valid,strlen(valid),&r)==PN_NO_MEMORY);pool.fail_at=0;
    pn_ls_grants_t grants={.count=1};memset(grants.session_id,'a',32);strcpy(grants.files[0].id,"file \"id\"\\名");memset(grants.files[0].token,'b',32);
    char reply[PN_LS_REPLY_MAX];size_t written=123;
    assert(pn_ls_encode_grants(&pool,&grants,reply,sizeof reply,&written)==PN_OK && written==strlen(reply));
    puts(reply);
    cJSON *parsed=cJSON_Parse(reply);assert(parsed);cJSON *files=cJSON_GetObjectItemCaseSensitive(parsed,"files");
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(parsed,"sessionId")->valuestring,grants.session_id));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(files,grants.files[0].id)->valuestring,grants.files[0].token));cJSON_Delete(parsed);
    char tiny[4]={'a','b','c',0};written=99;assert(pn_ls_encode_grants(&pool,&grants,tiny,sizeof tiny,&written)==PN_LIMIT && written==99 && !strcmp(tiny,"abc"));
    assert(!pool.used && !pool.live);return 0;
}
