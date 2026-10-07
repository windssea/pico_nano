/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读位置自动保存控制，接收已显示的位置。
 * English: automatic progress saving from positions already displayed.
 * 冻结：owner串行调用；失败保留dirty；同步I/O不承诺deadline。
 * Frozen: serialized owner calls; failures retain dirty state; synchronous I/O has no deadline guarantee.
 */
#include "pn_save_policy.h"
#include <string.h>
static bool valid(const pn_txt_progress_t *p) {
    return p && p->source_offset<=p->source_size && p->paragraph_version &&
        p->encoding>=PN_TEXT_UTF8 && p->encoding<=PN_TEXT_GBK;
}
static bool same(const pn_txt_progress_t *a,const pn_txt_progress_t *b) {
    return a->source_offset==b->source_offset && a->source_size==b->source_size &&
        a->encoding==b->encoding && a->paragraph_version==b->paragraph_version;
}
pn_status_t pn_save_policy_init(pn_save_policy_t *p,const pn_journal_io_t *io,
    const pn_book_id_t *expected,const pn_txt_progress_t *baseline,uint64_t now) {
    if (!p || !io || !io->read || !io->write_sync || !expected || (baseline && !valid(baseline))) return PN_INVALID;
    if (baseline && memcmp(baseline->book.sha256,expected->sha256,32)!=0) return PN_STALE_JOB;
    pn_save_policy_t result={.io=*io,.expected=*expected,.last_now_ms=now,.has_location=baseline!=NULL};
    if (baseline) result.current=*baseline;
    *p=result;return PN_OK;
}
pn_status_t pn_save_policy_presented(pn_save_policy_t *p,const pn_txt_progress_t *location,
    bool page_turn,uint64_t now) {
    if (!p || !p->io.read || !p->io.write_sync || !valid(location) || now<p->last_now_ms) return PN_INVALID;
    if (memcmp(location->book.sha256,p->expected.sha256,32)!=0) return PN_STALE_JOB;
    if (p->has_location && location->source_size!=p->current.source_size) return PN_INVALID;
    p->last_now_ms=now;
    if (p->has_location && same(&p->current,location)) return PN_OK;
    if (!p->dirty) p->dirty_since_ms=now;
    p->current=*location;p->has_location=true;p->dirty=true;
    if (page_turn && p->turns<5) p->turns++;
    return PN_OK;
}
pn_status_t pn_save_policy_flush(pn_save_policy_t *p,uint64_t now) {
    if (!p || !p->io.read || !p->io.write_sync || now<p->last_now_ms) return PN_INVALID;
    p->last_now_ms=now;
    if (!p->dirty) return PN_OK;
    pn_status_t status=pn_txt_progress_save(&p->io,&p->current);
    if (status==PN_OK) {p->dirty=false;p->turns=0;p->dirty_since_ms=0;p->retry_pending=false;}
    else {p->retry_pending=true;p->last_failed_ms=now;}
    return status;
}
pn_status_t pn_save_policy_tick(pn_save_policy_t *p,uint64_t now) {
    if (!p || !p->io.read || !p->io.write_sync || now<p->last_now_ms) return PN_INVALID;
    p->last_now_ms=now;
    if (!p->dirty || (p->turns<5 && now-p->dirty_since_ms<30000)) return PN_OK;
    if (p->retry_pending && now-p->last_failed_ms<1000) return PN_BUSY;
    return pn_save_policy_flush(p,now);
}
