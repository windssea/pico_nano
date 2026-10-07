/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：有限缓冲显示任务的公共契约，不执行扫描或波形操作。
 * English: bounded display-job contracts without scanning or waveform operations.
 *
 * 冻结：服务owner串行仲裁；扫描缓冲在完成前不得修改或复用。
 * Frozen: service-owner arbitration; never modify or reuse scanning buffers before completion.
 */
#include "pn_display.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)
int main(void) {
    uint8_t data[3][8]; pn_frame_t frames[3];
    for (int i=0;i<3;i++) CHECK(pn_frame_bind(&frames[i],data[i],sizeof data[i],4,4));
    pn_display_t d; pn_job_token_t t={1,1};
    CHECK(pn_display_init(&d,frames,3,t)==PN_OK);
    pn_draw_lease_t a,b,c; pn_frame_t *f;
    pn_display_job_t first,second;
    CHECK(pn_display_start(&d,&first)==PN_EMPTY);
    CHECK(pn_display_begin_draw(&d,t,&a,&f)==PN_OK);
    pn_frame_clear(f,0);
    CHECK(pn_display_publish(&d,&a,10,PN_REFRESH_GL16)==PN_OK);
    CHECK(pn_display_draw_frame(&d,&a,&f)==PN_INVALID);
    CHECK(pn_display_start(&d,&first)==PN_OK && first.page_id==10);
    CHECK(pn_frame_get(first.frame,0,0)==0);
    CHECK(pn_display_discard(&d,&a)==PN_INVALID);
    CHECK(pn_display_begin_draw(&d,t,&b,&f)==PN_OK && b.slot!=a.slot);
    pn_frame_clear(f,7);
    CHECK(pn_display_publish(&d,&b,11,PN_REFRESH_GL16)==PN_OK);
    CHECK(pn_display_start(&d,&second)==PN_BUSY);
    CHECK(pn_display_begin_draw(&d,t,&c,&f)==PN_OK && c.slot!=a.slot && c.slot!=b.slot);
    pn_frame_clear(f,15);
    CHECK(pn_display_publish(&d,&c,12,PN_REFRESH_DU)==PN_OK);
    CHECK(d.slots[b.slot].state==PN_BUFFER_FREE);
    CHECK(pn_display_publish(&d,&b,11,PN_REFRESH_GL16)==PN_INVALID);
    CHECK(pn_frame_get(first.frame,0,0)==0);
    pn_display_job_t forged=first; forged.lease.ticket++;
    CHECK(pn_display_complete(&d,&forged,true)==PN_INVALID && d.active==(int)a.slot);
    CHECK(pn_display_complete(&d,&first,true)==PN_OK);
    CHECK(pn_display_complete(&d,&first,true)==PN_INVALID);
    CHECK(pn_display_start(&d,&second)==PN_OK && second.page_id==12);
    CHECK(pn_frame_get(second.frame,0,0)==15);
    CHECK(pn_display_begin_draw(&d,t,&a,&f)==PN_OK);
    CHECK(pn_display_publish(&d,&a,13,PN_REFRESH_GL16)==PN_OK);
    CHECK(pn_display_begin_draw(&d,t,&b,&f)==PN_OK);
    pn_job_token_t newer={1,2};
    CHECK(pn_display_set_token(&d,newer)==PN_OK);
    CHECK(d.queued==-1 && d.slots[a.slot].state==PN_BUFFER_FREE);
    CHECK(d.slots[b.slot].state==PN_BUFFER_DRAWING);
    CHECK(d.slots[second.lease.slot].state==PN_BUFFER_SCANNING);
    CHECK(pn_display_draw_frame(&d,&b,&f)==PN_STALE_JOB);
    CHECK(pn_display_publish(&d,&b,14,PN_REFRESH_GL16)==PN_STALE_JOB);
    CHECK(d.slots[b.slot].state==PN_BUFFER_FREE);
    CHECK(pn_display_begin_draw(&d,t,&a,&f)==PN_STALE_JOB);
    CHECK(pn_display_set_token(&d,t)==PN_STALE_JOB);
    CHECK(pn_display_begin_draw(&d,newer,&a,&f)==PN_OK);
    CHECK(pn_display_publish(&d,&a,20,PN_REFRESH_GC16)==PN_OK);
    CHECK(pn_display_complete(&d,&second,false)==PN_STALE_JOB);
    CHECK(pn_display_start(&d,&first)==PN_OK && first.page_id==20);
    CHECK(pn_display_complete(&d,&first,false)==PN_IO);
    CHECK(d.active==-1);
    CHECK(pn_display_begin_draw(&d,newer,&a,&f)==PN_OK);
    CHECK(pn_display_discard(&d,&a)==PN_OK);
    CHECK(pn_display_discard(&d,&a)==PN_INVALID);
    d.next_ticket=UINT64_MAX;
    CHECK(pn_display_begin_draw(&d,newer,&a,&f)==PN_OK);
    CHECK(pn_display_discard(&d,&a)==PN_OK);
    CHECK(pn_display_begin_draw(&d,newer,&a,&f)==PN_LIMIT);
    // 长序列随机组合验证所有权，不以正常路径单次通过代替取消交错。
    // Check ownership across long mixed sequences instead of relying on a single happy path.
    CHECK(pn_display_init(&d,frames,3,t)==PN_OK);
    uint32_t seed=0x5049434fu;
    pn_display_job_t active_job={0};
    for (int step=0;step<20000;step++) {
        seed=seed*1664525u+1013904223u;
        unsigned operation=(seed>>16)%6;
        if (operation==0) {
            pn_draw_lease_t draw;
            pn_frame_t *target=NULL;
            pn_status_t status=pn_display_begin_draw(&d,d.current,&draw,&target);
            if (status==PN_OK) pn_frame_clear(target,(uint8_t)(step%16));
            else CHECK(status==PN_BUSY);
        } else if (operation==1 || operation==2) {
            for (size_t i=0;i<d.count;i++) if (d.slots[i].state==PN_BUFFER_DRAWING) {
                pn_draw_lease_t draw=d.slots[i].lease;
                if (operation==1) {
                    pn_status_t status=pn_display_publish(&d,&draw,(uint64_t)step,PN_REFRESH_GL16);
                    CHECK(status==PN_OK || status==PN_STALE_JOB);
                } else CHECK(pn_display_discard(&d,&draw)==PN_OK);
                break;
            }
        } else if (operation==3) {
            pn_display_job_t job;
            pn_status_t status=pn_display_start(&d,&job);
            if (status==PN_OK) active_job=job;
            else CHECK(status==PN_BUSY || status==PN_EMPTY);
        } else if (operation==4 && d.active>=0) {
            pn_status_t status=pn_display_complete(&d,&active_job,true);
            CHECK(status==PN_OK || status==PN_STALE_JOB);
        } else if (operation==5) {
            pn_job_token_t next={d.current.session,d.current.generation+1};
            CHECK(pn_display_set_token(&d,next)==PN_OK);
        }
        size_t ready=0,scanning=0;
        for (size_t i=0;i<d.count;i++) {
            ready+=d.slots[i].state==PN_BUFFER_READY;
            scanning+=d.slots[i].state==PN_BUFFER_SCANNING;
            if (d.slots[i].state==PN_BUFFER_READY) CHECK(d.queued==(int)i);
            if (d.slots[i].state==PN_BUFFER_SCANNING) CHECK(d.active==(int)i);
        }
        CHECK(ready<=1 && scanning<=1);
        CHECK((ready==0)==(d.queued==-1) && (scanning==0)==(d.active==-1));
    }
    if (d.active>=0) {
        pn_status_t status=pn_display_complete(&d,&active_job,true);
        CHECK(status==PN_OK || status==PN_STALE_JOB);
    }
    for (size_t i=0;i<d.count;i++) if (d.slots[i].state==PN_BUFFER_DRAWING) {
        pn_draw_lease_t draw=d.slots[i].lease;
        CHECK(pn_display_discard(&d,&draw)==PN_OK);
    }
    pn_job_token_t end={d.current.session+1,1};
    CHECK(pn_display_set_token(&d,end)==PN_OK);
    for (size_t i=0;i<d.count;i++) CHECK(d.slots[i].state==PN_BUFFER_FREE);
    // 部分重叠也必须拒绝，不能只检查两个指针相等。
    // Reject partial overlaps as well as identical storage pointers.
    frames[1]=frames[0]; frames[1].pixels++;
    CHECK(pn_display_init(&d,frames,3,t)==PN_INVALID);
    frames[1]=frames[0];
    CHECK(pn_display_init(&d,frames,3,t)==PN_INVALID);
    CHECK(pn_display_init(&d,frames,0,t)==PN_INVALID);
    puts("buffer ownership, queue replacement, stale completion, false handles and exhaustion passed");
    return 0;
}
