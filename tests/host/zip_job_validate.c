/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际ZIP的目录/全资源字节对照、配额、短读、取消、失效源及逐分配失败。
 * English: compare real ZIP directories/all resource bytes, budgets, short reads, cancellation, stale sources and allocation failures. */
#define _POSIX_C_SOURCE 200809L
#include "pn_zip.h"
#include "pn_cancel.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct {
    int fd;uint64_t size;size_t calls,bytes,chunk;bool cancel,stale,after_cancel,after_stale,in_step;pn_status_t error;
} source_t;
static size_t max_calls,max_bytes,cancelled_steps,aborts,faults;
static bool cancelled(void *ctx){return ((source_t *)ctx)->cancel;}
static pn_status_t valid(void *ctx){return ((source_t *)ctx)->stale?PN_STALE_MEDIA:PN_OK;}
static pn_status_t input(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){
    source_t *s=ctx;*n=0;s->calls++;if(s->in_step)assert(cap<=4096);if(s->error!=PN_OK)return s->error;if(off>s->size)return PN_IO;
    if(cap>s->size-off)cap=(size_t)(s->size-off);
    if(s->chunk && cap>s->chunk)cap=s->chunk;
    ssize_t got=pread(s->fd,out,cap,(off_t)off);if(got<0)return PN_IO;*n=(size_t)got;s->bytes+=*n;
    if(s->after_cancel)s->cancel=true;
    if(s->after_stale)s->stale=true;
    return PN_OK;
}
static pn_status_t step(pn_zip_job_t *job,pn_zip_t *zip,source_t *source){
    size_t calls=source->calls,bytes=source->bytes;source->in_step=true;pn_status_t s=pn_zip_job_step(job,zip);source->in_step=false;
    calls=source->calls-calls;bytes=source->bytes-bytes;assert(calls<=8 && bytes<=4096);
    if(calls>max_calls)max_calls=calls;
    if(bytes>max_bytes)max_bytes=bytes;
    if(s!=PN_OK)assert(!zip->impl);
    if(s==PN_CANCELLED)cancelled_steps++;
    return s;
}
static pn_status_t open_job(pn_zip_t *zip,pn_pool_t *pool,pn_text_source_t *input_source,source_t *source,bool pre_cancel){
    pn_zip_job_t job={0};size_t calls=source->calls;pn_status_t s=pn_zip_job_begin(&job,pool,input_source);assert(source->calls==calls);
    if(s!=PN_OK)return s;
    pn_zip_t busy={.impl=(void *)(uintptr_t)1};void *handle=job.impl;size_t used=pool->used;
    assert(pn_zip_job_step(&job,&busy)==PN_BUSY && busy.impl==(void *)(uintptr_t)1 && job.impl==handle && pool->used==used && source->calls==calls);
    size_t steps=0;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,source);
    do{
        if(pre_cancel){source->cancel=true;calls=source->calls;used=pool->used;handle=job.impl;assert(step(&job,zip,source)==PN_CANCELLED && source->calls==calls && pool->used==used && job.impl==handle);}
        source->cancel=false;s=step(&job,zip,source);assert(++steps<1000000);
    }while(s==PN_BUSY || s==PN_CANCELLED);
    pn_cancel_leave(&scope);source->cancel=false;pn_zip_job_close(&job);assert(!job.impl);return s;
}
static void compare(pn_zip_t *old,pn_zip_t *zip,bool payload){
    assert(pn_zip_count(old)==pn_zip_count(zip));
    for(uint32_t i=0;i<pn_zip_count(old);i++){
        pn_zip_info_t a,b;assert(pn_zip_info(old,i,&a)==PN_OK && pn_zip_info(zip,i,&b)==PN_OK && !memcmp(&a,&b,sizeof a));
        uint32_t index=UINT32_MAX;assert(pn_zip_find(zip,a.path,&index)==PN_OK && index==i);
        if(!payload || a.directory)continue;
        pn_zip_stream_t x={0},y={0};assert(pn_zip_stream_open(old,i,&x)==PN_OK && pn_zip_stream_open(zip,i,&y)==PN_OK);pn_status_t sx,sy;
        do{uint8_t left[4096],right[4096];size_t nl=0,nr=0;sx=pn_zip_stream_read(&x,left,sizeof left,&nl);sy=pn_zip_stream_read(&y,right,sizeof right,&nr);assert(sx==sy && nl==nr && !memcmp(left,right,nl) && (sx==PN_OK || sx==PN_EMPTY));}while(sx==PN_OK);
        assert(pn_zip_stream_verified(&x) && pn_zip_stream_verified(&y));pn_zip_stream_close(&x);pn_zip_stream_close(&y);
    }
}
static void abort_checks(pn_pool_t *pool,pn_text_source_t *src,source_t *source,size_t base_used,size_t base_live){
    pn_zip_job_t measured={0};pn_zip_t zip={0};assert(pn_zip_job_begin(&measured,pool,src)==PN_OK);size_t steps=0;pn_status_t s;
    do{s=step(&measured,&zip,source);steps++;assert(steps<1000000);}while(s==PN_BUSY);assert(s==PN_OK && pn_zip_close(&zip)==PN_OK && pool->used==base_used && pool->live==base_live);
    for(size_t stop=0;stop<=steps;stop++){
        pn_zip_job_t job={0};assert(pn_zip_job_begin(&job,pool,src)==PN_OK);
        for(size_t i=0;i<stop;i++){s=step(&job,&zip,source);assert(s==PN_BUSY || (s==PN_OK && i+1==stop));}
        pn_zip_job_close(&job);pn_zip_job_close(&job);assert(pn_zip_close(&zip)==PN_OK && pool->used==base_used && pool->live==base_live);aborts++;
    }
}
int main(int argc,char **argv){
    assert(argc==2 || (argc==3 && !strcmp(argv[2],"--faults")));source_t source={.fd=open(argv[1],O_RDONLY)};assert(source.fd>=0);struct stat info;assert(!fstat(source.fd,&info) && info.st_size>=0);source.size=(uint64_t)info.st_size;
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));pn_text_source_t src={&source,source.size,input,valid};pn_zip_t old={0};assert(pn_zip_open(&old,&pool,&src)==PN_OK);size_t base_used=pool.used,base_live=pool.live;
    size_t allocations=0;
    for(unsigned mode=0;mode<5;mode++){
        pn_zip_t zip={0};source.chunk=mode==1?1:mode==2?17:0;source.after_cancel=mode==3;if(mode==4)pool.cache_caps[PN_CACHE_CHAPTER]=0;
        size_t attempts=pool.attempts;assert(open_job(&zip,&pool,&src,&source,mode==0)==PN_OK);if(mode==0)allocations=pool.attempts-attempts;
        source.chunk=0;source.after_cancel=false;compare(&old,&zip,true);assert(pn_zip_close(&zip)==PN_OK && pool.used==base_used && pool.live==base_live);pool.cache_caps[PN_CACHE_CHAPTER]=384u*1024u;
    }
    pn_zip_job_t job={0};pn_zip_t zip={0};assert(pn_zip_job_begin(&job,&pool,&src)==PN_OK);source.after_stale=true;assert(step(&job,&zip,&source)==PN_STALE_MEDIA && !job.impl && !zip.impl);source.after_stale=source.stale=false;assert(pool.used==base_used && pool.live==base_live);
    assert(pn_zip_job_begin(&job,&pool,&src)==PN_OK);source.error=PN_IO;assert(step(&job,&zip,&source)==PN_IO && !job.impl && !zip.impl);source.error=PN_OK;assert(pool.used==base_used && pool.live==base_live);
    source.cancel=true;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,&source);assert(pn_zip_job_begin(&job,&pool,&src)==PN_CANCELLED && !job.impl);pn_cancel_leave(&scope);source.cancel=false;
    if(argc==3){
        for(size_t fault=1;fault<=allocations;fault++){
            pool.fail_at=pool.attempts+fault;pn_status_t s=open_job(&zip,&pool,&src,&source,false);assert(s==PN_OK || s==PN_NO_MEMORY);pool.fail_at=0;
            if(s==PN_OK)compare(&old,&zip,false);
            assert(pn_zip_close(&zip)==PN_OK && pool.used==base_used && pool.live==base_live);faults++;
        }
        abort_checks(&pool,&src,&source,base_used,base_live);
    }
    size_t count=pn_zip_count(&old);assert(pn_zip_close(&old)==PN_OK && !pool.used && !pool.live && !close(source.fd));
    printf("zip job entries=%zu calls_max=%zu bytes_max=%zu cancelled=%zu aborts=%zu faults=%zu peak=%zu used=%zu live=%zu\n",count,max_calls,max_bytes,cancelled_steps,aborts,faults,pool.peak,pool.used,pool.live);return 0;
}
