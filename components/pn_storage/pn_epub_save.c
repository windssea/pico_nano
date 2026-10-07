/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB保存阈值、重试与不确定写入确认。
 * English: EPUB save thresholds, retry and uncertain-write confirmation.
 * 冻结：失败保留最新显示位置，不把保存失败当显示失败。
 * Frozen: retain latest displayed location on failure; never confuse save and display failures.
 */
#include "pn_epub_save.h"
#include <string.h>
static bool ready(const pn_epub_save_t *p){return p && p->pool && p->io.read && p->io.write_sync;}
static bool same(const pn_epub_progress_t *a,const pn_epub_progress_t *b){return !memcmp(a->book.sha256,b->book.sha256,32) && !strcmp(a->location.path,b->location.path) && a->location.version==b->location.version && a->location.chapter_start==b->location.chapter_start && a->location.position.element==b->location.position.element && a->location.position.offset==b->location.position.offset && a->location.position.run==b->location.position.run && a->location.position.kind==b->location.position.kind;}
pn_status_t pn_epub_save_init(pn_epub_save_t *p,pn_pool_t *pool,const pn_journal_io_t *io,const pn_book_id_t *expected,const pn_epub_progress_t *baseline,uint64_t now){
    if(!p || !pool || !io || !io->read || !io->write_sync || !expected)return PN_INVALID;
    if(baseline){pn_status_t status=pn_epub_progress_validate(baseline);if(status!=PN_OK)return status;if(memcmp(baseline->book.sha256,expected->sha256,32))return PN_STALE_JOB;}
    *p=(pn_epub_save_t){.pool=pool,.io=*io,.expected=*expected,.last_now=now,.has_location=baseline!=NULL};
    if(baseline)p->current=*baseline;
    return PN_OK;
}
pn_status_t pn_epub_save_presented(pn_epub_save_t *p,const pn_epub_progress_t *progress,bool turn,uint64_t now){
    if(!ready(p) || now<p->last_now)return PN_INVALID;
    pn_status_t status=pn_epub_progress_validate(progress);if(status!=PN_OK)return status;
    if(memcmp(progress->book.sha256,p->expected.sha256,32))return PN_STALE_JOB;
    p->last_now=now;if(p->has_location && same(&p->current,progress))return PN_OK;
    if(!p->dirty)p->dirty_since=now;
    p->current=*progress;p->has_location=true;p->dirty=true;if(turn && p->turns<5)p->turns++;return PN_OK;
}
pn_status_t pn_epub_save_flush(pn_epub_save_t *p,uint64_t now){
    if(!ready(p) || now<p->last_now)return PN_INVALID;
    p->last_now=now;if(!p->dirty)return PN_OK;
    pn_status_t status=pn_epub_progress_save(&p->io,p->pool,&p->current);
    if(status!=PN_OK){pn_epub_progress_t loaded;if(pn_epub_progress_load(&p->io,p->pool,&p->expected,&loaded)==PN_OK && same(&loaded,&p->current))status=PN_OK;}
    if(status==PN_OK){p->dirty=false;p->retry_pending=false;p->turns=0;p->dirty_since=0;}
    else{p->retry_pending=true;p->last_failed=now;}
    return status;
}
pn_status_t pn_epub_save_tick(pn_epub_save_t *p,uint64_t now){
    if(!ready(p) || now<p->last_now)return PN_INVALID;
    p->last_now=now;if(!p->dirty)return PN_OK;
    if(p->retry_pending){if(now-p->last_failed<1000)return PN_BUSY;}
    else if(p->turns<5 && now-p->dirty_since<30000)return PN_OK;
    return pn_epub_save_flush(p,now);
}
pn_status_t pn_epub_save_confirm(void *ctx,const pn_epub_progress_t *progress,bool turn,uint64_t now){
    pn_epub_save_t *p=ctx;pn_status_t status=pn_epub_save_presented(p,progress,turn,now);
    return status==PN_OK?pn_epub_save_tick(p,now):status;
}
