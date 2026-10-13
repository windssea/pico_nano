/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实硬件端口控制流，PSA/DMA判定显式替身；不当作物理SHA速度证明。
 * English: real hardware-port control flow with explicit PSA/DMA doubles, never evidence of physical SHA speed. */
#define _POSIX_C_SOURCE 200809L
#include "device_identity.h"
#include "pn_identity.h"
#include "esp_memory_utils.h"
#include "psa/crypto.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
static uintptr_t blocked_begin,blocked_end;static bool fail_hash;static unsigned operations,calls;static size_t maximum;
bool esp_ptr_dma_capable(const void *p){return (uintptr_t)p<blocked_begin || (uintptr_t)p>=blocked_end;}
static void *allocate(void *ctx,size_t n){(void)ctx;void *p=malloc(n);blocked_begin=(uintptr_t)p;blocked_end=blocked_begin+n;return p;}
static void release(void *ctx,void *p){(void)ctx;free(p);blocked_begin=blocked_end=0;}
psa_status_t psa_crypto_init(void){return PSA_SUCCESS;}
psa_status_t psa_hash_setup(psa_hash_operation_t *op,int algorithm){assert(algorithm==PSA_ALG_SHA_256);*op=(psa_hash_operation_t){0};operations++;return PSA_SUCCESS;}
psa_status_t psa_hash_update(psa_hash_operation_t *op,const uint8_t *data,size_t n){if(fail_hash)return -1;uint8_t *p=realloc(op->data,op->size+n);assert(p);op->data=p;memcpy(p+op->size,data,n);op->size+=n;return PSA_SUCCESS;}
psa_status_t psa_hash_finish(psa_hash_operation_t *op,uint8_t *out,size_t cap,size_t *n){assert(cap>=32);pn_book_id_t id;assert(pn_identity_bytes(op->data,op->size,&id)==PN_OK);memcpy(out,id.sha256,32);*n=32;return PSA_SUCCESS;}
psa_status_t psa_hash_abort(psa_hash_operation_t *op){free(op->data);*op=(psa_hash_operation_t){0};operations--;return PSA_SUCCESS;}
static pn_status_t source(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){
    (void)ctx;assert(esp_ptr_dma_capable(out));calls++;if(cap>maximum)maximum=cap;
    for(size_t i=0;i<cap;i++)out[i]=(uint8_t)((at+i)*37u);
    *n=cap;return PN_OK;
}
int main(void){
    assert(pn_device_identity_init()==PN_OK && !operations);uint8_t bytes[65549];for(size_t i=0;i<sizeof bytes;i++)bytes[i]=(uint8_t)(i*37u);
    pn_book_id_t expected,actual;assert(pn_identity_bytes(bytes,sizeof bytes,&expected)==PN_OK);
    pn_pool_t pool;assert(!pn_pool_init(&pool,65536,allocate,release,NULL));
    assert(pn_identity_stream_pooled(&pool,NULL,sizeof bytes,source,&actual)==PN_OK && !memcmp(&actual,&expected,sizeof actual));
    assert(calls==17 && maximum==4096 && !pool.used && !pool.live && !operations);
    fail_hash=true;actual=expected;assert(pn_identity_stream_pooled(&pool,NULL,sizeof bytes,source,&actual)==PN_IO && !memcmp(&actual,&expected,sizeof actual));
    assert(!pool.used && !pool.live && !operations);
    // 文件入口也必须调用硬件端口并保持错误时的输出与池余额。 / File hashing must also call the hardware port and preserve output and pool balances on failure.
    char path[]="/tmp/pn-hardware-file-XXXXXX";int fd=mkstemp(path);assert(fd>=0);FILE *file=fdopen(fd,"wb");assert(file);
    assert(fwrite(bytes,1,sizeof bytes,file)==sizeof bytes && !fclose(file));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    fail_hash=false;unsigned previous_calls=calls;
    assert(pn_identity_file_pooled(&pool,&media,&lease,path,sizeof bytes,&actual)==PN_OK && !memcmp(&actual,&expected,sizeof actual));
    assert(calls==previous_calls && !operations && !pool.used && !pool.live);
    fail_hash=true;assert(pn_identity_file_pooled(&pool,&media,&lease,path,sizeof bytes,&actual)==PN_IO && !memcmp(&actual,&expected,sizeof actual));
    assert(!operations && !pool.used && !pool.live);
    assert(pn_media_release(&media,&lease)==PN_OK && !unlink(path));pn_identity_stream_port(NULL);return 0;
}
