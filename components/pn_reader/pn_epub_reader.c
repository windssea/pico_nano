/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：原生EPUB前后页与跨章导航，只确认真实显示的语义位置。
 * English: native EPUB page/chapter navigation confirming only actually displayed semantic locations.
 * 冻结：无永久页号，冷上一页按事件顺序重建，不对locator元组排序。
 * Frozen: no permanent page numbers; cold previous pages reconstruct event order without sorting locator tuples.
 */
#include "pn_profile.h"
#include "pn_epub_reader.h"
#include "pn_cancel.h"
#include <string.h>
_Static_assert(PN_ZIP_PATH_MAX==PN_EPUB_LOCATION_PATH_MAX,"Semantic/publication path bounds must match");
typedef struct {
    pn_cache_owner_t cache; ///< 邻页借用保护 / Neighbor borrowing protection
    pn_page_index_t *index;
    pn_pool_t *pool;pn_epub_t *epub;pn_book_id_t book;uint8_t salt[16];pn_layout_t layout;pn_epub_measure_t measure;pn_epub_page_t *page;
    pn_job_token_t token;uint64_t next_ticket,last_now;pn_epub_reader_receipt_t pending;pn_epub_reader_anchor_t visible;
    pn_epub_location_t history[PN_READER_HISTORY];size_t history_count,spine_count;bool preparing,has_visible;
    pn_epub_confirm_fn confirm;void *ctx;
    pn_epub_page_t ahead,behind; ///< 有界邻页节点 / Bounded neighbor nodes
    pn_epub_reader_anchor_t ahead_anchor,behind_anchor; ///< 缓存语义锚点 / Cached semantic anchors
    pn_epub_location_t ahead_target; ///< 下一页请求键，区分章节入口与首字位置 / Next-request key distinguishing chapter entry from first glyph
    bool cache_enabled; ///< shadow预排禁用递归缓存 / Shadow prefetch disables recursive caching
} reader_t;
static void cached_free(pn_epub_page_t *page){pn_free(page->nodes);pn_free(page->images);*page=(pn_epub_page_t){0};}
static size_t neighbor_bytes(void *ctx){reader_t *r=ctx;return pn_alloc_size(r->ahead.nodes)+pn_alloc_size(r->ahead.images)+pn_alloc_size(r->behind.nodes)+pn_alloc_size(r->behind.images);}
static void neighbor_drop(void *ctx){reader_t *r=ctx;cached_free(&r->ahead);cached_free(&r->behind);}
static bool cached_store(reader_t *r,pn_epub_page_t *to,const pn_epub_page_t *from){
    size_t need=from->count*sizeof(pn_epub_page_node_t)+from->image_count*sizeof(pn_epub_page_image_t);
    if(need && !pn_cache_allow(r->pool,PN_CACHE_NEIGHBOR,need+(from->count && from->image_count?pn_alloc_overhead():0)))return false;
    pn_epub_page_node_t *nodes=from->count?pn_cache_alloc(r->pool,PN_CACHE_NEIGHBOR,PN_MEM_LAYOUT,from->count*sizeof *nodes):NULL;
    pn_epub_page_image_t *images=from->image_count?pn_cache_alloc(r->pool,PN_CACHE_NEIGHBOR,PN_MEM_LAYOUT,from->image_count*sizeof *images):NULL;
    if((from->count && !nodes) || (from->image_count && !images)){pn_free(nodes);pn_free(images);return false;}
    if(nodes)memcpy(nodes,from->nodes,from->count*sizeof *nodes);
    if(images)memcpy(images,from->images,from->image_count*sizeof *images);
    cached_free(to);*to=*from;to->nodes=nodes;to->images=images;to->capacity=from->count;to->image_capacity=from->image_count;to->owner=r->pool;return true;
}
static bool layout_valid(const pn_layout_t *l){return l && l->width>0 && l->width<=4096 && l->height>0 && l->height<=4096 && l->line_height>0 && l->line_height<=l->height && l->indent>=0 && l->indent<l->width && l->paragraph_gap>=0 && l->paragraph_gap<=4096 && l->letter_spacing_64>=0 && l->letter_spacing_64<=4096;}
static bool same_position(pn_xhtml_position_t a,pn_xhtml_position_t b){return a.element==b.element && a.run==b.run && a.offset==b.offset && a.kind==b.kind;}
static bool same_location(const pn_epub_location_t *a,const pn_epub_location_t *b){return memchr(a->path,0,sizeof a->path) && memchr(b->path,0,sizeof b->path) && !strcmp(a->path,b->path) && a->version==b->version && a->chapter_start==b->chapter_start && same_position(a->position,b->position);}
static bool same_anchor(const pn_epub_reader_anchor_t *a,const pn_epub_reader_anchor_t *b){return a->has_next==b->has_next && same_location(&a->begin,&b->begin) && (!a->has_next || same_location(&a->next,&b->next));}
static void location(pn_epub_location_t *out,const char *path,const pn_xhtml_position_t *position){
    char saved[PN_ZIP_PATH_MAX];strcpy(saved,path);
    *out=(pn_epub_location_t){.version=PN_XHTML_LOCATOR_VERSION,.chapter_start=position==NULL};strcpy(out->path,saved);if(position)out->position=*position;
}
static pn_status_t check_location(reader_t *r,const pn_epub_location_t *l,size_t *index){
    if(!l || !memchr(l->path,0,sizeof l->path) || !*l->path)return PN_INVALID;
    if(l->version!=PN_XHTML_LOCATOR_VERSION)return PN_UNSUPPORTED;
    if(l->position.kind!=PN_XHTML_ELEMENT && l->position.kind!=PN_XHTML_TEXT_POSITION)return PN_INVALID;
    if(l->chapter_start){if(l->position.element || l->position.run || l->position.offset || l->position.kind!=PN_XHTML_ELEMENT)return PN_INVALID;}
    else if(!l->position.element || (l->position.kind==PN_XHTML_ELEMENT && (l->position.run || l->position.offset)))return PN_INVALID;
    pn_status_t status=pn_epub_spine_find(r->epub,l->path,index);return status==PN_EMPTY?PN_INVALID:status;
}
static pn_status_t linear(reader_t *r,size_t index,bool forward,pn_epub_location_t *out){
    while(index<r->spine_count){pn_epub_item_t item;pn_status_t status=pn_epub_spine(r->epub,index,&item);if(status!=PN_OK)return status;
        if(item.linear){location(out,item.path,NULL);return PN_OK;}
        if(forward)index++;else{if(!index)break;index--;}
    }
    return PN_EMPTY;
}
static pn_status_t next_chapter(reader_t *r,const char *path,bool forward,pn_epub_location_t *out){
    size_t index;pn_status_t status=pn_epub_spine_find(r->epub,path,&index);if(status!=PN_OK)return status;
    if(!forward && !index)return PN_EMPTY;
    return linear(r,forward?index+1:index-1,forward,out);
}
static pn_status_t load(reader_t *r,const pn_epub_location_t *at,const pn_xhtml_position_t *stop){
    const pn_xhtml_position_t *start=at->chapter_start?NULL:&at->position;
    pn_status_t status=stop?pn_epub_page_prepare_until(r->pool,r->epub,at->path,r->salt,start,stop,&r->layout,&r->measure,r->page):pn_epub_page_prepare(r->pool,r->epub,at->path,r->salt,start,&r->layout,&r->measure,r->page);
    if(status==PN_OK){size_t section;if(pn_epub_spine_find(r->epub,at->path,&section)==PN_OK){pn_page_index_entry_t e={.section=(uint32_t)section,.begin=r->page->begin_order,.end=r->page->has_next?r->page->next_order:UINT64_MAX,.first=r->page->begin,.next=r->page->next,.flags=r->page->has_next?1u:0u};pn_page_index_add(r->index,&e);}}
    return status;
}
static pn_status_t forward(reader_t *r,pn_epub_location_t *at){
    for(size_t i=0;i<=r->spine_count;i++){
        pn_status_t status=load(r,at,NULL);if(status!=PN_EMPTY)return status;
        status=next_chapter(r,at->path,true,at);if(status!=PN_OK)return status;
    }
    return PN_LIMIT;
}
static pn_status_t last(reader_t *r,pn_epub_location_t *at){
    pn_epub_location_t saved={0};bool have=false;
    size_t section;pn_page_index_entry_t e;bool indexed=pn_epub_spine_find(r->epub,at->path,&section)==PN_OK && pn_page_index_near(r->index,(uint32_t)section,UINT64_MAX,false,&e);
    if(indexed && e.end==UINT64_MAX)location(at,at->path,&e.first);
    else{pn_xhtml_position_t start;uint64_t order,start_order;pn_status_t status=pn_xhtml_window_start(r->pool,r->epub,at->path,NULL,r->salt,&order,&start_order,&start);if(status!=PN_OK)return status;if(indexed && e.begin>=start_order)location(at,at->path,&e.first);else if(start.element)location(at,at->path,&start);}
    for(;;){pn_status_t status=load(r,at,NULL);
        if(status==PN_EMPTY && have){*at=saved;return load(r,at,NULL);}
        if(status!=PN_OK)return status;
        location(&saved,at->path,&r->page->begin);have=true;
        if(!r->page->has_next)return PN_OK;
        if(r->page->next_order<=r->page->begin_order)return PN_CORRUPT;
        location(at,at->path,&r->page->next);
    }
}
static pn_status_t previous(reader_t *r,pn_epub_location_t *at){
    pn_status_t status;
    if(r->history_count){*at=r->history[r->history_count-1];return load(r,at,!strcmp(at->path,r->visible.begin.path)?&r->visible.begin.position:NULL);}
    location(at,r->visible.begin.path,NULL);
    // 当前事件顺序由完整源重新核对，不按locator元组猜测。/ Revalidate current event order against the full source, never infer it from locator tuples.
    if(!r->visible.begin.chapter_start){uint64_t order,start_order;size_t section;pn_xhtml_position_t start;
        status=pn_epub_spine_find(r->epub,at->path,&section);if(status!=PN_OK)return status;
        status=pn_xhtml_window_start(r->pool,r->epub,at->path,&r->visible.begin.position,r->salt,&order,&start_order,&start);if(status!=PN_OK)return status;
        pn_page_index_entry_t e;if(pn_page_index_near(r->index,(uint32_t)section,order,true,&e) && (e.end==order || e.begin>=start_order))location(at,at->path,&e.first);
        else if(start.element)location(at,at->path,&start);
    }
    // 以当前首位置为硬终点，不因重排后的自然页边界而覆盖当前首字。/ Use current beginning as a hard stop so natural reflow boundaries never overlap its first glyph.
    for(;;){status=load(r,at,&r->visible.begin.position);if(status==PN_EMPTY)break;if(status!=PN_OK)return status;
        if(r->page->has_next && same_position(r->page->next,r->visible.begin.position))return PN_OK;
        if(!r->page->has_next || r->page->next_order<=r->page->begin_order)return PN_CORRUPT;
        location(at,at->path,&r->page->next);
    }
    for(size_t i=0;i<r->spine_count;i++){
        pn_epub_location_t before;status=next_chapter(r,at->path,false,&before);if(status!=PN_OK)return status;*at=before;
        status=last(r,at);if(status!=PN_EMPTY)return status;
    }
    return PN_LIMIT;
}
static pn_status_t anchor(reader_t *r,const char *path,pn_epub_reader_anchor_t *out){
    pn_epub_reader_anchor_t value={0};location(&value.begin,path,&r->page->begin);
    if(r->page->has_next){if(r->page->next_order<=r->page->begin_order)return PN_CORRUPT;location(&value.next,path,&r->page->next);value.has_next=true;}
    else{pn_status_t status=next_chapter(r,path,true,&value.next);if(status!=PN_OK && status!=PN_EMPTY)return status;value.has_next=status==PN_OK;}
    *out=value;return PN_OK;
}
pn_status_t pn_epub_reader_init(pn_epub_reader_t *handle,pn_pool_t *pool,pn_epub_t *epub,const pn_book_id_t *book,const uint8_t salt[16],const pn_layout_t *layout,const pn_epub_measure_t *measure,pn_epub_page_t *page,pn_job_token_t token,pn_epub_confirm_fn confirm,void *ctx){
    if(!handle || handle->impl || !pool || !epub || !book || !salt || !layout_valid(layout) || !measure || !measure->glyph || !page || !page->nodes || !page->capacity || page->capacity>PN_PAGE_GLYPHS_MAX || page->image_capacity>PN_EPUB_PAGE_IMAGES_MAX || (page->image_capacity && !page->images) || !token.session || !token.generation)return PN_INVALID;
    bool random=false;for(unsigned i=0;i<16;i++)random=random || salt[i]!=0;if(!random)return PN_INVALID;
    pn_epub_info_t info;pn_status_t status=pn_epub_info(epub,&info);if(status!=PN_OK)return status;if(info.fixed_layout)return PN_UNSUPPORTED;
    reader_t *r=pn_alloc_tagged(pool,PN_MEM_LAYOUT,sizeof *r);if(!r)return PN_NO_MEMORY;
    *r=(reader_t){.pool=pool,.epub=epub,.book=*book,.layout=*layout,.measure=*measure,.page=page,.token=token,.next_ticket=1,.spine_count=info.spine_count,.confirm=confirm,.ctx=ctx,.cache_enabled=true};memcpy(r->salt,salt,16);page->valid=false;pn_cache_attach(pool,&r->cache,PN_CACHE_NEIGHBOR,r,neighbor_bytes,neighbor_drop);handle->impl=r;return PN_OK;
}
void pn_epub_reader_close(pn_epub_reader_t *handle){if(handle && handle->impl){reader_t *r=handle->impl;pn_cache_detach(&r->cache);r->page->valid=false;cached_free(&r->ahead);cached_free(&r->behind);pn_free(r);handle->impl=NULL;}}
static pn_status_t pn_epub_reader_prefetch_impl(pn_epub_reader_t *handle){
    if(!handle || !handle->impl)return PN_EMPTY;
    reader_t *r=handle->impl;if(!r->has_visible || !r->visible.has_next || r->preparing)return PN_EMPTY;
    if(r->ahead.valid && same_location(&r->ahead_target,&r->visible.next))return PN_EMPTY;
    pn_epub_page_t page={0};pn_status_t allocated=pn_epub_page_reserve(&page,r->pool,64,0);
    if(allocated!=PN_OK){cached_free(&page);return allocated;}
    reader_t copy=*r;copy.cache=(pn_cache_owner_t){0};copy.page=&page;copy.cache_enabled=false;copy.preparing=false;copy.confirm=NULL;
    pn_epub_reader_t shadow={&copy};pn_epub_reader_receipt_t receipt;
    pn_status_t status=pn_epub_reader_prepare(&shadow,PN_READ_NEXT,NULL,&receipt);
    if(status==PN_OK)status=pn_cancel_checkpoint();
    // 已验证页直接转移所有权，避免scratch到ahead的第二份分配与复制。/ Move the validated page instead of allocating and copying a second scratch-to-ahead buffer.
    if(status==PN_OK){cached_free(&r->ahead);r->ahead=page;page=(pn_epub_page_t){0};r->ahead_anchor=receipt.anchor;r->ahead_target=r->visible.next;}
    cached_free(&page);return status;
}
pn_status_t pn_epub_reader_prefetch(pn_epub_reader_t *handle){
    pn_profile_span_t span;pn_profile_begin(&span,handle && handle->impl?((reader_t *)handle->impl)->pool:NULL,"epub.prefetch");
    reader_t *r=handle && handle->impl?handle->impl:NULL;if(r){pn_cache_pin(&r->cache);pn_cache_speculate(r->pool,true);}
    pn_status_t status=pn_epub_reader_prefetch_impl(handle);if(r){pn_cache_speculate(r->pool,false);pn_cache_unpin(&r->cache);}pn_profile_end(&span,status);return status;
}
pn_status_t pn_epub_reader_prepare_impl(pn_epub_reader_t *handle,pn_read_intent_t intent,const pn_epub_location_t *jump,pn_epub_reader_receipt_t *out){
    if(!handle || !handle->impl || !out || intent<PN_READ_FIRST || intent>PN_READ_JUMP)return PN_INVALID;
    reader_t *r=handle->impl;if(r->preparing)return PN_BUSY;if(!r->next_ticket)return PN_LIMIT;
    if(!r->has_visible && intent!=PN_READ_FIRST && intent!=PN_READ_JUMP)return PN_EMPTY;
    pn_epub_page_t *cached=NULL;pn_epub_reader_anchor_t prepared={0};bool saved_forward=false;
    if(r->cache_enabled){
        if(intent==PN_READ_NEXT && r->ahead.valid && r->visible.has_next && same_location(&r->ahead_target,&r->visible.next)){cached=&r->ahead;prepared=r->ahead_anchor;}
        if(intent==PN_READ_PREVIOUS && r->behind.valid && r->history_count && same_location(&r->behind_anchor.begin,&r->history[r->history_count-1])){cached=&r->behind;prepared=r->behind_anchor;}
        pn_cache_observe(r->pool,PN_CACHE_NEIGHBOR,cached!=NULL);
        if(cached && r->page->owner==r->pool && cached->owner==r->pool){
            size_t section;pn_status_t checked=check_location(r,&prepared.begin,&section);if(checked!=PN_OK)return checked;
            // 命中自有节点时直接交接，固定外部缓冲仍走复制兼容路径。/ Transfer owned nodes on hits; fixed external buffers keep the copying compatibility path.
            if(r->page->valid && r->has_visible && same_position(r->page->begin,r->visible.begin.position)){
                pn_epub_page_t *opposite=intent==PN_READ_PREVIOUS?&r->ahead:&r->behind;cached_free(opposite);*opposite=*r->page;
                if(intent==PN_READ_PREVIOUS){r->ahead_anchor=r->visible;r->ahead_target=prepared.next;}else r->behind_anchor=r->visible;
            }else pn_epub_page_release(r->page);
            *r->page=*cached;*cached=(pn_epub_page_t){0};
            if(!pn_cache_allow(r->pool,PN_CACHE_NEIGHBOR,1))neighbor_drop(r);
            r->pending=(pn_epub_reader_receipt_t){handle,r->token,r->next_ticket++,intent,prepared};r->preparing=true;*out=r->pending;return PN_OK;
        }
        if((intent==PN_READ_NEXT || intent==PN_READ_PREVIOUS) && r->page->valid && r->has_visible && same_position(r->page->begin,r->visible.begin.position)){pn_epub_page_t *slot=intent==PN_READ_PREVIOUS?&r->ahead:&r->behind;if(slot!=cached && cached_store(r,slot,r->page)){if(slot==&r->ahead){r->ahead_anchor=r->visible;saved_forward=true;}else r->behind_anchor=r->visible;}}
    }
    if(intent!=PN_READ_NEXT && intent!=PN_READ_PREVIOUS){cached_free(&r->ahead);cached_free(&r->behind);}
    if(cached && cached->count<=r->page->capacity && cached->image_count<=r->page->image_capacity){
        size_t index;pn_status_t status=check_location(r,&prepared.begin,&index);if(status!=PN_OK)return status;
        pn_epub_page_node_t *nodes=r->page->nodes;pn_epub_page_image_t *images=r->page->images;size_t cap=r->page->capacity,icap=r->page->image_capacity;pn_pool_t *owner=r->page->owner;
        if(cached->count)memcpy(nodes,cached->nodes,cached->count*sizeof *nodes);
        if(cached->image_count)memcpy(images,cached->images,cached->image_count*sizeof *images);
        *r->page=*cached;r->page->nodes=nodes;r->page->images=images;r->page->capacity=cap;r->page->image_capacity=icap;r->page->owner=owner;
        if(saved_forward)r->ahead_target=prepared.next;
        r->pending=(pn_epub_reader_receipt_t){handle,r->token,r->next_ticket++,intent,prepared};r->preparing=true;*out=r->pending;return PN_OK;
    }
    r->page->valid=false;pn_epub_location_t at;pn_status_t status=PN_OK;
    if(intent==PN_READ_FIRST)status=linear(r,0,true,&at);
    else if(intent==PN_READ_NEXT){if(!r->visible.has_next)return PN_EMPTY;at=r->visible.next;}
    else if(intent==PN_READ_CURRENT)at=r->visible.begin;
    else if(intent==PN_READ_JUMP){size_t index;status=check_location(r,jump,&index);if(status==PN_OK){at=*jump;if(!at.chapter_start){uint64_t order;status=pn_xhtml_order(r->pool,r->epub,at.path,&at.position,r->salt,&order);if(status==PN_EMPTY)status=PN_INVALID;}}}
    if(intent==PN_READ_PREVIOUS)status=previous(r,&at);
    else if(status==PN_OK)status=forward(r,&at);
    pn_epub_reader_anchor_t candidate;
    if(status==PN_OK)status=anchor(r,at.path,&candidate);
    if(status!=PN_OK){r->page->valid=false;return status;}
    if(saved_forward)r->ahead_target=candidate.next;
    r->pending=(pn_epub_reader_receipt_t){handle,r->token,r->next_ticket++,intent,candidate};r->preparing=true;*out=r->pending;return PN_OK;
}
pn_status_t pn_epub_reader_prepare(pn_epub_reader_t *handle,pn_read_intent_t intent,const pn_epub_location_t *jump,pn_epub_reader_receipt_t *out){
    pn_profile_span_t span;pn_profile_begin(&span,handle && handle->impl?((reader_t *)handle->impl)->pool:NULL,intent==PN_READ_NEXT?"epub.next":intent==PN_READ_PREVIOUS?"epub.previous":intent==PN_READ_JUMP?"epub.jump":"epub.prepare");
    reader_t *r=handle && handle->impl?handle->impl:NULL;if(r)pn_cache_reclaim(r->pool,r->pool->cache_reserve);
    bool borrowing=r && r->cache_enabled && ((intent==PN_READ_NEXT && r->ahead.valid) || (intent==PN_READ_PREVIOUS && r->behind.valid));
    if(borrowing)pn_cache_pin(&r->cache);
    pn_status_t status=pn_epub_reader_prepare_impl(handle,intent,jump,out);if(borrowing)pn_cache_unpin(&r->cache);pn_profile_end(&span,status);return status;
}
pn_status_t pn_epub_reader_page(pn_epub_reader_t *handle,const pn_epub_page_t **out){
    if(!handle || !handle->impl || !out)return PN_INVALID;
    reader_t *r=handle->impl;if(!r->page->valid)return PN_EMPTY;
    size_t index;pn_status_t status=check_location(r,r->preparing?&r->pending.anchor.begin:&r->visible.begin,&index);if(status!=PN_OK)return status;
    *out=r->page;return PN_OK;
}
pn_status_t pn_epub_reader_progress(pn_epub_reader_t *handle,pn_epub_progress_t *out){
    if(!handle || !handle->impl || !out)return PN_INVALID;
    reader_t *r=handle->impl;if(!r->has_visible)return PN_EMPTY;size_t index;pn_status_t status=check_location(r,&r->visible.begin,&index);if(status!=PN_OK)return status;
    *out=(pn_epub_progress_t){r->book,r->visible.begin};return PN_OK;
}
static pn_status_t complete_impl(pn_epub_reader_t *handle,const pn_epub_reader_receipt_t *receipt,bool success,uint64_t now){
    if(!handle || !handle->impl || !receipt || receipt->owner!=handle)return PN_INVALID;
    reader_t *r=handle->impl;if(receipt->token.session!=r->token.session || receipt->token.generation!=r->token.generation)return PN_STALE_JOB;
    if(!r->preparing || receipt->ticket!=r->pending.ticket || receipt->intent!=r->pending.intent || !same_anchor(&receipt->anchor,&r->pending.anchor) || now<r->last_now)return PN_INVALID;
    r->preparing=false;r->last_now=now;if(!success){r->page->valid=false;return PN_IO;}
    size_t index;pn_status_t status=check_location(r,&receipt->anchor.begin,&index);if(status!=PN_OK){r->page->valid=false;return status;}
    if(receipt->intent==PN_READ_NEXT && r->has_visible){if(r->history_count==PN_READER_HISTORY){memmove(r->history,r->history+1,(PN_READER_HISTORY-1)*sizeof r->history[0]);r->history_count--;}
        r->history[r->history_count++]=r->visible.begin;}
    else if(receipt->intent==PN_READ_PREVIOUS){if(r->history_count)r->history_count--;}
    else if(receipt->intent==PN_READ_FIRST || receipt->intent==PN_READ_JUMP)r->history_count=0;
    r->visible=receipt->anchor;r->has_visible=true;
    if(r->confirm){pn_epub_progress_t progress={r->book,r->visible.begin};return r->confirm(r->ctx,&progress,receipt->intent==PN_READ_NEXT || receipt->intent==PN_READ_PREVIOUS,now);}
    return PN_OK;
}
pn_status_t pn_epub_reader_reflow(pn_epub_reader_t *handle,const pn_layout_t *layout){
    if(!handle || !handle->impl || !layout_valid(layout))return PN_INVALID;
    reader_t *r=handle->impl;
    if(r->token.generation==UINT32_MAX)return PN_LIMIT;
    r->layout=*layout;r->token.generation++;r->index=NULL;r->preparing=false;r->page->valid=false;r->history_count=0;cached_free(&r->ahead);cached_free(&r->behind);return PN_OK;
}
pn_status_t pn_epub_reader_complete(pn_epub_reader_t *handle,const pn_epub_reader_receipt_t *receipt,bool success,uint64_t now){
    // 显示回执提交屏障不接受输入取消，仍验证实际介质。/ The display-receipt commit barrier ignores input cancellation but validates actual media.
    pn_cancel_scope_t shield;pn_cancel_enter(&shield,NULL,NULL);
    pn_status_t status=complete_impl(handle,receipt,success,now);pn_cancel_leave(&shield);return status;
}
void pn_epub_reader_trim(pn_epub_reader_t *handle){if(handle && handle->impl){reader_t *r=handle->impl;cached_free(&r->ahead);cached_free(&r->behind);}}
void pn_epub_reader_index(pn_epub_reader_t *handle,pn_page_index_t *index){if(handle && handle->impl)((reader_t *)handle->impl)->index=index;}
