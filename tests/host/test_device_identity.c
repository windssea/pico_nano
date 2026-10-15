/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实硬件端口控制流，PSA/DMA判定显式替身；不当作物理SHA速度证明。
 * English: real hardware-port control flow with explicit PSA/DMA doubles, never evidence of physical SHA speed. */
#define _POSIX_C_SOURCE 200809L
#include "device_identity.h"
#include "pn_identity.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"
#include "psa/crypto.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
static uintptr_t blocked_begin,blocked_end;static bool fail_hash,external_stack,fail_internal;static unsigned operations,calls;static size_t maximum;static uintptr_t internal_begin,internal_end;
bool esp_ptr_dma_capable(const void *p){uintptr_t at=(uintptr_t)p;if(at>=internal_begin && at<internal_end)return true;return !external_stack && (at<blocked_begin || at>=blocked_end);}
static void *allocate(void *ctx,size_t n){(void)ctx;void *p=malloc(n);blocked_begin=(uintptr_t)p;blocked_end=blocked_begin+n;return p;}
static void release(void *ctx,void *p){(void)ctx;free(p);blocked_begin=blocked_end=0;}
// 内部DMA余量与临时块替身；SPIRAM分配视为不可DMA。/ Doubles for internal DMA headroom and transient blocks; SPIRAM allocations are non-DMA.
static size_t largest;static unsigned borrowed,spiram;
static bool watching;static size_t file_reads,file_bytes,file_max;
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *p,size_t size,size_t count,FILE *f){size_t n=__real_fread(p,size,count,f);if(watching){assert(esp_ptr_dma_capable(p));file_reads++;file_bytes+=n*size;if(size*count>file_max)file_max=size*count;}return n;}
static size_t free_bytes=200u*1024u;
size_t heap_caps_get_largest_free_block(uint32_t caps){assert(caps&MALLOC_CAP_INTERNAL);return largest;}
size_t heap_caps_get_free_size(uint32_t caps){assert(caps&MALLOC_CAP_INTERNAL);return free_bytes;}
void *heap_caps_aligned_alloc(size_t alignment,size_t size,uint32_t caps){assert(alignment==16 && (caps&MALLOC_CAP_DMA) && (size==32u*1024u || size==4096));if(fail_internal)return NULL;void *p=aligned_alloc(alignment,size);assert(p);borrowed++;internal_begin=(uintptr_t)p;internal_end=internal_begin+size;return p;}
void *heap_caps_malloc(size_t size,uint32_t caps){assert(caps&MALLOC_CAP_SPIRAM);void *p=malloc(size);blocked_begin=(uintptr_t)p;blocked_end=blocked_begin+size;spiram++;return p;}
void heap_caps_free(void *ptr){if((uintptr_t)ptr==blocked_begin){blocked_begin=blocked_end=0;spiram--;}else{borrowed--;internal_begin=internal_end=0;}free(ptr);}
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
typedef struct {void *data;unsigned drops;} menu_t;
static size_t menu_bytes(void *ctx){return pn_alloc_size(((menu_t *)ctx)->data);}
static void menu_drop(void *ctx){menu_t *m=ctx;pn_free(m->data);m->data=NULL;m->drops++;}
int main(int argc,char **argv){
    if(argc>1){
        assert(argc==3);largest=80u*1024u;free_bytes=127u*1024u;assert(pn_device_identity_init()==PN_OK);
        if(getenv("PN_IDENTITY_BORROWED"))pn_identity_pooled_port(NULL);
        pn_pool_t pool;assert(!pn_pool_init(&pool,(size_t)strtoull(argv[2],NULL,10),allocate,release,NULL));
        pn_media_t media;pn_media_init(&media);pn_media_lease_t lease;assert(!pn_media_attach(&media,1) && !pn_media_acquire(&media,PN_MEDIA_READ,&lease));
        pn_book_id_t actual;watching=true;pn_status_t status=pn_identity_file_pooled(&pool,&media,&lease,argv[1],UINT64_MAX,&actual);watching=false;
        assert(!status && !pool.used && !pool.live && !operations && !borrowed && !spiram && !pn_media_release(&media,&lease));
        printf("file reads=%zu bytes=%zu maximum=%zu attempts=%zu peak=%zu digest=",file_reads,file_bytes,file_max,pool.attempts,pool.peak);for(unsigned i=0;i<32;i++)printf("%02x",actual.sha256[i]);puts("");return 0;
    }
    largest=96u*1024u;assert(pn_device_identity_init()==PN_OK && !operations && !borrowed && !spiram);largest=0;uint8_t bytes[65549];for(size_t i=0;i<sizeof bytes;i++)bytes[i]=(uint8_t)(i*37u);
    pn_book_id_t expected,actual;assert(pn_identity_bytes(bytes,sizeof bytes,&expected)==PN_OK);
    pn_pool_t pool;assert(!pn_pool_init(&pool,65536,allocate,release,NULL));
    assert(pn_identity_stream_pooled(&pool,NULL,sizeof bytes,source,&actual)==PN_OK && !memcmp(&actual,&expected,sizeof actual));
    assert(calls==17 && maximum==4096 && !pool.used && !pool.live && !operations);
    // 有内部余量时借32KiB块，读完归还；摘要逐字节一致。/ With internal headroom, borrow and return a 32 KiB block with an identical digest.
    largest=96u*1024u;calls=0;maximum=0;assert(pn_identity_stream_pooled(&pool,NULL,sizeof bytes,source,&actual)==PN_OK && !memcmp(&actual,&expected,sizeof actual));
    assert(calls==3 && maximum==32u*1024u && !borrowed && !spiram && !pool.used && !pool.live && !operations);
    // 真机内部状态（最大块80KiB、空闲127KiB）可用32KiB块；最大块或空闲不足退回4KiB。/ Device-like internal state (80 KiB block, 127 KiB free) uses the 32 KiB block; a short block or free total falls back to 4 KiB.
    largest=80u*1024u;free_bytes=127u*1024u;calls=0;maximum=0;assert(pn_identity_stream_pooled(&pool,NULL,sizeof bytes,source,&actual)==PN_OK && calls==3 && maximum==32u*1024u && !borrowed);
    largest=64u*1024u-1u;calls=0;maximum=0;assert(pn_identity_stream_pooled(&pool,NULL,sizeof bytes,source,&actual)==PN_OK && calls==17 && maximum==4096);
    largest=80u*1024u;free_bytes=96u*1024u-1u;calls=0;maximum=0;assert(pn_identity_stream_pooled(&pool,NULL,sizeof bytes,source,&actual)==PN_OK && calls==17 && maximum==4096);free_bytes=200u*1024u;
    largest=0;
    external_stack=true;calls=0;maximum=0;assert(pn_identity_stream_pooled(&pool,NULL,sizeof bytes,source,&actual)==PN_OK && calls==17 && maximum==4096 && !borrowed && !operations && !memcmp(&actual,&expected,sizeof actual));
    fail_internal=true;actual=expected;assert(pn_identity_stream_pooled(&pool,NULL,sizeof bytes,source,&actual)==PN_NO_MEMORY && !memcmp(&actual,&expected,sizeof actual) && !pool.used && !pool.live);fail_internal=false;external_stack=false;
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
    assert(pn_media_release(&media,&lease)==PN_OK && !unlink(path));
    // 自管端口不为无用缓冲驱逐菜单；显式关闭能力后对照原借用路径。
    // Owning scratch must not evict menus for an unused buffer; disabling the capability compares the original borrowed path.
    fail_hash=false;largest=80u*1024u;free_bytes=127u*1024u;assert(!pn_pool_init(&pool,192u*1024u,allocate,release,NULL));
    menu_t menu={0};menu.data=pn_cache_alloc(&pool,PN_CACHE_GLYPH,PN_MEM_UI,150u*1024u);assert(menu.data);memset(menu.data,0x5a,150u*1024u);
    pn_cache_owner_t owner={0};pn_cache_attach(&pool,&owner,PN_CACHE_GLYPH,&menu,menu_bytes,menu_drop);size_t used=pool.used,attempts=pool.attempts;
    assert(!pn_identity_stream_pooled(&pool,NULL,sizeof bytes,source,&actual) && !memcmp(&actual,&expected,sizeof actual) && menu.data && !menu.drops && pool.used==used && pool.attempts==attempts && ((uint8_t *)menu.data)[0]==0x5a);
    pn_identity_pooled_port(NULL);
    assert(!pn_identity_stream_pooled(&pool,NULL,sizeof bytes,source,&actual) && !memcmp(&actual,&expected,sizeof actual) && menu.drops==1 && !menu.data && !pool.used && !pool.live);
    pn_cache_detach(&owner);pn_identity_stream_port(NULL);return 0;
}
