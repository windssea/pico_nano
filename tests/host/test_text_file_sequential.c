/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实stdio包装计数，校验连续扫描、随机/缓存混读及失败边界。
 * English: count real stdio calls and verify scans, mixed random/cache reads and failure boundaries. */
#define _POSIX_C_SOURCE 200809L
#include "pn_text_file.h"
#include "pn_identity.h"
#include "pn_cancel.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned seeks,reads;static bool fail_seek,short_reads;
int __real_fseek(FILE *,long,int);
size_t __real_fread(void *,size_t,size_t,FILE *);
int __wrap_fseek(FILE *f,long at,int whence){seeks++;if(fail_seek){fail_seek=false;errno=EIO;return -1;}return __real_fseek(f,at,whence);}
size_t __wrap_fread(void *out,size_t size,size_t count,FILE *f){reads++;if(short_reads && count>997){count=997;short_reads=false;}return __real_fread(out,size,count,f);}
static bool cancelled(void *ctx){(void)ctx;return true;}
static void check(pn_text_source_t *s,const uint8_t *expected,uint64_t at,size_t cap){
    uint8_t out[4096];size_t n=123;assert(cap<=sizeof out);
    assert(s->read_at(s->ctx,at,out,cap,&n)==PN_OK && n==cap && !memcmp(out,expected+at,n));
}
int main(void){
    uint8_t *bytes=malloc(65549);assert(bytes);for(unsigned i=0;i<65549;i++)bytes[i]=(uint8_t)(i*37u+i/4096);
    char path[]="/tmp/pn-sequential-XXXXXX";int fd=mkstemp(path);assert(fd>=0 && write(fd,bytes,65549)==65549 && !close(fd));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);pn_text_file_t file={0};pn_text_source_t source;
    pn_book_id_t expected,actual;assert(pn_identity_bytes(bytes,65549,&expected)==PN_OK);
    assert(pn_text_file_open(&file,&media,&lease,path,&source)==PN_OK);
    assert(pn_identity_stream(source.ctx,source.size,source.read_at,&actual)==PN_OK && !memcmp(&expected,&actual,sizeof actual));
    assert(seeks==0 && reads==17); // 完整连续扫描不定位，每个字节仍参加SHA。/ No seeks during a full sequential scan; every byte still enters SHA.
    check(&source,bytes,source.size,0);assert(seeks==0 && reads==17);
    assert(pn_text_file_close(&file)==PN_OK && pn_text_file_open(&file,&media,&lease,path,&source)==PN_OK);seeks=reads=0;
    pn_pool_t pool;assert(!pn_pool_init(&pool,64u*1024u,NULL,NULL,NULL));
    assert(pn_identity_stream_pooled(&pool,source.ctx,source.size,source.read_at,&actual)==PN_OK && !memcmp(&expected,&actual,sizeof actual));
    assert(seeks==0 && reads==3 && !pool.used && !pool.live);
    assert(pn_text_file_close(&file)==PN_OK && pn_text_file_open(&file,&media,&lease,path,&source)==PN_OK);seeks=reads=0;
    check(&source,bytes,3,10);check(&source,bytes,4096,4096);check(&source,bytes,2,10);check(&source,bytes,8192,4096);
    assert(seeks==0 && reads==3); // 缓存命中不移动实际游标。/ Cache hits do not move the real cursor.
    check(&source,bytes,20000,4096);assert(seeks==1 && reads==4);
    fail_seek=true;uint8_t out[4096];size_t n=123;
    assert(source.read_at(source.ctx,0,out,sizeof out,&n)==PN_IO && n==123);
    check(&source,bytes,24096,4096);assert(seeks==3 && reads==5);
    check(&source,bytes,4090,32);assert(seeks==4 && reads==6);
    assert(pn_text_file_close(&file)==PN_OK && pn_text_file_open(&file,&media,&lease,path,&source)==PN_OK);seeks=reads=0;short_reads=true;
    assert(pn_identity_stream(source.ctx,source.size,source.read_at,&actual)==PN_OK && !memcmp(&expected,&actual,sizeof actual));
    assert(seeks==1); // 短读后连续直读不定位，仅末尾小读按缓存块重新对齐。/ Continue directly after a short read; only the final small read realigns its cache block.
    // 新多块缓存的装载也必须接受正常短读，并只在完整填块后发布。/ New multi-block fills must accept normal short reads and publish only complete blocks.
    assert(pn_text_file_cache_bind(&source,&pool)==PN_OK);short_reads=true;check(&source,bytes,13,17);
    unsigned cached_reads=reads;check(&source,bytes,14,17);assert(reads==cached_reads);
    pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,NULL);unsigned before=reads;n=123;
    assert(source.read_at(source.ctx,0,out,sizeof out,&n)==PN_CANCELLED && n==123 && reads==before);pn_cancel_leave(&scope);
    assert(pn_media_detach(&media)==PN_OK);n=123;
    assert(source.read_at(source.ctx,0,out,sizeof out,&n)==PN_STALE_MEDIA && n==123 && reads==before);
    assert(pn_text_file_close(&file)==PN_OK && !file.read_position_known && !pool.used && !pool.live && !pool.caches && pn_media_release(&media,&lease)==PN_OK);
    assert(pn_media_attach(&media,2)==PN_OK && pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    assert(pn_text_file_open(&file,&media,&lease,path,&source)==PN_OK);actual=expected;
    // 文件被外部截断的故障夹具：完整扫描失败，不能返回部分内容的摘要。/ Fault fixture truncates the file externally: a failed full scan must never return a partial digest.
    assert(!truncate(path,30001));assert(pn_identity_stream(source.ctx,source.size,source.read_at,&actual)==PN_IO && !memcmp(&expected,&actual,sizeof actual));
    assert(pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK && !unlink(path));free(bytes);
    puts("sequential file: exact full SHA, zero sequential seeks, mixed cache/random reads, short reads, failed seek, cancel and stale media passed");return 0;
}
