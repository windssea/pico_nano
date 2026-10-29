/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：按原文件内容计算SHA-256身份，仅用于内容识别。
 * English: original-file SHA-256 identity for content matching only.
 * 冻结：不写文件；调用方排除文件并发变更，不用于安全认证。
 * Frozen: no file writes; caller excludes concurrent mutation; not for security authentication.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_profile.h"
#include "pn_identity.h"
#include "pn_cancel.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

typedef pn_identity_accumulator_t sha_t;
static uint32_t rotate(uint32_t x, unsigned n) { return (x>>n)|(x<<(32-n)); }
static void compress(sha_t *s, const uint8_t *data) {
    static const uint32_t k[64]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64];
    for (unsigned i=0;i<16;i++) w[i]=((uint32_t)data[i*4]<<24)|((uint32_t)data[i*4+1]<<16)|((uint32_t)data[i*4+2]<<8)|data[i*4+3];
    for (unsigned i=16;i<64;i++) {
        uint32_t x=w[i-15], y=w[i-2];
        w[i]=w[i-16]+(rotate(x,7)^rotate(x,18)^(x>>3))+w[i-7]+(rotate(y,17)^rotate(y,19)^(y>>10));
    }
    uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for (unsigned i=0;i<64;i++) {
        uint32_t t1=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^((~e)&g))+k[i]+w[i];
        uint32_t t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static void update(sha_t *s, const uint8_t *data, size_t n) {
    s->bytes+=n;
    while (n) {
        size_t take=64-s->used; if (take>n) take=n;
        memcpy(s->block+s->used,data,take); s->used+=take; data+=take; n-=take;
        if (s->used==64) { compress(s,s->block);s->used=0; }
    }
}
static void finish(sha_t *s, pn_book_id_t *id) {
    uint64_t bits=s->bytes*8; s->block[s->used++]=0x80;
    if (s->used>56) { memset(s->block+s->used,0,64-s->used);compress(s,s->block);s->used=0; }
    memset(s->block+s->used,0,56-s->used);
    for (unsigned i=0;i<8;i++) s->block[63-i]=(uint8_t)(bits>>(i*8));
    compress(s,s->block);
    for (unsigned i=0;i<32;i++) id->sha256[i]=(uint8_t)(s->h[i/4]>>(24-8*(i%4)));
}
typedef struct {FILE *file;pn_media_t *media;const pn_media_lease_t *lease;uint64_t offset;} file_hash_t;
void pn_identity_accumulator_init(pn_identity_accumulator_t *s){*s=(sha_t){.h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};}
void pn_identity_accumulator_update(pn_identity_accumulator_t *s,const void *p,size_t n){update(s,p,n);}
void pn_identity_accumulator_finish(pn_identity_accumulator_t *s,pn_book_id_t *id){finish(s,id);memset(s,0,sizeof *s);}
static pn_status_t file_hash_read(void *ctx,uint64_t offset,uint8_t *bytes,size_t capacity,size_t *got){
    file_hash_t *source=ctx;pn_status_t status=pn_cancel_checkpoint();
    if(status==PN_OK)status=pn_media_validate(source->media,source->lease);
    if(status!=PN_OK)return status;
    if(offset!=source->offset)return PN_INVALID;
    *got=fread(bytes,1,capacity,source->file);source->offset+=*got;
    return ferror(source->file)?PN_IO:PN_OK;
}
pn_status_t pn_identity_file_pooled(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,uint64_t limit,pn_book_id_t *id){
    if(!media || !lease || !path || !*path || !id)return PN_INVALID;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    FILE *file=fopen(path,"rb");if(!file)return PN_IO;
    struct stat info;pn_book_id_t result;
    if(fstat(fileno(file),&info) || !S_ISREG(info.st_mode) || info.st_size<0)status=PN_IO;
    else if((uint64_t)info.st_size>limit)status=PN_LIMIT;
    else{
        file_hash_t source={file,media,lease,0};pn_profile_span_t span;pn_profile_begin(&span,pool,"content.sha");
        // 共用硬件/软件流式摘要，连续读取；仍校验全部原文，不能由大小猜身份。 / Use shared hardware/software streaming SHA and sequential reads; verify all bytes, never infer identity from size.
        status=pool?pn_identity_stream_pooled(pool,&source,(uint64_t)info.st_size,file_hash_read,&result):pn_identity_stream(&source,(uint64_t)info.st_size,file_hash_read,&result);
        if(status==PN_OK && (fgetc(file)!=EOF || ferror(file)))status=PN_IO;
        pn_profile_end(&span,status);
    }
    if(fclose(file) && status==PN_OK)status=PN_IO;
    pn_status_t current=pn_media_validate(media,lease);if(current!=PN_OK)return current;
    if(status==PN_OK)*id=result;
    return status;
}
pn_status_t pn_identity_file(pn_media_t *media,const pn_media_lease_t *lease,const char *path,uint64_t limit,pn_book_id_t *id){
    return pn_identity_file_pooled(NULL,media,lease,path,limit,id);
}

