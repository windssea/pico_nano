/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享TXT阅读会话，区分准备页和实际显示位置。
 * English: shared TXT reading session separating prepared pages from visible positions.
 * 冻结：owner串行调用；仅成功显示提交进度；源和glyph缓冲保持有效。
 * Frozen: serialized owner calls; commit progress only after successful presentation; source and glyph buffers remain valid.
 */
#include "pn_reader.h"
#include "pn_cancel.h"
#include <string.h>
static bool token_equal(pn_job_token_t a,pn_job_token_t b){return a.session==b.session && a.generation==b.generation;}
static bool copy_page(pn_reader_t *r,pn_text_page_t *to,const pn_text_page_t *from){
    pn_page_glyph_t *nodes=from->count?pn_alloc(r->neighbor_pool,from->count*sizeof *nodes):NULL;
    if(from->count && !nodes)return false;
    if(from->count)memcpy(nodes,from->glyphs,from->count*sizeof *nodes);
    pn_free(to->glyphs);*to=*from;to->glyphs=nodes;to->capacity=from->count;return true;
}
void pn_reader_neighbors(pn_reader_t *r,pn_pool_t *pool){if(!r)return;pn_free(r->ahead.glyphs);pn_free(r->behind.glyphs);r->ahead=(pn_text_page_t){0};r->behind=(pn_text_page_t){0};r->neighbor_pool=pool;r->neighbor_token=r->token;}
pn_status_t pn_reader_prefetch(pn_reader_t *r){
    if(!r || !r->neighbor_pool || !r->has_visible || r->preparing || r->visible.end>=r->decoder.source.size)return PN_EMPTY;
    if(r->ahead.valid && r->ahead_anchor.begin==r->visible.end && token_equal(r->token,r->neighbor_token))return PN_EMPTY;
    pn_page_glyph_t *nodes=pn_alloc(r->neighbor_pool,r->page.capacity*sizeof *nodes);if(!nodes)return PN_NO_MEMORY;
    pn_reader_t copy=*r;copy.neighbor_pool=NULL;copy.page=(pn_text_page_t){.glyphs=nodes,.capacity=r->page.capacity};copy.preparing=false;copy.save=NULL;
    pn_reader_receipt_t result;pn_status_t status=pn_reader_prepare(&copy,PN_READ_NEXT,0,&result);
    if(status==PN_OK)status=pn_cancel_checkpoint();
    if(status==PN_OK){if(copy_page(r,&r->ahead,&copy.page)){r->ahead_anchor=result.anchor;r->neighbor_token=r->token;}else status=PN_NO_MEMORY;}
    pn_free(nodes);return status;
}
static bool valid_layout(const pn_layout_t *l){return l && l->width>0 && l->width<=4096 && l->height>0 && l->height<=4096 && l->line_height>0 && l->line_height<=l->height && l->ascent>=0 && l->ascent<=l->line_height && l->indent>=0 && l->indent<l->width && l->paragraph_gap>=0 && l->paragraph_gap<=4096 && l->letter_spacing_64>=0 && l->letter_spacing_64<=4096;}
static bool same_anchor(pn_reader_anchor_t a,pn_reader_anchor_t b){return a.begin==b.begin && a.end==b.end && a.paragraph==b.paragraph && a.next_paragraph==b.next_paragraph;}
static bool checkpoint(const pn_reader_t *r,uint64_t offset,bool strict,pn_page_index_entry_t *out){
    bool found=pn_page_index_near(r->index,0,offset,strict,out);
    if(r->checkpoints && r->checkpoint_count<=512)for(size_t j=0;j<r->checkpoint_count;j++){pn_reader_anchor_t p=r->checkpoints[j];if((strict?p.begin<offset:p.begin<=offset) && (!found || p.begin>out->begin)){*out=(pn_page_index_entry_t){.section=UINT32_MAX,.begin=p.begin,.end=p.begin+1,.flags=p.paragraph?1u:0u};found=true;}}
    return found;
}
pn_status_t pn_reader_init(pn_reader_t *r,const pn_text_source_t *source,pn_text_encoding_t encoding,
    const pn_book_id_t *book,const pn_layout_t *layout,const pn_font_metrics_t *metrics,
    pn_page_glyph_t *glyphs,size_t capacity,pn_job_token_t token,pn_save_policy_t *save){
    if(!r || !book || !valid_layout(layout) || !metrics || !metrics->advance || !glyphs || !capacity || capacity>PN_PAGE_GLYPHS_MAX || !token.session || !token.generation)return PN_INVALID;
    if(save && (!save->io.read || !save->io.write_sync || memcmp(save->expected.sha256,book->sha256,32)!=0))return PN_INVALID;
    pn_reader_t result={.book=*book,.layout=*layout,.metrics=*metrics,.token=token,.next_ticket=1,.save=save,.page={.glyphs=glyphs,.capacity=capacity}};
    pn_status_t status=pn_text_open(&result.decoder,source,encoding);if(status!=PN_OK)return status;
    *r=result;return PN_OK;
}
static pn_status_t seek(const pn_reader_t *r,pn_text_reader_t *d,uint64_t offset,bool *paragraph){
    if(offset<d->content_begin || offset>d->source.size)return PN_INVALID;
    d->cursor=d->content_begin;*paragraph=true;
    pn_page_index_entry_t entry;if(checkpoint(r,offset,false,&entry) && entry.begin>=d->content_begin && entry.end<=d->source.size){d->cursor=entry.begin;*paragraph=(entry.flags&1)!=0;}
    while(d->cursor<offset){pn_text_char_t c;pn_status_t s=pn_text_next(d,&c);if(s!=PN_OK)return s;if(d->cursor>offset)return PN_INVALID;*paragraph=c.codepoint==10;}
    return PN_OK;
}
pn_status_t pn_reader_align(const pn_reader_t *r,uint64_t offset,uint64_t *aligned){
    if(!r || !aligned || !r->decoder.source.read_at)return PN_INVALID;
    pn_text_reader_t work=r->decoder;work.cursor=work.content_begin;
    pn_page_index_entry_t entry;if(checkpoint(r,offset,false,&entry) && entry.begin>=work.content_begin && entry.end<=work.source.size)work.cursor=entry.begin;
    uint64_t previous=work.cursor;
    while(work.cursor<offset && work.cursor<work.source.size){pn_text_char_t c;previous=work.cursor;pn_status_t s=pn_text_next(&work,&c);if(s!=PN_OK)return s;}
    // 越过末尾时退回最后一个字符，避免落在空页。/ Past the end, fall back to the last character so the jump never lands on an empty page.
    *aligned=work.cursor>=work.source.size && work.source.size>work.content_begin && offset>=work.source.size?previous:work.cursor;
    return PN_OK;
}
pn_status_t pn_reader_prepare(pn_reader_t *r,pn_read_intent_t intent,uint64_t offset,pn_reader_receipt_t *receipt){
    if(!r || !receipt || !r->decoder.source.read_at || intent<PN_READ_FIRST || intent>PN_READ_JUMP)return PN_INVALID;
    if(r->preparing)return PN_BUSY;
    if(!r->next_ticket)return PN_LIMIT;
    if(intent!=PN_READ_FIRST && intent!=PN_READ_JUMP && !r->has_visible)return PN_EMPTY;
    pn_text_page_t *cached=NULL;pn_reader_anchor_t cached_anchor={0};
    if(r->neighbor_pool && token_equal(r->token,r->neighbor_token)){
        if(intent==PN_READ_NEXT && r->ahead.valid && r->ahead_anchor.begin==r->visible.end){cached=&r->ahead;cached_anchor=r->ahead_anchor;}
        if(intent==PN_READ_PREVIOUS && r->behind.valid && r->history_count && same_anchor(r->behind_anchor,r->history[r->history_count-1])){cached=&r->behind;cached_anchor=r->behind_anchor;}
        if((intent==PN_READ_NEXT || intent==PN_READ_PREVIOUS) && r->page.valid && r->has_visible && r->page.begin==r->visible.begin && r->page.end==r->visible.end){
            pn_text_page_t *slot=intent==PN_READ_PREVIOUS?&r->ahead:&r->behind;
            if(slot!=cached && copy_page(r,slot,&r->page)){if(slot==&r->ahead)r->ahead_anchor=r->visible;else r->behind_anchor=r->visible;}
        }
    }
    if(intent!=PN_READ_NEXT && intent!=PN_READ_PREVIOUS && r->neighbor_pool)pn_reader_neighbors(r,r->neighbor_pool);
    if(cached && cached->count<=r->page.capacity){
        pn_status_t checked=r->decoder.source.validate?r->decoder.source.validate(r->decoder.source.ctx):PN_OK;
        if(checked!=PN_OK)return checked;
        pn_page_glyph_t *nodes=r->page.glyphs;size_t capacity=r->page.capacity;
        if(cached->count)memcpy(nodes,cached->glyphs,cached->count*sizeof *nodes);
        r->page=*cached;r->page.glyphs=nodes;r->page.capacity=capacity;
        r->pending=(pn_reader_receipt_t){r,r->token,r->next_ticket++,intent,cached_anchor};r->preparing=true;*receipt=r->pending;return PN_OK;
    }
    pn_text_reader_t work=r->decoder;bool paragraph=true;uint64_t begin=work.content_begin;
    pn_status_t status=PN_OK;
    if(intent==PN_READ_NEXT){begin=r->visible.end;paragraph=r->visible.next_paragraph;}
    else if(intent==PN_READ_CURRENT){begin=r->visible.begin;paragraph=r->visible.paragraph;}
    else if(intent==PN_READ_JUMP){begin=offset;status=seek(r,&work,begin,&paragraph);}
    else if(intent==PN_READ_PREVIOUS){
        if(r->visible.begin==work.content_begin)return PN_EMPTY;
        if(r->history_count){pn_reader_anchor_t a=r->history[r->history_count-1];begin=a.begin;paragraph=a.paragraph;}
        else {
            work.cursor=work.content_begin;work.source.size=r->visible.begin;
            pn_page_index_entry_t entry;bool indexed=pn_page_index_near(r->index,0,r->visible.begin,true,&entry) && entry.begin>=work.content_begin && entry.end<=r->decoder.source.size;
            if(!indexed || entry.end!=r->visible.begin)indexed=checkpoint(r,r->visible.begin,true,&entry) && entry.begin>=work.content_begin && entry.end<=r->decoder.source.size;
            if(indexed)work.cursor=entry.begin;
            pn_reader_anchor_t previous={0};bool first=true;
            if(indexed)first=(entry.flags&1)!=0;
            while(work.cursor<work.source.size){previous.begin=work.cursor;previous.paragraph=first;status=pn_text_paginate(&work,&r->layout,&r->metrics,first,&r->page);if(status!=PN_OK)return status;previous.end=r->page.end;previous.next_paragraph=r->page.next_paragraph_start;first=previous.next_paragraph;
                pn_page_index_entry_t recorded={.begin=previous.begin,.end=previous.end,.flags=(previous.paragraph?1u:0u)|(previous.next_paragraph?2u:0u)};pn_page_index_add(r->index,&recorded);}
            r->pending=(pn_reader_receipt_t){r,r->token,r->next_ticket++,intent,previous};r->preparing=true;*receipt=r->pending;return PN_OK;
        }
    }
    if(status!=PN_OK)return status;
    work.cursor=begin;status=pn_text_paginate(&work,&r->layout,&r->metrics,paragraph,&r->page);if(status!=PN_OK)return status;
    pn_reader_anchor_t anchor={begin,r->page.end,paragraph,r->page.next_paragraph_start};
    pn_page_index_entry_t recorded={.begin=anchor.begin,.end=anchor.end,.flags=(paragraph?1u:0u)|(anchor.next_paragraph?2u:0u)};pn_page_index_add(r->index,&recorded);
    r->pending=(pn_reader_receipt_t){r,r->token,r->next_ticket++,intent,anchor};r->preparing=true;*receipt=r->pending;return PN_OK;
}
pn_status_t pn_reader_progress(const pn_reader_t *r,pn_txt_progress_t *progress){
    if(!r || !progress || !r->decoder.source.read_at)return PN_INVALID;
    if(!r->has_visible)return PN_EMPTY;
    *progress=(pn_txt_progress_t){r->book,r->decoder.source.size,r->visible.begin,r->decoder.encoding,1};return PN_OK;
}
static pn_status_t complete_impl(pn_reader_t *r,const pn_reader_receipt_t *receipt,bool success,uint64_t now){
    if(!r || !receipt || receipt->owner!=r)return PN_INVALID;
    if(receipt->token.session!=r->token.session || receipt->token.generation!=r->token.generation)return PN_STALE_JOB;
    if(!r->preparing || receipt->ticket!=r->pending.ticket || receipt->intent!=r->pending.intent || !same_anchor(receipt->anchor,r->pending.anchor))return PN_INVALID;
    r->preparing=false;
    if(!success){r->page.valid=false;return PN_IO;}
    if(r->decoder.source.validate){pn_status_t s=r->decoder.source.validate(r->decoder.source.ctx);if(s!=PN_OK){r->page.valid=false;return s;}}
    if(receipt->intent==PN_READ_NEXT && r->has_visible){
        if(r->history_count==PN_READER_HISTORY){memmove(r->history,r->history+1,(PN_READER_HISTORY-1)*sizeof r->history[0]);r->history_count--;}
        r->history[r->history_count++]=r->visible;
    }else if(receipt->intent==PN_READ_PREVIOUS){if(r->history_count)r->history_count--;}
    else if(receipt->intent==PN_READ_FIRST || receipt->intent==PN_READ_JUMP)r->history_count=0;
    r->visible=receipt->anchor;r->has_visible=true;
    if(r->save){pn_txt_progress_t progress;pn_status_t s=pn_reader_progress(r,&progress);if(s!=PN_OK)return s;return pn_save_policy_presented(r->save,&progress,receipt->intent==PN_READ_NEXT || receipt->intent==PN_READ_PREVIOUS,now);}
    return PN_OK;
}
pn_status_t pn_reader_reflow(pn_reader_t *r,const pn_layout_t *layout){
    if(!r || !r->decoder.source.read_at || !valid_layout(layout))return PN_INVALID;
    if(r->token.generation==UINT32_MAX)return PN_LIMIT;
    r->layout=*layout;r->token.generation++;r->index=NULL;r->preparing=false;r->page.valid=false;r->history_count=0;pn_reader_neighbors(r,r->neighbor_pool);return PN_OK;
}
pn_status_t pn_reader_complete(pn_reader_t *r,const pn_reader_receipt_t *receipt,bool success,uint64_t now){
    // 已显示的物理回执不能被后到输入撤销，介质验证仍执行。/ Later input cannot undo a physical display receipt; media validation still runs.
    pn_cancel_scope_t shield;pn_cancel_enter(&shield,NULL,NULL);
    pn_status_t status=complete_impl(r,receipt,success,now);pn_cancel_leave(&shield);return status;
}
