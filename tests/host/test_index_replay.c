/* SPDX-License-Identifier: Apache-2.0
 * 中文：原小步分页为逐页oracle，覆盖索引精确定位、段落、取消及失卡。
 * English: original small-step paging is the per-page oracle for exact compiler seeking, paragraphs, cancellation and lost media. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_page.h"
#include "pn_text_file.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static pn_status_t glyph(void *ctx,uint32_t cp,const pn_xhtml_style_t *style,pn_epub_metrics_t *out){(void)ctx;(void)cp;*out=(pn_epub_metrics_t){.advance_64=(style->bold?11:10)*64,.ascent=12,.descent=3,.line_height=20,.pixels=16};return PN_OK;}
static pn_status_t image(void *ctx,const char *path,int mw,int mh,int *w,int *h){(void)ctx;(void)path;*w=mw<20?mw:20;*h=mh<30?mh:30;return PN_OK;}
static bool position(pn_xhtml_position_t a,pn_xhtml_position_t b){return a.element==b.element && a.offset==b.offset && a.run==b.run && a.kind==b.kind;}
static unsigned cancel_calls;
static bool cancel(void *ctx){(void)ctx;return ++cancel_calls==12;}
static unsigned prepare(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],const pn_xhtml_position_t *start,const pn_layout_t *layout,const pn_epub_measure_t *measure,pn_epub_page_t *page,bool compiler){
    pn_epub_page_job_t job={0};assert(pn_epub_page_job_begin(&job,pool,epub,path,salt,start,layout,measure,page)==PN_OK);unsigned steps=0;pn_status_t s;
    do{s=compiler?pn_epub_page_job_index_step(&job):pn_epub_page_job_step(&job);assert(++steps<100000);}while(s==PN_BUSY);
    if(s==PN_NO_MEMORY)s=pn_epub_page_prepare(pool,epub,path,salt,start,layout,measure,page);
    if(s!=PN_OK)fprintf(stderr,"page status=%d compiler=%d path=%s element=%llu offset=%llu steps=%u\n",s,compiler,path,(unsigned long long)(start?start->element:0),(unsigned long long)(start?start->offset:0),steps);
    assert(s==PN_OK && page->valid);pn_epub_page_job_close(&job);assert(page->valid && !job.impl);return steps;
}
int main(int argc,char **argv){
    assert(argc==2);pn_pool_t pool;assert(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_t media;pn_media_init(&media);pn_media_lease_t lease;pn_text_file_t file={0};pn_text_source_t source;pn_zip_t zip={0};pn_epub_t epub={0};uint8_t salt[16]={1};
    assert(pn_media_attach(&media,1)==PN_OK && pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK && pn_text_file_open(&file,&media,&lease,argv[1],&source)==PN_OK);
    assert(pn_zip_open(&zip,&pool,&source)==PN_OK && pn_epub_open(&epub,&pool,&zip,salt)==PN_OK);
    pn_layout_t layout={100,80,20,12,7,4,0,false};pn_epub_measure_t measure={NULL,glyph,image};pn_epub_page_t old={0},next={0};assert(pn_epub_page_reserve(&old,&pool,64,2)==PN_OK && pn_epub_page_reserve(&next,&pool,64,2)==PN_OK);
    pn_epub_t compiled={0};assert(pn_epub_open(&compiled,&pool,&zip,salt)==PN_OK);
    unsigned pages=0,slow=0,fast=0,continued=0;
    for(unsigned chapter=0;chapter<3;chapter++){
        char path[24];snprintf(path,sizeof path,"c%u.xhtml",chapter);pn_xhtml_position_t at={0};pn_epub_page_job_t continuous={0};
        do{
            slow+=prepare(&pool,&epub,path,salt,at.element?&at:NULL,&layout,&measure,&old,false);
            if(!continuous.impl)assert(pn_epub_page_job_begin(&continuous,&pool,&compiled,path,salt,at.element?&at:NULL,&layout,&measure,&next)==PN_OK);
            pn_status_t result;do{result=pn_epub_page_job_index_step(&continuous);fast++;}while(result==PN_BUSY);
            if(result==PN_NO_MEMORY)result=pn_epub_page_prepare(&pool,&compiled,path,salt,at.element?&at:NULL,&layout,&measure,&next);
            assert(result==PN_OK && next.valid);
            assert(old.count==next.count && old.image_count==next.image_count && old.begin_order==next.begin_order && old.next_order==next.next_order && old.has_next==next.has_next && position(old.begin,next.begin) && (!old.has_next || position(old.next,next.next)));
            for(size_t n=0;n<old.count;n++){pn_epub_page_node_t *a=&old.nodes[n],*b=&next.nodes[n];assert(position(a->position,b->position) && a->source_order==b->source_order && a->codepoint==b->codepoint && a->x_64==b->x_64 && a->baseline==b->baseline && a->image_index==b->image_index && !memcmp(&a->style,&b->style,sizeof a->style) && !memcmp(&a->metrics,&b->metrics,sizeof a->metrics));}
            for(size_t n=0;n<old.image_count;n++)assert(!strcmp(old.images[n].path,next.images[n].path) && old.images[n].width==next.images[n].width && old.images[n].height==next.images[n].height);
            pages++;if(!old.has_next)break;at=old.next;
            if(pn_epub_page_job_index_continue(&continuous)==PN_OK)continued++;else pn_epub_page_job_close(&continuous);
            if(pages==65){
                if(continuous.impl){pn_cancel_scope_t scope;cancel_calls=0;pn_cancel_enter(&scope,cancel,NULL);pn_status_t s=pn_epub_page_job_index_step(&continuous);pn_cancel_leave(&scope);assert(s==PN_CANCELLED && !continuous.impl && !next.valid);}
                pn_epub_page_job_t job={0};assert(pn_epub_page_job_begin(&job,&pool,&epub,path,salt,&at,&layout,&measure,&next)==PN_OK);pn_cancel_scope_t scope;cancel_calls=0;pn_cancel_enter(&scope,cancel,NULL);pn_status_t s=pn_epub_page_job_index_step(&job);pn_cancel_leave(&scope);assert(s==PN_CANCELLED && !job.impl && !next.valid);
            }
            assert(pages<2000);
        }while(true);
        pn_epub_page_job_close(&continuous);
    }
    assert(pages>100 && fast<slow/3 && continued>100);
    pn_epub_page_job_t job={0};assert(pn_epub_page_job_begin(&job,&pool,&epub,"bad.xhtml",salt,NULL,&layout,&measure,&next)==PN_OK);pn_status_t s;do{s=pn_epub_page_job_index_step(&job);}while(s==PN_BUSY);assert(s==PN_CORRUPT && !next.valid);
    assert(pn_epub_page_job_begin(&job,&pool,&epub,"c0.xhtml",salt,NULL,&layout,&measure,&next)==PN_OK);
    do{s=pn_epub_page_job_index_step(&job);}while(s==PN_BUSY);
    assert(s==PN_OK && pn_epub_page_job_index_continue(&job)==PN_OK && pn_media_detach(&media)==PN_OK);assert(pn_epub_page_job_index_step(&job)==PN_STALE_MEDIA && !job.impl && !next.valid);
    pn_epub_page_release(&old);pn_epub_page_release(&next);pn_epub_close(&compiled);pn_epub_close(&epub);(void)pn_zip_close(&zip);(void)pn_text_file_close(&file);assert(pn_media_release(&media,&lease)==PN_OK && !pool.used && !pool.live);
    printf("compiler replay: %u exact pages, original_steps=%u compiler_steps=%u, cancel/corrupt/lost-media and zero leaks passed\n",pages,slow,fast);return 0;
}
