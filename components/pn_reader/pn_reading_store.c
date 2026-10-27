/* SPDX-License-Identifier: Apache-2.0
 * 中文：从续读包提取原清理策略，启动包不增加总额度。/ English: original resume cleanup shared with bootstrap without increasing quotas. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reading_store.h"
#include "pn_cancel.h"
#include "pn_profile.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#ifndef ESP_PLATFORM
#define store_stat lstat
#else
#define store_stat stat
#endif
static pn_reading_space_fn tf_space,state_space;
static void *space_ctx;
void pn_reading_store_space_ports(pn_reading_space_fn tf,pn_reading_space_fn state,void *ctx){tf_space=tf;state_space=state;space_ctx=ctx;}
static pn_status_t capacity(const char *dir,bool state,uint64_t *n){
    pn_reading_space_fn fn=state?state_space:tf_space;
    if(fn){pn_profile_span_t span;pn_profile_begin(&span,NULL,state?"cache.capacity.internal":"cache.capacity.tf");pn_status_t s=fn(space_ctx,dir,n);pn_profile_end(&span,s);return s;}
#ifdef ESP_PLATFORM
    (void)dir;(void)n;return PN_UNSUPPORTED;
#else
    struct statvfs fs;if(statvfs(dir,&fs))return PN_IO;*n=(uint64_t)fs.f_bavail*fs.f_frsize;return PN_OK;
#endif
}
static bool owned(const char *name,bool state){
    size_t n=strlen(name);if(n<69)return false;
    for(unsigned i=0;i<64;i++)if(!strchr("0123456789abcdef",name[i]))return false;
    const char *s=name+64;size_t base=0;
    if(!strncmp(s,state?".pnbt":".pnbs",5))base=69;
    else if(!strncmp(s,state?".pnst":".pnsv",5))base=69;
    else if(s[0]=='.' && (s[1]=='0' || s[1]=='1') && !strncmp(s+2,state?".pnrt":".pnr",state?5:4))base=state?71:70;
    return base && (n==base || (n==base+4 && (!strcmp(name+base,".tmp") || !strcmp(name+base,".bak"))));
}
pn_status_t pn_reading_store_room(pn_media_t *media,const pn_media_lease_t *lease,const char *dir,const char *target,bool state,uint64_t need){
    uint64_t quota=state?16384u:16u*1024u*1024u,reserve=state?65536u:512u*1024u;
    if(!media || !lease || !dir || !target || (lease->access!=PN_MEDIA_CACHE_WRITE && lease->access!=PN_MEDIA_WRITE))return PN_INVALID;
    if(need>quota)return PN_EMPTY;
    for(unsigned pass=0;pass<128;pass++){
        pn_status_t s=pn_cancel_checkpoint();if(s==PN_OK)s=pn_media_validate(media,lease);if(s!=PN_OK)return s;
        uint64_t total=0,free_bytes;char victim[384]="";time_t oldest=0;DIR *d=opendir(dir);if(!d)return PN_IO;
        struct dirent *entry;unsigned count=0;
        while((entry=readdir(d))){
            // 扫描可能覆盖数千个文件，输入/失卡须在条目间让出；取消不据不完整总量授予配额。/ A scan may cover thousands of files: yield to input/media loss between entries and never admit quota from an incomplete total.
            s=pn_cancel_checkpoint();if(s==PN_OK)s=pn_media_validate(media,lease);if(s!=PN_OK){closedir(d);return s;}
            if(++count>8192){closedir(d);return PN_LIMIT;}if(!owned(entry->d_name,state))continue;
            char path[384];int n=snprintf(path,sizeof path,"%s/%s",dir,entry->d_name);struct stat st;
            if(n<0 || (size_t)n>=sizeof path || store_stat(path,&st) || !S_ISREG(st.st_mode) || st.st_size<0)continue;
            total+=(uint64_t)st.st_size;if(!strncmp(path,target,strlen(target)))continue;
            if(!*victim || st.st_mtime<oldest){strcpy(victim,path);oldest=st.st_mtime;}
        }
        closedir(d);s=pn_cancel_checkpoint();if(s==PN_OK)s=capacity(dir,state,&free_bytes);if(s==PN_OK)s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
        if(total<=quota-need && free_bytes>=need+reserve)return PN_OK;
        if(!*victim)return PN_EMPTY;
        s=pn_cancel_checkpoint();if(s==PN_OK)s=pn_media_validate(media,lease);if(s!=PN_OK)return s;
        if(remove(victim))return PN_IO;
    }
    return PN_EMPTY;
}
