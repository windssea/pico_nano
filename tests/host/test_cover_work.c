/* SPDX-License-Identifier: Apache-2.0
 * 中文：分步封面与原同步完整oracle、暂定像素屏障、各阶段释放和来源/租约交接。
 * English: staged covers against full synchronous oracles, provisional-pixel barriers, phase cleanup and source/lease handoffs. */
#define _POSIX_C_SOURCE 200809L
#include "pn_cover.h"
#include "pn_cancel.h"
#include "pn_profile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static const uint8_t salt[16]={1};
static const char *phases[32];static size_t first[32],phase_count,current;
static uint64_t clock_us(void *ctx){(void)ctx;return 1;}
static void emit(void *ctx,const pn_profile_event_t *e){(void)ctx;if(strncmp(e->phase,"cover.",6))return;for(size_t i=0;i<phase_count;i++)if(!strcmp(phases[i],e->phase))return;assert(phase_count<32);phases[phase_count]=e->phase;first[phase_count++]=current;}
static bool cancel(void *ctx){(void)ctx;return true;}
static bool cancel_during(void *ctx){return ++*(unsigned *)ctx>=6;}
static pn_status_t step(pn_cover_work_t *w,pn_shelf_covers_t *covers,pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,bool *changed){current++;return pn_shelf_covers_work(w,covers,page,pool,media,NULL,salt,2u*1024u*1024u,changed);}
static void reset(pn_shelf_covers_t *covers,pn_catalog_page_t *page){pn_shelf_covers_reset(covers,page);memset(covers->pixels[0],0x5a,PN_COVER_BYTES);}
int main(int argc,char **argv){
    assert(!pn_cover_refresh_due(false,1000,0) && pn_cover_refresh_due(true,1000,0));
    assert(!pn_cover_refresh_due(true,1999,1000) && pn_cover_refresh_due(true,2000,1000) && !pn_cover_refresh_due(true,999,1000));
    assert(argc==3);pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_catalog_page_t *page=calloc(1,sizeof *page);pn_shelf_covers_t *covers=calloc(1,sizeof *covers);uint8_t *oracle=malloc(PN_COVER_BYTES),*marker=malloc(PN_COVER_BYTES);assert(page && covers && oracle && marker);memset(marker,0x5a,PN_COVER_BYTES);
    for(unsigned book=1;book<3;book++){
        page->count=page->total=1;snprintf(page->items[0].path,sizeof page->items[0].path,"%s",argv[book]);page->items[0].format=book==1?PN_BOOK_EPUB:PN_BOOK_TXT;
        pn_frame_t thumb;assert(pn_frame_bind(&thumb,oracle,PN_COVER_BYTES,PN_COVER_WIDTH,PN_COVER_HEIGHT));pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
        assert(pn_cover_render(&pool,&media,&lease,&page->items[0],salt,2u*1024u*1024u,&thumb)==PN_OK && pn_media_release(&media,&lease)==PN_OK && !pool.used);
        pn_cover_work_t work={0};bool changed;pn_status_t s;reset(covers,page);phase_count=current=0;pn_profile_port(clock_us,emit,NULL);
        do{s=step(&work,covers,page,&pool,&media,&changed);assert(current<8192);if(s==PN_BUSY)assert(!changed && covers->state[0]==PN_COVER_PENDING && !memcmp(marker,covers->pixels[0],PN_COVER_BYTES));}while(s==PN_BUSY);
        assert(s==PN_OK && changed && !memcmp(oracle,covers->pixels[0],PN_COVER_BYTES) && !pool.used && !pool.live && !pn_media_active(&media));pn_profile_port(NULL,NULL,NULL);size_t completed=current,count=phase_count,stop[32];memcpy(stop,first,sizeof stop);
        // 每一种阶段前中断都必须释放暂存/只读租约，并允许USB独占。/ Abort before each observed phase, release provisional storage/read leases, and allow exclusive USB.
        for(size_t p=0;p<count;p++){
            reset(covers,page);current=0;for(size_t k=0;k<stop[p]-1;k++)assert(step(&work,covers,page,&pool,&media,&changed)==PN_BUSY);
            pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,NULL);assert(step(&work,covers,page,&pool,&media,&changed)==PN_CANCELLED && !changed);pn_cancel_leave(&scope);
            assert(!memcmp(marker,covers->pixels[0],PN_COVER_BYTES));assert(pn_media_request_usb(&media)==PN_OK);pn_cover_work_abort(&work);assert(!pool.used && !pool.live && !pn_media_active(&media));
            assert(pn_media_acquire(&media,PN_MEDIA_USB,&lease)==PN_OK && pn_media_release(&media,&lease)==PN_OK && pn_media_cancel_usb(&media)==PN_OK);
        }
        // 缓存关闭也要拒绝处理中换来源；已完成解码不能绕过最后复核。/ Reject changed sources even with caching disabled; completed decoding cannot bypass final revalidation.
        // 同页输入保留提取任务；阶段内部取消后完整接续，像素仍与同步完整解码一致。/ Same-page input retains the extraction job; resume after cancellation inside each phase and match the complete synchronous decode.
        for(size_t p=0;p<count;p++){
            reset(covers,page);current=0;for(size_t k=0;k<stop[p]-1;k++)assert(step(&work,covers,page,&pool,&media,&changed)==PN_BUSY);
            void *retained=work.extract;unsigned checks=0;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel_during,&checks);s=step(&work,covers,page,&pool,&media,&changed);pn_cancel_leave(&scope);
            if(s==PN_CANCELLED)assert(work.extract==retained && !changed && !memcmp(marker,covers->pixels[0],PN_COVER_BYTES));
            for(size_t k=0;s==PN_BUSY || s==PN_CANCELLED;k++){assert(k<8192);s=step(&work,covers,page,&pool,&media,&changed);}
            assert(s==PN_OK && !memcmp(oracle,covers->pixels[0],PN_COVER_BYTES) && !pool.used && !pool.live && !pn_media_active(&media));
        }
        for(size_t p=0;p<count;p++)if(!strcmp(phases[p],"cover.finish")){
            reset(covers,page);for(size_t k=0;k<stop[p]-1;k++)assert(step(&work,covers,page,&pool,&media,&changed)==PN_BUSY);
            FILE *file=fopen(argv[book],"ab");assert(file);long size=ftell(file);assert(size>=0 && fputc('X',file)!=EOF && !fclose(file));
            assert(step(&work,covers,page,&pool,&media,&changed)==PN_STALE_JOB && !changed && !memcmp(marker,covers->pixels[0],PN_COVER_BYTES) && !pool.used && !pn_media_active(&media));
            file=fopen(argv[book],"r+b");assert(file && !ftruncate(fileno(file),size) && !fclose(file));
        }
        // 第一张已完整验证即可呈现，后一本仍待提取；像素保持与原同步路径一致。/ Present the first fully verified cover while a second book is pending, with pixels identical to the synchronous oracle.
        page->count=page->total=2;page->items[1]=page->items[0];reset(covers,page);current=0;
        do{s=step(&work,covers,page,&pool,&media,&changed);assert(current<8192);}while(s==PN_BUSY);
        assert(s==PN_OK && changed && covers->state[0]==PN_COVER_READY && covers->state[1]==PN_COVER_PENDING && pn_shelf_covers_pending(covers,page));
        assert(pn_cover_refresh_due(changed,1000,0) && !memcmp(oracle,covers->pixels[0],PN_COVER_BYTES));
        pn_cover_work_abort(&work);page->count=page->total=1;
        reset(covers,page);assert(step(&work,covers,page,&pool,&media,&changed)==PN_BUSY);snprintf(page->items[0].path,sizeof page->items[0].path,"/tmp/different-cover-item");
        assert(step(&work,covers,page,&pool,&media,&changed)==PN_BUSY && !pn_cover_work_pending(&work) && !pool.used && !pn_media_active(&media) && !memcmp(marker,covers->pixels[0],PN_COVER_BYTES));snprintf(page->items[0].path,sizeof page->items[0].path,"%s",argv[book]);
        // 介质代次改变不能发布半帧；任务可以交错正常读租约，关闭不替别人释放。/ A media epoch change cannot publish partial frames; ordinary read leases may coexist and must not be released by the job.
        reset(covers,page);assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);assert(step(&work,covers,page,&pool,&media,&changed)==PN_BUSY);assert(pn_media_detach(&media)==PN_OK);
        assert(step(&work,covers,page,&pool,&media,&changed)==PN_STALE_MEDIA && !changed && !memcmp(marker,covers->pixels[0],PN_COVER_BYTES));pn_cover_work_abort(&work);assert(pn_media_active(&media)==1 && pn_media_release(&media,&lease)==PN_OK && !pool.used && !pool.live && pn_media_attach(&media,book+2)==PN_OK);
        printf("cover work book=%u phases=%zu complete_steps=%zu\n",book,count,completed);
    }
    free(marker);free(oracle);free(covers);free(page);puts("cover work: independent full-image oracle, no provisional slots, per-phase cancel/USB cleanup and media-epoch barriers passed");return 0;
}
