/* SPDX-License-Identifier: Apache-2.0
 * 中文：平台无关、有界输入/取消邮箱，池与模型不在锁内。/ English: platform-neutral bounded input/cancellation mailbox; no allocation or model calls under locks. */
#include "pn_async.h"
#include <string.h>
static void lock(pn_async_t *q){while(atomic_flag_test_and_set_explicit(&q->guard,memory_order_acquire)){} }
static void unlock(pn_async_t *q){atomic_flag_clear_explicit(&q->guard,memory_order_release);}
void pn_async_init(pn_async_t *q){memset(q,0,sizeof *q);atomic_flag_clear(&q->guard);atomic_init(&q->epoch,1);atomic_init(&q->background,false);atomic_init(&q->view,0);}
static void root_mode(pn_async_t *q,bool enabled){
    if(q->root_enabled==enabled)return;
    if(q->root_epoch==UINT64_MAX){q->root_enabled=false;return;}
    q->root_epoch++;q->root_enabled=enabled;
}
void pn_async_set_view(pn_async_t *q,uint32_t view,bool root){if(!q)return;lock(q);root_mode(q,root);atomic_store_explicit(&q->view,view,memory_order_release);unlock(q);}
void pn_async_set_root(pn_async_t *q,bool root){if(!q)return;lock(q);root_mode(q,root);unlock(q);}
bool pn_async_root_current(pn_async_t *q,const pn_async_event_t *e){if(!q || !e)return false;lock(q);bool current=q->root_enabled && e->root_epoch && e->root_epoch==q->root_epoch;unlock(q);return current;}
static pn_status_t send_event(pn_async_t *q,const pn_async_event_t *e,bool root){
    if(!q || !e || !e->bytes || e->bytes>PN_ASYNC_PAYLOAD)return PN_INVALID;
    lock(q);
    if(q->controls_blocked && !(e->navigation && e->urgent)){q->dropped++;unlock(q);return PN_OK;}
    pn_async_event_t next=*e;next.root_epoch=root && q->root_enabled && e->view==atomic_load_explicit(&q->view,memory_order_acquire)?q->root_epoch:0;
    if(e->urgent || atomic_load_explicit(&q->background,memory_order_acquire))atomic_fetch_add_explicit(&q->epoch,1,memory_order_release);
    if(e->navigation){if(q->has_navigation)q->coalesced++;if(!q->has_navigation || !q->navigation.urgent || e->urgent)q->navigation=next;q->has_navigation=true;}
    else if(q->count<PN_ASYNC_CONTROLS){q->controls[(q->first+q->count)%PN_ASYNC_CONTROLS]=next;q->count++;}
    else{q->dropped+=q->count+1;q->count=0;q->first=0;q->overflow=true;atomic_fetch_add_explicit(&q->epoch,1,memory_order_release);}
    unlock(q);return PN_OK;
}
pn_status_t pn_async_send(pn_async_t *q,const pn_async_event_t *e){return send_event(q,e,false);}
pn_status_t pn_async_send_root(pn_async_t *q,const pn_async_event_t *e){return send_event(q,e,true);}
pn_status_t pn_async_take(pn_async_t *q,pn_async_event_t *e){
    if(!q || !e)return PN_INVALID;
    lock(q);pn_status_t status=PN_OK;
    if(q->overflow){q->overflow=false;status=PN_CANCELLED;}
    else if(q->has_navigation){*e=q->navigation;q->has_navigation=false;}
    else if(q->count){*e=q->controls[q->first];q->first=(q->first+1)%PN_ASYNC_CONTROLS;q->count--;}
    else status=PN_EMPTY;
    unlock(q);return status;
}
static bool cancelled(void *ctx){pn_async_scope_t *s=ctx;return s->epoch!=atomic_load_explicit(&s->owner->epoch,memory_order_acquire);}
void pn_async_begin(pn_async_t *q,pn_async_scope_t *s,bool idle){
    lock(q);s->owner=q;s->epoch=atomic_load_explicit(&q->epoch,memory_order_acquire);atomic_store_explicit(&q->background,idle,memory_order_release);
    bool urgent=q->has_navigation && q->navigation.urgent;
    for(unsigned i=0;i<q->count;i++)urgent=urgent || q->controls[(q->first+i)%PN_ASYNC_CONTROLS].urgent;
    if((idle && (q->has_navigation || q->count || q->overflow)) || urgent)atomic_fetch_add_explicit(&q->epoch,1,memory_order_release);
    unlock(q);pn_cancel_enter(&s->cancel,cancelled,s);
}
void pn_async_end(pn_async_scope_t *s){pn_cancel_leave(&s->cancel);atomic_store_explicit(&s->owner->background,false,memory_order_release);}
void pn_async_block_controls(pn_async_t *q,bool blocked){
    if(!q)return;
    lock(q);q->controls_blocked=blocked;
    if(blocked){q->dropped+=q->count;q->count=0;q->first=0;q->overflow=false;
        root_mode(q,false);
        if(q->has_navigation && !q->navigation.urgent){q->has_navigation=false;q->dropped++;}}
    unlock(q);
}
