/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：受限元数据解析，ID仅用于协议匹配，不构造保存路径。
 * English: bounded metadata parsing; IDs match protocol fields and never construct destination paths.
 * 冻结：不修改cJSON全局分配器、不读取预览图片、不把MIME当格式证明。
 * Frozen: no cJSON hook changes, preview-image reads or MIME-based format proof.
 */
#include "pn_localsend_json.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>
static bool preflight(const uint8_t *bytes,size_t n){
    unsigned depth=0,tokens=0;bool quoted=false,escaped=false;
    for(size_t i=0;i<n;i++){
        unsigned c=bytes[i];if(!c)return false;
        if(quoted){
            if(escaped){if(c=='u' && i+4<n && !memcmp(bytes+i+1,"0000",4))return false;escaped=false;}
            else if(c=='\\')escaped=true;
            else if(c=='"')quoted=false;
            else if(c<32)return false;
        }else if(c=='"'){quoted=true;tokens++;}
        else if(c=='{' || c=='['){if(++depth>8)return false;tokens++;}
        else if(c=='}' || c==']'){if(!depth)return false;depth--;}
        else if(c==',')tokens++;
        else if(c=='-' || (c>='0' && c<='9')){
            size_t digit=i+(c=='-');
            if(digit<n && bytes[digit]=='0' && digit+1<n && bytes[digit+1]>='0' && bytes[digit+1]<='9')return false;
            while(i+1<n && ((bytes[i+1]>='0' && bytes[i+1]<='9') || bytes[i+1]=='.' || bytes[i+1]=='e' || bytes[i+1]=='E' || bytes[i+1]=='+' || bytes[i+1]=='-'))i++;
        }
        if(tokens>256)return false;
    }
    return !quoted && !escaped && !depth;
}
static bool unique_tree(cJSON *v){
    if(cJSON_IsObject(v))for(cJSON *a=v->child;a;a=a->next)for(cJSON *b=a->next;b;b=b->next)if(!strcmp(a->string,b->string))return false;
    for(cJSON *child=v->child;child;child=child->next)if(!unique_tree(child))return false;
    return true;
}
static bool string(cJSON *object,const char *key,char *out,size_t cap){
    cJSON *value=cJSON_GetObjectItemCaseSensitive(object,key);
    if(!cJSON_IsString(value) || !value->valuestring)return false;
    size_t n=strlen(value->valuestring);if(!n || n>=cap)return false;
    memcpy(out,value->valuestring,n+1);return true;
}
static int hex(unsigned c){return c>='0' && c<='9'?(int)(c-'0'):c>='a' && c<='f'?(int)(c-'a'+10):c>='A' && c<='F'?(int)(c-'A'+10):-1;}
static bool digest(cJSON *object,pn_ls_file_t *file){
    cJSON *v=cJSON_GetObjectItemCaseSensitive(object,"sha256");if(!v || cJSON_IsNull(v))return true;
    if(!cJSON_IsString(v) || strlen(v->valuestring)!=64)return false;
    for(size_t i=0;i<32;i++){int a=hex((unsigned char)v->valuestring[i*2]),b=hex((unsigned char)v->valuestring[i*2+1]);if(a<0 || b<0)return false;file->digest.sha256[i]=(uint8_t)(a*16+b);}
    file->has_digest=true;return true;
}
pn_status_t pn_ls_parse_prepare(pn_pool_t *pool,const uint8_t *bytes,size_t length,pn_ls_request_t *out){
    if(!pool || !bytes || !out || !length)return PN_INVALID;
    if(length>PN_LS_JSON_MAX)return PN_LIMIT;
    if(!preflight(bytes,length))return PN_INVALID;
    char *copy=pn_alloc_tagged(pool,PN_MEM_TRANSFER,length+1);if(!copy)return PN_NO_MEMORY;
    memcpy(copy,bytes,length);copy[length]=0;
    cJSON *json=cJSON_ParseWithLengthOpts(copy,length+1,NULL,true);pn_free(copy);
    if(!json)return PN_INVALID;
    if(!cJSON_IsObject(json) || !unique_tree(json)){cJSON_Delete(json);return PN_INVALID;}
    pn_ls_request_t *r=pn_alloc_tagged(pool,PN_MEM_TRANSFER,sizeof *r);if(!r){cJSON_Delete(json);return PN_NO_MEMORY;}memset(r,0,sizeof *r);
    cJSON *info=cJSON_GetObjectItemCaseSensitive(json,"info"),*files=cJSON_GetObjectItemCaseSensitive(json,"files");
    bool valid=cJSON_IsObject(info) && cJSON_IsObject(files) && string(info,"alias",r->alias,sizeof r->alias) && string(info,"version",r->version,sizeof r->version) && string(info,"fingerprint",r->fingerprint,sizeof r->fingerprint);
    for(cJSON *f=valid?files->child:NULL;f && valid;f=f->next){
        if(r->count>=PN_LS_FILES_MAX){valid=false;break;}
        pn_ls_file_t *file=&r->files[r->count++];
        valid=cJSON_IsObject(f) && string(f,"id",file->id,sizeof file->id) && !strcmp(f->string,file->id) && string(f,"fileName",file->name,sizeof file->name) && digest(f,file);
        cJSON *size=cJSON_GetObjectItemCaseSensitive(f,"size");
        valid=valid && cJSON_IsNumber(size) && isfinite(size->valuedouble) && size->valuedouble>0 && size->valuedouble<=512u*1024u*1024u && floor(size->valuedouble)==size->valuedouble;
        if(valid)file->size=(uint64_t)size->valuedouble;
    }
    pn_status_t status=valid && r->count?PN_OK:PN_INVALID;
    if(status==PN_OK)*out=*r;
    pn_free(r);cJSON_Delete(json);return status;
}
static bool token_ok(const char *token){
    if(!memchr(token,0,33) || strlen(token)!=32)return false;
    for(size_t i=0;i<32;i++)if(hex((unsigned char)token[i])<0)return false;
    return true;
}
pn_status_t pn_ls_encode_grants(pn_pool_t *pool,const pn_ls_grants_t *grants,char *out,size_t capacity,size_t *written){
    if(!pool || !grants || !out || !written || !grants->count || grants->count>PN_LS_FILES_MAX || !token_ok(grants->session_id))return PN_INVALID;
    for(size_t i=0;i<grants->count;i++){
        if(!memchr(grants->files[i].id,0,PN_LS_ID_MAX) || !*grants->files[i].id || !token_ok(grants->files[i].token))return PN_INVALID;
        for(size_t j=0;j<i;j++)if(!strcmp(grants->files[i].id,grants->files[j].id))return PN_INVALID;
    }
    char *scratch=pn_alloc_tagged(pool,PN_MEM_TRANSFER,PN_LS_REPLY_MAX);if(!scratch)return PN_NO_MEMORY;
    cJSON *json=cJSON_CreateObject();bool valid=json && cJSON_AddStringToObject(json,"sessionId",grants->session_id);
    cJSON *files=valid?cJSON_AddObjectToObject(json,"files"):NULL;valid=valid && files;
    for(size_t i=0;i<grants->count && valid;i++)valid=cJSON_AddStringToObject(files,grants->files[i].id,grants->files[i].token)!=NULL;
    pn_status_t status=valid?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK && !cJSON_PrintPreallocated(json,scratch,PN_LS_REPLY_MAX,false))status=PN_LIMIT;
    if(status==PN_OK){size_t length=strlen(scratch);if(length>=capacity)status=PN_LIMIT;else{memcpy(out,scratch,length+1);*written=length;}}
    cJSON_Delete(json);pn_free(scratch);return status;
}
