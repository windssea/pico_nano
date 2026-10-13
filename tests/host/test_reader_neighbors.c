/* SPDX-License-Identifier: Apache-2.0
 * 中文：邻页命中、失败不提交、重排及低预算退化。/ English: neighbor hits, no commits on failure, reflow and low-budget fallback. */
#include "pn_reader.h"
#include "pn_async.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct {uint8_t text[8000];unsigned reads,measures;bool lost;pn_async_t *interrupt;unsigned stop_measure;} source_t;
static pn_status_t read_at(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){source_t *s=ctx;s->reads++;if(s->lost)return PN_STALE_MEDIA;if(cap>sizeof s->text-off)cap=sizeof s->text-off;memcpy(out,s->text+off,cap);*n=cap;return PN_OK;}
static pn_status_t validate(void *ctx){return ((source_t *)ctx)->lost?PN_STALE_MEDIA:pn_cancel_checkpoint();}
static pn_status_t measure(void *ctx,uint32_t cp,int32_t *out){(void)cp;source_t *s=ctx;s->measures++;if(s->interrupt && s->measures==s->stop_measure){pn_async_event_t e={.bytes=1};pn_async_send(s->interrupt,&e);}*out=5*64;return PN_OK;}
int main(void){
 source_t s={0};memset(s.text,'a',sizeof s.text);pn_text_source_t source={&s,sizeof s.text,read_at,validate};pn_book_id_t book={{42}};
 pn_layout_t layout={25,20,10,8,0,0,0};pn_font_metrics_t metrics={&s,measure};pn_page_glyph_t nodes[100];pn_reader_t r;pn_pool_t pool;assert(!pn_pool_init(&pool,65536,NULL,NULL,NULL));
 assert(pn_reader_init(&r,&source,PN_TEXT_UTF8,&book,&layout,&metrics,nodes,100,(pn_job_token_t){1,1},NULL)==PN_OK);pn_reader_neighbors(&r,&pool);
 pn_reader_receipt_t receipt;assert(pn_reader_prepare(&r,PN_READ_FIRST,0,&receipt)==PN_OK && pn_reader_complete(&r,&receipt,true,1)==PN_OK);
 pn_reader_anchor_t visible=r.visible;uint64_t ticket=r.next_ticket;pn_page_glyph_t saved[100];memcpy(saved,nodes,sizeof saved);
 assert(pn_reader_prefetch(&r)==PN_OK && !memcmp(&visible,&r.visible,sizeof visible) && r.next_ticket==ticket && !r.history_count && !r.preparing && !memcmp(saved,nodes,sizeof saved));
 s.reads=s.measures=0;assert(pn_reader_prepare(&r,PN_READ_NEXT,0,&receipt)==PN_OK && !s.reads && !s.measures && receipt.anchor.begin==10);assert(pn_reader_complete(&r,&receipt,false,2)==PN_IO && r.visible.begin==0);
 assert(pn_reader_prepare(&r,PN_READ_NEXT,0,&receipt)==PN_OK && !s.measures && pn_reader_complete(&r,&receipt,true,3)==PN_OK);
 assert(pn_reader_prepare(&r,PN_READ_PREVIOUS,0,&receipt)==PN_OK && !s.measures && pn_reader_complete(&r,&receipt,true,4)==PN_OK && r.visible.begin==0);
 pn_async_t q;pn_async_init(&q);pn_async_scope_t scope;pn_async_begin(&q,&scope,true);pn_async_event_t e={.bytes=1};pn_async_send(&q,&e);r.ahead.valid=false;
 assert(pn_reader_prefetch(&r)==PN_CANCELLED && !r.ahead.valid && r.visible.begin==0);pn_async_end(&scope);
 pn_async_take(&q,&e);s.interrupt=&q;s.stop_measure=3;s.measures=0;pn_async_begin(&q,&scope,true);
 assert(pn_reader_prefetch(&r)==PN_CANCELLED && s.measures==3 && !r.ahead.valid && r.visible.begin==0);pn_async_end(&scope);s.interrupt=NULL;pn_async_take(&q,&e);
 pool.fail_at=pool.attempts+1;assert(pn_reader_prefetch(&r)==PN_NO_MEMORY && r.visible.begin==0);pool.fail_at=0;
 assert(pn_reader_prefetch(&r)==PN_OK);layout.width=40;assert(pn_reader_reflow(&r,&layout)==PN_OK && !r.ahead.valid && !r.behind.valid);
 s.measures=0;assert(pn_reader_prepare(&r,PN_READ_CURRENT,0,&receipt)==PN_OK && s.measures && receipt.anchor.end==16 && pn_reader_complete(&r,&receipt,true,5)==PN_OK);
 assert(pn_reader_prefetch(&r)==PN_OK);s.lost=true;assert(pn_reader_prepare(&r,PN_READ_NEXT,0,&receipt)==PN_STALE_MEDIA && r.visible.begin==0);
 s.lost=false;assert(pn_reader_prepare(&r,PN_READ_NEXT,0,&receipt)==PN_OK);pn_async_begin(&q,&scope,false);e.urgent=true;pn_async_send(&q,&e);assert(pn_cancel_checkpoint()==PN_CANCELLED);
 assert(pn_reader_complete(&r,&receipt,true,6)==PN_OK && r.visible.begin==16 && pn_cancel_checkpoint()==PN_CANCELLED);pn_async_end(&scope);
 pn_reader_neighbors(&r,NULL);assert(!pool.used && !pool.live);puts("TXT neighbors: no speculative commit, cached forward/reverse, failure, cancel, memory, reflow and media passed");return 0;
}
