/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：主页个性化偏好A/B存储，魔数PNHM版本1，固定252字节。
 * English: Home personalization A/B journal, magic PNHM version 1, fixed 252 bytes.
 */
#include "pn_home_prefs.h"
#include "pn_crc32.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define PAYLOAD_SIZE (8+PN_HOME_HEADER_MAX+PN_HOME_BANNER_MAX)
_Static_assert(PAYLOAD_SIZE<=PN_JOURNAL_PAYLOAD_MAX,"pn_home_prefs exceeds journal payload");
static bool valid_header(const char *s,size_t len){
    if(len>=PN_HOME_HEADER_MAX)return false;
    for(size_t i=0;i<len;i++){unsigned char c=(unsigned char)s[i];if(c<32 || c==127)return false;}
    return true;
}
static bool valid_banner(const char *s,size_t len){
    if(len>=PN_HOME_BANNER_MAX)return false;
    if(!len)return true;
    if(s[0]=='.')return false;
    for(size_t i=0;i<len;i++){unsigned char c=(unsigned char)s[i];if(c<32 || c==127 || c=='/' || c=='\\')return false;}
    return true;
}
pn_home_prefs_t pn_home_prefs_default(void){return (pn_home_prefs_t){0};}
pn_status_t pn_home_prefs_files(pn_journal_files_t *f,pn_media_t *m,const pn_media_lease_t *l,const char *root,pn_journal_io_t *io){
    if(!f || !m || !l || !root || !*root || !io)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];
    int n=snprintf(a,sizeof a,"%s/home_prefs.a",root);if(n<0 || (size_t)n>=sizeof a)return PN_LIMIT;
    n=snprintf(b,sizeof b,"%s/home_prefs.b",root);if(n<0 || (size_t)n>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(f,m,l,a,b,io);
}
pn_status_t pn_home_prefs_load(const pn_journal_io_t *io,pn_home_prefs_t *out){
    if(!io || !out)return PN_INVALID;
    pn_record_t r;pn_status_t s=pn_journal_load(io,&r);if(s!=PN_OK)return s;
    if(r.size!=PAYLOAD_SIZE || memcmp(r.payload,"PNHM",4))return PN_CORRUPT;
    if(r.payload[4]!=1 || r.payload[7])return PN_UNSUPPORTED;
    size_t hlen=r.payload[5],blen=r.payload[6];
    if(!valid_header((const char *)r.payload+8,hlen) || !valid_banner((const char *)r.payload+8+PN_HOME_HEADER_MAX,blen))return PN_CORRUPT;
    pn_home_prefs_t v={0};
    if(hlen)memcpy(v.header,r.payload+8,hlen);
    if(blen)memcpy(v.banner,r.payload+8+PN_HOME_HEADER_MAX,blen);
    *out=v;return PN_OK;
}
static size_t bounded_len(const char *s,size_t limit){size_t n=0;while(n<limit && s[n])n++;return n;}
pn_status_t pn_home_prefs_save(const pn_journal_io_t *io,const pn_home_prefs_t *prefs){
    if(!io || !prefs)return PN_INVALID;
    size_t hlen=bounded_len(prefs->header,PN_HOME_HEADER_MAX),blen=bounded_len(prefs->banner,PN_HOME_BANNER_MAX);
    if(!valid_header(prefs->header,hlen) || !valid_banner(prefs->banner,blen))return PN_INVALID;
    uint8_t data[PAYLOAD_SIZE]={0};
    memcpy(data,"PNHM",4);data[4]=1;data[5]=(uint8_t)hlen;data[6]=(uint8_t)blen;
    if(hlen)memcpy(data+8,prefs->header,hlen);
    if(blen)memcpy(data+8+PN_HOME_HEADER_MAX,prefs->banner,blen);
    return pn_journal_save(io,data,sizeof data);
}
pn_status_t pn_home_prefs_load_dir(pn_media_t *state_media,const char *state_dir,pn_home_prefs_t *out){
    if(!state_media || !state_dir || !*state_dir || !out)return PN_INVALID;
    pn_media_lease_t lease={0};pn_status_t status=pn_media_acquire(state_media,PN_MEDIA_READ,&lease);
    if(status!=PN_OK)return status;
    pn_journal_files_t files;pn_journal_io_t io;pn_home_prefs_t value=pn_home_prefs_default();
    status=pn_home_prefs_files(&files,state_media,&lease,state_dir,&io);
    if(status==PN_OK)status=pn_home_prefs_load(&io,&value);
    (void)pn_media_release(state_media,&lease);
    if(status==PN_EMPTY){value=pn_home_prefs_default();status=PN_OK;}
    if(status==PN_OK)*out=value;
    return status;
}
pn_status_t pn_home_prefs_save_dir(pn_media_t *state_media,const char *state_dir,const pn_home_prefs_t *prefs){
    if(!state_media || !state_dir || !*state_dir || !prefs)return PN_INVALID;
    pn_media_lease_t lease={0};pn_status_t status=pn_media_acquire(state_media,PN_MEDIA_WRITE,&lease);
    if(status!=PN_OK)return status;
    pn_journal_files_t files;pn_journal_io_t io;
    status=pn_home_prefs_files(&files,state_media,&lease,state_dir,&io);
    if(status==PN_OK)status=pn_home_prefs_save(&io,prefs);
    (void)pn_media_release(state_media,&lease);
    return status;
}
#define BANNER_CACHE_HDR 168u
static void put_u32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static uint32_t get_u32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
pn_status_t pn_home_banner_cache_save(pn_media_t *state_media,const char *state_dir,const char *banner_name,const uint8_t *pixels,size_t bytes){
    if(!state_media || !state_dir || !*state_dir || !banner_name || !*banner_name || !pixels || !bytes)return PN_INVALID;
    size_t nlen=bounded_len(banner_name,PN_HOME_BANNER_MAX);
    if(!nlen || nlen>=PN_HOME_BANNER_MAX)return PN_INVALID;
    char path[PN_JOURNAL_PATH_MAX],tmp[PN_JOURNAL_PATH_MAX];
    if((size_t)snprintf(path,sizeof path,"%s/home_banner.raw",state_dir)>=sizeof path || (size_t)snprintf(tmp,sizeof tmp,"%s/home_banner.tmp",state_dir)>=sizeof tmp)return PN_LIMIT;
    pn_media_lease_t lease={0};pn_status_t st=pn_media_acquire(state_media,PN_MEDIA_WRITE,&lease);
    if(st!=PN_OK)return st;
    uint8_t hdr[BANNER_CACHE_HDR]={0};
    memcpy(hdr,"PNHB",4);put_u32(hdr+4,1u);put_u32(hdr+8,(uint32_t)bytes);
    memcpy(hdr+16,banner_name,nlen);
    uint32_t crc=pn_crc32_update(0u,hdr+16,PN_HOME_BANNER_MAX);
    crc=pn_crc32_update(crc,pixels,bytes);
    put_u32(hdr+12,crc);
    FILE *f=fopen(tmp,"wb");
    if(!f)st=PN_IO;
    else{
        bool ok=fwrite(hdr,1,sizeof hdr,f)==sizeof hdr && fwrite(pixels,1,bytes,f)==bytes && fflush(f)==0;
        if(fclose(f)!=0)ok=false;
        if(!ok){(void)remove(tmp);st=PN_IO;}
        else{
            (void)remove(path);
            if(rename(tmp,path)!=0){(void)remove(tmp);st=PN_IO;}
        }
    }
    (void)pn_media_release(state_media,&lease);
    return st;
}
pn_status_t pn_home_banner_cache_load(pn_media_t *state_media,const char *state_dir,const char *banner_name,uint8_t *pixels,size_t bytes){
    if(!state_media || !state_dir || !*state_dir || !banner_name || !*banner_name || !pixels || !bytes)return PN_INVALID;
    size_t nlen=bounded_len(banner_name,PN_HOME_BANNER_MAX);
    if(!nlen || nlen>=PN_HOME_BANNER_MAX)return PN_INVALID;
    char path[PN_JOURNAL_PATH_MAX];
    if((size_t)snprintf(path,sizeof path,"%s/home_banner.raw",state_dir)>=sizeof path)return PN_LIMIT;
    pn_media_lease_t lease={0};pn_status_t st=pn_media_acquire(state_media,PN_MEDIA_READ,&lease);
    if(st!=PN_OK)return st;
    FILE *f=fopen(path,"rb");
    if(!f){(void)pn_media_release(state_media,&lease);return PN_EMPTY;}
    uint8_t hdr[BANNER_CACHE_HDR];
    bool ok=fread(hdr,1,sizeof hdr,f)==sizeof hdr && !memcmp(hdr,"PNHB",4) && get_u32(hdr+4)==1u && get_u32(hdr+8)==(uint32_t)bytes && !strncmp((const char *)hdr+16,banner_name,PN_HOME_BANNER_MAX) && fread(pixels,1,bytes,f)==bytes;
    (void)fclose(f);
    (void)pn_media_release(state_media,&lease);
    if(!ok)return PN_EMPTY;
    uint32_t crc=pn_crc32_update(0u,hdr+16,PN_HOME_BANNER_MAX);
    crc=pn_crc32_update(crc,pixels,bytes);
    return crc==get_u32(hdr+12)?PN_OK:PN_EMPTY;
}
void pn_home_banner_cache_clear(pn_media_t *state_media,const char *state_dir){
    if(!state_media || !state_dir || !*state_dir)return;
    char path[PN_JOURNAL_PATH_MAX];
    if((size_t)snprintf(path,sizeof path,"%s/home_banner.raw",state_dir)>=sizeof path)return;
    pn_media_lease_t lease={0};
    if(pn_media_acquire(state_media,PN_MEDIA_WRITE,&lease)==PN_OK){(void)remove(path);(void)pn_media_release(state_media,&lease);}
}
