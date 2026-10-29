/* SPDX-License-Identifier: Apache-2.0
 * 中文：SDK管理SHA外设锁，文件读取与完整身份语义留在共享层。/ English: SDK owns SHA peripheral locking; shared code owns file reads and complete identities. */
#include "device_identity.h"
#include "pn_identity.h"
#include "pn_profile.h"
#include "pn_cancel.h"
#include "psa/crypto.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <string.h>
#if defined(CONFIG_PN_PROFILE) && CONFIG_PN_PROFILE
#include "esp_log.h"
static const char *TAG="pn.identity";
#endif
static pn_status_t stream_impl(void *ctx,uint64_t size,pn_identity_read_fn read,uint8_t *buffer,size_t capacity,pn_book_id_t *out){
    psa_hash_operation_t op=PSA_HASH_OPERATION_INIT;
    pn_status_t status=psa_hash_setup(&op,PSA_ALG_SHA_256)==PSA_SUCCESS?PN_OK:PN_IO;
    uint64_t offset=0;pn_book_id_t result;
#if defined(CONFIG_PN_PROFILE) && CONFIG_PN_PROFILE
    uint64_t read_us=0,hash_us=0,started=0;
#endif
    while(status==PN_OK && offset<size){
        status=pn_cancel_checkpoint();if(status!=PN_OK)break;
        size_t cap=size-offset<capacity?(size_t)(size-offset):capacity,n=0;
#if defined(CONFIG_PN_PROFILE) && CONFIG_PN_PROFILE
        started=esp_timer_get_time();
#endif
        status=read(ctx,offset,buffer,cap,&n);
#if defined(CONFIG_PN_PROFILE) && CONFIG_PN_PROFILE
        read_us+=(uint64_t)esp_timer_get_time()-started;started=esp_timer_get_time();
#endif
        if(status!=PN_OK)break;
        if(!n || n>cap){status=PN_IO;break;}
        if(psa_hash_update(&op,buffer,n)!=PSA_SUCCESS){status=PN_IO;break;}
#if defined(CONFIG_PN_PROFILE) && CONFIG_PN_PROFILE
        hash_us+=(uint64_t)esp_timer_get_time()-started;
#endif
        offset+=n;
    }
#if defined(CONFIG_PN_PROFILE) && CONFIG_PN_PROFILE
    if(size>=1024u*1024u)ESP_LOGI(TAG,"stream bytes=%llu block=%zu read_us=%llu hash_us=%llu",(unsigned long long)size,capacity,(unsigned long long)read_us,(unsigned long long)hash_us);
#endif
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
    _Alignas(16) uint8_t buffer[4096]={0};
    if(esp_ptr_dma_capable(buffer))return stream_impl(ctx,size,read,buffer,sizeof buffer,out);
    // 传输worker可用PSRAM栈，不能再假设局部数组能DMA；小块申请失败明确返回。/ Transfer workers may have PSRAM stacks: never assume local arrays are DMA-capable; fail explicitly if the small internal block is unavailable.
    uint8_t *internal=heap_caps_aligned_alloc(16,sizeof buffer,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);if(!internal)return PN_NO_MEMORY;
    pn_status_t status=stream_impl(ctx,size,read,internal,sizeof buffer,out);heap_caps_free(internal);return status;
}
// 48KiB块优先：真机内部最大块约80KiB、空闲约127KiB，48+32/48+64 gates刚好满足；失败退回32KiB，再退4KiB。
// Prefer a 48 KiB block: the device has about 80 KiB largest/127 KiB free, so 48+32/48+64 gates fit; fall back to 32 KiB, then 4 KiB.
#define DMA_STREAM_BYTES (32u*1024u)
#define DMA_STREAM_LARGE (48u*1024u)
#define DMA_INTERNAL_RESERVE (64u*1024u)
#define DMA_LARGEST_RESERVE (32u*1024u)
static bool stream_headroom(size_t block,uint32_t caps){return heap_caps_get_largest_free_block(caps)>=block+DMA_LARGEST_RESERVE && heap_caps_get_free_size(caps)>=block+DMA_INTERNAL_RESERVE;}
static pn_status_t stream(void *ctx,uint64_t size,pn_identity_read_fn read,uint8_t *buffer,size_t capacity,pn_book_id_t *out){
    if(buffer && capacity && esp_ptr_dma_capable(buffer) && !((uintptr_t)buffer&3u))return stream_impl(ctx,size,read,buffer,capacity,out);
    const uint32_t caps=MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    if(size>DMA_STREAM_BYTES){
        size_t blocks[]={DMA_STREAM_LARGE,DMA_STREAM_BYTES};
        for(unsigned i=0;i<2;i++)if(stream_headroom(blocks[i],caps)){
            uint8_t *block=heap_caps_aligned_alloc(16,blocks[i],caps);
            if(block){pn_status_t status=stream_impl(ctx,size,read,block,blocks[i],out);heap_caps_free(block);return status;}
        }
    }else if(size>4096u && stream_headroom(DMA_STREAM_BYTES,caps)){
        uint8_t *block=heap_caps_aligned_alloc(16,DMA_STREAM_BYTES,caps);
        if(block){pn_status_t status=stream_impl(ctx,size,read,block,DMA_STREAM_BYTES,out);heap_caps_free(block);return status;}
    }
    return dma_stream(ctx,size,read,out);
}
/// 大流由端口直接取得原DMA工作区，省去不用的池内PSRAM缓冲；内部余量门和4KiB退路不变。/ Large streams obtain the original DMA scratch directly, avoiding unused pool PSRAM; internal headroom gates and the 4 KiB fallback stay unchanged.
static pn_status_t pooled_stream(void *ctx,uint64_t size,pn_identity_read_fn read,pn_book_id_t *out){return stream(ctx,size,read,NULL,0,out);}
static pn_status_t job_begin(void *state){psa_hash_operation_t *op=state;*op=(psa_hash_operation_t)PSA_HASH_OPERATION_INIT;return psa_hash_setup(op,PSA_ALG_SHA_256)==PSA_SUCCESS?PN_OK:PN_IO;}
static pn_status_t job_read(void *state,void *ctx,uint64_t offset,size_t cap,pn_identity_read_fn read,uint8_t *buffer,size_t capacity,size_t *got){
    if(cap>capacity)cap=capacity;
    pn_profile_span_t timing;pn_profile_begin(&timing,NULL,"content.sha-read");
    size_t n=0;pn_status_t s=read(ctx,offset,buffer,cap,&n);pn_profile_end(&timing,s);if(s!=PN_OK)return s;
    if(!n || n>cap)return PN_IO;
    pn_profile_begin(&timing,NULL,"content.sha-update");
    s=psa_hash_update(state,buffer,n)==PSA_SUCCESS?PN_OK:PN_IO;pn_profile_end(&timing,s);if(s!=PN_OK)return s;
    *got=n;return PN_OK;
}
/// 小块复用原4KiB工作区，每轮最多32KiB或4ms软预算；已哈希的前缀在取消时仍交还游标。
/// Reuse the original 4 KiB scratch for at most 32 KiB or a soft 4 ms slice; return a hashed prefix before honoring cancellation.
static pn_status_t job_batch(void *state,void *ctx,uint64_t offset,size_t cap,pn_identity_read_fn read,uint8_t *buffer,size_t *got){
    const int64_t began=esp_timer_get_time();size_t done=0;if(cap>DMA_STREAM_BYTES)cap=DMA_STREAM_BYTES;
    do{
        pn_status_t s=pn_cancel_checkpoint();size_t n=0;
        if(s==PN_OK)s=job_read(state,ctx,offset+done,cap-done,read,buffer,4096,&n);
        if(s!=PN_OK){if(s==PN_CANCELLED && done)break;return s;}
        done+=n;
    }while(done<cap && esp_timer_get_time()-began<4000);
    *got=done;return PN_OK;
}
/// 每步归还DMA工作区，不在界面输入轮之间占用内部大块。/ Return DMA scratch every step instead of retaining a large internal block between input ticks.
__attribute__((noinline)) static pn_status_t job_small(void *state,void *ctx,uint64_t offset,size_t cap,pn_identity_read_fn read,size_t *got){
    _Alignas(16) uint8_t buffer[4096]={0};
    if(esp_ptr_dma_capable(buffer))return job_batch(state,ctx,offset,cap,read,buffer,got);
    uint8_t *block=heap_caps_aligned_alloc(16,sizeof buffer,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);if(!block)return PN_NO_MEMORY;
    pn_status_t s=job_batch(state,ctx,offset,cap,read,block,got);heap_caps_free(block);return s;
}
static pn_status_t job_step(void *state,void *ctx,uint64_t offset,size_t cap,pn_identity_read_fn read,size_t *got){
    const uint32_t caps=MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    // 大块不足时逐级降到16/8KiB，保留相同内部余量，不长期占用DMA内存。/ Fall back through 16/8 KiB while preserving the same internal reserves and no retained DMA scratch.
    const size_t sizes[]={DMA_STREAM_BYTES,16u*1024u,8u*1024u};
    for(unsigned i=0;cap>4096 && i<sizeof sizes/sizeof sizes[0];i++)if(stream_headroom(sizes[i],caps)){
        uint8_t *block=heap_caps_aligned_alloc(16,sizes[i],caps);
        if(block){pn_status_t s=job_read(state,ctx,offset,cap,read,block,sizes[i],got);heap_caps_free(block);return s;}
    }
    return job_small(state,ctx,offset,cap,read,got);
}
static pn_status_t job_finish(void *state,pn_book_id_t *out){size_t n=0;return psa_hash_finish(state,out->sha256,sizeof out->sha256,&n)==PSA_SUCCESS && n==sizeof out->sha256?PN_OK:PN_IO;}
static void job_abort(void *state){(void)psa_hash_abort(state);}
/// 源块摘要复用SDK增量SHA，不重复读取或改变分块身份。/ Source-block digests reuse incremental SDK SHA without rereads or changed block identities.
static pn_status_t digest_update(void *state,const void *bytes,size_t size){return psa_hash_update(state,bytes,size)==PSA_SUCCESS?PN_OK:PN_IO;}
static const pn_identity_digest_port_t captured={sizeof(psa_hash_operation_t),job_begin,digest_update,job_finish,job_abort};
static const pn_identity_job_port_t incremental={sizeof(psa_hash_operation_t),job_begin,job_step,job_finish,job_abort};
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
    if(status==PN_OK)status=pooled_stream(NULL,8201,block_probe,&actual);
    if(status==PN_OK && memcmp(&expected,&actual,sizeof actual))status=PN_CORRUPT;
    // 分块摘要跨调用保存SDK上下文；自检仍与同源完整软件摘要一致。
    // Chunked hashing retains its SDK context across calls, self-tested against the complete shared software digest.
    if(status==PN_OK){
        psa_hash_operation_t operation=PSA_HASH_OPERATION_INIT;status=job_begin(&operation);
        for(uint64_t offset=0;status==PN_OK && offset<8201;){size_t n=0;status=job_step(&operation,NULL,offset,8201-offset<4096?(size_t)(8201-offset):4096,block_probe,&n);if(status==PN_OK)offset+=n;}
        if(status==PN_OK)status=job_finish(&operation,&actual);
        job_abort(&operation);
    }
    if(status==PN_OK && memcmp(&expected,&actual,sizeof actual))status=PN_CORRUPT;
    if(status==PN_OK){pn_identity_stream_port(stream);pn_identity_pooled_port(pooled_stream);pn_identity_job_port(&incremental);pn_identity_digest_port(&captured);}
    return status;
}
