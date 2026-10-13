/* SPDX-License-Identifier: Apache-2.0
 * 中文：诊断端口和嵌套scope均属调用线程。/ English: diagnostic ports and nested scopes belong to the calling thread. */
#include "pn_profile.h"
static _Thread_local pn_profile_clock_fn clock_port;
static _Thread_local pn_profile_emit_fn emit_port;
static _Thread_local void *port_ctx;
static _Thread_local pn_profile_span_t *current;
void pn_profile_port(pn_profile_clock_fn clock,pn_profile_emit_fn emit,void *ctx){if(current)return;clock_port=clock;emit_port=emit;port_ctx=ctx;}
void pn_profile_begin(pn_profile_span_t *s,pn_pool_t *pool,const char *phase){
    *s=(pn_profile_span_t){0};if(!clock_port || !emit_port)return;
    s->previous=current;s->pool=pool;s->phase=phase;s->started=clock_port(port_ctx);s->begin_bytes=pool?pool->used:0;s->peak_bytes=s->begin_bytes;s->attempts=pool?pool->attempts:0;s->active=true;current=s;
}
void pn_profile_allocation(pn_pool_t *pool){for(pn_profile_span_t *s=current;s;s=s->previous)if(s->pool==pool && pool->used>s->peak_bytes)s->peak_bytes=pool->used;}
void pn_profile_end(pn_profile_span_t *s,pn_status_t status){
    if(!s->active || current!=s)return;
    uint64_t end=clock_port(port_ctx);current=s->previous;s->active=false;
    pn_profile_event_t event={s->phase,end>=s->started?end-s->started:0,status,s->begin_bytes,s->pool?s->pool->used:0,s->peak_bytes,s->pool?s->pool->attempts-s->attempts:0,s->pool};emit_port(port_ctx,&event);
}
