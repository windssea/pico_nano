/* SPDX-License-Identifier: Apache-2.0
 * 中文：诊断端口和嵌套scope均属调用线程。/ English: diagnostic ports and nested scopes belong to the calling thread. */
#include "pn_profile.h"
static _Thread_local pn_profile_clock_fn clock_port;
static _Thread_local pn_profile_emit_fn emit_port;
static _Thread_local void *port_ctx;
static _Thread_local pn_profile_span_t *current;
void pn_profile_port(pn_profile_clock_fn clock,pn_profile_emit_fn emit,void *ctx){if(current)return;clock_port=clock;emit_port=emit;port_ctx=ctx;}
uint64_t pn_profile_time(void){return clock_port && emit_port?clock_port(port_ctx):0;}
void pn_profile_observe(pn_pool_t *pool,const char *phase,pn_status_t status,uint64_t elapsed,const pn_profile_io_t *io){
    if(!clock_port || !emit_port)return;
    pn_profile_event_t e={.phase=phase,.elapsed_us=elapsed,.status=status,.pool=pool,.begin_bytes=pool?pool->used:0,.end_bytes=pool?pool->used:0,.peak_bytes=pool?pool->used:0};
    if(io)e.io=*io;
    emit_port(port_ctx,&e);
}
void pn_profile_file(pn_pool_t *pool,const char *phase,pn_status_t status,const char *resource){
    if(!clock_port || !emit_port)return;
    pn_profile_event_t event={.phase=phase,.status=status,.pool=pool,.resource=resource};
    if(pool)event.begin_bytes=event.end_bytes=event.peak_bytes=pool->used;
    emit_port(port_ctx,&event);
}
void pn_profile_position(pn_pool_t *pool,const char *phase,pn_status_t status,const pn_profile_position_t *position){
    if(!clock_port || !emit_port)return;
    pn_profile_event_t event={.phase=phase,.status=status,.pool=pool,.position=position};
    emit_port(port_ctx,&event);
}
void pn_profile_begin(pn_profile_span_t *s,pn_pool_t *pool,const char *phase){
    *s=(pn_profile_span_t){0};if(!clock_port || !emit_port)return;
    s->previous=current;s->pool=pool;s->phase=phase;s->started=clock_port(port_ctx);s->begin_bytes=pool?pool->used:0;s->peak_bytes=s->begin_bytes;s->attempts=pool?pool->attempts:0;s->active=true;current=s;
}
void pn_profile_allocation(pn_pool_t *pool){for(pn_profile_span_t *s=current;s;s=s->previous)if(s->pool==pool && pool->used>s->peak_bytes)s->peak_bytes=pool->used;}
void pn_profile_end(pn_profile_span_t *s,pn_status_t status){
    if(!s->active || current!=s)return;
    uint64_t end=clock_port(port_ctx);current=s->previous;s->active=false;
    pn_cache_snapshot(s->pool);
    pn_profile_event_t event={.phase=s->phase,.elapsed_us=end>=s->started?end-s->started:0,.status=status,.begin_bytes=s->begin_bytes,.end_bytes=s->pool?s->pool->used:0,.peak_bytes=s->peak_bytes,.attempts=s->pool?s->pool->attempts-s->attempts:0,.pool=s->pool};emit_port(port_ctx,&event);
}
