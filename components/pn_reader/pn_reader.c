/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享TXT阅读会话，区分准备页和实际显示位置。
 * English: shared TXT reading session separating prepared pages from visible positions.
 * 冻结：owner串行调用；仅成功显示提交进度；源和glyph缓冲保持有效。
 * Frozen: serialized owner calls; commit progress only after successful presentation; source and glyph buffers remain valid.
 */
#include "pn_reader.h"
#include <string.h>
static bool valid_layout(const pn_layout_t *l){return l && l->width>0 && l->width<=4096 && l->height>0 && l->height<=4096 && l->line_height>0 && l->line_height<=l->height && l->ascent>=0 && l->ascent<=l->line_height && l->indent>=0 && l->indent<l->width && l->paragraph_gap>=0 && l->paragraph_gap<=4096 && l->letter_spacing_64>=0 && l->letter_spacing_64<=4096;}
static bool same_anchor(pn_reader_anchor_t a,pn_reader_anchor_t b){return a.begin==b.begin && a.end==b.end && a.paragraph==b.paragraph && a.next_paragraph==b.next_paragraph;}
pn_status_t pn_reader_init(pn_reader_t *r,const pn_text_source_t *source,pn_text_encoding_t encoding,
    const pn_book_id_t *book,const pn_layout_t *layout,const pn_font_metrics_t *metrics,
    pn_page_glyph_t *glyphs,size_t capacity,pn_job_token_t token,pn_save_policy_t *save){
    if(!r || !book || !valid_layout(layout) || !metrics || !metrics->advance || !glyphs || !capacity || capacity>PN_PAGE_GLYPHS_MAX || !token.session || !token.generation)return PN_INVALID;
    if(save && (!save->io.read || !save->io.write_sync || memcmp(save->expected.sha256,book->sha256,32)!=0))return PN_INVALID;
    pn_reader_t result={.book=*book,.layout=*layout,.metrics=*metrics,.token=token,.next_ticket=1,.save=save,.page={.glyphs=glyphs,.capacity=capacity}};
    pn_status_t status=pn_text_open(&result.decoder,source,encoding);if(status!=PN_OK)return status;
    *r=result;return PN_OK;
}
static pn_status_t seek(pn_text_reader_t *d,uint64_t offset,bool *paragraph){
    if(offset<d->content_begin || offset>d->source.size)return PN_INVALID;
    d->cursor=d->content_begin;*paragraph=true;
    while(d->cursor<offset){pn_text_char_t c;pn_status_t s=pn_text_next(d,&c);if(s!=PN_OK)return s;if(d->cursor>offset)return PN_INVALID;*paragraph=c.codepoint==10;}
    return PN_OK;
}
pn_status_t pn_reader_align(const pn_reader_t *r,uint64_t offset,uint64_t *aligned){
    if(!r || !aligned || !r->decoder.source.read_at)return PN_INVALID;
    pn_text_reader_t work=r->decoder;work.cursor=work.content_begin;
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
    pn_text_reader_t work=r->decoder;bool paragraph=true;uint64_t begin=work.content_begin;
    pn_status_t status=PN_OK;
    if(intent==PN_READ_NEXT){begin=r->visible.end;paragraph=r->visible.next_paragraph;}
    else if(intent==PN_READ_CURRENT){begin=r->visible.begin;paragraph=r->visible.paragraph;}
    else if(intent==PN_READ_JUMP){begin=offset;status=seek(&work,begin,&paragraph);}
    else if(intent==PN_READ_PREVIOUS){
        if(r->visible.begin==work.content_begin)return PN_EMPTY;
        if(r->history_count){pn_reader_anchor_t a=r->history[r->history_count-1];begin=a.begin;paragraph=a.paragraph;}
        else {
            work.cursor=work.content_begin;work.source.size=r->visible.begin;
            pn_reader_anchor_t previous={0};bool first=true;
            while(work.cursor<work.source.size){previous.begin=work.cursor;previous.paragraph=first;status=pn_text_paginate(&work,&r->layout,&r->metrics,first,&r->page);if(status!=PN_OK)return status;previous.end=r->page.end;previous.next_paragraph=r->page.next_paragraph_start;first=previous.next_paragraph;}
            r->pending=(pn_reader_receipt_t){r,r->token,r->next_ticket++,intent,previous};r->preparing=true;*receipt=r->pending;return PN_OK;
        }
    }
    if(status!=PN_OK)return status;
    work.cursor=begin;status=pn_text_paginate(&work,&r->layout,&r->metrics,paragraph,&r->page);if(status!=PN_OK)return status;
    pn_reader_anchor_t anchor={begin,r->page.end,paragraph,r->page.next_paragraph_start};
    r->pending=(pn_reader_receipt_t){r,r->token,r->next_ticket++,intent,anchor};r->preparing=true;*receipt=r->pending;return PN_OK;
}
pn_status_t pn_reader_progress(const pn_reader_t *r,pn_txt_progress_t *progress){
    if(!r || !progress || !r->decoder.source.read_at)return PN_INVALID;
    if(!r->has_visible)return PN_EMPTY;
    *progress=(pn_txt_progress_t){r->book,r->decoder.source.size,r->visible.begin,r->decoder.encoding,1};return PN_OK;
}
pn_status_t pn_reader_complete(pn_reader_t *r,const pn_reader_receipt_t *receipt,bool success,uint64_t now){
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
    r->layout=*layout;r->token.generation++;r->preparing=false;r->page.valid=false;r->history_count=0;return PN_OK;
}
