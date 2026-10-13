/* SPDX-License-Identifier: Apache-2.0
 * 中文：校验大块读取、短读、预算退路与失败不提交摘要。
 * English: verify larger reads, short reads, budget fallback and unchanged digests on failure. */
#include "pn_identity.h"
#include "pn_alloc.h"
#include "pn_cancel.h"
#include <assert.h>
#include <string.h>
typedef struct {unsigned calls;size_t maximum,short_cap;uint64_t read_bytes;pn_status_t fault;} source_t;
static pn_status_t read_bytes(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){
    source_t *s=ctx;s->calls++;if(cap>s->maximum)s->maximum=cap;
    if(s->fault!=PN_OK)return s->fault;
    if(s->short_cap && cap>s->short_cap)cap=s->short_cap;
    for(size_t i=0;i<cap;i++)out[i]=(uint8_t)((at+i)*37u);
    *n=cap;s->read_bytes+=cap;return PN_OK;
}
static pn_status_t scan(pn_pool_t *p,source_t *s,pn_book_id_t *out){
    return pn_identity_stream_pooled(p,s,65549,read_bytes,out);
}
static bool cancelled(void *ctx){(void)ctx;return true;}
static pn_status_t failed_port(void *ctx,uint64_t size,pn_identity_read_fn read,uint8_t *buffer,size_t cap,pn_book_id_t *out){
    (void)ctx;assert(size==65549 && read==read_bytes && buffer && cap==32768);
    memset(out,0,sizeof *out);return PN_IO;
}
int main(void){
    pn_pool_t pool;assert(!pn_pool_init(&pool,64u*1024u,NULL,NULL,NULL));uint8_t bytes[65549];
    for(size_t i=0;i<sizeof bytes;i++)bytes[i]=(uint8_t)(i*37u);
    pn_book_id_t expected,actual;assert(pn_identity_bytes(bytes,sizeof bytes,&expected)==PN_OK);source_t source={0};
    assert(scan(&pool,&source,&actual)==PN_OK && !memcmp(&expected,&actual,sizeof actual));
    assert(source.calls==3 && source.maximum==32768 && source.read_bytes==65549 && !pool.used && !pool.live);
    pool.fail_at=pool.attempts+1;source=(source_t){0};
    assert(scan(&pool,&source,&actual)==PN_OK && !memcmp(&expected,&actual,sizeof actual));
    assert(source.calls==17 && source.maximum==4096 && !pool.used && !pool.live);pool.fail_at=0;
    pn_pool_t small;assert(!pn_pool_init(&small,16u*1024u,NULL,NULL,NULL));source=(source_t){0};
    assert(scan(&small,&source,&actual)==PN_OK && !memcmp(&expected,&actual,sizeof actual) && source.calls==17 && !small.used && !small.live);
    source=(source_t){.short_cap=997};assert(scan(&pool,&source,&actual)==PN_OK && !memcmp(&expected,&actual,sizeof actual) && source.read_bytes==65549);
    source=(source_t){.fault=PN_STALE_MEDIA};actual=expected;assert(scan(&pool,&source,&actual)==PN_STALE_MEDIA && !memcmp(&expected,&actual,sizeof actual));
    source=(source_t){.fault=PN_IO};assert(scan(&pool,&source,&actual)==PN_IO && !memcmp(&expected,&actual,sizeof actual));
    pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,NULL);source=(source_t){0};
    assert(scan(&pool,&source,&actual)==PN_CANCELLED && !memcmp(&expected,&actual,sizeof actual) && !source.calls);pn_cancel_leave(&scope);
    pn_identity_stream_port(failed_port);assert(scan(&pool,&source,&actual)==PN_IO && !memcmp(&expected,&actual,sizeof actual));pn_identity_stream_port(NULL);
    assert(!pool.used && !pool.live);return 0;
}
