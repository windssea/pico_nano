/* SPDX-License-Identifier: Apache-2.0
 * 中文：SDK管理SHA外设锁，文件读取与完整身份语义留在共享层。/ English: SDK owns SHA peripheral locking; shared code owns file reads and complete identities. */
#include "device_identity.h"
#include "pn_identity.h"
#include "pn_cancel.h"
#include "psa/crypto.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"
#include <string.h>
static pn_status_t stream_impl(void *ctx,uint64_t size,pn_identity_read_fn read,uint8_t *buffer,size_t capacity,pn_book_id_t *out){
    psa_hash_operation_t op=PSA_HASH_OPERATION_INIT;
    pn_status_t status=psa_hash_setup(&op,PSA_ALG_SHA_256)==PSA_SUCCESS?PN_OK:PN_IO;
    uint64_t offset=0;pn_book_id_t result;
    while(status==PN_OK && offset<size){
        status=pn_cancel_checkpoint();if(status!=PN_OK)break;
        size_t cap=size-offset<capacity?(size_t)(size-offset):capacity,n=0;
        status=read(ctx,offset,buffer,cap,&n);if(status!=PN_OK)break;
        if(!n || n>cap){status=PN_IO;break;}
        if(psa_hash_update(&op,buffer,n)!=PSA_SUCCESS){status=PN_IO;break;}offset+=n;
    }
    size_t written=0;
    if(status==PN_OK && (psa_hash_finish(&op,result.sha256,sizeof result.sha256,&written)!=PSA_SUCCESS || written!=sizeof result.sha256))status=PN_IO;
    (void)psa_hash_abort(&op);
    if(status==PN_OK)*out=result;
    return status;
}
// 非DMA的PSRAM缓冲会触发SDMMC临时转运；用有界内部栈恢复直接多扇区读取。/ Non-DMA PSRAM buffers trigger SDMMC staging; bounded internal stack storage restores direct multi-sector reads.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static pn_status_t dma_stream(void *ctx,uint64_t size,pn_identity_read_fn read,pn_book_id_t *out){
    _Alignas(16) uint8_t buffer[4096];return stream_impl(ctx,size,read,buffer,sizeof buffer,out);
}
// PSRAM目标在S3上由SDMMC按16扇区中转；整书摘要临时借32KiB内部DMA块，保留64KiB内部余量，读完即还。
// S3 SDMMC bounces PSRAM targets through 16-sector chunks; whole-book hashing borrows a 32 KiB internal DMA block, keeping 64 KiB internal headroom, and returns it immediately.
#define DMA_STREAM_BYTES (32u*1024u)
#define DMA_INTERNAL_RESERVE (64u*1024u)
static pn_status_t stream(void *ctx,uint64_t size,pn_identity_read_fn read,uint8_t *buffer,size_t capacity,pn_book_id_t *out){
    if(esp_ptr_dma_capable(buffer) && !((uintptr_t)buffer&3u))return stream_impl(ctx,size,read,buffer,capacity,out);
    const uint32_t caps=MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    if(size>4096u && heap_caps_get_largest_free_block(caps)>=DMA_STREAM_BYTES+DMA_INTERNAL_RESERVE){
        uint8_t *block=heap_caps_aligned_alloc(16,DMA_STREAM_BYTES,caps);
        if(block){pn_status_t status=stream_impl(ctx,size,read,block,DMA_STREAM_BYTES,out);heap_caps_free(block);return status;}
    }
    return dma_stream(ctx,size,read,out);
}
static pn_status_t probe(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){
    (void)ctx;if(at>=3 || !cap)return PN_IO;*n=3-(size_t)at;if(*n>cap)*n=cap;memcpy(out,"abc"+at,*n);return PN_OK;
}
static pn_status_t block_probe(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){
    (void)ctx;if(at>=8201 || !cap)return PN_IO;*n=8201-(size_t)at;if(*n>cap)*n=cap;
    for(size_t i=0;i<*n;i++)out[i]=(uint8_t)((at+i)*37u);
    return PN_OK;
}
pn_status_t pn_device_identity_init(void){
    if(psa_crypto_init()!=PSA_SUCCESS)return PN_IO;
    uint8_t buffer[512];pn_book_id_t expected,actual;pn_status_t status=pn_identity_bytes((const uint8_t *)"abc",3,&expected);
    if(status==PN_OK)status=stream(NULL,3,probe,buffer,sizeof buffer,&actual);
    if(status==PN_OK && memcmp(&expected,&actual,sizeof actual))status=PN_CORRUPT;
    // 覆盖DMA分块和非整块末尾，与共享软件实现逐字节比较。/ Cover DMA chunks and a partial tail against the shared software result.
    if(status==PN_OK)status=pn_identity_stream(NULL,8201,block_probe,&expected);
    if(status==PN_OK)status=stream(NULL,8201,block_probe,buffer,sizeof buffer,&actual);
    if(status==PN_OK && memcmp(&expected,&actual,sizeof actual))status=PN_CORRUPT;
    // 非DMA调用缓冲覆盖内部临时块分支（余量不足时为4KiB栈分支）。/ A non-DMA caller buffer covers the transient internal block branch (or the 4 KiB stack branch without headroom).
    uint8_t *external=status==PN_OK?heap_caps_malloc(64,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;
    if(external){status=stream(NULL,8201,block_probe,external,64,&actual);heap_caps_free(external);
        if(status==PN_OK && memcmp(&expected,&actual,sizeof actual))status=PN_CORRUPT;}
    if(status==PN_OK)pn_identity_stream_port(stream);
    return status;
}
