/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实书源完整SHA证明的重开复用、失效与坏身份拒绝。
 * English: reopen reuse, invalidation and mismatch rejection of real full-source SHA proofs. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_app.h"
#include "pn_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned memo_hits;
static uint64_t clock_us(void *ctx){(void)ctx;return 1000000;}
static void observe(void *ctx,const pn_profile_event_t *e){(void)ctx;if(!strcmp(e->phase,"epub.open.source-memo"))memo_hits++;}
static pn_status_t open_trusted(pn_epub_open_job_t *job,pn_epub_app_t *app,pn_pool_t *pool,pn_media_t *media,char **argv,const pn_book_id_t *id,uint64_t size){
    pn_status_t s=pn_epub_app_open_job_begin_trusted(job,pool,media,argv[1],argv[2],argv[3],44,0,id,size);
    assert(s==PN_OK && !pn_epub_app_open_job_feedback_due(job));
    for(unsigned n=0;n<20000;n++){s=pn_epub_app_open_job_advance(job,app);if(s!=PN_BUSY)return s;}
    assert(!"opening never completed");return PN_BUSY;
}
int main(int argc,char **argv){
    assert(argc==4);pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    pn_profile_port(clock_us,observe,NULL);pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t lease;pn_text_file_t file={0};pn_text_source_t source;pn_book_id_t id;
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    assert(pn_text_file_open(&file,&media,&lease,argv[1],&source)==PN_OK);
    assert(pn_text_file_identity_pooled(&pool,&source,&id)==PN_OK);uint64_t size=source.size;
    assert(pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK);
    pn_epub_app_t app={0};pn_epub_open_job_t job={0};
    assert(open_trusted(&job,&app,&pool,&media,argv,&id,size)==PN_OK && !job.impl);
#ifdef PN_TEST_IMMUTABLE_FILE
    assert(memo_hits==1 && !pn_epub_app_verify_pending(&app));
#else
    assert(!memo_hits && pn_epub_app_verify_pending(&app));
#endif
    for(unsigned n=0;pn_epub_app_verify_pending(&app);n++){assert(n<10000);pn_status_t s=pn_epub_app_verify_step(&app);assert(s==PN_OK || s==PN_BUSY);}
    assert(pn_epub_app_discard_unshown(&app)==PN_OK && !pool.used && !pn_media_active(&media));
#ifdef PN_TEST_IMMUTABLE_FILE
    pn_book_id_t wrong=id;wrong.sha256[0]^=1;
    assert(open_trusted(&job,&app,&pool,&media,argv,&wrong,size)==PN_STALE_JOB && !app.impl);
    assert(pn_epub_app_open_job_close(&job)==PN_OK && !pool.used && !pn_media_active(&media));
#endif
    // 换代后即使路径、长度、expected完全相同也必须重新完整复核。/ A new epoch requires full verification even with identical path, size and expected digest.
    assert(pn_media_detach(&media)==PN_OK && pn_media_attach(&media,2)==PN_OK);unsigned before=memo_hits;
    assert(open_trusted(&job,&app,&pool,&media,argv,&id,size)==PN_OK && memo_hits==before && pn_epub_app_verify_pending(&app));
    for(unsigned n=0;pn_epub_app_verify_pending(&app);n++){assert(n<10000);pn_status_t s=pn_epub_app_verify_step(&app);assert(s==PN_OK || s==PN_BUSY);}
    assert(pn_epub_app_discard_unshown(&app)==PN_OK && !pool.used && !pool.live && !pn_media_active(&media));
    puts("reopen source proof, epoch invalidation and cleanup passed");return 0;
}
