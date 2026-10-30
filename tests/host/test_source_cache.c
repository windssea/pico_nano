/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际文件随机小读的像素源一致性、IO计数、预算回收和过期租约。
 * English: Real-file random-read equivalence, IO counts, bounded reclamation and expired leases. */
#define _POSIX_C_SOURCE 200809L
#include "pn_text_file.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static uint8_t expected(uint64_t offset){return (uint8_t)(offset*17u+(offset>>8));}
static void check(const pn_text_source_t *source,uint64_t offset,size_t capacity){
    uint8_t data[8192];assert(capacity<=sizeof data);size_t n=SIZE_MAX;
    assert(source->read_at(source->ctx,offset,data,capacity,&n)==PN_OK);
    size_t want=source->size-offset<capacity?(size_t)(source->size-offset):capacity;assert(n==want);
    for(size_t i=0;i<n;i++)assert(data[i]==expected(offset+i));
}
static bool cancelled(void *ctx){return *(bool *)ctx;}
int main(void){
    char path[]="/tmp/pn-source-cache-XXXXXX";int fd=mkstemp(path);assert(fd>=0);uint8_t block[4096];
    for(unsigned b=0;b<48;b++){for(unsigned i=0;i<sizeof block;i++)block[i]=expected((uint64_t)b*sizeof block+i);assert(write(fd,block,sizeof block)==sizeof block);}
    assert(close(fd)==0);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_pool_t pool,other;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));assert(!pn_pool_init(&other,6u*1024u*1024u,NULL,NULL,NULL));
    uint64_t reads[2],seeks[2];
    for(unsigned mode=0;mode<2;mode++){
        pn_text_file_t file={0};pn_text_source_t source={0};assert(pn_text_file_open(&file,&media,&lease,path,&source)==PN_OK);
        if(mode){assert(pn_text_file_cache_bind(&source,&pool)==PN_OK);assert(pn_text_file_cache_bind(&source,&pool)==PN_OK);assert(pn_text_file_cache_bind(&source,&other)==PN_BUSY);}
        for(unsigned round=0;round<4;round++)for(unsigned i=0;i<24;i++)check(&source,(uint64_t)((i*7+round*3)%24)*4096+4093,7);
        reads[mode]=file.io_reads;seeks[mode]=file.io_seeks;
        check(&source,source.size,0);check(&source,source.size-2,17);check(&source,123,8192);
        if(mode){
            pn_cache_snapshot(&pool);assert(pool.cache_stats[PN_CACHE_SOURCE].bytes && pool.cache_stats[PN_CACHE_SOURCE].hits);
            assert(pool.cache_stats[PN_CACHE_SOURCE].bytes==pool.used);
            assert(pool.cache_stats[PN_CACHE_SOURCE].bytes<=pool.cache_caps[PN_CACHE_SOURCE]);
            // 回收只丢可重建块，源摘要、句柄与已读内容保持有效。/ Reclamation drops only rebuildable blocks, retaining source identities, handles and content.
            pn_book_id_t identity,again;assert(pn_text_file_identity_pooled(&pool,&source,&identity)==PN_OK);
            size_t bytes=pool.used;assert(pn_cache_reclaim(&pool,SIZE_MAX)==bytes && !file.read_cache && !pool.used);
            uint64_t physical=file.io_reads;assert(pn_text_file_identity_pooled(&pool,&source,&again)==PN_OK && !memcmp(&identity,&again,sizeof identity) && file.io_reads==physical);
            check(&source,123,19);assert(file.io_reads>physical);
            for(unsigned i=0;i<48;i++)check(&source,(uint64_t)i*4096+111,23);
            for(unsigned i=48;i>0;i--)check(&source,(uint64_t)(i-1)*4096+111,23);
            pn_cache_snapshot(&pool);assert(pool.cache_stats[PN_CACHE_SOURCE].bytes<=pool.cache_caps[PN_CACHE_SOURCE]);
            // 两个活源共享同一类别硬顶，不按文件把预算翻倍。/ Two live sources share the class cap rather than doubling its budget per file.
            pn_text_file_t second={0};pn_text_source_t another={0};assert(pn_text_file_open(&second,&media,&lease,path,&another)==PN_OK);assert(pn_text_file_cache_bind(&another,&pool)==PN_OK);
            for(unsigned i=0;i<32;i++)check(&another,(uint64_t)i*4096+17,5);
            // 首源已满，后来字体也必须取得块，反复访问两源仍有热命中。/ A later font must obtain blocks when the first source is full; alternating sources retain hot hits.
            uint64_t hits=second.cache_hits;check(&another,31u*4096+17,5);assert(second.cache_hits==hits+1);
            for(unsigned i=0;i<5;i++){check(&source,17,5);check(&another,31u*4096+17,5);}
            hits=second.cache_hits;check(&another,31u*4096+17,5);assert(second.cache_hits==hits+1);
            pn_cache_snapshot(&pool);assert(pool.cache_stats[PN_CACHE_SOURCE].bytes==pool.used && pool.cache_stats[PN_CACHE_SOURCE].bytes<=pool.cache_caps[PN_CACHE_SOURCE]);assert(pn_text_file_close(&second)==PN_OK);
            // 冷跨界字形只填一次窗；窗口内/窗口尾和近EOF仍逐字节核对。/ A cold crossing glyph fills one window; check its interior/tail and near EOF byte for byte.
            pn_cache_reclaim(&pool,SIZE_MAX);physical=file.io_reads;check(&source,4093,17);assert(file.io_reads==physical+1);
            physical=file.io_reads;check(&source,4094,17);assert(file.io_reads==physical);
            check(&source,8188,17);check(&source,source.size-13,17);
            // 字体供数窗逐字节一致，跨界请求可一次覆盖；更大调用和窗口修改错误仍沿原契约。/ Font supply windows preserve every byte, cover crossing requests once, and keep large-call/error contracts.
            for(unsigned window=512;window<=4096;window*=2){
                pn_cache_reclaim(&pool,SIZE_MAX);assert(pn_text_file_cache_window(&source,window)==PN_OK);
                physical=file.io_reads;uint64_t physical_bytes=file.io_bytes;check(&source,window-3,17);
                assert(file.io_reads==physical+1 && file.io_bytes-physical_bytes==window);
                physical=file.io_reads;check(&source,window-2,17);assert(file.io_reads==physical);
                check(&source,source.size-13,17);check(&source,223,3000);check(&source,123,8192);
                pn_cache_snapshot(&pool);assert(pool.cache_stats[PN_CACHE_SOURCE].bytes<=pool.cache_caps[PN_CACHE_SOURCE]);
            }
            assert(pn_text_file_cache_window(&source,0)==PN_INVALID && file.cache_fill_bytes==4096);
            // 字体1KiB窗口交错访问12个4KiB区；相邻窗合并后第二遍不能再读卡，逆序覆盖前插路径。
            // Interleave font-sized 1 KiB windows across twelve 4 KiB regions; merged windows need no second-pass I/O, and reverse order exercises prepending.
            for(unsigned reverse=0;reverse<2;reverse++){
                pn_cache_reclaim(&pool,SIZE_MAX);assert(pn_text_file_cache_window(&source,1024)==PN_OK);
                for(unsigned q=0;q<4;q++)for(unsigned region=0;region<12;region++)check(&source,(uint64_t)region*8192+(reverse?3-q:q)*1024+17,23);
                physical=file.io_reads;uint64_t physical_bytes=file.io_bytes;
                for(unsigned q=0;q<4;q++)for(unsigned region=0;region<12;region++)check(&source,(uint64_t)region*8192+q*1024+17,23);
                printf("Adjacent font windows reverse=%u: rereads=%llu bytes=%llu\n",reverse,(unsigned long long)(file.io_reads-physical),(unsigned long long)(file.io_bytes-physical_bytes));
                fflush(stdout);
                assert(file.io_reads==physical && file.io_bytes==physical_bytes);
                pn_cache_snapshot(&pool);assert(pool.cache_stats[PN_CACHE_SOURCE].bytes<=12u*4200u+2048u);
            }
            // 合并后的跨段请求仍逐字节相同；不填尚未请求的间隙。/ Merged cross-segment requests remain byte exact, without filling unrequested gaps.
            physical=file.io_reads;check(&source,1001,80);assert(file.io_reads==physical);
            pn_cache_reclaim(&pool,SIZE_MAX);physical=file.io_reads;check(&source,17,23);check(&source,3*1024+17,23);assert(file.io_reads==physical+2);
            // 可重放混合轨迹覆盖重叠/跨界/改窗与EOF，所有返回字节独立核对。/ A replayable mixed trace covers overlap, crossing, window changes and EOF with independent byte checks.
            uint32_t random=0x27182818u;
            for(unsigned i=0;i<2048;i++){
                random=random*1664525u+1013904223u;
                if(i%31u==0)assert(pn_text_file_cache_window(&source,512u<<(i%4u))==PN_OK);
                uint64_t at=(random>>4)%(source.size+1);size_t cap=(random>>21)%4097u;check(&source,at,cap);
            }
            pn_cache_reclaim(&pool,SIZE_MAX);pool.fail_at=pool.attempts+1;check(&source,21,13);pool.fail_at=0;
            check(&source,4096+21,13);pn_cache_reclaim(&pool,SIZE_MAX);
            pool.cache_caps[PN_CACHE_SOURCE]=0;check(&source,9*4096+21,13);assert(!pool.used);pool.cache_caps[PN_CACHE_SOURCE]=132u*1024u;
            pool.limit=128;check(&source,2*4096+21,13);assert(!pool.used);pool.limit=6u*1024u*1024u;
            check(&source,21,13);
            bool stop=true;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,&stop);uint8_t out=77;size_t n=42;
            assert(source.read_at(source.ctx,21,&out,1,&n)==PN_CANCELLED && n==42 && out==77);pn_cancel_leave(&scope);
            assert(pn_media_detach(&media)==PN_OK);n=42;
            assert(source.read_at(source.ctx,21,&out,1,&n)==PN_STALE_MEDIA && n==42 && out==77);
        }
        assert(pn_text_file_close(&file)==PN_OK && !file.read_cache && !file.cache_owner.pool && !pool.used && !pool.live && !pool.caches);
    }
    printf("File random trace: single-block reads=%llu seeks=%llu; multi-block reads=%llu seeks=%llu; identical bytes\n",(unsigned long long)reads[0],(unsigned long long)seeks[0],(unsigned long long)reads[1],(unsigned long long)seeks[1]);
    assert(reads[1]*2<reads[0]);assert(pn_media_release(&media,&lease)==PN_OK);assert(unlink(path)==0);
    puts("Source cache: shared cap, eviction, cancellation, stale hits, fallback and release passed");return 0;
}
