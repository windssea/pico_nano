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

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} sha_t;
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
void pn_identity_stream_port(pn_identity_stream_port_fn port){stream_port=port;}
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
    uint8_t *buffer=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,32u*1024u);
    pn_status_t status=buffer?identity_stream(ctx,size,read,buffer,32u*1024u,out):default_stream(ctx,size,read,out);
    pn_free(buffer);pn_profile_end(&span,status);return status;
}
