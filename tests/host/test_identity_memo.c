/* SPDX-License-Identifier: Apache-2.0
 * 中文：已验证身份备忘：同实例/同代/无替换租约才命中；写租约、换代、大小变化与主机外部写入都回到完整校验。
 * English: verified-identity memo hits only for the same instance/generation without replacing leases; write leases, new generations, size changes and host external writes fall back to full verification. */
#define _POSIX_C_SOURCE 200809L
#include "pn_text_file.h"
#include "pn_identity_cache.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pn_book_id_t open_identity(pn_media_t *media,const char *path,uint64_t *bytes){
    pn_media_lease_t read;assert(pn_media_acquire(media,PN_MEDIA_READ,&read)==PN_OK);
    pn_text_file_t file={0};pn_text_source_t source;assert(pn_text_file_open(&file,media,&read,path,&source)==PN_OK);
    pn_book_id_t id;assert(pn_text_file_identity(&source,&id)==PN_OK);*bytes=file.io_bytes;
    assert(pn_text_file_close(&file)==PN_OK && pn_media_release(media,&read)==PN_OK);return id;
}
int main(void){
    char path[]="/tmp/pn-memo-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    static char data[200000];for(size_t i=0;i<sizeof data;i++)data[i]=(char)('a'+i%26);
    assert(write(fd,data,sizeof data)==(ssize_t)sizeof data && !close(fd));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    uint64_t first,again;pn_book_id_t a=open_identity(&media,path,&first);pn_book_id_t b=open_identity(&media,path,&again);
    assert(first>=sizeof data && !memcmp(a.sha256,b.sha256,32));
#ifdef PN_TEST_IMMUTABLE_FILE
    // 租约保护的介质：重开只读备忘，不再整文件读取。/ Lease-protected media: reopening uses the memo without whole-file reads.
    assert(again<sizeof data);
    // 缓存写不能替换源文件，不使备忘失效。/ Cache writes cannot replace sources and keep the memo valid.
    pn_media_lease_t cache;assert(pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&cache)==PN_OK && pn_media_release(&media,&cache)==PN_OK);
    (void)open_identity(&media,path,&again);assert(again<sizeof data);
    // 发放过WRITE后必须完整重算，并识别同大小内容替换。/ After a WRITE lease the identity is recomputed in full, catching same-size replacement.
    pn_media_lease_t write;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&write)==PN_OK);
    FILE *f=fopen(path,"r+b");assert(f && fputc('Z',f)!=EOF && !fclose(f));assert(pn_media_release(&media,&write)==PN_OK);
    pn_book_id_t c=open_identity(&media,path,&again);assert(again>=sizeof data && memcmp(a.sha256,c.sha256,32));
    (void)open_identity(&media,path,&again);assert(again<sizeof data);
    // 换代（如USB/换卡）后完整重算。/ A new generation (USB/card change) recomputes in full.
    assert(pn_media_detach(&media)==PN_OK && pn_media_attach(&media,2)==PN_OK);
    (void)open_identity(&media,path,&again);assert(again>=sizeof data);
    puts("identity memo: reuse on same generation, cache writes kept, WRITE/epoch invalidate, same-size replacement caught");
#else
    // 主机外部进程不遵守租约：每次都完整校验。/ Host external processes ignore leases: always verify in full.
    assert(again>=sizeof data);
    puts("identity memo: disabled on host, full verification each open");
#endif
    // 后台完整复核也写入同代备忘；仅有预期身份不算完成校验，WRITE之后必须重新验证。
    // Full background verification also seeds the generation memo; an expected identity alone is unverified and WRITE requires verification again.
    pn_book_id_t expected=open_identity(&media,path,&again);pn_media_lease_t lease;
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK && pn_media_release(&media,&lease)==PN_OK);
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_text_file_t file={0};pn_text_source_t source;assert(pn_text_file_open(&file,&media,&lease,path,&source)==PN_OK);
    pn_book_id_t cached=expected;assert(pn_text_file_identity_cached(&source,&cached)==PN_EMPTY && !file.io_bytes);
    pn_pool_t pool;assert(!pn_pool_init(&pool,1024u*1024u,NULL,NULL,NULL));pn_text_file_verify_t verify={0};
    assert(pn_text_file_verify_begin(&verify,&pool,&source,&expected,false,NULL,NULL)==PN_OK);
    pn_status_t s;do{s=pn_text_file_verify_step(&verify,NULL);}while(s==PN_BUSY);assert(s==PN_OK);pn_text_file_verify_close(&verify);
    assert(pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK);
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK && pn_text_file_open(&file,&media,&lease,path,&source)==PN_OK);
#ifdef PN_TEST_IMMUTABLE_FILE
    assert(pn_text_file_identity_cached(&source,&cached)==PN_OK && !memcmp(&cached,&expected,sizeof cached) && !file.io_bytes);
