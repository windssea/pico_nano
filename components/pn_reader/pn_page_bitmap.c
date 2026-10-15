/* SPDX-License-Identifier: Apache-2.0
 * 中文：固定阅读画布的分片预绘制缓存，池和回收契约与前台共用。
 * English: incremental prerender cache for the fixed reader canvas, sharing foreground pool/reclamation contracts. */
#include "pn_page_bitmap.h"
#include "pn_cancel.h"
#include <string.h>
#define PAGE_BYTES (684u*1216u/2u)
static size_t bytes(void *ctx){return pn_alloc_size(((pn_page_bitmap_t *)ctx)->frame.pixels);}
void pn_page_bitmap_invalidate(pn_page_bitmap_t *b){if(b){b->cursor=b->total=0;b->missing=0;b->working=b->ready=false;}}
static void drop(void *ctx){pn_page_bitmap_t *b=ctx;pn_free(b->frame.pixels);b->frame=(pn_frame_t){0};pn_page_bitmap_invalidate(b);}
void pn_page_bitmap_init(pn_page_bitmap_t *b,pn_pool_t *pool){if(b && pool && !b->owner.pool){b->enabled=true;pn_cache_attach(pool,&b->owner,PN_CACHE_BITMAP,b,bytes,drop);}}
void pn_page_bitmap_enable(pn_page_bitmap_t *b,bool enabled){if(b){b->enabled=enabled;if(!enabled)drop(b);}}
pn_status_t pn_page_bitmap_begin(pn_page_bitmap_t *b,size_t total){
    if(!b || !b->owner.pool || total>4096)return PN_INVALID;
    pn_page_bitmap_invalidate(b);if(!b->enabled || !total)return PN_EMPTY;
    if(!b->frame.pixels){uint8_t *pixels=pn_cache_alloc(b->owner.pool,PN_CACHE_BITMAP,PN_MEM_FRAME,PAGE_BYTES);if(!pixels){b->stats.denied++;return PN_EMPTY;}(void)pn_frame_bind(&b->frame,pixels,PAGE_BYTES,684,1216);}
    pn_frame_clear(&b->frame,15);b->total=total;b->working=true;return PN_OK;
}
pn_status_t pn_page_bitmap_step(pn_page_bitmap_t *b,pn_page_bitmap_paint_fn paint,void *ctx,size_t budget){
    if(!b || !paint || !budget || budget>64)return PN_INVALID;
    if(!b->enabled || !b->working || !b->frame.pixels || b->ready)return PN_EMPTY;
    pn_cache_pin(&b->owner);pn_status_t s=pn_cancel_checkpoint();size_t count=b->total-b->cursor;if(count>budget)count=budget;
    if(s==PN_OK)s=paint(ctx,&b->frame,b->cursor,count,&b->missing);
    if(s==PN_OK)s=pn_cancel_checkpoint();
    if(s==PN_OK){b->cursor+=count;b->stats.steps++;if(b->cursor==b->total){b->ready=true;b->working=false;b->stats.completed++;}}
    else pn_page_bitmap_invalidate(b);
    pn_cache_unpin(&b->owner);return s;
}
pn_status_t pn_page_bitmap_copy(pn_page_bitmap_t *b,pn_frame_t *to,unsigned *missing){
    if(!b || !to || !missing || !to->pixels || to->width!=684 || to->height!=1216 || to->stride<342)return PN_INVALID;
    if(!b->ready || !b->frame.pixels)return PN_EMPTY;
    pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    pn_cache_pin(&b->owner);for(int y=0;y<1216;y++)memcpy(to->pixels+(size_t)y*to->stride,b->frame.pixels+(size_t)y*b->frame.stride,342);
    *missing=b->missing;b->stats.hits++;pn_cache_unpin(&b->owner);return PN_OK;
}
void pn_page_bitmap_close(pn_page_bitmap_t *b){if(b){pn_cache_detach(&b->owner);drop(b);}}
pn_page_bitmap_stats_t pn_page_bitmap_stats(const pn_page_bitmap_t *b){pn_page_bitmap_stats_t s=b?b->stats:(pn_page_bitmap_stats_t){0};if(b)s.bytes=bytes((void *)b);return s;}
