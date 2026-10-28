/* SPDX-License-Identifier: Apache-2.0
 * 中文：从续读包提取原清理策略，启动包不增加总额度。/ English: original resume cleanup shared with bootstrap without increasing quotas. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reading_store.h"
#include "pn_cancel.h"
#include "pn_profile.h"
#include "pn_crc32.h"
#include <errno.h>
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
static uint32_t root_u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
pn_status_t pn_reading_root_load(const char *path,const char magic[4],pn_book_id_t hashes[2],pn_pool_t *pool,const char *phase){
    if(!path || !magic || !hashes)return PN_INVALID;
    struct stat st;pn_status_t s=PN_OK;
    if(store_stat(path,&st))s=(errno==ENOENT || errno==ENOTDIR)?PN_EMPTY:PN_IO;
    else if(!S_ISREG(st.st_mode) || st.st_size!=76)s=PN_CORRUPT;
    uint8_t bytes[76];
    if(s==PN_OK){
        FILE *f=fopen(path,"rb");
        if(!f)s=PN_IO;
        else {if(fread(bytes,1,sizeof bytes,f)!=sizeof bytes || fgetc(f)!=EOF || ferror(f))s=PN_IO;if(fclose(f))s=PN_IO;}
        if(s==PN_OK && (memcmp(bytes,magic,4) || root_u32(bytes+4)!=1 || root_u32(bytes+72)!=pn_crc32_update(0,bytes,72)))s=PN_CORRUPT;
        if(s==PN_OK){memcpy(hashes[0].sha256,bytes+8,32);memcpy(hashes[1].sha256,bytes+40,32);}
    }
    if(phase){
        char event[80];snprintf(event,sizeof event,"%s.%s",phase,s==PN_OK?"valid":s==PN_EMPTY?"missing":s==PN_CORRUPT?"corrupt":"io");
        pn_profile_file(pool,event,s,path);
    }
    return s;
}
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
/// 先回收临时/邻页/帧包，再回收来源与启动结构；相同档位仍按旧时间排序。/ Reclaim staging/neighbor/frame files before source/bootstrap proofs; retain the old timestamp order within a tier.
static unsigned retention(const char *name,bool state){
    size_t n=strlen(name);if(n>=4 && !strcmp(name+n-4,".tmp"))return 0;
    bool backup=n>=4 && !strcmp(name+n-4,".bak");
    if(!strncmp(name+64,state?".pnbt":".pnbs",5) || !strncmp(name+64,state?".pnst":".pnsv",5))return backup?5:6;
    return name[65]=='1'?(backup?1:2):(backup?3:4);
}
#define ROOM_CANDIDATES 4u
typedef struct {
    char name[76];
    uint64_t bytes;
    time_t modified;
    ino_t inode;
    dev_t device;
    unsigned rank;
} candidate_t;
typedef struct {
    pn_media_t *media;
    pn_media_lease_t lease;
    const char *directory,*target;
    uint64_t need,writes;
    bool state;
} volume_t;
enum { ROOM_CREATE,ROOM_MKDIR,ROOM_OPEN,ROOM_SCAN,ROOM_CLOSE,ROOM_CAPACITY,ROOM_VERIFY,ROOM_DELETE };
typedef struct {
    volume_t volumes[2];
    size_t count,current,prefix,start;
    DIR *directory;
    candidate_t candidates[ROOM_CANDIDATES];
    uint64_t total,work_us,max_us;
    unsigned phase,entries,eligible,kept,removed;
    bool create,quota_cleanup;
} admission_t;
static uint64_t quota(const volume_t *v){return v->state?16384u:16u*1024u*1024u;}
static pn_status_t admission_checked(const admission_t *a){
    pn_status_t s=pn_cancel_checkpoint();
    for(size_t i=0;s==PN_OK && i<a->count;i++){
        s=pn_media_validate(a->volumes[i].media,&a->volumes[i].lease);
        if(s==PN_OK && a->volumes[i].media->writes!=a->volumes[i].writes)s=PN_STALE_JOB;
    }
    return s;
}
static pn_status_t admission_init(admission_t *a,const pn_reading_store_request_t *requests,size_t count,bool create){
    if(!requests || !count || count>2)return PN_INVALID;
    *a=(admission_t){.count=count,.create=create,.phase=create?ROOM_CREATE:ROOM_OPEN};
    for(size_t i=0;i<count;i++){
        const pn_reading_store_request_t *r=requests+i;
        if(!r->media || !r->lease || !r->directory || !*r->directory || !r->target || !*r->target || strlen(r->directory)>=384 || strlen(r->target)>=384 ||
           (r->lease->access!=PN_MEDIA_CACHE_WRITE && r->lease->access!=PN_MEDIA_WRITE))return PN_INVALID;
        a->volumes[i]=(volume_t){r->media,*r->lease,r->directory,r->target,r->need,r->media->writes,r->state};
        if(r->need>quota(a->volumes+i))return PN_EMPTY;
    }
    a->start=a->prefix=a->volumes[0].directory[0]=='/'?1u:0u;
    return admission_checked(a);
}
static pn_status_t joined(const volume_t *v,const char *name,char path[384]){
    int n=snprintf(path,384,"%s/%s",v->directory,name);return n<0 || n>=384?PN_LIMIT:PN_OK;
}
static void candidate_add(admission_t *a,const char *name,const struct stat *st,unsigned rank){
    unsigned at=0;
    while(at<a->kept && (a->candidates[at].rank<rank || (a->candidates[at].rank==rank && a->candidates[at].modified<=st->st_mtime)))at++;
    a->eligible++;if(at==ROOM_CANDIDATES)return;
    if(a->kept<ROOM_CANDIDATES)a->kept++;
    for(unsigned i=a->kept-1;i>at;i--)a->candidates[i]=a->candidates[i-1];
    candidate_t *c=a->candidates+at;strcpy(c->name,name);c->bytes=(uint64_t)st->st_size;c->modified=st->st_mtime;c->inode=st->st_ino;c->device=st->st_dev;c->rank=rank;
}
/// 只借用同一独占writer的目录快照；扫描完整后才消费候选，删除后从原总量扣除核对过的大小。/ Borrow a directory snapshot under the same exclusive writer; consume candidates only after a complete scan, subtracting checked sizes after deletion.
static pn_status_t admission_advance(admission_t *a){
    pn_status_t s=admission_checked(a);if(s!=PN_OK)return s;
    volume_t *v=a->volumes+a->current;char path[384];struct stat st;
    switch(a->phase){
    case ROOM_CREATE:case ROOM_MKDIR:{
        const char *dir=v->directory;
        while(dir[a->prefix] && dir[a->prefix]!='/')a->prefix++;
        size_t length=a->prefix-a->start;
        if(!length || (length==1 && dir[a->start]=='.') || (length==2 && dir[a->start]=='.' && dir[a->start+1]=='.'))return PN_INVALID;
        memcpy(path,dir,a->prefix);path[a->prefix]=0;
        if(a->phase==ROOM_MKDIR){if(mkdir(path,0700))return PN_IO;}
        else if(store_stat(path,&st)){if(errno!=ENOENT)return PN_IO;a->phase=ROOM_MKDIR;return PN_BUSY;}
        else if(!S_ISDIR(st.st_mode))return PN_IO;
        if(!dir[a->prefix])a->phase=ROOM_OPEN;
        else{a->start=++a->prefix;a->phase=ROOM_CREATE;}
        return PN_BUSY;
    }
    case ROOM_OPEN:
        a->total=0;a->entries=a->eligible=a->kept=0;
        a->directory=opendir(v->directory);if(!a->directory)return PN_IO;a->phase=ROOM_SCAN;return PN_BUSY;
    case ROOM_SCAN:{
        errno=0;struct dirent *entry=readdir(a->directory);
        if(!entry){if(errno)return PN_IO;a->phase=ROOM_CLOSE;return PN_BUSY;}
        if(++a->entries>8192)return PN_LIMIT;
        if(!owned(entry->d_name,v->state))return PN_BUSY;
        s=joined(v,entry->d_name,path);if(s!=PN_OK)return s;
        if(store_stat(path,&st))return PN_IO;
        if(!S_ISREG(st.st_mode))return PN_BUSY;
        if(st.st_size<0 || (uint64_t)st.st_size>UINT64_MAX-a->total)return PN_LIMIT;
        a->total+=(uint64_t)st.st_size;
        if(!strncmp(path,v->target,strlen(v->target)))return PN_BUSY;
        const char *base=strrchr(v->target,'/');base=base?base+1:v->target;
        unsigned rank=retention(entry->d_name,v->state);
        if(rank>=5 && owned(base,v->state) && !memcmp(entry->d_name,base,64))return PN_BUSY;
        candidate_add(a,entry->d_name,&st,rank);return PN_BUSY;
    }
    case ROOM_CLOSE:
        {int result=closedir(a->directory);a->directory=NULL;if(result)return PN_IO;}
        a->phase=a->total>quota(v)-v->need?ROOM_VERIFY:ROOM_CAPACITY;a->quota_cleanup=a->phase==ROOM_VERIFY;return PN_BUSY;
    case ROOM_CAPACITY:{
        uint64_t free_bytes;s=capacity(v->directory,v->state,&free_bytes);if(s==PN_OK)s=admission_checked(a);if(s!=PN_OK)return s;
        if(a->total<=quota(v)-v->need && free_bytes>=v->need+(v->state?65536u:512u*1024u)){
            if(++a->current==a->count)return PN_OK;
            a->removed=0;a->start=a->prefix=a->volumes[a->current].directory[0]=='/'?1u:0u;
            a->phase=a->create?ROOM_CREATE:ROOM_OPEN;return PN_BUSY;
        }
        a->quota_cleanup=a->total>quota(v)-v->need;a->phase=ROOM_VERIFY;return PN_BUSY;
    }
    case ROOM_VERIFY:
        if(a->removed>=128)return PN_EMPTY;
        if(!a->kept){if(!a->eligible)return PN_EMPTY;a->phase=ROOM_OPEN;return PN_BUSY;}
        s=joined(v,a->candidates[0].name,path);if(s!=PN_OK)return s;
        if(store_stat(path,&st))return PN_IO;
        // 目标变化就放弃本次快照，不能删掉换代或被替换的文件。/ Abandon a changed candidate; never delete a replaced or new-generation file.
        {const candidate_t *c=a->candidates;if(!S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size!=c->bytes || st.st_mtime!=c->modified || st.st_ino!=c->inode || st.st_dev!=c->device)return PN_STALE_JOB;}
        a->phase=ROOM_DELETE;return PN_BUSY;
    case ROOM_DELETE:
        s=joined(v,a->candidates[0].name,path);if(s!=PN_OK)return s;
        if(remove(path))return PN_IO;
        a->total-=a->candidates[0].bytes;a->removed++;a->eligible--;a->kept--;
        memmove(a->candidates,a->candidates+1,a->kept*sizeof *a->candidates);
        pn_profile_file(NULL,a->quota_cleanup?"cache.evict.quota":"cache.evict.space",PN_OK,path);
        a->quota_cleanup=a->total>quota(v)-v->need;a->phase=a->quota_cleanup?ROOM_VERIFY:ROOM_CAPACITY;return PN_BUSY;
    default:return PN_INVALID;
    }
}
void pn_reading_store_admit_abort(pn_reading_admission_t *h){
    if(!h || !h->impl)return;
    admission_t *a=h->impl;if(a->directory)closedir(a->directory);pn_free(a);h->impl=NULL;
}
pn_status_t pn_reading_store_admit(pn_reading_admission_t *h,pn_pool_t *pool,const pn_reading_store_request_t *requests,size_t count){
    if(!h || !pool)return PN_INVALID;
    if(!h->impl){
        admission_t init;pn_status_t s=admission_init(&init,requests,count,true);if(s!=PN_OK)return s;
        admission_t *a=pn_alloc_optional(pool,PN_MEM_INDEX,sizeof *a);if(!a)return PN_NO_MEMORY;
        *a=init;h->impl=a;return PN_BUSY;
    }
    admission_t *a=h->impl;
    bool same=requests && count==a->count;
    for(size_t i=0;same && i<count;i++){
        const volume_t *v=a->volumes+i;const pn_reading_store_request_t *r=requests+i;
        same=r->lease && r->media==v->media && r->lease->owner==v->lease.owner && r->lease->ticket==v->lease.ticket && r->lease->epoch==v->lease.epoch && r->lease->access==v->lease.access && r->directory==v->directory && r->target==v->target && r->state==v->state && r->need==v->need;
    }
    uint64_t began=pn_profile_time();pn_status_t s=same?admission_advance(a):PN_STALE_JOB;uint64_t now=pn_profile_time(),elapsed=now>=began?now-began:0;
    a->work_us+=elapsed;if(elapsed>a->max_us)a->max_us=elapsed;
    if(s!=PN_BUSY){pn_profile_observe(pool,"cache.admit.work",s,a->work_us,NULL);pn_profile_observe(pool,"cache.admit.max-step",s,a->max_us,NULL);pn_reading_store_admit_abort(h);}
    return s;
}
pn_status_t pn_reading_store_room(pn_media_t *media,const pn_media_lease_t *lease,const char *dir,const char *target,bool state,uint64_t need){
    pn_reading_store_request_t request={media,lease,dir,target,need,state};admission_t a;
    pn_status_t s=admission_init(&a,&request,1,false);
    if(s==PN_OK)do{s=admission_advance(&a);}while(s==PN_BUSY);
    if(a.directory)closedir(a.directory);
    return s;
}