#else
    assert(pn_text_file_identity_cached(&source,&cached)==PN_EMPTY && !file.io_bytes);
#endif
    assert(pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK && !pool.used && !pool.live);
    // 后台身份扫描完成也应交出同代证明，主机仍不建立可复用备忘。/ Background identity scans must publish same-generation proof; host builds still never memoize it.
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK && pn_media_release(&media,&lease)==PN_OK);
    pn_identity_cache_t background_cache={0};pn_identity_cache_job_t job={0};
    assert(pn_identity_cache_job_begin(&job,&pool,&media,path)==PN_OK);
    do{s=pn_identity_cache_job_step(&job,&background_cache);}while(s==PN_BUSY);assert(s==PN_OK);pn_identity_cache_job_close(&job);
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK && pn_text_file_open(&file,&media,&lease,path,&source)==PN_OK);
#ifdef PN_TEST_IMMUTABLE_FILE
    assert(pn_text_file_identity_cached(&source,&cached)==PN_OK && !memcmp(&cached,&expected,sizeof cached) && !file.io_bytes);
#else
    assert(pn_text_file_identity_cached(&source,&cached)==PN_EMPTY && !file.io_bytes);
#endif
    assert(pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK && !pool.used && !pool.live);
    // 最近书预校验不受持久身份命中阻挡；部分读取和调度去重都不能充当来源证明。
    // Recent-book preverification ignores persisted metadata hits; partial reads and scheduling deduplication are never source proof.
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK && pn_media_release(&media,&lease)==PN_OK);
    pn_identity_warm_t warm={0};
    assert(pn_identity_cache_warm_begin(&warm,&job,&pool,&media,path,10)==PN_OK);
    assert(pn_identity_cache_warm_begin(&warm,&job,&pool,&media,path,10)==PN_BUSY && warm.count==1);
    assert(pn_identity_cache_job_step(&job,&background_cache)==PN_BUSY);pn_identity_cache_job_close(&job);
    assert(pn_identity_cache_warm_begin(&warm,&job,&pool,&media,path,10)==PN_EMPTY && !job.impl);
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK && pn_text_file_open(&file,&media,&lease,path,&source)==PN_OK);
    assert(pn_text_file_identity_cached(&source,&cached)==PN_EMPTY && !file.io_bytes);
    assert(pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK);
    background_cache.dirty=false;assert(pn_identity_cache_warm_begin(&warm,&job,&pool,&media,path,11)==PN_OK);
    do{s=pn_identity_cache_job_step(&job,&background_cache);}while(s==PN_BUSY);assert(s==PN_OK && !background_cache.dirty);pn_identity_cache_job_close(&job);
    assert(pn_identity_cache_warm_begin(&warm,&job,&pool,&media,path,11)==PN_EMPTY);
    s=pn_identity_cache_warm_begin(&warm,&job,&pool,&media,path,12);
#ifdef PN_TEST_IMMUTABLE_FILE
    assert(s==PN_EMPTY && !job.impl);
#else
    assert(s==PN_OK);pn_identity_cache_job_close(&job);
#endif
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK && pn_media_release(&media,&lease)==PN_OK);
    assert(pn_identity_cache_warm_begin(&warm,&job,&pool,&media,path,12)==PN_OK);pn_identity_cache_job_close(&job);
    assert(pn_media_detach(&media)==PN_OK && pn_media_attach(&media,3)==PN_OK);
    assert(pn_identity_cache_warm_begin(&warm,&job,&pool,&media,path,12)==PN_OK);pn_identity_cache_job_close(&job);
    assert(pn_identity_cache_warm_begin(&warm,&job,&pool,&media,"/missing/pn-second-book",12)==PN_IO && warm.count==2);
    assert(pn_identity_cache_warm_begin(&warm,&job,&pool,&media,"/missing/pn-third-book",12)==PN_EMPTY && !job.impl);
    assert(!pool.used && !pool.live && !pn_media_active(&media));
    puts("recent verification: metadata never proves content, cancelled prefix unverified, two-candidate cap, WRITE/epoch reset");
    unlink(path);return 0;
}
