/* SPDX-License-Identifier: Apache-2.0
 * 中文：邮箱过载、抢占、取消屏障和并发生产者。/ English: mailbox overload, preemption, cancellation barriers and concurrent producer. */
#include "pn_async.h"
#include <assert.h>
#include <threads.h>
#include <stdio.h>
#include <string.h>
static pn_async_event_t event(unsigned value,bool nav,bool urgent){pn_async_event_t e={.bytes=sizeof value,.navigation=nav,.urgent=urgent,.view=15};memcpy(e.payload,&value,sizeof value);return e;}
static unsigned value(pn_async_event_t e){unsigned v;memcpy(&v,e.payload,sizeof v);return v;}
typedef struct {pn_async_t *queue;atomic_bool done;} producer_t;
static int produce(void *ctx){producer_t *p=ctx;for(unsigned i=1;i<=10000;i++){pn_async_event_t e=event(i,true,false);assert(pn_async_send(p->queue,&e)==PN_OK);}atomic_store(&p->done,true);return 0;}
int main(void){
 pn_async_t q;pn_async_init(&q);pn_async_event_t e;
 for(unsigned i=1;i<=100;i++){e=event(i,true,false);assert(pn_async_send(&q,&e)==PN_OK);}
 assert(q.coalesced==99 && pn_async_take(&q,&e)==PN_OK && value(e)==100 && pn_async_take(&q,&e)==PN_EMPTY);
 e=event(1,true,true);pn_async_send(&q,&e);e=event(2,true,false);pn_async_send(&q,&e);assert(pn_async_take(&q,&e)==PN_OK && value(e)==1);
 for(unsigned i=0;i<PN_ASYNC_CONTROLS+1;i++){e=event(i,false,false);pn_async_send(&q,&e);}assert(pn_async_take(&q,&e)==PN_CANCELLED && pn_async_take(&q,&e)==PN_EMPTY && q.dropped==PN_ASYNC_CONTROLS+1);
 pn_async_scope_t scope;pn_async_begin(&q,&scope,true);assert(pn_cancel_checkpoint()==PN_OK);e=event(3,false,false);pn_async_send(&q,&e);assert(pn_cancel_checkpoint()==PN_CANCELLED);
 pn_cancel_scope_t shield;pn_cancel_enter(&shield,NULL,NULL);assert(pn_cancel_checkpoint()==PN_OK);pn_cancel_leave(&shield);assert(pn_cancel_checkpoint()==PN_CANCELLED);pn_async_end(&scope);assert(pn_cancel_checkpoint()==PN_OK);
 // 等待空邮箱和开始预排之间收到输入也必须取消。/ Input between observing empty and beginning prefetch must cancel too.
 pn_async_begin(&q,&scope,true);assert(pn_cancel_checkpoint()==PN_CANCELLED);pn_async_end(&scope);pn_async_take(&q,&e);
 pn_async_begin(&q,&scope,false);e=event(4,true,false);pn_async_send(&q,&e);assert(pn_cancel_checkpoint()==PN_OK);e=event(5,true,true);pn_async_send(&q,&e);assert(pn_cancel_checkpoint()==PN_CANCELLED);pn_async_end(&scope);pn_async_take(&q,&e);
 producer_t p={.queue=&q};atomic_init(&p.done,false);thrd_t thread;assert(thrd_create(&thread,produce,&p)==thrd_success);unsigned last=0;
 do{pn_status_t s=pn_async_take(&q,&e);assert(s==PN_OK || s==PN_EMPTY);if(s==PN_OK){assert(value(e)>last);last=value(e);}}while(!atomic_load(&p.done));
 thrd_join(thread,NULL);if(pn_async_take(&q,&e)==PN_OK)last=value(e);assert(last==10000);
 // 开书门清除旧普通事件，连点不能重开；紧急返回仍保留。/ Opening clears old ordinary events and repeated taps; urgent Back remains available.
 pn_async_init(&q);e=event(1,false,false);pn_async_send(&q,&e);e=event(2,true,false);pn_async_send(&q,&e);
 pn_async_block_controls(&q,true);
 for(unsigned i=0;i<10;i++){e=event(i,false,true);assert(pn_async_send(&q,&e)==PN_OK);}
 e=event(72,true,true);pn_async_send(&q,&e);assert(pn_async_take(&q,&e)==PN_OK && value(e)==72);
 assert(pn_async_take(&q,&e)==PN_EMPTY && q.dropped>=12);
 pn_async_block_controls(&q,false);
 e=event(3,false,false);pn_async_send(&q,&e);assert(pn_async_take(&q,&e)==PN_OK && value(e)==3);
 puts("async: bounded/coalesced, urgent priority, cancellation barriers and producer concurrency passed");return 0;
}
