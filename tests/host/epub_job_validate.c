/* SPDX-License-Identifier: Apache-2.0
 * 中文：用户EPUB的分步元信息/章节全量对照及逐阶段取消、媒体和分配边界。
 * English: full metadata/spine comparison for user EPUBs, with per-step cancellation, media and allocation boundaries. */
#include "pn_epub.h"
#include "pn_text_file.h"
#include "pn_cancel.h"
#include "expat.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const uint8_t salt[16]={1};
static enum XML_Error last_error;
enum XML_Status __real_XML_Parse(XML_Parser,const char *,int,int);
enum XML_Status __wrap_XML_Parse(XML_Parser parser,const char *bytes,int size,int final){enum XML_Status s=__real_XML_Parse(parser,bytes,size,final);last_error=XML_GetErrorCode(parser);return s;}
static bool stop(void *ctx){(void)ctx;return true;}
static uint64_t hash_bytes(uint64_t h,const void *ptr,size_t size){const uint8_t *p=ptr;for(size_t i=0;i<size;i++){h^=p[i];h*=UINT64_C(1099511628211);}return h;}
static uint64_t number(uint64_t hash,uint64_t n){for(unsigned i=0;i<8;i++){uint8_t byte=(uint8_t)(n>>(8*i));hash=hash_bytes(hash,&byte,1);}return hash;}
static uint64_t publication(pn_epub_t *book){
    pn_epub_info_t info;assert(pn_epub_info(book,&info)==PN_OK);uint64_t hash=UINT64_C(1469598103934665603);
    const char *strings[]={info.title,info.creator,info.identifier,info.language,info.package_path,info.nav_path,info.ncx_path,info.cover_path};
    for(size_t i=0;i<sizeof strings/sizeof strings[0];i++)hash=hash_bytes(hash,strings[i],strlen(strings[i])+1);
    hash=number(hash,info.manifest_count);hash=number(hash,info.spine_count);hash=number(hash,info.version);hash=number(hash,info.fixed_layout);hash=number(hash,info.cover_declared);
    for(size_t i=0;i<info.spine_count;i++){pn_epub_item_t item;assert(pn_epub_spine(book,i,&item)==PN_OK);
        hash=hash_bytes(hash,item.id,strlen(item.id)+1);hash=hash_bytes(hash,item.path,strlen(item.path)+1);hash=number(hash,item.zip_index);hash=number(hash,item.media);hash=number(hash,item.linear);}
    return hash;
}
static pn_status_t finish(pn_epub_job_t *job,pn_epub_t *book,size_t *steps){
    pn_status_t s=PN_BUSY;*steps=0;
    while(job->impl && s==PN_BUSY){assert(!book->impl && ++*steps<65536);s=pn_epub_job_step(job,book);if(s==PN_BUSY)assert(!book->impl);}
    return s;
}
int main(int argc,char **argv){
    if(argc!=2 && argc!=3)return 2;
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t media;pn_media_init(&media);pn_media_lease_t lease={0};pn_text_file_t file={0};pn_text_source_t source={0};pn_zip_t zip={0};pn_epub_t expected={0},book={0};
    assert(pn_media_attach(&media,1)==PN_OK && pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    assert(pn_text_file_open(&file,&media,&lease,argv[1],&source)==PN_OK && pn_zip_open(&zip,&pool,&source)==PN_OK);
    size_t before=pool.attempts;pn_status_t strict=pn_epub_open(&expected,&pool,&zip,salt);size_t strict_attempts=pool.attempts-before;
    uint64_t digest=0;if(strict==PN_OK)digest=publication(&expected);pn_epub_close(&expected);
    size_t used=pool.used,live=pool.live,at=pool.attempts,steps=0;pn_epub_job_t job={0};
    pn_status_t s=pn_epub_job_begin(&job,&pool,&zip,salt);if(s==PN_OK)s=finish(&job,&book,&steps);
    assert(s==strict && !job.impl);if(s==PN_OK)assert(publication(&book)==digest);pn_epub_close(&book);assert(pool.used==used && pool.live==live);
    size_t attempts=pool.attempts-at;
    if(strict==PN_OK){
        // 每一步之前取消时不读、不发布，既可继续也可显式关闭。/ Cancellation before each step neither reads nor publishes; jobs can resume or close explicitly.
        for(size_t pause=0;pause<steps;pause++){
            assert(pn_epub_job_begin(&job,&pool,&zip,salt)==PN_OK);
            for(size_t k=0;k<pause;k++)assert(pn_epub_job_step(&job,&book)==PN_BUSY && !book.impl);
            pn_cancel_scope_t scope;pn_cancel_enter(&scope,stop,NULL);assert(pn_epub_job_step(&job,&book)==PN_CANCELLED && job.impl && !book.impl);pn_cancel_leave(&scope);
            if(pause&1){size_t rest;assert(finish(&job,&book,&rest)==PN_OK && publication(&book)==digest);pn_epub_close(&book);}else pn_epub_job_close(&job);
            assert(!job.impl && pool.used==used && pool.live==live && pn_media_active(&media)==1);
        }
        if(argc==3){
            for(size_t fail=1;fail<=strict_attempts;fail++){
                pool.fail_at=pool.attempts+fail;s=pn_epub_open(&book,&pool,&zip,salt);
                if(s!=PN_NO_MEMORY || book.impl || pool.used!=used || pool.live!=live)fprintf(stderr,"strict fault=%zu/%zu status=%d xml=%d bytes=%zu/%zu\n",fail,strict_attempts,(int)s,(int)last_error,pool.used,used);
                assert(s==PN_NO_MEMORY && !book.impl && pool.used==used && pool.live==live);pool.fail_at=0;
            }
            for(size_t fail=1;fail<=attempts;fail++){
                pool.fail_at=pool.attempts+fail;s=pn_epub_job_begin(&job,&pool,&zip,salt);size_t count;
                if(s==PN_OK)s=finish(&job,&book,&count);
                if(s!=PN_NO_MEMORY || job.impl || book.impl || pool.used!=used || pool.live!=live)fprintf(stderr,"fault=%zu/%zu status=%d xml=%d job=%d book=%d bytes=%zu/%zu live=%zu/%zu\n",fail,attempts,(int)s,(int)last_error,job.impl!=NULL,book.impl!=NULL,pool.used,used,pool.live,live);
                assert(s==PN_NO_MEMORY && !job.impl && !book.impl && pool.used==used && pool.live==live);pool.fail_at=0;
            }
        }
        // 已完成的部分模型不能在介质代次变化后发布。/ A partial model cannot be published after a media generation change.
        assert(pn_epub_job_begin(&job,&pool,&zip,salt)==PN_OK);
        for(size_t i=0;i<steps/2;i++)assert(pn_epub_job_step(&job,&book)==PN_BUSY);
        assert(pn_media_detach(&media)==PN_OK && pn_epub_job_step(&job,&book)==PN_STALE_MEDIA && !job.impl && !book.impl && pool.used==used);
    }
    pn_epub_job_close(&job);assert(pn_zip_close(&zip)==PN_OK && pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK);
    assert(!pool.used && !pool.live && !pn_media_active(&media));
    printf("publication status=%d steps=%zu allocations=%zu digest=%016llx peak=%zu used=%zu live=%zu\n",(int)strict,steps,attempts,(unsigned long long)digest,pool.peak,pool.used,pool.live);return 0;
}