pn_status_t pn_identity_bytes(const uint8_t *bytes,size_t n,pn_book_id_t *out){
    if((!bytes && n) || !out)return PN_INVALID;
#if SIZE_MAX > UINT64_MAX / 8
    if((uint64_t)n>UINT64_MAX/8)return PN_LIMIT;
#endif
    sha_t hash={.h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};update(&hash,bytes,n);pn_book_id_t result;finish(&hash,&result);*out=result;return PN_OK;
}
static pn_identity_stream_port_fn stream_port;
static pn_identity_pooled_port_fn pooled_port;
static const pn_identity_job_port_t *job_port;
static const pn_identity_digest_port_t *digest_port;
typedef struct {pn_identity_digest_port_t port;pn_pool_t *pool;uint64_t bytes;bool active;max_align_t state[];} digest_t;
void pn_identity_digest_port(const pn_identity_digest_port_t *port){digest_port=port;}
void pn_identity_digest_close(pn_identity_digest_t *d){if(d && d->impl){digest_t *s=d->impl;if(s->active)s->port.abort(s->state);pn_free(s);d->impl=NULL;}}
pn_status_t pn_identity_digest_begin(pn_identity_digest_t *d,pn_pool_t *pool){
    if(!d || !pool)return PN_INVALID;
    digest_t *s=d->impl;
    if(s){if(s->pool!=pool)return PN_INVALID;if(s->active)return PN_BUSY;}
    else{
        const pn_identity_digest_port_t *p=digest_port;if(!p)return PN_EMPTY;
        if(!p->bytes || p->bytes>SIZE_MAX-sizeof *s || !p->begin || !p->update || !p->finish || !p->abort)return PN_INVALID;
        s=pn_alloc_optional(pool,PN_MEM_DOCUMENT,sizeof *s+p->bytes);if(!s)return PN_NO_MEMORY;
        memset(s,0,sizeof *s+p->bytes);s->port=*p;s->pool=pool;d->impl=s;
    }
    s->bytes=0;pn_status_t status=s->port.begin(s->state);s->active=status==PN_OK;if(!s->active)s->port.abort(s->state);return status;
}
pn_status_t pn_identity_digest_update(pn_identity_digest_t *d,const void *bytes,size_t n){
    digest_t *s=d?d->impl:NULL;if(!s || !s->active || (!bytes && n))return PN_INVALID;
    pn_status_t status=n>UINT64_MAX/8-s->bytes?PN_LIMIT:s->port.update(s->state,bytes,n);
    if(status==PN_OK)s->bytes+=n;else{s->port.abort(s->state);s->active=false;}return status;
}
pn_status_t pn_identity_digest_finish(pn_identity_digest_t *d,pn_book_id_t *out){
    digest_t *s=d?d->impl:NULL;if(!s || !s->active || !out)return PN_INVALID;
    pn_book_id_t result;pn_status_t status=s->port.finish(s->state,&result);s->port.abort(s->state);s->active=false;if(status==PN_OK)*out=result;return status;
}
void pn_identity_stream_port(pn_identity_stream_port_fn port){stream_port=port;pooled_port=NULL;job_port=NULL;digest_port=NULL;}
void pn_identity_pooled_port(pn_identity_pooled_port_fn port){pooled_port=port;}
static pn_status_t software_stream(void *ctx,uint64_t size,pn_identity_read_fn read,uint8_t *buffer,size_t capacity,pn_book_id_t *out){
    sha_t hash={.h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    while(hash.bytes<size){size_t cap=size-hash.bytes<capacity?(size_t)(size-hash.bytes):capacity,n=0;
        pn_status_t status=pn_cancel_checkpoint();if(status==PN_OK)status=read(ctx,hash.bytes,buffer,cap,&n);
        if(status!=PN_OK)return status;
        if(!n || n>cap)return PN_IO;
        update(&hash,buffer,n);
    }
    pn_book_id_t result;finish(&hash,&result);*out=result;return PN_OK;
}
static pn_status_t identity_stream(void *ctx,uint64_t size,pn_identity_read_fn read,uint8_t *buffer,size_t capacity,pn_book_id_t *out){
    if(!read || !out)return PN_INVALID;
    if(size>UINT64_MAX/8)return PN_LIMIT;
    if(stream_port){pn_book_id_t result;pn_status_t status=stream_port(ctx,size,read,buffer,capacity,&result);if(status==PN_OK)*out=result;return status;}
    return software_stream(ctx,size,read,buffer,capacity,out);
}
// 退路单独成帧；32KiB从池分配，不把4KiB栈缓冲叠加到常规路径。/ Isolate the fallback frame; allocate 32 KiB from the pool instead of stacking another 4 KiB buffer on the normal path.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static pn_status_t default_stream(void *ctx,uint64_t size,pn_identity_read_fn read,pn_book_id_t *out){
    uint8_t buffer[4096];return identity_stream(ctx,size,read,buffer,sizeof buffer,out);
}
pn_status_t pn_identity_stream(void *ctx,uint64_t size,pn_identity_read_fn read,pn_book_id_t *out){
    pn_profile_span_t span;pn_profile_begin(&span,NULL,"content.stream-sha");
    pn_status_t status=default_stream(ctx,size,read,out);pn_profile_end(&span,status);return status;
}
pn_status_t pn_identity_stream_pooled(pn_pool_t *pool,void *ctx,uint64_t size,pn_identity_read_fn read,pn_book_id_t *out){
    if(!pool || !read || !out)return PN_INVALID;
    if(size>UINT64_MAX/8)return PN_LIMIT;
    if(size<=4096)return pn_identity_stream(ctx,size,read,out);
    pn_profile_span_t span;pn_profile_begin(&span,pool,"content.stream-sha");
    if(pooled_port){pn_book_id_t result;pn_status_t status=pooled_port(ctx,size,read,&result);if(status==PN_OK)*out=result;pn_profile_end(&span,status);return status;}
    uint8_t *buffer=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,32u*1024u);
    pn_status_t status=buffer?identity_stream(ctx,size,read,buffer,32u*1024u,out):default_stream(ctx,size,read,out);
    pn_free(buffer);pn_profile_end(&span,status);return status;
}
typedef struct {sha_t hash;uint8_t buffer[32768];} software_job_t;
static pn_status_t software_job_begin(void *ctx){software_job_t *s=ctx;s->hash=(sha_t){.h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};return PN_OK;}
static pn_status_t software_job_step(void *ctx,void *source,uint64_t offset,size_t capacity,pn_identity_read_fn read,size_t *got){
    software_job_t *s=ctx;size_t n=0;pn_profile_span_t timing;pn_profile_begin(&timing,NULL,"content.sha-read");
    pn_status_t status=read(source,offset,s->buffer,capacity,&n);pn_profile_end(&timing,status);
    if(status!=PN_OK)return status;
    if(!n || n>capacity)return PN_IO;
    pn_profile_begin(&timing,NULL,"content.sha-update");update(&s->hash,s->buffer,n);pn_profile_end(&timing,PN_OK);*got=n;return PN_OK;
}
static pn_status_t software_job_finish(void *ctx,pn_book_id_t *out){finish(&((software_job_t *)ctx)->hash,out);return PN_OK;}
static void software_job_abort(void *ctx){(void)ctx;}
static const pn_identity_job_port_t software_job_port={sizeof(software_job_t),software_job_begin,software_job_step,software_job_finish,software_job_abort};
typedef struct {
    pn_identity_job_port_t port;pn_pool_t *pool;void *source;pn_identity_read_fn read;
    uint64_t bytes,size;pn_status_t status;pn_book_id_t result;bool active;
    max_align_t state[];
} identity_job_t;
void pn_identity_job_port(const pn_identity_job_port_t *port){job_port=port;}
void pn_identity_job_close(pn_identity_job_t *job){
    if(!job || !job->impl)return;
    identity_job_t *s=job->impl;if(s->active)s->port.abort(s->state);pn_free(s);job->impl=NULL;
}
pn_status_t pn_identity_job_begin(pn_identity_job_t *job,pn_pool_t *pool,void *ctx,uint64_t size,pn_identity_read_fn read){
    if(!job || !pool || !read)return PN_INVALID;
    if(job->impl)return PN_BUSY;
    if(size>UINT64_MAX/8)return PN_LIMIT;
    const pn_identity_job_port_t *port=job_port?job_port:&software_job_port;
    if(!port->bytes || port->bytes>SIZE_MAX-sizeof(identity_job_t) || !port->begin || !port->step || !port->finish || !port->abort)return PN_INVALID;
    pn_status_t status=pn_cancel_checkpoint();if(status!=PN_OK)return status;
    identity_job_t *s=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *s+port->bytes);if(!s)return PN_NO_MEMORY;
    memset(s,0,sizeof *s+port->bytes);s->port=*port;s->pool=pool;s->source=ctx;s->read=read;s->size=size;s->status=PN_BUSY;s->active=true;
    status=s->port.begin(s->state);if(status!=PN_OK){s->port.abort(s->state);pn_free(s);return status;}job->impl=s;return PN_OK;
}
pn_status_t pn_identity_job_progress(const pn_identity_job_t *job,uint64_t *bytes,uint64_t *size){
    if(!job || !bytes || !size)return PN_INVALID;
    if(!job->impl)return PN_EMPTY;
    const identity_job_t *s=job->impl;*bytes=s->bytes;*size=s->size;return PN_OK;
}
pn_status_t pn_identity_job_step(pn_identity_job_t *job,pn_book_id_t *out){
    if(!job || !job->impl || !out)return PN_INVALID;
    identity_job_t *s=job->impl;
    if(s->status!=PN_BUSY){if(s->status==PN_OK)*out=s->result;return s->status;}
    pn_status_t status=pn_cancel_checkpoint();if(status!=PN_OK)return status;
    pn_profile_span_t span;pn_profile_begin(&span,s->pool,"content.sha-step");
    if(s->bytes<s->size){
        size_t capacity=s->size-s->bytes<32768?(size_t)(s->size-s->bytes):32768,n=0;
        status=s->port.step(s->state,s->source,s->bytes,capacity,s->read,&n);
        if(status==PN_OK){if(!n || n>capacity)status=PN_IO;else{s->bytes+=n;status=PN_BUSY;}}
    }else{
        pn_book_id_t result;status=s->port.finish(s->state,&result);
        if(status==PN_OK){s->result=result;*out=result;}
    }
    if(status!=PN_BUSY && status!=PN_CANCELLED){s->status=status;s->port.abort(s->state);s->active=false;}
    pn_profile_end(&span,status);return status;
}
