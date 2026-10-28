/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实目录的分步配额、批量候选、取消与失卡。/ English: stepped quotas, bounded candidate batches, cancellation and media loss on real directories. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reading_store.h"
#include "pn_cancel.h"
#include "pn_profile.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned scans,handles,stats,reads,queries,deletes,evictions;
static uint64_t available=64u*1024u*1024u;
static const char *fail_stat;
static bool directory_error,close_error;
static pn_media_t *eject;
DIR *__real_opendir(const char *);
DIR *__wrap_opendir(const char *path){DIR *d=__real_opendir(path);scans++;if(d)handles++;return d;}
struct dirent *__real_readdir(DIR *);
struct dirent *__wrap_readdir(DIR *d){reads++;if(directory_error){errno=EIO;return NULL;}return __real_readdir(d);}
int __real_closedir(DIR *);
int __wrap_closedir(DIR *d){int s=__real_closedir(d);assert(handles);handles--;return close_error?-1:s;}
int __real_lstat(const char *,struct stat *);
int __wrap_lstat(const char *p,struct stat *st){stats++;if(fail_stat && !strcmp(p,fail_stat)){errno=EIO;return -1;}return __real_lstat(p,st);}
int __real_remove(const char *);
int __wrap_remove(const char *p){deletes++;return __real_remove(p);}
static bool cancel(void *ctx){return *(bool *)ctx;}
static uint64_t clock_us(void *ctx){(void)ctx;return 1;}
static void observe(void *ctx,const pn_profile_event_t *e){(void)ctx;if(!strncmp(e->phase,"cache.evict.",12))evictions++;}
static pn_status_t space(void *ctx,const char *path,uint64_t *n){(void)ctx;(void)path;queries++;*n=available;if(eject){assert(pn_media_detach(eject)==PN_OK);eject=NULL;}return PN_OK;}
static void path(char *out,size_t cap,const char *dir,unsigned i,const char *suffix){int n=snprintf(out,cap,"%s/%064x%s",dir,i,suffix);assert(n>0 && (size_t)n<cap);}
static void file(const char *name,size_t size){FILE *f=fopen(name,"wb");assert(f && !ftruncate(fileno(f),(off_t)size) && !fclose(f));}
static pn_status_t step(pn_reading_admission_t *job,pn_pool_t *pool,const pn_reading_store_request_t *r,size_t n){
    unsigned a=stats,b=reads,c=queries,d=deletes;
    pn_status_t s=pn_reading_store_admit(job,pool,r,n);
    // 每轮至多一个元数据/目录条目/容量/删除动作，不以主机毫秒代替面板实测。/ Each tick does at most one metadata, directory-entry, capacity or delete operation; host milliseconds never stand in for panel measurements.
    assert(stats-a<=1 && reads-b<=1 && queries-c<=1 && deletes-d<=1);
    return s;
}
static pn_status_t finish(pn_reading_admission_t *job,pn_pool_t *pool,const pn_reading_store_request_t *r,size_t n){
    pn_status_t s;unsigned ticks=0;do{s=step(job,pool,r,n);assert(++ticks<20000);}while(s==PN_BUSY);assert(!job->impl && !handles && !pool->used && !pool->live);return s;
}
int main(int argc,char **argv){
    assert(argc==2);pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));pn_media_t media,state;
    pn_media_init(&media);pn_media_init(&state);assert(pn_media_attach(&media,1)==PN_OK && pn_media_attach(&state,1)==PN_OK);
    pn_media_lease_t writer={0},trust={0};assert(pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&writer)==PN_OK && pn_media_acquire(&state,PN_MEDIA_WRITE,&trust)==PN_OK);
    pn_profile_port(clock_us,observe,NULL);pn_reading_store_space_ports(space,space,NULL);
    char dir[384],roots[384],target[384],root[384],name[384],notes[384];
    assert(snprintf(dir,sizeof dir,"%s/new/reading",argv[1])>0 && snprintf(roots,sizeof roots,"%s/internal/roots",argv[1])>0);
    path(target,sizeof target,dir,9999,".0.pnr");path(root,sizeof root,roots,9999,".0.pnrt");
    pn_reading_store_request_t r[2]={{&media,&writer,dir,target,512,false},{&state,&trust,roots,root,152,true}};pn_reading_admission_t job={0};
    assert(finish(&job,&pool,r,2)==PN_OK);
    // 千项目录跨轮保留游标，只完成扫描和容量查询后授权。/ A thousand-entry directory retains its cursor across ticks; only a complete scan and capacity query authorize admission.
    for(unsigned i=0;i<1024;i++){path(name,sizeof name,dir,i,".0.pnr");file(name,4);}
    unsigned before=scans,ticks=0;pn_status_t s;
    do{s=step(&job,&pool,r,2);assert(++ticks<3000);if(ticks<1000)assert(s==PN_BUSY);}while(s==PN_BUSY);
    assert(s==PN_OK && ticks>1024 && scans-before==2 && !handles && !pool.used);
    // 重绑请求、低预算、目录错误不能授权，也不能留下打开的游标。/ Rebound requests, low budgets and directory faults neither authorize nor leave open cursors.
    assert(step(&job,&pool,r,2)==PN_BUSY);assert(pn_reading_store_admit(&job,&pool,r,1)==PN_STALE_JOB && !job.impl && !handles);
    size_t limit=pool.limit;pool.limit=0;assert(step(&job,&pool,r,2)==PN_NO_MEMORY && !job.impl);pool.limit=limit;
    path(name,sizeof name,dir,0,".0.pnr");fail_stat=name;assert(finish(&job,&pool,r,2)==PN_IO);fail_stat=NULL;
    directory_error=true;assert(finish(&job,&pool,r,2)==PN_IO);directory_error=false;
    close_error=true;assert(finish(&job,&pool,r,2)==PN_IO);close_error=false;
    eject=&media;assert(finish(&job,&pool,r,2)==PN_STALE_MEDIA);assert(pn_media_release(&media,&writer)==PN_OK && pn_media_attach(&media,2)==PN_OK && pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&writer)==PN_OK);
    // 换成隔离配额夹具：20个1MiB缓存，删6个即可；四候选一批只需两次完整扫描。/ Isolated quota fixture: twenty 1 MiB caches need six removals; batches of four need only two complete scans.
    assert(snprintf(dir,sizeof dir,"%s/quota",argv[1])>0 && !mkdir(dir,0700));path(target,sizeof target,dir,9999,".0.pnr");
    r[0].need=2u*1024u*1024u;
    for(unsigned i=0;i<20;i++){path(name,sizeof name,dir,i,i<8?".1.pnr.tmp":".0.pnr");file(name,1024u*1024u);}
    assert(snprintf(notes,sizeof notes,"%s/notes.txt",dir)>0);file(notes,7);before=scans;unsigned removed=evictions,calls=queries;
    assert(finish(&job,&pool,r,1)==PN_OK && evictions-removed==6 && scans-before==2 && queries-calls==1 && !access(notes,F_OK));
    for(unsigned i=8;i<20;i++){path(name,sizeof name,dir,i,".0.pnr");assert(!access(name,F_OK));}
    // 不同取消点和失卡点都关闭游标；取消后不再删除，也不把部分计费带进下一作业。/ Every cancellation/media-loss cut closes cursors; no later deletion or partial-accounting reuse survives cancellation.
    assert(snprintf(dir,sizeof dir,"%s/cancel",argv[1])>0 && !mkdir(dir,0700));path(target,sizeof target,dir,9999,".0.pnr");r[0].need=512;available=0;
    assert(snprintf(notes,sizeof notes,"%s/notes.txt",dir)>0);file(notes,3);
    for(unsigned loss=0;loss<2;loss++)for(unsigned cut=0;cut<64;cut++){
        for(unsigned i=0;i<3;i++){path(name,sizeof name,dir,i,".0.pnr");file(name,4);}
        s=PN_BUSY;for(unsigned i=0;i<=cut && s==PN_BUSY;i++)s=step(&job,&pool,r,1);
        unsigned kept=evictions;
        if(job.impl){
            if(loss){assert(pn_media_detach(&media)==PN_OK);assert(step(&job,&pool,r,1)==PN_STALE_MEDIA);}
            else{bool stopped=true;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,&stopped);assert(step(&job,&pool,r,1)==PN_CANCELLED);pn_cancel_leave(&scope);}
        }
        assert(!job.impl && !handles && !pool.used && !pool.live && evictions==kept && !access(notes,F_OK));
        if(!media.available)assert(pn_media_release(&media,&writer)==PN_OK && pn_media_attach(&media,100+cut)==PN_OK && pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&writer)==PN_OK);
    }
    assert(pn_media_release(&media,&writer)==PN_OK && pn_media_release(&state,&trust)==PN_OK);pn_reading_store_space_ports(NULL,NULL,NULL);pn_profile_port(NULL,NULL,NULL);
    printf("admission: 1024 entries yielded, quota cleanup 6 removals/2 scans, cancel/media cuts 128, pool peak=%zu; original caps unchanged\n",pool.peak);return 0;
}
