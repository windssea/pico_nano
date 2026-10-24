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
    pn_epub_page_job_t index_job;pn_epub_page_t index_page;pn_book_index_state_t index_at; ///< 独立有界编译候选，不借正文节点 / Separate bounded compiler candidate, never borrowing body nodes
    pn_epub_page_t ahead,behind; ///< 有界邻页节点 / Bounded neighbor nodes
    pn_epub_reader_anchor_t ahead_anchor,behind_anchor; ///< 缓存语义锚点 / Cached semantic anchors
    pn_epub_location_t ahead_target; ///< 下一页请求键，区分章节入口与首字位置 / Next-request key distinguishing chapter entry from first glyph
    bool cache_enabled; ///< shadow预排禁用递归缓存 / Shadow prefetch disables recursive caching
    size_t ahead_warm; ///< 邻页替换后从零预热 / Start warming from zero after neighbor replacement
    pn_epub_page_job_t prefetch;pn_epub_page_t candidate; ///< 原池内未验证候选 / Unverified candidate in the original pool
    pn_epub_location_t candidate_at,candidate_target;size_t candidate_chapters;bool prefetch_failed; ///< 跨轮目标与失败退路 / Target across ticks and failure fallback
} reader_t;
static void cached_free(pn_epub_page_t *page){pn_free(page->nodes);pn_free(page->images);*page=(pn_epub_page_t){0};}
static size_t neighbor_bytes(void *ctx){reader_t *r=ctx;return pn_alloc_size(r->ahead.nodes)+pn_alloc_size(r->ahead.images)+pn_alloc_size(r->behind.nodes)+pn_alloc_size(r->behind.images)+pn_alloc_size(r->candidate.nodes)+pn_alloc_size(r->candidate.images);}
static void prefetch_abort(reader_t *r){pn_epub_page_job_close(&r->prefetch);cached_free(&r->candidate);r->candidate_chapters=0;}
void pn_epub_reader_prefetch_abort(pn_epub_reader_t *handle){if(handle && handle->impl){reader_t *r=handle->impl;prefetch_abort(r);r->prefetch_failed=false;memset(&r->candidate_target,0,sizeof r->candidate_target);}}
static void neighbor_drop(void *ctx){reader_t *r=ctx;prefetch_abort(r);cached_free(&r->ahead);cached_free(&r->behind);r->ahead_warm=0;}
static bool cached_store(reader_t *r,pn_epub_page_t *to,const pn_epub_page_t *from){
    size_t need=from->count*sizeof(pn_epub_page_node_t)+from->image_count*sizeof(pn_epub_page_image_t);
    if(need && !pn_cache_allow(r->pool,PN_CACHE_NEIGHBOR,need+(from->count && from->image_count?pn_alloc_overhead():0)))return false;
    pn_epub_page_node_t *nodes=from->count?pn_cache_alloc(r->pool,PN_CACHE_NEIGHBOR,PN_MEM_LAYOUT,from->count*sizeof *nodes):NULL;
    pn_epub_page_image_t *images=from->image_count?pn_cache_alloc(r->pool,PN_CACHE_NEIGHBOR,PN_MEM_LAYOUT,from->image_count*sizeof *images):NULL;
    if((from->count && !nodes) || (from->image_count && !images)){pn_free(nodes);pn_free(images);return false;}
    if(nodes)memcpy(nodes,from->nodes,from->count*sizeof *nodes);
    if(images)memcpy(images,from->images,from->image_count*sizeof *images);
    cached_free(to);*to=*from;to->nodes=nodes;to->images=images;to->capacity=from->count;to->image_capacity=from->image_count;to->owner=r->pool;if(to==&r->ahead)r->ahead_warm=0;return true;
}
static bool layout_valid(const pn_layout_t *l){return l && l->width>0 && l->width<=4096 && l->height>0 && l->height<=4096 && l->line_height>0 && l->line_height<=l->height && l->indent>=0 && l->indent<l->width && l->paragraph_gap>=0 && l->paragraph_gap<=4096 && l->letter_spacing_64>=0 && l->letter_spacing_64<=4096;}
static bool same_position(pn_xhtml_position_t a,pn_xhtml_position_t b){return a.element==b.element && a.run==b.run && a.offset==b.offset && a.kind==b.kind;}
static bool same_location(const pn_epub_location_t *a,const pn_epub_location_t *b){return memchr(a->path,0,sizeof a->path) && memchr(b->path,0,sizeof b->path) && !strcmp(a->path,b->path) && a->version==b->version && a->chapter_start==b->chapter_start && same_position(a->position,b->position);}
static bool same_anchor(const pn_epub_reader_anchor_t *a,const pn_epub_reader_anchor_t *b){return a->has_next==b->has_next && same_location(&a->begin,&b->begin) && (!a->has_next || same_location(&a->next,&b->next));}
static void location(pn_epub_location_t *out,const char *path,const pn_xhtml_position_t *position){
    // 路径允许原位更新，避免每层调用再保留1024字节栈副本。/ Permit in-place paths without another 1024-byte stack copy at each call depth.
    memmove(out->path,path,strlen(path)+1);out->version=PN_XHTML_LOCATOR_VERSION;out->chapter_start=position==NULL;
    out->position=position?*position:(pn_xhtml_position_t){0};
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
static pn_status_t load_page(reader_t *r,const pn_epub_location_t *at,const pn_xhtml_position_t *stop,pn_epub_page_t *page){
    const pn_xhtml_position_t *start=at->chapter_start?NULL:&at->position;
    pn_status_t status=stop?pn_epub_page_prepare_until(r->pool,r->epub,at->path,r->salt,start,stop,&r->layout,&r->measure,page):pn_epub_page_prepare(r->pool,r->epub,at->path,r->salt,start,&r->layout,&r->measure,page);
    if(status==PN_OK){size_t section;if(pn_epub_spine_find(r->epub,at->path,&section)==PN_OK){pn_page_index_entry_t e={.section=(uint32_t)section,.begin=page->begin_order,.end=page->has_next?page->next_order:UINT64_MAX,.first=page->begin,.next=page->next,.flags=page->has_next?1u:0u};pn_page_index_add(r->index,&e);}}
    return status;
}
static pn_status_t load(reader_t *r,const pn_epub_location_t *at,const pn_xhtml_position_t *stop){return load_page(r,at,stop,r->page);}
static pn_status_t forward_page(reader_t *r,pn_epub_location_t *at,pn_epub_page_t *page){
    for(size_t i=0;i<=r->spine_count;i++){
        pn_status_t status=load_page(r,at,NULL,page);if(status!=PN_EMPTY)return status;
        status=next_chapter(r,at->path,true,at);if(status!=PN_OK)return status;
    }
    return PN_LIMIT;
}
static pn_status_t forward(reader_t *r,pn_epub_location_t *at){return forward_page(r,at,r->page);}
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
static pn_status_t anchor_page(reader_t *r,const char *path,const pn_epub_page_t *page,pn_epub_reader_anchor_t *out){
    memset(out,0,sizeof *out);location(&out->begin,path,&page->begin);
    if(page->has_next){if(page->next_order<=page->begin_order)return PN_CORRUPT;location(&out->next,path,&page->next);out->has_next=true;}
    else{pn_status_t status=next_chapter(r,path,true,&out->next);if(status!=PN_OK && status!=PN_EMPTY)return status;out->has_next=status==PN_OK;}
    return PN_OK;
}
static pn_status_t anchor(reader_t *r,const char *path,pn_epub_reader_anchor_t *out){return anchor_page(r,path,r->page,out);}
pn_status_t pn_epub_reader_init(pn_epub_reader_t *handle,pn_pool_t *pool,pn_epub_t *epub,const pn_book_id_t *book,const uint8_t salt[16],const pn_layout_t *layout,const pn_epub_measure_t *measure,pn_epub_page_t *page,pn_job_token_t token,pn_epub_confirm_fn confirm,void *ctx){
    if(!handle || handle->impl || !pool || !epub || !book || !salt || !layout_valid(layout) || !measure || !measure->glyph || !page || !page->nodes || !page->capacity || page->capacity>PN_PAGE_GLYPHS_MAX || page->image_capacity>PN_EPUB_PAGE_IMAGES_MAX || (page->image_capacity && !page->images) || !token.session || !token.generation)return PN_INVALID;
    bool random=false;for(unsigned i=0;i<16;i++)random=random || salt[i]!=0;if(!random)return PN_INVALID;
    pn_epub_info_t info;pn_status_t status=pn_epub_info(epub,&info);if(status!=PN_OK)return status;if(info.fixed_layout)return PN_UNSUPPORTED;
    reader_t *r=pn_alloc_tagged(pool,PN_MEM_LAYOUT,sizeof *r);if(!r)return PN_NO_MEMORY;
    *r=(reader_t){.pool=pool,.epub=epub,.book=*book,.layout=*layout,.measure=*measure,.page=page,.token=token,.next_ticket=1,.spine_count=info.spine_count,.confirm=confirm,.ctx=ctx,.cache_enabled=true};memcpy(r->salt,salt,16);page->valid=false;pn_cache_attach(pool,&r->cache,PN_CACHE_NEIGHBOR,r,neighbor_bytes,neighbor_drop);handle->impl=r;return PN_OK;
}
void pn_epub_reader_close(pn_epub_reader_t *handle){if(handle && handle->impl){reader_t *r=handle->impl;pn_epub_reader_index_abort(handle);prefetch_abort(r);pn_cache_detach(&r->cache);r->page->valid=false;cached_free(&r->ahead);cached_free(&r->behind);pn_free(r);handle->impl=NULL;}}
/// 邻页基准：待确认页（面板扫描期间）优先，否则已显示页；只读，不改提交状态。
/// Neighbor base: the pending page (during a panel scan) first, else the visible page; read-only, never changing committed state.
static const pn_epub_reader_anchor_t *neighbor_base(const reader_t *r){return r->preparing?&r->pending.anchor:r->has_visible?&r->visible:NULL;}
bool pn_epub_reader_next_ready(const pn_epub_reader_t *handle){const reader_t *r=handle?handle->impl:NULL;const pn_epub_reader_anchor_t *base=r?neighbor_base(r):NULL;return base && base->has_next && r->ahead.valid && same_location(&r->ahead_target,&base->next);}
bool pn_epub_reader_next_pending(const pn_epub_reader_t *handle){const reader_t *r=handle?handle->impl:NULL;const pn_epub_reader_anchor_t *base=r?neighbor_base(r):NULL;return base && base->has_next && !r->prefetch_failed && !pn_epub_reader_next_ready(handle);}
pn_status_t pn_epub_reader_visit_next(pn_epub_reader_t *handle,pn_epub_next_fn visit,void *ctx){
    if(!handle || !handle->impl || !visit)return PN_INVALID;
    reader_t *r=handle->impl;const pn_epub_reader_anchor_t *base=neighbor_base(r);
    if(!base || !base->has_next || !r->ahead.valid || !same_location(&r->ahead_target,&base->next))return PN_EMPTY;
    size_t section;pn_status_t s=pn_cancel_checkpoint();if(s==PN_OK)s=pn_epub_spine_find(r->epub,r->ahead_anchor.begin.path,&section);if(s!=PN_OK)return s;
    pn_cache_pin(&r->cache);s=visit(ctx,&r->ahead,&r->ahead_anchor,r->token);pn_cache_unpin(&r->cache);return s;
}
static pn_status_t pn_epub_reader_prefetch_impl(pn_epub_reader_t *handle){
    if(!handle || !handle->impl)return PN_EMPTY;
    pn_status_t checkpoint=pn_cancel_checkpoint();if(checkpoint!=PN_OK)return checkpoint;
    reader_t *r=handle->impl;const pn_epub_reader_anchor_t *base=neighbor_base(r);if(!base || !base->has_next)return PN_EMPTY;
    if(r->ahead.valid && same_location(&r->ahead_target,&base->next))return PN_EMPTY;
    if(!same_location(&r->candidate_target,&base->next)){prefetch_abort(r);r->prefetch_failed=false;r->candidate_target=base->next;r->candidate_at=base->next;}
    if(r->prefetch_failed)return PN_EMPTY;
    // 邻页优先；连续编译可能仍pin同一模型，开始实际预排前先归还，避免互等。/ Neighbors take priority: release a continuous compiler's model pin before actual prefetch work to avoid mutual waiting.
    pn_epub_reader_index_abort(handle);
    pn_status_t status;
    if(r->candidate.valid)status=PN_OK;
    else{
    // 一个有界小步内只屏蔽输入撤销，介质校验仍执行；步间保留游标给同目标NEXT接管。/ Shield input cancellation for one bounded step, retaining media validation and cursors for a matching NEXT between steps.
    pn_cancel_scope_t step;pn_cancel_enter(&step,NULL,NULL);
    if(!r->prefetch.impl){
        status=pn_epub_page_reserve(&r->candidate,r->pool,64,0);
        if(status==PN_OK)status=pn_epub_page_job_begin(&r->prefetch,r->pool,r->epub,r->candidate_at.path,r->salt,r->candidate_at.chapter_start?NULL:&r->candidate_at.position,&r->layout,&r->measure,&r->candidate);
        if(status==PN_OK)status=PN_BUSY;
    }else status=pn_epub_page_job_step(&r->prefetch);
    pn_cancel_leave(&step);
    }
    checkpoint=pn_cancel_checkpoint();if(checkpoint!=PN_OK && (status==PN_BUSY || status==PN_OK))return checkpoint;
    if(status==PN_BUSY)return status;
    if(status==PN_EMPTY && ++r->candidate_chapters<=r->spine_count){
        status=next_chapter(r,r->candidate_at.path,true,&r->candidate_at);
        if(status==PN_OK)return PN_BUSY;
    }
    pn_epub_reader_anchor_t prepared;
    if(status==PN_OK)status=anchor_page(r,r->candidate_at.path,&r->candidate,&prepared);
    if(status==PN_OK)status=pn_cancel_checkpoint();
    if(status==PN_OK){cached_free(&r->ahead);r->ahead=r->candidate;r->candidate=(pn_epub_page_t){0};r->ahead_anchor=prepared;r->ahead_target=r->candidate_target;r->ahead_warm=0;}
    else r->prefetch_failed=status!=PN_NO_MEMORY && status!=PN_CANCELLED && status!=PN_BUSY;
    prefetch_abort(r);return status;
}
pn_status_t pn_epub_reader_prefetch(pn_epub_reader_t *handle){
    pn_profile_span_t span;pn_profile_begin(&span,handle && handle->impl?((reader_t *)handle->impl)->pool:NULL,"epub.prefetch");
    reader_t *r=handle && handle->impl?handle->impl:NULL;if(r){pn_cache_pin(&r->cache);pn_cache_speculate(r->pool,true);}
    pn_status_t status=pn_epub_reader_prefetch_impl(handle);if(r){pn_cache_speculate(r->pool,false);pn_cache_unpin(&r->cache);}pn_profile_end(&span,status);return status;
}
bool pn_epub_reader_warm_pending(const pn_epub_reader_t *handle){
    const reader_t *r=handle?handle->impl:NULL;
    const pn_epub_reader_anchor_t *base=r?neighbor_base(r):NULL;
    return base && r->ahead.valid && base->has_next && same_location(&r->ahead_target,&base->next) && r->ahead_warm<r->ahead.count && !r->ahead.image_count;
}
pn_status_t pn_epub_reader_warm_next(pn_epub_reader_t *handle,pn_epub_warm_fn warm,void *ctx,size_t budget){
    if(!handle || !handle->impl || !warm || !budget || budget>64)return PN_INVALID;
    reader_t *r=handle->impl;
    const pn_epub_reader_anchor_t *base=neighbor_base(r);
    if(!base || !r->ahead.valid || !base->has_next || !same_location(&r->ahead_target,&base->next) || r->ahead_warm>=r->ahead.count || r->ahead.image_count)return PN_EMPTY;
    pn_cache_pin(&r->cache);pn_status_t s=PN_OK;size_t ordinal;
    for(size_t i=0;i<budget && r->ahead_warm<r->ahead.count;i++){
        s=pn_cancel_checkpoint();if(s==PN_OK)s=pn_epub_spine_find(r->epub,r->ahead_anchor.begin.path,&ordinal);if(s!=PN_OK)break;
        s=warm(ctx,&r->ahead.nodes[r->ahead_warm]);
        if(s==PN_NO_MEMORY){r->ahead_warm=r->ahead.count;s=PN_EMPTY;break;}if(s!=PN_OK && s!=PN_EMPTY)break;r->ahead_warm++;
    }
    pn_cache_unpin(&r->cache);return s;
}
static pn_status_t prepare_impl(pn_epub_reader_t *handle,pn_read_intent_t intent,const pn_epub_location_t *jump,pn_epub_reader_receipt_t *out,bool yielding){
    if(!handle || !handle->impl || !out || intent<PN_READ_FIRST || intent>PN_READ_JUMP)return PN_INVALID;
    reader_t *r=handle->impl;if(r->preparing)return PN_BUSY;if(!r->next_ticket)return PN_LIMIT;
    // 同目标NEXT接续已完成的校验工作，避免新输入丢弃候选后整章重读；取消仍不提交页面。/ A matching NEXT continues validated work instead of discarding and rereading the chapter; cancellation still commits no page.
    if(intent==PN_READ_NEXT && r->has_visible && ((yielding && r->visible.has_next && !r->prefetch_failed) || ((r->prefetch.impl || r->candidate.valid) && same_location(&r->candidate_target,&r->visible.next)))){
        pn_status_t s;do{s=pn_epub_reader_prefetch(handle);}while(!yielding && s==PN_BUSY);
        if(yielding && s==PN_BUSY)return PN_BUSY;
        if(s==PN_CANCELLED || s==PN_STALE_MEDIA){prefetch_abort(r);return s;}
    }
    prefetch_abort(r);r->prefetch_failed=false;memset(&r->candidate_target,0,sizeof r->candidate_target);
    if(!r->has_visible && intent!=PN_READ_FIRST && intent!=PN_READ_JUMP)return PN_EMPTY;
    pn_epub_page_t *cached=NULL;pn_epub_reader_anchor_t *prepared=&r->pending.anchor;bool saved_forward=false;
    if(r->cache_enabled){
        if(intent==PN_READ_NEXT && r->ahead.valid && r->visible.has_next && same_location(&r->ahead_target,&r->visible.next)){cached=&r->ahead;*prepared=r->ahead_anchor;}
        if(intent==PN_READ_PREVIOUS && r->behind.valid && r->history_count && same_location(&r->behind_anchor.begin,&r->history[r->history_count-1])){cached=&r->behind;*prepared=r->behind_anchor;}
        pn_cache_observe(r->pool,PN_CACHE_NEIGHBOR,cached!=NULL);
        if(cached && r->page->owner==r->pool && cached->owner==r->pool){
            size_t section;pn_status_t checked=check_location(r,&prepared->begin,&section);if(checked!=PN_OK)return checked;
            // 命中自有节点时直接交接，固定外部缓冲仍走复制兼容路径。/ Transfer owned nodes on hits; fixed external buffers keep the copying compatibility path.
            if(r->page->valid && r->has_visible && same_position(r->page->begin,r->visible.begin.position)){
                pn_epub_page_t *opposite=intent==PN_READ_PREVIOUS?&r->ahead:&r->behind;cached_free(opposite);*opposite=*r->page;
                if(intent==PN_READ_PREVIOUS){r->ahead_anchor=r->visible;r->ahead_target=prepared->next;r->ahead_warm=0;}else r->behind_anchor=r->visible;
            }else pn_epub_page_release(r->page);
            *r->page=*cached;*cached=(pn_epub_page_t){0};
            if(!pn_cache_allow(r->pool,PN_CACHE_NEIGHBOR,1))neighbor_drop(r);
            r->pending.owner=handle;r->pending.token=r->token;r->pending.ticket=r->next_ticket++;r->pending.intent=intent;r->preparing=true;*out=r->pending;return PN_OK;
        }
        if((intent==PN_READ_NEXT || intent==PN_READ_PREVIOUS) && r->page->valid && r->has_visible && same_position(r->page->begin,r->visible.begin.position)){pn_epub_page_t *slot=intent==PN_READ_PREVIOUS?&r->ahead:&r->behind;if(slot!=cached && cached_store(r,slot,r->page)){if(slot==&r->ahead){r->ahead_anchor=r->visible;saved_forward=true;}else r->behind_anchor=r->visible;}}
    }
    if(intent!=PN_READ_NEXT && intent!=PN_READ_PREVIOUS){cached_free(&r->ahead);cached_free(&r->behind);}
    if(cached && cached->count<=r->page->capacity && cached->image_count<=r->page->image_capacity){
        size_t index;pn_status_t status=check_location(r,&prepared->begin,&index);if(status!=PN_OK)return status;
        pn_epub_page_node_t *nodes=r->page->nodes;pn_epub_page_image_t *images=r->page->images;size_t cap=r->page->capacity,icap=r->page->image_capacity;pn_pool_t *owner=r->page->owner;
        if(cached->count)memcpy(nodes,cached->nodes,cached->count*sizeof *nodes);
        if(cached->image_count)memcpy(images,cached->images,cached->image_count*sizeof *images);
        *r->page=*cached;r->page->nodes=nodes;r->page->images=images;r->page->capacity=cap;r->page->image_capacity=icap;r->page->owner=owner;
        if(saved_forward)r->ahead_target=prepared->next;
        r->pending.owner=handle;r->pending.token=r->token;r->pending.ticket=r->next_ticket++;r->pending.intent=intent;r->preparing=true;*out=r->pending;return PN_OK;
    }
    r->page->valid=false;pn_epub_location_t at;pn_status_t status=PN_OK;
    if(intent==PN_READ_FIRST)status=linear(r,0,true,&at);
    else if(intent==PN_READ_NEXT){if(!r->visible.has_next)return PN_EMPTY;at=r->visible.next;}
    else if(intent==PN_READ_CURRENT)at=r->visible.begin;
    else if(intent==PN_READ_JUMP){size_t index;status=check_location(r,jump,&index);if(status==PN_OK){at=*jump;if(!at.chapter_start){uint64_t order;status=pn_xhtml_order(r->pool,r->epub,at.path,&at.position,r->salt,&order);if(status==PN_EMPTY)status=PN_INVALID;}}}
    if(intent==PN_READ_PREVIOUS)status=previous(r,&at);
    else if(status==PN_OK)status=forward(r,&at);
    if(status==PN_OK)status=anchor(r,at.path,prepared);
    if(status!=PN_OK){r->page->valid=false;return status;}
    if(saved_forward)r->ahead_target=prepared->next;
    r->pending.owner=handle;r->pending.token=r->token;r->pending.ticket=r->next_ticket++;r->pending.intent=intent;r->preparing=true;*out=r->pending;return PN_OK;
}
static pn_status_t prepare(pn_epub_reader_t *handle,pn_read_intent_t intent,const pn_epub_location_t *jump,pn_epub_reader_receipt_t *out,bool yielding){
    pn_epub_reader_index_abort(handle);
    pn_profile_span_t span;pn_profile_begin(&span,handle && handle->impl?((reader_t *)handle->impl)->pool:NULL,intent==PN_READ_NEXT?"epub.next":intent==PN_READ_PREVIOUS?"epub.previous":intent==PN_READ_JUMP?"epub.jump":"epub.prepare");
    reader_t *r=handle && handle->impl?handle->impl:NULL;
    bool borrowing=r && r->cache_enabled && ((intent==PN_READ_NEXT && (r->ahead.valid || r->prefetch.impl || r->candidate.valid)) || (intent==PN_READ_PREVIOUS && r->behind.valid));
    if(borrowing)pn_cache_pin(&r->cache);
    if(r)pn_cache_reclaim(r->pool,r->pool->cache_reserve);
    pn_status_t status=prepare_impl(handle,intent,jump,out,yielding);if(borrowing)pn_cache_unpin(&r->cache);pn_profile_end(&span,status);return status;
}
pn_status_t pn_epub_reader_prepare(pn_epub_reader_t *h,pn_read_intent_t i,const pn_epub_location_t *j,pn_epub_reader_receipt_t *r){return prepare(h,i,j,r,false);}
pn_status_t pn_epub_reader_prepare_step(pn_epub_reader_t *h,pn_read_intent_t i,const pn_epub_location_t *j,pn_epub_reader_receipt_t *r){return prepare(h,i,j,r,true);}
pn_status_t pn_epub_reader_prepared(const pn_epub_reader_t *handle,pn_epub_reader_receipt_t *out){
    if(!handle || !handle->impl || !out)return PN_INVALID;
    const reader_t *r=handle->impl;
    if(!r->preparing || !r->page->valid || r->pending.token.session!=r->token.session || r->pending.token.generation!=r->token.generation)return PN_EMPTY;
    *out=r->pending;return PN_OK;
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
    // 只有成功呈现才能改变模型首选槽，缓存缺失/忙不改变阅读回执。/ Only successful presentation changes the preferred model slot; missing/busy caches never alter reading receipts.
    (void)pn_xhtml_cache_select(r->epub,r->visible.begin.path,r->salt);
    if(r->confirm){pn_epub_progress_t progress={r->book,r->visible.begin};return r->confirm(r->ctx,&progress,receipt->intent==PN_READ_NEXT || receipt->intent==PN_READ_PREVIOUS,now);}
    return PN_OK;
}
pn_status_t pn_epub_reader_reflow(pn_epub_reader_t *handle,const pn_layout_t *layout){
    pn_epub_reader_index_abort(handle);
    if(!handle || !handle->impl || !layout_valid(layout))return PN_INVALID;
    reader_t *r=handle->impl;
    if(r->token.generation==UINT32_MAX)return PN_LIMIT;
    prefetch_abort(r);r->prefetch_failed=false;memset(&r->candidate_target,0,sizeof r->candidate_target);
    r->layout=*layout;r->token.generation++;r->index=NULL;r->preparing=false;r->page->valid=false;r->history_count=0;cached_free(&r->ahead);cached_free(&r->behind);return PN_OK;
}
pn_status_t pn_epub_reader_complete(pn_epub_reader_t *handle,const pn_epub_reader_receipt_t *receipt,bool success,uint64_t now){
    // 显示回执提交屏障不接受输入取消，仍验证实际介质。/ The display-receipt commit barrier ignores input cancellation but validates actual media.
    pn_cancel_scope_t shield;pn_cancel_enter(&shield,NULL,NULL);
    pn_status_t status=complete_impl(handle,receipt,success,now);pn_cancel_leave(&shield);return status;
}
void pn_epub_reader_trim(pn_epub_reader_t *handle){if(handle && handle->impl){reader_t *r=handle->impl;prefetch_abort(r);cached_free(&r->ahead);cached_free(&r->behind);}}
void pn_epub_reader_index(pn_epub_reader_t *handle,pn_page_index_t *index){if(handle && handle->impl)((reader_t *)handle->impl)->index=index;}

void pn_epub_reader_index_abort(pn_epub_reader_t *handle){
    if(!handle || !handle->impl)return;
    reader_t *r=handle->impl;pn_epub_page_job_close(&r->index_job);pn_epub_page_release(&r->index_page);
}
pn_status_t pn_epub_reader_index_step(pn_epub_reader_t *handle,pn_book_index_state_t *state){
    if(!handle || !handle->impl || !state)return PN_INVALID;
    reader_t *r=handle->impl;if(state->complete)return PN_OK;
    if(state->section>=r->spine_count){state->complete=true;return PN_OK;}
    if(r->index_job.impl && (state->section!=r->index_at.section || state->pages!=r->index_at.pages || state->position.element!=r->index_at.position.element || state->position.run!=r->index_at.position.run || state->position.offset!=r->index_at.position.offset || state->position.kind!=r->index_at.position.kind))pn_epub_reader_index_abort(handle);
    pn_status_t s=PN_OK;
    if(!r->index_job.impl){
        pn_epub_item_t item;s=pn_epub_spine(r->epub,state->section,&item);if(s!=PN_OK)return s;
        if(!item.linear){state->section++;state->position=(pn_xhtml_position_t){0};return PN_OK;}
        r->index_at=*state;s=pn_epub_page_reserve(&r->index_page,r->pool,64,2);
        if(s==PN_OK){pn_profile_span_t span;pn_profile_begin(&span,r->pool,"index.begin");s=pn_epub_page_job_begin(&r->index_job,r->pool,r->epub,item.path,r->salt,state->position.element?&r->index_at.position:NULL,&r->layout,&r->measure,&r->index_page);pn_profile_end(&span,s);}
        if(s!=PN_OK){pn_epub_reader_index_abort(handle);return s;}
    }
    s=pn_epub_page_job_index_step(&r->index_job);if(s==PN_BUSY)return s;
    // 已验证窗口尾部不足一页时沿原同步分页退路重放；真实OOM仍向外返回。/ Replay through the original synchronous pagination fallback when a verified window ends mid-page; genuine OOM still propagates.
    if(s==PN_NO_MEMORY){pn_profile_span_t span;pn_profile_begin(&span,r->pool,"index.fallback");pn_epub_item_t item;s=pn_epub_spine(r->epub,state->section,&item);if(s==PN_OK)s=pn_epub_page_prepare(r->pool,r->epub,item.path,r->salt,state->position.element?&r->index_at.position:NULL,&r->layout,&r->measure,&r->index_page);pn_profile_end(&span,s);}
    if(s==PN_OK){
        pn_epub_page_t *p=&r->index_page;if(!p->valid || (p->has_next && p->next_order<=p->begin_order))s=PN_CORRUPT;
        else{pn_page_index_entry_t e={.section=state->section,.begin=p->begin_order,.end=p->has_next?p->next_order:UINT64_MAX,.first=p->begin,.next=p->next,.flags=p->has_next?1u:0u};pn_page_index_add(r->index,&e);state->pages++;if(p->has_next)state->position=p->next;else{state->section++;state->position=(pn_xhtml_position_t){0};}}
    }else if(s==PN_EMPTY){state->section++;state->position=(pn_xhtml_position_t){0};s=PN_OK;}
    if(s==PN_OK && state->section==r->index_at.section && pn_epub_page_job_index_continue(&r->index_job)==PN_OK){r->index_at=*state;return PN_OK;}
    pn_epub_reader_index_abort(handle);if(s==PN_OK && state->section>=r->spine_count)state->complete=true;return s;
}
