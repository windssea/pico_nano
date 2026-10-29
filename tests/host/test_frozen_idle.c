/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实冻结会话的软预算、取消、租约及完整续读帧。/ English: real frozen-session soft budgets, cancellation, leases and complete resume pixels. */
#define _POSIX_C_SOURCE 200809L
#include "pn_hot_session.h"
#include "pn_profile.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static uint8_t screen[415872],expected[415872];
static uint64_t clock_value,write_cost;static unsigned clocks,writes,stored,hits,presents,cancel_after;static bool cancelled;
static uint64_t clock_us(void *ctx){(void)ctx;clocks++;return clock_value;}
static uint64_t ticking_clock(void *ctx){(void)ctx;clocks++;clock_value+=2000;return clock_value;}
static void observe(void *ctx,const pn_profile_event_t *event){
    (void)ctx;
    if(!strcmp(event->phase,"resume.write")){writes++;clock_value+=write_cost;if(cancel_after && writes==cancel_after)cancelled=true;}
    if(!strcmp(event->phase,"resume.stored"))stored++;
    if(!strcmp(event->phase,"resume.hit"))hits++;
}
static bool stop(void *ctx){(void)ctx;return cancelled;}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t refresh){(void)ctx;(void)refresh;memcpy(screen,frame->pixels,sizeof screen);presents++;return PN_OK;}
int main(int argc,char **argv){
    assert(argc==4);
    for(unsigned kind=1;kind<=2;kind++){
        char base[512],state[600],tf[600];snprintf(base,sizeof base,"%s/frozen-%u",argv[3],kind);assert(!mkdir(base,0700));
        snprintf(state,sizeof state,"%s/state",base);snprintf(tf,sizeof tf,"%s/tf",base);assert(!mkdir(state,0700) && !mkdir(tf,0700));assert(!setenv("PN_READING_CACHE",tf,1));
        pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
        pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
        pn_reader_app_t txt={0};pn_epub_app_t epub={0};pn_hot_session_t hot={0};bool is_epub=kind==2;
        assert((is_epub?pn_epub_app_open_on_media(&epub,&pool,&media,argv[kind],NULL,state,32,0):pn_reader_app_open_on_media(&txt,&pool,&media,argv[kind],NULL,state,32,0))==PN_OK);
        assert((is_epub?pn_epub_app_step(&epub,PN_APP_OPEN,1,present,NULL):pn_reader_app_step(&txt,PN_APP_OPEN,1,present,NULL))==PN_OK);
        assert((is_epub?pn_epub_app_step(&epub,PN_APP_NEXT,2,present,NULL):pn_reader_app_step(&txt,PN_APP_NEXT,2,present,NULL))==PN_OK);
        memcpy(expected,screen,sizeof screen);unsigned shown=presents;
        assert(pn_hot_session_park(&hot,&pool,&media,argv[kind],1,3,&txt,&epub)==PN_OK);
        assert(pn_hot_session_checkpoint_pending(&hot));
        clock_value=0;write_cost=0;writes=stored=hits=clocks=0;pn_profile_port(clock_us,observe,NULL);
        pn_cancel_scope_t scope;cancelled=true;pn_cancel_enter(&scope,stop,NULL);
        assert(pn_hot_session_deferred_idle(&hot,clock_us,NULL)==PN_CANCELLED && pn_hot_session_checkpoint_pending(&hot) && !hot.deferred_failed);
        pn_cancel_leave(&scope);cancelled=false;
        // 配额写租约争用不旋转到无限；无时钟进展也有32步硬上限。/ Writer contention never spins indefinitely; the 32-step cap holds even without clock progress.
        pn_media_lease_t busy={0};assert(pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&busy)==PN_OK);
        assert(pn_hot_session_deferred_idle(&hot,clock_us,NULL)==PN_BUSY && pn_hot_session_checkpoint_pending(&hot));assert(clocks<256);
        // 关闭诊断也必须遵守真实调度钟，不能退化为仅32步限制。/ Disabling diagnostics must retain the scheduling clock, never degrade to only the 32-step cap.
        pn_profile_port(NULL,NULL,NULL);clocks=0;
        assert(pn_hot_session_deferred_idle(&hot,ticking_clock,NULL)==PN_BUSY && clocks==5);
        assert(pn_hot_session_deferred_idle(&hot,NULL,NULL)==PN_INVALID);
        pn_profile_port(clock_us,observe,NULL);
        assert(pn_media_release(&media,&busy)==PN_OK);
        // 批次进行中出现输入，下一原步骤即取消，不能为了排空预算继续写。/ Input arriving inside a batch cancels at the next original step instead of draining the remaining budget.
        write_cost=2000;cancel_after=1;pn_cancel_enter(&scope,stop,NULL);
        pn_status_t interrupted=PN_BUSY;unsigned attempts=0;
        while(interrupted!=PN_CANCELLED){interrupted=pn_hot_session_deferred_idle(&hot,clock_us,NULL);assert(interrupted==PN_OK || interrupted==PN_BUSY || interrupted==PN_CANCELLED);assert(++attempts<160);}
        assert(writes==1 && pn_hot_session_checkpoint_pending(&hot) && !hot.deferred_failed);
        pn_cancel_leave(&scope);cancelled=false;cancel_after=0;
        unsigned slow_before=writes;uint64_t slow_at=clock_value;write_cost=20000;
        assert(pn_hot_session_deferred_idle(&hot,clock_us,NULL)==PN_BUSY && writes==slow_before+1 && clock_value-slow_at==20000);
        unsigned batches=0;
        while(pn_hot_session_checkpoint_pending(&hot)){
            unsigned before=writes;uint64_t at=clock_value;write_cost=2000;
            pn_status_t s=pn_hot_session_deferred_idle(&hot,clock_us,NULL);assert(s==PN_OK || s==PN_BUSY);
            assert(writes-before<=4 && clock_value-at<=8000);assert(++batches<160);
        }
        assert(stored==1 && presents==shown && !memcmp(screen,expected,sizeof screen));
        assert(pn_hot_session_drop_for_switch(&hot)==PN_OK && !pool.used && !pool.live);
        assert((is_epub?pn_epub_app_open_on_media(&epub,&pool,&media,argv[kind],NULL,state,32,10):pn_reader_app_open_on_media(&txt,&pool,&media,argv[kind],NULL,state,32,10))==PN_OK);
        assert((is_epub?pn_epub_app_step(&epub,PN_APP_OPEN,11,present,NULL):pn_reader_app_step(&txt,PN_APP_OPEN,11,present,NULL))==PN_OK);
        assert(hits==1 && !memcmp(screen,expected,sizeof screen));
        assert((is_epub?pn_epub_app_close(&epub,12):pn_reader_app_close(&txt,12))==PN_OK && !pool.used && !pool.live);
        pn_profile_port(NULL,NULL,NULL);printf("frozen idle kind=%u batches=%u steps=%u exact_pixels=1 budget=8000us/32 cancellation=1 contention=1\n",kind,batches,writes);
    }
    return 0;
}
