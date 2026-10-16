/* SPDX-License-Identifier: Apache-2.0
 * 中文：同源升级状态机；签名与副作用只由显式端口提供。
 * English: shared update state machine with cryptography and effects supplied by explicit ports. */
#include "pn_update.h"
#include <string.h>

static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool fixed_string(char *out,const uint8_t *p,size_t n){
    const uint8_t *end=memchr(p,0,n);if(!end)return false;
    for(const uint8_t *q=end;q<p+n;q++)if(*q)return false;
    memcpy(out,p,n);return true;
}
pn_status_t pn_update_version(const char *text,uint32_t *out){
    if(!text || !out)return PN_INVALID;
    uint32_t result=0;
    for(unsigned part=0;part<3;part++){
        if(*text<'0' || *text>'9')return PN_INVALID;
        unsigned value=0,digits=0;
        do{value=value*10u+(unsigned)(*text++-'0');if(value>999 || ++digits>3)return PN_INVALID;}while(*text>='0' && *text<='9');
        if(digits>1 && text[-(int)digits]=='0')return PN_INVALID;
        result=result*1000u+value;
        if(part<2){if(*text++!='.')return PN_INVALID;}else if(*text)return PN_INVALID;
    }
    *out=result;return PN_OK;
}
bool pn_update_gitee_url(const char *url){
    const size_t root=sizeof(PN_OTA_GITEE_ROOT)-1;
    if(!url || strncmp(url,PN_OTA_GITEE_ROOT,root) || !url[root])return false;
    for(const char *p=url+root;*p;p++)if(!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='-' || *p=='_' || *p=='.' || *p=='/'))return false;
    return !strstr(url+root,"..") && !strstr(url+root,"//");
}
static pn_status_t fail(pn_update_t *u,pn_status_t error){
    if(u->opened){u->port.abort(u->ctx);u->opened=false;}
    u->phase=PN_OTA_FAILED;u->error=error;return error;
}
pn_status_t pn_update_prepare(pn_update_t *out,const uint8_t *data,size_t size,const char *current,const pn_update_target_t *target,const pn_update_port_t *port,void *ctx){
    if(!out || !data || size!=PN_OTA_MANIFEST_BYTES || !target || !port || !current || !port->verify_signature || !port->check_bootloader || !port->begin || !port->write || !port->seal || !port->verify_flash || !port->activate || !port->abort)return PN_INVALID;
    if(out->phase==PN_OTA_ACCEPTED || out->phase==PN_OTA_WRITING || out->phase==PN_OTA_VERIFIED)return PN_BUSY;
    if(memcmp(data,"PNOTA1\r\n",8))return PN_CORRUPT;
    pn_status_t status=port->verify_signature(ctx,data,PN_OTA_PAYLOAD_BYTES,data+PN_OTA_PAYLOAD_BYTES);if(status!=PN_OK)return status;
    if(le32(data+8)!=1 || le32(data+16)!=1 || le32(data+20)!=9 || le32(data+24)!=1 || le32(data+28)!=1 || le32(data+32)!=1)return PN_UNSUPPORTED;
    for(size_t i=496;i<512;i++)if(data[i])return PN_UNSUPPORTED;
    pn_update_t u={.port=*port,.ctx=ctx,.phase=PN_OTA_ACCEPTED,.target_slot=1u-target->running_slot};
    u.manifest.image_bytes=le32(data+12);u.manifest.bootloader_bytes=le32(data+36);
    if(u.manifest.image_bytes<PN_OTA_IMAGE_PREFIX || u.manifest.image_bytes>PN_OTA_MAX_IMAGE || !u.manifest.bootloader_bytes || u.manifest.bootloader_bytes>0x8000)return PN_LIMIT;
    if(!fixed_string(u.manifest.version,data+40,32) || !fixed_string(u.manifest.url,data+176,256) || !fixed_string(u.manifest.notes,data+432,64))return PN_CORRUPT;
    memcpy(u.manifest.source_commit,data+72,40);u.manifest.source_commit[40]=0;
    for(unsigned i=0;i<40;i++)if(!((data[72+i]>='0' && data[72+i]<='9') || (data[72+i]>='a' && data[72+i]<='f')))return PN_CORRUPT;
    uint32_t next=0,old=0;
    if(pn_update_version(u.manifest.version,&next)!=PN_OK || pn_update_version(current,&old)!=PN_OK || !pn_update_gitee_url(u.manifest.url))return PN_CORRUPT;
    if(next<=old)return PN_EMPTY;
    if(target->running_slot>1 || !target->layout_compatible || !target->running_confirmed)return PN_UNSUPPORTED;
    memcpy(u.manifest.image_sha256,data+112,32);memcpy(u.manifest.bootloader_sha256,data+144,32);
    status=port->check_bootloader(ctx,u.manifest.bootloader_bytes,u.manifest.bootloader_sha256);if(status!=PN_OK)return status;
    *out=u;return PN_OK;
}
static bool image_header(const pn_update_t *u){
    const uint8_t *p=u->prefix;
    return p[0]==0xe9 && p[1]>0 && p[1]<=16 && p[12]==9 && !p[13] && le32(p+32)==0xabcd5432 &&
        memchr(p+48,0,32) && !strcmp((const char *)p+48,u->manifest.version) &&
        memchr(p+80,0,32) && !strcmp((const char *)p+80,"pico_nano");
}
pn_status_t pn_update_feed(pn_update_t *u,const uint8_t *data,size_t n){
    if(!u || (!data && n))return PN_INVALID;
    if(u->phase!=PN_OTA_ACCEPTED && u->phase!=PN_OTA_WRITING)return PN_INVALID;
    if(!n)return PN_OK;
    if(n>u->manifest.image_bytes-u->received)return fail(u,PN_CORRUPT);
    u->received+=(uint32_t)n;
    if(u->prefix_used<sizeof u->prefix){
        size_t take=sizeof u->prefix-u->prefix_used;if(take>n)take=n;
        memcpy(u->prefix+u->prefix_used,data,take);u->prefix_used+=take;data+=take;n-=take;
        if(u->prefix_used<sizeof u->prefix)return PN_OK;
        if(!image_header(u))return fail(u,PN_CORRUPT);
        pn_status_t status=u->port.begin(u->ctx,u->target_slot,u->manifest.image_bytes);
        if(status!=PN_OK)return fail(u,status);
        u->opened=true;u->phase=PN_OTA_WRITING;
        status=u->port.write(u->ctx,u->prefix,sizeof u->prefix);if(status!=PN_OK)return fail(u,status);
    }
    if(n){pn_status_t status=u->port.write(u->ctx,data,n);if(status!=PN_OK)return fail(u,status);}
    return PN_OK;
}
pn_status_t pn_update_finish(pn_update_t *u){
    if(!u || (u->phase!=PN_OTA_WRITING && u->phase!=PN_OTA_ACCEPTED))return PN_INVALID;
    if(u->received!=u->manifest.image_bytes || !u->opened)return fail(u,PN_CORRUPT);
    pn_status_t status=u->port.seal(u->ctx);
    if(status!=PN_OK)return fail(u,status);
    u->opened=false;
    status=u->port.verify_flash(u->ctx,u->manifest.image_bytes,u->manifest.image_sha256);
    if(status!=PN_OK)return fail(u,status);
    u->phase=PN_OTA_VERIFIED;return PN_OK;
}
pn_status_t pn_update_commit(pn_update_t *u){
    if(!u || u->phase!=PN_OTA_VERIFIED)return PN_INVALID;
    pn_status_t status=u->port.activate(u->ctx,u->target_slot);if(status!=PN_OK)return fail(u,status);
    u->phase=PN_OTA_COMMITTED;return PN_OK;
}
pn_status_t pn_update_cancel(pn_update_t *u){
    if(!u)return PN_INVALID;
    if(u->phase==PN_OTA_COMMITTED)return PN_BUSY;
    if(u->opened){u->port.abort(u->ctx);u->opened=false;}
    u->phase=PN_OTA_CANCELLED;u->error=PN_CANCELLED;return PN_OK;
}
