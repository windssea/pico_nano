/* SPDX-License-Identifier: Apache-2.0
 * 中文：邮箱过载、抢占、取消屏障和并发生产者。/ English: mailbox overload, preemption, cancellation barriers and concurrent producer. */
#include "pn_async.h"
#include <assert.h>
#include <threads.h>
#include <stdio.h>
#include <string.h>
static pn_async_event_t event(unsigned value,bool nav,bool urgent){pn_async_event_t e={.bytes=sizeof value,.navigation=nav,.urgent=urgent,.view=15};memcpy(e.payload,&value,sizeof value);return e;}
static unsigned value(pn_async_event_t e){unsigned v;memcpy(&v,e.payload,sizeof v);return v;}
// 首屏新手势不取消准备、不提前执行、不重复；失败/换页/紧急事件清除暂存。 / Fresh first-page gestures neither cancel preparation nor execute early or twice; failure, view changes and urgency discard the hold.
static void first_page(void){
 pn_async_t q;pn_async_init(&q);pn_async_event_t e=event(1,true,false),out;
 assert(pn_async_first_view(&q,15)==PN_BUSY);pn_async_block_controls(&q,true);assert(pn_async_first_view(&q,15)==PN_OK);
 pn_async_scope_t scope;pn_async_begin(&q,&scope,true);assert(pn_async_first_navigation(&q,&e)==PN_OK && pn_cancel_checkpoint()==PN_OK);
 e=event(2,true,false);assert(pn_async_first_navigation(&q,&e)==PN_OK && pn_cancel_checkpoint()==PN_OK);
 assert(pn_async_take(&q,&out)==PN_EMPTY);pn_async_first_complete(&q,true);pn_async_block_controls(&q,false);
 assert(pn_async_take(&q,&out)==PN_OK && value(out)==1 && pn_async_take(&q,&out)==PN_EMPTY);pn_async_end(&scope);
 assert(pn_async_first_navigation(&q,&e)==PN_OK && pn_async_take(&q,&out)==PN_OK && value(out)==2 && pn_async_take(&q,&out)==PN_EMPTY);
 for(unsigned abort=0;abort<3;abort++){
  pn_async_block_controls(&q,true);assert(pn_async_first_view(&q,15)==PN_OK);assert(pn_async_first_navigation(&q,&e)==PN_OK);
  if(abort==0)pn_async_first_complete(&q,false);
  if(abort==1)pn_async_block_controls(&q,false);
  if(abort==2){pn_async_set_view(&q,16,false);pn_async_first_complete(&q,true);}
  assert(pn_async_take(&q,&out)==PN_EMPTY);
 }
 pn_async_block_controls(&q,true);assert(pn_async_first_view(&q,15)==PN_OK);assert(pn_async_first_navigation(&q,&e)==PN_OK);
 pn_async_begin(&q,&scope,true);e=event(9,true,true);assert(pn_async_send(&q,&e)==PN_OK && pn_cancel_checkpoint()==PN_CANCELLED);
 e=event(3,true,false);assert(pn_async_first_navigation(&q,&e)==PN_OK);pn_async_first_complete(&q,true);
 assert(pn_async_take(&q,&out)==PN_OK && value(out)==9 && pn_async_take(&q,&out)==PN_EMPTY);pn_async_end(&scope);
 pn_async_block_controls(&q,true);assert(pn_async_first_view(&q,15)==PN_OK);e.view=16;assert(pn_async_first_navigation(&q,&e)==PN_CANCELLED);pn_async_first_complete(&q,false);
}
typedef struct {pn_async_t *queue;atomic_bool done;} producer_t;
static int produce(void *ctx){producer_t *p=ctx;for(unsigned i=1;i<=10000;i++){pn_async_event_t e=event(i,true,false);assert(pn_async_send(p->queue,&e)==PN_OK);}atomic_store(&p->done,true);return 0;}
int main(void){
 first_page();
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
 // 固定底栏只跨连续一级页保留；过时捕获、普通投递、临时模态与开书门都不能借用旧资格。
 // Fixed navigation survives only continuous roots; stale captures, ordinary sends, transient modals and opening gates cannot borrow old eligibility.
 pn_async_init(&q);pn_async_set_view(&q,100,true);e=event(80,false,true);e.view=100;assert(pn_async_send_root(&q,&e)==PN_OK);
 pn_async_set_view(&q,101,true);assert(pn_async_take(&q,&e)==PN_OK && e.root_epoch && e.view==100 && pn_async_root_current(&q,&e));pn_async_event_t held=e;
 pn_async_set_root(&q,false);pn_async_set_root(&q,true);assert(!pn_async_root_current(&q,&held));
 e.view=100;assert(pn_async_send_root(&q,&e)==PN_OK && pn_async_take(&q,&e)==PN_OK && !e.root_epoch);
 e=held;e.view=101;assert(pn_async_send(&q,&e)==PN_OK && pn_async_take(&q,&e)==PN_OK && !e.root_epoch);
 pn_async_set_view(&q,102,false);e=held;e.view=102;assert(pn_async_send_root(&q,&e)==PN_OK);pn_async_set_view(&q,103,true);assert(pn_async_take(&q,&e)==PN_OK && !e.root_epoch);
 e.view=103;assert(pn_async_send_root(&q,&e)==PN_OK && pn_async_take(&q,&held)==PN_OK && pn_async_root_current(&q,&held));pn_async_block_controls(&q,true);pn_async_block_controls(&q,false);pn_async_set_view(&q,104,true);assert(!pn_async_root_current(&q,&held));
 q.root_epoch=UINT64_MAX-1;pn_async_set_root(&q,false);pn_async_set_root(&q,true);assert(!q.root_enabled && q.root_epoch==UINT64_MAX);
 pn_async_set_view(&q,105,true);e.view=105;assert(pn_async_send_root(&q,&e)==PN_OK && pn_async_take(&q,&e)==PN_OK && !e.root_epoch && !pn_async_root_current(&q,&e));
 assert(!pn_async_root_current(NULL,&e) && !pn_async_root_current(&q,NULL));pn_async_set_view(NULL,0,true);pn_async_set_root(NULL,true);
 // 已确认的底栏合并只接纳已取出的普通批次；优先导航、溢出和后来的新输入不能被重置掉。
 // Confirmed root merging accepts only the drained ordinary batch; priority navigation, overflow and later input must never be reset away.
 pn_async_init(&q);pn_async_set_view(&q,16,true);pn_async_begin(&q,&scope,false);pn_async_event_t pending[PN_ASYNC_CONTROLS];size_t n=999;
 for(unsigned i=0;i<3;i++){e=event(i,false,true);pn_async_send(&q,&e);}assert(pn_cancel_checkpoint()==PN_CANCELLED);
 assert(pn_async_drain_root_controls(&scope,pending,2,&n)==PN_LIMIT && n==999 && q.count==3 && pn_cancel_checkpoint()==PN_CANCELLED);
 assert(pn_async_drain_root_controls(&scope,pending,PN_ASYNC_CONTROLS,&n)==PN_OK && n==3 && !q.count && pn_cancel_checkpoint()==PN_OK);
 for(unsigned i=0;i<3;i++)assert(value(pending[i])==i);
 e=event(4,false,true);pn_async_send(&q,&e);assert(pn_cancel_checkpoint()==PN_CANCELLED);e=event(80,true,true);pn_async_send(&q,&e);n=999;
 assert(pn_async_drain_root_controls(&scope,pending,PN_ASYNC_CONTROLS,&n)==PN_BUSY && n==999 && q.count==1 && q.has_navigation && pn_cancel_checkpoint()==PN_CANCELLED);
 assert(pn_async_take(&q,&e)==PN_OK && value(e)==80);assert(pn_async_drain_root_controls(&scope,pending,PN_ASYNC_CONTROLS,&n)==PN_OK && n==1 && value(pending[0])==4 && pn_cancel_checkpoint()==PN_OK);
 for(unsigned i=0;i<=PN_ASYNC_CONTROLS;i++){e=event(i,false,false);pn_async_send(&q,&e);}n=999;
 assert(pn_async_drain_root_controls(&scope,pending,PN_ASYNC_CONTROLS,&n)==PN_CANCELLED && n==999 && pn_cancel_checkpoint()==PN_CANCELLED);
 assert(pn_async_drain_root_controls(&scope,pending,PN_ASYNC_CONTROLS,&n)==PN_OK && !n && pn_cancel_checkpoint()==PN_OK);
 pn_async_set_root(&q,false);assert(pn_async_drain_root_controls(&scope,pending,PN_ASYNC_CONTROLS,&n)==PN_CANCELLED);
 pn_async_end(&scope);pn_async_set_root(&q,true);pn_async_begin(&q,&scope,true);assert(pn_async_drain_root_controls(&scope,pending,PN_ASYNC_CONTROLS,&n)==PN_CANCELLED);pn_async_end(&scope);
 assert(pn_async_drain_root_controls(NULL,pending,PN_ASYNC_CONTROLS,&n)==PN_INVALID);
 puts("async: bounded/coalesced, urgent priority, cancellation barriers, root batches and producer concurrency passed");return 0;
}
