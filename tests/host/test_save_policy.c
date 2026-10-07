/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读位置自动保存控制，接收已显示的位置。
 * English: automatic progress saving from positions already displayed.
 * 冻结：owner串行调用；失败保留dirty；同步I/O不承诺deadline。
 * Frozen: serialized owner calls; failures retain dirty state; synchronous I/O has no deadline guarantee.
 */
#include "pn_save_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint8_t data[2][PN_JOURNAL_RECORD_MAX];size_t sizes[2];unsigned writes;bool fail; } disk_t;
static pn_status_t read_record(void *ctx,unsigned slot,uint8_t *out,size_t cap,size_t *n) {
    disk_t *d=ctx;if (!d->sizes[slot]) return PN_EMPTY;assert(d->sizes[slot]<=cap);*n=d->sizes[slot];memcpy(out,d->data[slot],*n);return PN_OK;
}
static pn_status_t write_record(void *ctx,unsigned slot,const uint8_t *bytes,size_t n) {
    disk_t *d=ctx;d->writes++;if(d->fail)return PN_IO;memcpy(d->data[slot],bytes,n);d->sizes[slot]=n;return PN_OK;
}
int main(void) {
    disk_t d={0};pn_journal_io_t io={&d,read_record,write_record};
    pn_txt_progress_t location={.source_size=100,.encoding=PN_TEXT_UTF8,.paragraph_version=1};location.book.sha256[0]=42;
    pn_save_policy_t p;assert(pn_save_policy_init(&p,&io,&location.book,&location,0)==PN_OK);
    assert(pn_save_policy_flush(&p,10)==PN_OK && d.writes==0);
    for (unsigned i=1;i<=5;i++) {
        location.source_offset=i;assert(pn_save_policy_presented(&p,&location,true,10+i)==PN_OK);
        assert(pn_save_policy_tick(&p,10+i)==PN_OK);
        assert(d.writes==(i==5?1u:0u));
    }
    assert(!p.dirty);pn_txt_progress_t restored;
    assert(pn_txt_progress_load(&io,&location.book,&restored)==PN_OK && restored.source_offset==5);
    location.source_offset=6;assert(pn_save_policy_presented(&p,&location,true,100)==PN_OK);
    location.source_offset=7;assert(pn_save_policy_presented(&p,&location,true,29000)==PN_OK);
    assert(pn_save_policy_tick(&p,30099)==PN_OK && d.writes==1);
    d.fail=true;assert(pn_save_policy_tick(&p,30100)==PN_IO && p.dirty && p.turns==2 && p.dirty_since_ms==100);
    assert(pn_save_policy_flush(&p,30101)==PN_IO && p.dirty);
    d.fail=false;assert(pn_save_policy_tick(&p,30102)==PN_BUSY && p.dirty);
    assert(pn_save_policy_tick(&p,31101)==PN_OK && !p.dirty);
    location.source_offset=8;assert(pn_save_policy_presented(&p,&location,false,31102)==PN_OK);
    assert(pn_save_policy_flush(&p,31103)==PN_OK && !p.dirty);
    assert(pn_save_policy_tick(&p,0)==PN_INVALID);
    pn_txt_progress_t wrong=location;wrong.book.sha256[1]=1;
    assert(pn_save_policy_presented(&p,&wrong,true,31104)==PN_STALE_JOB && !p.dirty);
    assert(pn_save_policy_presented(&p,&location,true,31104)==PN_OK && !p.dirty);
    assert(pn_save_policy_init(&p,&io,&location.book,NULL,0)==PN_OK);
    assert(pn_save_policy_presented(&p,&location,false,UINT64_MAX-30000)==PN_OK);
    assert(pn_save_policy_tick(&p,UINT64_MAX)==PN_OK && !p.dirty);
    puts("save policy: five turns, thirty seconds, flush, retry and stale identity passed");return 0;
}
