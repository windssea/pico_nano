/* SPDX-License-Identifier: Apache-2.0
 * 中文：嵌套局部峰值、分类释放与禁用端口。/ English: nested local peaks, classified frees and disabled ports. */
#include "pn_profile.h"
#include <assert.h>
static uint64_t tick;static unsigned emitted;static pn_profile_event_t events[3];
static uint64_t clock_us(void *ctx){(void)ctx;return tick;}
static void emit(void *ctx,const pn_profile_event_t *e){(void)ctx;events[emitted++]=*e;}
int main(void){
    pn_pool_t p;assert(!pn_pool_init(&p,4096,NULL,NULL,NULL));
    void *baseline=pn_alloc(&p,2000);assert(baseline);pn_free(baseline);size_t global=p.peak;
    pn_profile_port(clock_us,emit,NULL);pn_profile_span_t outer,inner;
    tick=10;pn_profile_begin(&outer,&p,"outer");void *a=pn_alloc_tagged(&p,PN_MEM_FONT,100);assert(a);size_t a_bytes=p.used;
    tick=20;pn_profile_begin(&inner,&p,"inner");void *b=pn_alloc_tagged(&p,PN_MEM_LAYOUT,200);assert(b);size_t simultaneous=p.used;
    pn_free(b);tick=30;pn_profile_end(&inner,PN_CANCELLED);pn_free(a);tick=50;pn_profile_end(&outer,PN_OK);
    assert(emitted==2 && events[0].elapsed_us==10 && events[0].status==PN_CANCELLED && events[0].begin_bytes==a_bytes && events[0].peak_bytes==simultaneous);
    assert(events[1].elapsed_us==40 && !events[1].end_bytes && events[1].peak_bytes==simultaneous && p.peak==global);
    assert(!p.memory[PN_MEM_FONT].used && !p.memory[PN_MEM_LAYOUT].live && p.memory[PN_MEM_FONT].allocations==1);
    p.fail_at=p.attempts+1;assert(!pn_alloc_tagged(&p,PN_MEM_IMAGE,1) && p.memory[PN_MEM_IMAGE].failures==1);
    pn_profile_io_t io={4096,1,2,3};size_t attempts=p.attempts;
    assert(pn_profile_time()==50);pn_profile_observe(&p,"job.wall",PN_CANCELLED,40,&io);
    assert(emitted==3 && events[2].elapsed_us==40 && events[2].io.bytes==4096 && events[2].io.hits==3 && p.attempts==attempts);
    pn_profile_port(NULL,NULL,NULL);pn_profile_begin(&outer,&p,"off");pn_profile_end(&outer,PN_IO);pn_profile_observe(&p,"off",PN_IO,10,&io);
    assert(!pn_profile_time() && emitted==3 && !p.used && !p.live);return 0;
}
