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
static pn_status_t step(pn_cover_work_t *w,pn_shelf_covers_t *covers,pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,bool *changed){current++;return pn_shelf_covers_work(w,covers,page,pool,media,NULL,salt,2u*1024u*1024u,changed);}
static void reset(pn_shelf_covers_t *covers,pn_catalog_page_t *page){pn_shelf_covers_reset(covers,page);memset(covers->pixels[0],0x5a,PN_COVER_BYTES);}
int main(int argc,char **argv){
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
        for(size_t p=0;p<count;p++)if(!strcmp(phases[p],"cover.finish")){
            reset(covers,page);for(size_t k=0;k<stop[p]-1;k++)assert(step(&work,covers,page,&pool,&media,&changed)==PN_BUSY);
            FILE *file=fopen(argv[book],"ab");assert(file);long size=ftell(file);assert(size>=0 && fputc('X',file)!=EOF && !fclose(file));
            assert(step(&work,covers,page,&pool,&media,&changed)==PN_STALE_JOB && !changed && !memcmp(marker,covers->pixels[0],PN_COVER_BYTES) && !pool.used && !pn_media_active(&media));
            file=fopen(argv[book],"r+b");assert(file && !ftruncate(fileno(file),size) && !fclose(file));
        }
        reset(covers,page);assert(step(&work,covers,page,&pool,&media,&changed)==PN_BUSY);snprintf(page->items[0].path,sizeof page->items[0].path,"/tmp/different-cover-item");
        assert(step(&work,covers,page,&pool,&media,&changed)==PN_BUSY && !pn_cover_work_pending(&work) && !pool.used && !pn_media_active(&media) && !memcmp(marker,covers->pixels[0],PN_COVER_BYTES));snprintf(page->items[0].path,sizeof page->items[0].path,"%s",argv[book]);
        // 介质代次改变不能发布半帧；任务可以交错正常读租约，关闭不替别人释放。/ A media epoch change cannot publish partial frames; ordinary read leases may coexist and must not be released by the job.
        reset(covers,page);assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);assert(step(&work,covers,page,&pool,&media,&changed)==PN_BUSY);assert(pn_media_detach(&media)==PN_OK);
        assert(step(&work,covers,page,&pool,&media,&changed)==PN_STALE_MEDIA && !changed && !memcmp(marker,covers->pixels[0],PN_COVER_BYTES));pn_cover_work_abort(&work);assert(pn_media_active(&media)==1 && pn_media_release(&media,&lease)==PN_OK && !pool.used && !pool.live && pn_media_attach(&media,book+2)==PN_OK);
        printf("cover work book=%u phases=%zu complete_steps=%zu\n",book,count,completed);
    }
    free(marker);free(oracle);free(covers);free(page);puts("cover work: independent full-image oracle, no provisional slots, per-phase cancel/USB cleanup and media-epoch barriers passed");return 0;
}
