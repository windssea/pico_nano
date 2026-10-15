/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际共享控制器的输入/相位/定稿与故障路径，不把端口替身当光学验收。
 * English: exercise the real shared controller's input/phases/settling and faults; port doubles are not optical acceptance. */
#include "pn_list_motion.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    pn_pool_t pool;pn_list_motion_t motion;pn_frame_t frame;unsigned calls[6];
    pn_list_stage_t fail_stage;bool fail,cancel_phase,omit_receipt,null_gray,render_fail;int last_offset;
} fixture_t;
static pn_status_t render(void *ctx,int offset,bool binary,const pn_frame_t **out){
    fixture_t *f=ctx;f->last_offset=offset;
    if(f->render_fail){f->render_fail=false;return PN_IO;}
    if(!binary && f->null_gray){*out=NULL;return PN_OK;}
    pn_frame_clear(&f->frame,7);
    for(int y=334;y<934;y++)for(int x=32;x<652;x++)pn_frame_pixel(&f->frame,x,y,binary?(((y+offset)/25)%2?0:15):(((y+offset)/25)%2?3:13));
    *out=&f->frame;return PN_OK;
}
static pn_status_t present(void *ctx,pn_list_stage_t stage,const pn_frame_t *frame,pn_follow_box_t area,const pn_follow_job_t *job){
    fixture_t *f=ctx;assert(area.x==32 && area.y==334 && area.width==620 && area.height==600);f->calls[stage]++;
    if(stage==PN_LIST_PHASE){assert(job && frame==&f->frame && pn_follow_stats(job->owner).borrowed);
        if(f->cancel_phase){assert(pn_follow_unsubmit(job->owner,job)==PN_OK);return PN_CANCELLED;}
        if(f->omit_receipt)return PN_OK;
        bool failed=f->fail && f->fail_stage==stage;if(failed)f->fail=false;
        return pn_follow_complete(job->owner,job,!failed);
    }
    assert(!job);if(stage==PN_LIST_REFERENCE)assert(!pn_list_motion_stats(&f->motion).pending);
    if(f->fail && f->fail_stage==stage){f->fail=false;return PN_IO;}
    return PN_OK;
}
static void open_fixture(fixture_t *f,size_t budget){
    memset(f,0,sizeof *f);assert(pn_pool_init(&f->pool,budget,NULL,NULL,NULL)==PN_OK);
    uint8_t *pixels=pn_alloc_tagged(&f->pool,PN_MEM_FRAME,415872);assert(pixels && pn_frame_bind(&f->frame,pixels,415872,684,1216));
    assert(pn_list_motion_open(&f->motion,&f->pool,(pn_follow_box_t){32,334,620,600},2400,0,render,f,present,f)==PN_OK);
}
static void close_fixture(fixture_t *f){pn_list_motion_close(&f->motion);assert(!f->motion.impl);pn_free(f->frame.pixels);assert(f->pool.used==0 && f->pool.live==0);}
static pn_status_t pointer(fixture_t *f,unsigned count,int x,int y,bool valid,uint64_t now,int expected){int click=-2;pn_status_t status=PN_INVALID;assert(pn_list_motion_pointer(&f->motion,count,x,y,valid,now,&click,&status));assert(click==expected);return status;}
static void begin(fixture_t *f){assert(pointer(f,1,400,850,true,100,-1)==PN_OK);assert(pointer(f,1,400,750,true,1100,-1)==PN_OK);assert(f->calls[PN_LIST_NORMALIZE]==1 && pn_list_motion_stats(&f->motion).offset==100);}
static void retarget(fixture_t *f){assert(pointer(f,1,400,713,true,2100,-1)==PN_OK);assert(pn_list_motion_stats(&f->motion).pending>0);}
static void drain(fixture_t *f,uint64_t now){for(unsigned i=0;i<8;i++)assert(pn_list_motion_tick(&f->motion,now+i)==PN_OK);assert(!pn_list_motion_stats(&f->motion).pending);}
int main(void){fixture_t f;
    open_fixture(&f,6u*1024u*1024u);
    int click;pn_status_t status;assert(!pn_list_motion_pointer(&f.motion,1,10,300,true,0,&click,&status));
    assert(pointer(&f,1,400,500,true,10,-1)==PN_OK && pointer(&f,0,0,0,true,20,500)==PN_OK);assert(!f.calls[PN_LIST_NORMALIZE]);
    begin(&f);retarget(&f);assert(pn_list_motion_offset(&f.motion,900)==PN_BUSY && pn_list_motion_extent(&f.motion,1200)==PN_BUSY);
    f.cancel_phase=true;size_t pending=pn_list_motion_stats(&f.motion).pending;assert(pn_list_motion_tick(&f.motion,2110)==PN_CANCELLED && pn_list_motion_stats(&f.motion).pending==pending && !pn_list_motion_stats(&f.motion).phases);f.cancel_phase=false;
    drain(&f,2200);assert(pn_list_motion_tick(&f.motion,2700)==PN_EMPTY && !f.calls[PN_LIST_SETTLE]);
    assert(pointer(&f,0,0,0,true,2800,-1)==PN_OK);assert(!pn_list_motion_stats(&f.motion).coasting);
    assert(pn_list_motion_tick(&f.motion,3119)==PN_EMPTY && pn_list_motion_tick(&f.motion,3120)==PN_OK);assert(f.calls[PN_LIST_REFERENCE]==1 && f.calls[PN_LIST_SETTLE]==1 && !pn_list_motion_stats(&f.motion).active);close_fixture(&f);

    // 故障后的释放、二指和失焦不产生点击；恢复限频且只确认恢复回执。/ Failed releases, multi-touch and focus loss never click; recovery is rate-limited and receipt-confirmed.
    for(unsigned kind=0;kind<4;kind++){open_fixture(&f,6u*1024u*1024u);begin(&f);retarget(&f);
        if(kind==0){f.fail=true;f.fail_stage=PN_LIST_PHASE;assert(pn_list_motion_tick(&f.motion,2200)==PN_IO);}
        else if(kind==1){f.omit_receipt=true;assert(pn_list_motion_tick(&f.motion,2200)==PN_INVALID);}
        else{assert(pointer(&f,kind==2?2:0,0,0,kind==2,2200,-1)==PN_OK);drain(&f,2210);}
        assert(pointer(&f,0,0,0,true,2300,-1)==PN_OK);
        if(kind<2){assert(pn_list_motion_tick(&f.motion,3199)==PN_EMPTY && !f.calls[PN_LIST_RECOVER]);assert(pn_list_motion_tick(&f.motion,3200)==PN_OK && f.calls[PN_LIST_RECOVER]==1);}
        else assert(pn_list_motion_tick(&f.motion,2620)==PN_OK && f.calls[PN_LIST_SETTLE]==1);
        close_fixture(&f);
    }
    open_fixture(&f,6u*1024u*1024u);f.fail=true;f.fail_stage=PN_LIST_NORMALIZE;
    assert(pointer(&f,1,400,850,true,100,-1)==PN_OK && pointer(&f,1,400,750,true,1100,-1)==PN_IO);assert(pointer(&f,0,0,0,true,1110,-1)==PN_OK);assert(pn_list_motion_tick(&f.motion,2100)==PN_OK && f.calls[PN_LIST_RECOVER]==1 && !pn_list_motion_stats(&f.motion).offset);close_fixture(&f);
    open_fixture(&f,6u*1024u*1024u);begin(&f);assert(pointer(&f,0,0,0,true,1200,-1)==PN_OK);f.null_gray=true;assert(pn_list_motion_tick(&f.motion,1520)==PN_INVALID && f.calls[PN_LIST_ABANDON]==1);f.null_gray=false;assert(pn_list_motion_tick(&f.motion,2520)==PN_OK);close_fixture(&f);
    open_fixture(&f,6u*1024u*1024u);begin(&f);assert(pointer(&f,0,0,0,true,1200,-1)==PN_OK);f.fail=true;f.fail_stage=PN_LIST_SETTLE;assert(pn_list_motion_tick(&f.motion,1520)==PN_IO && f.calls[PN_LIST_SETTLE]==1 && !pn_list_motion_stats(&f.motion).active);
    f.fail=true;f.fail_stage=PN_LIST_RECOVER;assert(pn_list_motion_tick(&f.motion,2520)==PN_IO && f.calls[PN_LIST_RECOVER]==1);assert(pn_list_motion_tick(&f.motion,3519)==PN_EMPTY && f.calls[PN_LIST_RECOVER]==1);assert(pn_list_motion_tick(&f.motion,3520)==PN_OK && f.calls[PN_LIST_RECOVER]==2);close_fixture(&f);
    open_fixture(&f,6u*1024u*1024u);begin(&f);retarget(&f);assert(pn_list_motion_abort(&f.motion)==PN_OK && !pn_list_motion_stats(&f.motion).active && f.calls[PN_LIST_ABANDON]==1 && !f.calls[PN_LIST_SETTLE]);close_fixture(&f);
    open_fixture(&f,1024u*1024u);assert(pointer(&f,1,400,850,true,100,-1)==PN_OK && pointer(&f,1,400,750,true,1100,-1)==PN_EMPTY && pointer(&f,0,0,0,true,1200,-1)==PN_OK);assert(!f.calls[PN_LIST_NORMALIZE] && !pn_list_motion_stats(&f.motion).active);close_fixture(&f);
    // 惯性沿同一offset移动，边界有界；惯性中的绘制失败停止并灰阶恢复。/ Inertia shares the same bounded offset; a drawing failure while coasting stops and restores gray.
    open_fixture(&f,6u*1024u*1024u);assert(pointer(&f,1,400,850,true,100,-1)==PN_OK && pointer(&f,1,400,750,true,120,-1)==PN_OK && pointer(&f,0,0,0,true,130,-1)==PN_OK);assert(pn_list_motion_stats(&f.motion).coasting);assert(pn_list_motion_tick(&f.motion,150)==PN_OK && pn_list_motion_stats(&f.motion).offset>100);f.render_fail=true;assert(pn_list_motion_tick(&f.motion,180)==PN_IO && !pn_list_motion_stats(&f.motion).active);assert(pn_list_motion_tick(&f.motion,1180)==PN_OK && f.calls[PN_LIST_RECOVER]==1);close_fixture(&f);
    puts("list motion: drag/tap separation, phase receipts, prehandoff cancel, inertia, multi-touch, focus loss, failed normalization/phase/render/settle, recovery, exit and low budget passed");return 0;
}
