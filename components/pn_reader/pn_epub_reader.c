/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：原生EPUB前后页与跨章导航，只确认真实显示的语义位置。
 * English: native EPUB page/chapter navigation confirming only actually displayed semantic locations.
 * 冻结：无永久页号，冷上一页按事件顺序重建，不对locator元组排序。
 * Frozen: no permanent page numbers; cold previous pages reconstruct event order without sorting locator tuples.
 */
#include "pn_epub_reader.h"
#include <string.h>
_Static_assert(PN_ZIP_PATH_MAX==PN_EPUB_LOCATION_PATH_MAX,"Semantic/publication path bounds must match");
typedef struct {
    pn_pool_t *pool;pn_epub_t *epub;pn_book_id_t book;uint8_t salt[16];pn_layout_t layout;pn_epub_measure_t measure;pn_epub_page_t *page;
    pn_job_token_t token;uint64_t next_ticket,last_now;pn_epub_reader_receipt_t pending;pn_epub_reader_anchor_t visible;
    pn_epub_location_t history[PN_READER_HISTORY];size_t history_count,spine_count;bool preparing,has_visible;
    pn_epub_confirm_fn confirm;void *ctx;
} reader_t;
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
    return stop?pn_epub_page_prepare_until(r->pool,r->epub,at->path,r->salt,start,stop,&r->layout,&r->measure,r->page):pn_epub_page_prepare(r->pool,r->epub,at->path,r->salt,start,&r->layout,&r->measure,r->page);
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
    reader_t *r=pn_alloc(pool,sizeof *r);if(!r)return PN_NO_MEMORY;
    *r=(reader_t){.pool=pool,.epub=epub,.book=*book,.layout=*layout,.measure=*measure,.page=page,.token=token,.next_ticket=1,.spine_count=info.spine_count,.confirm=confirm,.ctx=ctx};memcpy(r->salt,salt,16);page->valid=false;handle->impl=r;return PN_OK;
}
void pn_epub_reader_close(pn_epub_reader_t *handle){if(handle && handle->impl){reader_t *r=handle->impl;r->page->valid=false;pn_free(r);handle->impl=NULL;}}
pn_status_t pn_epub_reader_prepare(pn_epub_reader_t *handle,pn_read_intent_t intent,const pn_epub_location_t *jump,pn_epub_reader_receipt_t *out){
    if(!handle || !handle->impl || !out || intent<PN_READ_FIRST || intent>PN_READ_JUMP)return PN_INVALID;
    reader_t *r=handle->impl;if(r->preparing)return PN_BUSY;if(!r->next_ticket)return PN_LIMIT;
    if(!r->has_visible && intent!=PN_READ_FIRST && intent!=PN_READ_JUMP)return PN_EMPTY;
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
    r->pending=(pn_epub_reader_receipt_t){handle,r->token,r->next_ticket++,intent,candidate};r->preparing=true;*out=r->pending;return PN_OK;
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
pn_status_t pn_epub_reader_complete(pn_epub_reader_t *handle,const pn_epub_reader_receipt_t *receipt,bool success,uint64_t now){
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
    r->layout=*layout;r->token.generation++;r->preparing=false;r->page->valid=false;r->history_count=0;return PN_OK;
}
