/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：PNWC v1载荷：魔数、版本、SSID长度、口令长度、SSID、口令。
 * English: PNWC v1 payload: magic, version, SSID length, password length, SSID, password.
 * 冻结：未知版本拒绝；临时载荷用后清零；不在任何输出中回显口令。
 * Frozen: unknown versions are rejected; temporary payloads are wiped after use; passwords are never echoed in outputs.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_network_store.h"
#include <stdio.h>
#include <string.h>
#define VERSION 1
static void wipe(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
void pn_network_wipe(pn_network_credentials_t *c){if(c)wipe(c,sizeof *c);}
static bool utf8(const char *s,size_t n){
    for(size_t i=0;i<n;){unsigned char c=(unsigned char)s[i];size_t len=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:0;
        if(!len || i+len>n || (len==1 && c<32) || c==127)return false;
        for(size_t k=1;k<len;k++)if(((unsigned char)s[i+k]>>6)!=2)return false;
        i+=len;}
    return true;
}
pn_status_t pn_network_validate(const pn_network_credentials_t *c,bool allow_empty){
    if(!c)return PN_INVALID;
    size_t ssid=strnlen(c->ssid,sizeof c->ssid),pass=strnlen(c->password,sizeof c->password);
    if(ssid>PN_NETWORK_SSID_MAX || pass>PN_NETWORK_PASSWORD_MAX)return PN_INVALID;
    if(!ssid)return allow_empty && !pass?PN_OK:PN_INVALID;
    if(!utf8(c->ssid,ssid))return PN_INVALID;
    if(pass && pass<8)return PN_INVALID;
    for(size_t i=0;i<pass;i++)if((unsigned char)c->password[i]<32 || (unsigned char)c->password[i]>126)return PN_INVALID;
    return PN_OK;
}
pn_status_t pn_network_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];int n=snprintf(a,sizeof a,"%s/network.a",root);
    if(n<0 || (size_t)n>=sizeof a)return PN_LIMIT;
    n=snprintf(b,sizeof b,"%s/network.b",root);
    if(n<0 || (size_t)n>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(files,media,lease,a,b,io);
}
pn_status_t pn_network_load(const pn_journal_io_t *io,pn_network_credentials_t *out){
    if(!io || !out)return PN_INVALID;
    pn_record_t record;pn_status_t status=pn_journal_load(io,&record);
    if(status!=PN_OK){wipe(&record,sizeof record);return status;}
    pn_network_credentials_t c={0};const uint8_t *p=record.payload;
    if(record.size<8 || memcmp(p,"PNWC",4) || p[4]!=VERSION || p[5] || p[6]>PN_NETWORK_SSID_MAX || p[7]>PN_NETWORK_PASSWORD_MAX || record.size!=8u+p[6]+p[7])status=p[4]!=VERSION && !memcmp(p,"PNWC",4)?PN_UNSUPPORTED:PN_CORRUPT;
    if(status==PN_OK){memcpy(c.ssid,p+8,p[6]);memcpy(c.password,p+8+p[6],p[7]);
        if(strlen(c.ssid)!=p[6] || strlen(c.password)!=p[7] || pn_network_validate(&c,true)!=PN_OK)status=PN_CORRUPT;}
    if(status==PN_OK)*out=c;
    wipe(&c,sizeof c);wipe(&record,sizeof record);return status;
}
pn_status_t pn_network_save(const pn_journal_io_t *io,const pn_network_credentials_t *c){
    if(!io)return PN_INVALID;
    pn_status_t status=pn_network_validate(c,true);if(status!=PN_OK)return status;
    size_t ssid=strlen(c->ssid),pass=strlen(c->password);uint8_t payload[8+PN_NETWORK_SSID_MAX+PN_NETWORK_PASSWORD_MAX];
    memcpy(payload,"PNWC",4);payload[4]=VERSION;payload[5]=0;payload[6]=(uint8_t)ssid;payload[7]=(uint8_t)pass;
    memcpy(payload+8,c->ssid,ssid);memcpy(payload+8+ssid,c->password,pass);
    status=pn_journal_save(io,payload,8+ssid+pass);
    wipe(payload,sizeof payload);return status;
}
