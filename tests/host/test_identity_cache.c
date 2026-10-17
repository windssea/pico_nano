/* 书籍身份预计算缓存：分步摘要等于完整检查身份，命中条件、淘汰、保存读回、损坏与改写。
 * Precomputed book-identity cache: stepped digests equal full inspection identities, hit conditions, eviction, save/load, damage and rewrites. */
#include "pn_identity_cache.h"
#include "pn_text_file.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void write_file(const char *path,size_t bytes,unsigned seed){FILE *f=fopen(path,"wb");assert(f);for(size_t i=0;i<bytes;i++)fputc((int)((i*seed+7u)%251u+1u),f);assert(!fclose(f));}
int main(int argc,char **argv){
    assert(argc==2);const char *dir=argv[1];
    pn_pool_t pool;assert(!pn_pool_init(&pool,1024*1024,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    char book[512],cache_file[512];snprintf(book,sizeof book,"%s/book.txt",dir);snprintf(cache_file,sizeof cache_file,"%s/identities.bin",dir);
    write_file(book,200000,13u);
    // 期望身份：常规完整检查。/ Expected identity: the ordinary complete inspection.
    pn_book_id_t expected;{pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);pn_text_file_t file={0};pn_text_source_t source;pn_text_encoding_t encoding;
     assert(pn_text_file_open(&file,&media,&lease,book,&source)==PN_OK && pn_text_file_identity_pooled(&pool,&source,&expected)==PN_OK);(void)encoding;assert(pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK);}
    static pn_identity_cache_t cache;assert(pn_identity_cache_load(&cache,cache_file)==PN_EMPTY && !cache.count);
    pn_identity_cache_job_t job={0};assert(pn_identity_cache_job_begin(&job,&pool,&media,book)==PN_OK && pn_media_active(&media)==1);
    pn_status_t s;unsigned steps=0;while((s=pn_identity_cache_job_step(&job,&cache))==PN_BUSY)steps++;
    assert(s==PN_OK && steps>=2);pn_identity_cache_job_close(&job);pn_identity_cache_job_close(&job);assert(pn_media_active(&media)==0 && !pool.used);
    uint64_t size;int64_t mtime;assert(pn_identity_cache_stat(book,&size,&mtime) && size==200000);
    pn_book_id_t found;assert(pn_identity_cache_find(&cache,book,size,mtime,&found) && !memcmp(found.sha256,expected.sha256,32));
    assert(!pn_identity_cache_find(&cache,book,size+1,mtime,&found) && !pn_identity_cache_find(&cache,book,size,mtime+2,&found));
    // 保存读回一致；损坏整份丢弃。/ Save/load round trip; damage discards everything.
    assert(cache.dirty && pn_identity_cache_save(&cache,cache_file)==PN_OK && !cache.dirty);
    static pn_identity_cache_t loaded;assert(pn_identity_cache_load(&loaded,cache_file)==PN_OK && loaded.count==1 && pn_identity_cache_find(&loaded,book,size,mtime,&found));
    {FILE *f=fopen(cache_file,"r+b");assert(f);assert(!fseek(f,30,SEEK_SET));fputc(0x5a,f);assert(!fclose(f));}
    assert(pn_identity_cache_load(&loaded,cache_file)==PN_CORRUPT && !loaded.count);
    // 满时淘汰最旧；同路径更新不重复。/ Evict the oldest when full; updating a path never duplicates it.
    pn_book_id_t fake={{0}};for(unsigned i=0;i<PN_IDENTITY_CACHE_MAX+3;i++){char p[64];snprintf(p,sizeof p,"/x/%u",i);fake.sha256[0]=(uint8_t)i;pn_identity_cache_put(&cache,p,i,i,&fake);}
    assert(cache.count==PN_IDENTITY_CACHE_MAX && !pn_identity_cache_find(&cache,book,size,mtime,&found));
    pn_identity_cache_put(&cache,"/x/70",70,70,&fake);assert(cache.count==PN_IDENTITY_CACHE_MAX);pn_identity_cache_forget(&cache,"/x/70");assert(cache.count==PN_IDENTITY_CACHE_MAX-1);
    // 计算期间改写文件：不记。/ A rewrite during computation is not recorded.
    memset(&cache,0,sizeof cache);assert(pn_identity_cache_job_begin(&job,&pool,&media,book)==PN_OK);assert(pn_identity_cache_job_step(&job,&cache)==PN_BUSY);
    write_file(book,200001,17u);while((s=pn_identity_cache_job_step(&job,&cache))==PN_BUSY){}
    assert(s!=PN_OK && !cache.count);pn_identity_cache_job_close(&job);assert(!pool.used && pn_media_active(&media)==0);
    unlink(book);unlink(cache_file);printf("identity cache ok steps=%u\n",steps);return 0;
}
