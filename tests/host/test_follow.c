/* SPDX-License-Identifier: Apache-2.0
 * 中文：独立目标/计数oracle验证逐相、反向、回执、取消及板级差分，不测光学。
 * English: independent target/count oracle checks phases, reversals, receipts, cancellation and board differences, without optical measurements. */
#include "pn_follow.h"
#include "device_frame.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define W 17
#define H 13
static bool target[W*H];static unsigned left[W*H];
static unsigned rng=31337;
static unsigned random_number(void){rng=rng*1664525u+1013904223u;return rng;}
static bool cancel(void *ctx){(void)ctx;return true;}
static size_t count_pending(void){size_t n=0;for(size_t i=0;i<W*H;i++)n+=left[i]!=0;return n;}
static void phase(pn_follow_t *follow,bool success){
    pn_follow_job_t job;size_t before=count_pending();assert(before && pn_follow_prepare(follow,&job)==PN_OK && pn_follow_stats(follow).pending==before);
    pn_follow_job_t corrupt=job;corrupt.box.width++;int direction=42;assert(pn_follow_drive(&corrupt,0,0,&direction)==PN_STALE_JOB && direction==42);
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){size_t i=(size_t)y*W+x;assert(pn_follow_drive(&job,x,y,&direction)==PN_OK && direction==(left[i]?(target[i]?1:-1):0));}
    pn_follow_row_t row={0};assert(pn_follow_row(&corrupt,job.box.y,&row)==PN_STALE_JOB && !row.cells);assert(pn_follow_row(&job,job.box.y-1,&row)==PN_INVALID);
    for(int y=job.box.y;y<job.box.y+job.box.height;y++){assert(pn_follow_row(&job,y,&row)==PN_OK && row.x==job.box.x && row.width==job.box.width);for(int x=row.x;x<row.x+row.width;x++){int value=row.cells[x-row.x];size_t i=(size_t)y*W+x;assert((value==9 || value==-9?0:value>0?1:-1)==(left[i]?(target[i]?1:-1):0));}}
    assert(pn_follow_complete(follow,&corrupt,true)==PN_STALE_JOB && pn_follow_stats(follow).pending==before);
    assert(pn_follow_complete(follow,&job,success)==(success?PN_OK:PN_IO));
    if(success)for(size_t i=0;i<W*H;i++)if(left[i])left[i]--;
    assert(pn_follow_stats(follow).pending==count_pending());assert(pn_follow_complete(follow,&job,true)==PN_STALE_JOB && pn_follow_row(&job,job.box.y,&row)==PN_STALE_JOB);
}
int main(void){
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    uint8_t pixels[H*12];memset(pixels,0xa5,sizeof pixels);pn_frame_t frame={pixels,W,H,12};pn_frame_clear(&frame,15);
    pn_follow_t follow={0};assert(pn_follow_open(&follow,&pool,&frame,8,7)==PN_OK);
    assert(pn_follow_confirmed(&follow,&frame)==PN_OK);
    pn_follow_job_t job={.ticket=42};assert(pn_follow_prepare(&follow,&job)==PN_EMPTY && job.ticket==42);
    pn_follow_box_t full={0,0,W,H};assert(pn_follow_mark(&follow,&frame,full)==PN_EMPTY);
    pn_frame_pixel(&frame,0,0,0);target[0]=true;left[0]=8;assert(pn_follow_mark(&follow,&frame,(pn_follow_box_t){0,0,1,1})==PN_OK);
    assert(pn_follow_confirmed(&follow,&frame)==PN_BUSY);
    assert(pn_follow_prepare(&follow,&job)==PN_OK);assert(pn_follow_mark(&follow,&frame,full)==PN_BUSY && pn_follow_reset(&follow,&frame)==PN_BUSY && pn_follow_close(&follow)==PN_BUSY && pn_follow_abandon(&follow)==PN_BUSY);
    assert(pn_follow_unsubmit(&follow,&job)==PN_OK && pn_follow_stats(&follow).known && pn_follow_stats(&follow).pending==1);
    pn_follow_job_t stale=job;phase(&follow,true);assert(left[0]==7 && pn_follow_complete(&follow,&stale,true)==PN_STALE_JOB);
    // 保持相同目标不补次数，半程反向立即用擦白的完整7次。
    // Holding a target never replenishes counts; reversing mid-flight starts all seven light phases.
    assert(pn_follow_mark(&follow,&frame,full)==PN_EMPTY);pn_frame_pixel(&frame,0,0,15);target[0]=false;left[0]=7;assert(pn_follow_mark(&follow,&frame,full)==PN_OK);
    phase(&follow,false);assert(!pn_follow_stats(&follow).known && pn_follow_mark(&follow,&frame,full)==PN_IO && pn_follow_prepare(&follow,&job)==PN_IO);
    assert(pn_follow_confirmed(&follow,&frame)==PN_IO);
    assert(pn_follow_reset(&follow,&frame)==PN_OK);memset(left,0,sizeof left);
    pn_frame_pixel(&frame,3,2,4);size_t marks=pn_follow_stats(&follow).marks;assert(pn_follow_mark(&follow,&frame,full)==PN_UNSUPPORTED && pn_follow_stats(&follow).marks==marks);pn_frame_pixel(&frame,3,2,15);
    pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,NULL);assert(pn_follow_mark(&follow,&frame,full)==PN_CANCELLED && pn_follow_reset(&follow,&frame)==PN_CANCELLED);pn_cancel_leave(&scope);
    for(unsigned round=0;round<2000;round++){
        unsigned x=random_number()%W,y=random_number()%H;size_t i=(size_t)y*W+x;bool black=(random_number()&8u)!=0;pn_frame_pixel(&frame,(int)x,(int)y,black?0:15);
        bool changed=black!=target[i];if(changed){target[i]=black;left[i]=black?8:7;}
        assert(pn_follow_mark(&follow,&frame,(pn_follow_box_t){(int)x,(int)y,1,1})==(changed?PN_OK:PN_EMPTY));assert(pn_follow_stats(&follow).pending==count_pending());
        if((round%3)==0 && count_pending())phase(&follow,true);
    }
    while(count_pending())phase(&follow,true);
    assert(pn_follow_confirmed(&follow,&frame)==PN_OK);unsigned value=pn_frame_get(&frame,0,0);pn_frame_pixel(&frame,0,0,value==0?15:0);assert(pn_follow_confirmed(&follow,&frame)==PN_STALE_JOB);pn_frame_pixel(&frame,0,0,value);
    assert(pn_follow_prepare(&follow,&job)==PN_EMPTY);
    uint64_t serial=follow.serial;assert(pn_follow_close(&follow)==PN_OK && pool.used==0 && follow.serial==serial);
    assert(pn_follow_open(&follow,&pool,&frame,8,7)==PN_OK);pn_frame_pixel(&frame,2,2,pn_frame_get(&frame,2,2)==0?15:0);assert(pn_follow_mark(&follow,&frame,(pn_follow_box_t){2,2,1,1})==PN_OK && pn_follow_prepare(&follow,&job)==PN_OK && job.ticket>serial);assert(pn_follow_complete(&follow,&stale,true)==PN_STALE_JOB);assert(pn_follow_unsubmit(&follow,&job)==PN_OK && pn_follow_close(&follow)==PN_BUSY && pn_follow_abandon(&follow)==PN_OK && pn_follow_close(&follow)==PN_OK);
    // 原生旋转、框外保持、两端角落与所有差分字节采用完整像素oracle。
    // Use a complete pixel oracle for native rotation, outside holds, opposite corners and every difference byte.
    uint8_t *base=pn_alloc_tagged(&pool,PN_MEM_FRAME,415872);assert(base && pn_frame_bind(&frame,base,415872,684,1216));pn_frame_clear(&frame,15);
    size_t used=pool.used,limit=pool.limit;pool.limit=used+512u*1024u;size_t attempts=pool.attempts;assert(pn_follow_open(&follow,&pool,&frame,8,8)==PN_EMPTY && pool.attempts==attempts && pool.used==used);pool.limit=limit;
    assert(pn_follow_open(&follow,&pool,&frame,8,8)==PN_OK && pn_follow_stats(&follow).bytes>684u*1216u && pn_follow_stats(&follow).bytes<684u*1216u+256u);
    pn_frame_rect(&frame,1,1,11,13,0);pn_frame_pixel(&frame,683,1215,0);assert(pn_follow_mark(&follow,&frame,(pn_follow_box_t){0,0,684,1216})==PN_OK && pn_follow_prepare(&follow,&job)==PN_OK);
    uint8_t *difference=malloc(684u*1216u);assert(difference);bool lines[684];uint8_t columns[608];memset(difference,0xa5,684u*1216u);
    assert(pn_device_follow_difference(&job,difference,1,lines,columns)==PN_INVALID && difference[0]==0xa5);
    assert(pn_device_follow_difference(&job,difference,684u*1216u,lines,columns)==PN_OK);
    for(int x=0;x<684;x++)for(int y=0;y<1216;y++){bool active=pn_frame_get(&frame,x,y)==0;assert(difference[(size_t)(683-x)*1216u+(size_t)y]==(active?0x0f:0x88));}
    assert(lines[0] && lines[672] && !lines[683] && columns[607]==0xff && !columns[100]);
    assert(pn_follow_stats(&follow).pending==144 && !pn_follow_stats(&follow).phases);assert(pn_follow_complete(&follow,&job,true)==PN_OK);
    for(unsigned i=1;i<8;i++)assert(pn_follow_prepare(&follow,&job)==PN_OK && pn_follow_complete(&follow,&job,true)==PN_OK);
    assert(!pn_follow_stats(&follow).pending && pn_follow_stats(&follow).phases==8);
    pn_frame_clear(&frame,15);assert(pn_follow_mark(&follow,&frame,(pn_follow_box_t){0,0,684,1216})==PN_OK && pn_follow_prepare(&follow,&job)==PN_OK && pn_device_follow_difference(&job,difference,684u*1216u,lines,columns)==PN_OK && difference[1215]==0xf0);
    assert(pn_follow_unsubmit(&follow,&job)==PN_OK && pn_follow_abandon(&follow)==PN_OK && pn_follow_close(&follow)==PN_OK);
    // 管理列表区域以外保留真实灰阶，不分配整屏状态或驱动题头/底栏。
    // Preserve real grays outside a management list without full-screen state allocation or header/footer drives.
    pn_frame_clear(&frame,7);pn_follow_box_t viewport={32,334,620,528};pn_frame_rect(&frame,viewport.x,viewport.y,viewport.width,viewport.height,15);
    assert(pn_follow_open(&follow,&pool,&frame,7,8)==PN_UNSUPPORTED && pn_follow_open_area(&follow,&pool,&frame,viewport,7,8)==PN_OK);
    assert(pn_follow_stats(&follow).bytes>620u*528u && pn_follow_stats(&follow).bytes<620u*528u+256u);
    pn_frame_rect(&frame,32,334,7,9,0);assert(pn_follow_mark(&follow,&frame,(pn_follow_box_t){31,334,8,9})==PN_INVALID && !pn_follow_stats(&follow).pending);
    assert(pn_follow_mark(&follow,&frame,(pn_follow_box_t){32,334,7,9})==PN_OK && pn_follow_prepare(&follow,&job)==PN_OK && pn_device_follow_difference(&job,difference,684u*1216u,lines,columns)==PN_OK);
    for(int x=0;x<684;x++)for(int y=0;y<1216;y++)assert(difference[(size_t)(683-x)*1216u+(size_t)y]==(x>=32 && x<39 && y>=334 && y<343?0x0f:0x88));
    assert(pn_follow_unsubmit(&follow,&job)==PN_OK && pn_follow_reset(&follow,&frame)==PN_OK && pn_follow_close(&follow)==PN_OK);
    follow.serial=UINT64_MAX;assert(pn_follow_open_area(&follow,&pool,&frame,viewport,7,8)==PN_OK);pn_frame_pixel(&frame,32,334,15);
    assert(pn_follow_mark(&follow,&frame,(pn_follow_box_t){32,334,1,1})==PN_OK && pn_follow_prepare(&follow,&job)==PN_LIMIT && !pn_follow_stats(&follow).borrowed && pn_follow_abandon(&follow)==PN_OK && pn_follow_close(&follow)==PN_OK);
    pn_free(base);free(difference);assert(pool.used==0 && pool.live==0);
    puts("follow: 2000 retarget/receipt rounds, binary boundary, cancel/stale/failed receipts, bounded memory and native full-pixel differences passed");return 0;
}
