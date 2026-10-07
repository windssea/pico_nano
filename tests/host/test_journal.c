/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界A/B记录与文件适配；调用方提供稳定路径和有效租约。
 * English: bounded A/B records and file adapter; caller provides stable paths and valid leases.
 * 冻结：不格式化、不创建目录；owner串行使用，路径不可指向同一文件。
 * Frozen: no formatting or directory creation; serialized owner calls, distinct backing files.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_journal.h"
#include "pn_progress.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct { uint8_t bytes[2][PN_JOURNAL_RECORD_MAX]; size_t sizes[2]; bool exists[2]; size_t cutoff; bool fail_sync; bool fail_read; } disk_t;
static pn_status_t read_slot(void *ctx, unsigned slot, uint8_t *out, size_t cap, size_t *size) {
    disk_t *d=ctx;
    if (d->fail_read) return PN_IO;
    if (!d->exists[slot]) return PN_EMPTY;
    assert(d->sizes[slot]<=cap); *size=d->sizes[slot]; memcpy(out,d->bytes[slot],*size); return PN_OK;
}
static pn_status_t write_slot(void *ctx, unsigned slot, const uint8_t *data, size_t size) {
    disk_t *d=ctx; size_t n=size<d->cutoff?size:d->cutoff;
    d->exists[slot]=true; d->sizes[slot]=n; memcpy(d->bytes[slot],data,n);
    return n<size || d->fail_sync ? PN_IO : PN_OK;
}
int main(int argc, char **argv) {
    if (argc==3 || argc==4) {
        pn_media_t m; pn_media_init(&m); assert(pn_media_attach(&m,1)==PN_OK);
        pn_media_lease_t l; assert(pn_media_acquire(&m,PN_MEDIA_WRITE,&l)==PN_OK);
        char a[384],b[384]; snprintf(a,sizeof a,"%s/a",argv[2]); snprintf(b,sizeof b,"%s/b",argv[2]);
        pn_journal_files_t files; pn_journal_io_t io;
        assert(pn_journal_files_init(&files,&m,&l,a,b,&io)==PN_OK);
        pn_txt_progress_t p={.source_size=1000,.encoding=PN_TEXT_UTF8,.paragraph_version=1}; p.book.sha256[0]=42;
        if (argc==4 && strcmp(argv[1],"save")==0) {
            p.source_offset=strtoull(argv[3],NULL,10); assert(pn_txt_progress_save(&io,&p)==PN_OK);
        } else {
            assert(strcmp(argv[1],"load")==0);
            pn_book_id_t expected=p.book; assert(pn_txt_progress_load(&io,&expected,&p)==PN_OK);
            printf("%llu\n",(unsigned long long)p.source_offset);
        }
        assert(pn_media_release(&m,&l)==PN_OK); return 0;
    }
    disk_t disk={.cutoff=SIZE_MAX}; pn_journal_io_t io={&disk,read_slot,write_slot}; pn_record_t r={0};
    assert(pn_journal_load(&io,&r)==PN_EMPTY);
    const uint8_t old[]={1,2,3}, newer[]={4,5,6,7};
    assert(pn_journal_save(&io,old,sizeof old)==PN_OK);
    assert(pn_journal_load(&io,&r)==PN_OK && r.sequence==1 && r.size==3 && memcmp(r.payload,old,3)==0);
    disk_t baseline=disk;
    for (size_t cut=0;cut<24;cut++) {
        disk=baseline; disk.cutoff=cut;
        assert(pn_journal_save(&io,newer,sizeof newer)==PN_IO);
        assert(pn_journal_load(&io,&r)==PN_OK && r.sequence==1 && memcmp(r.payload,old,3)==0);
    }
    uint8_t full[PN_JOURNAL_PAYLOAD_MAX]; memset(full,0xa5,sizeof full);
    for (size_t cut=0;cut<PN_JOURNAL_RECORD_MAX;cut++) {
        disk=baseline; disk.cutoff=cut;
        assert(pn_journal_save(&io,full,sizeof full)==PN_IO);
        assert(pn_journal_load(&io,&r)==PN_OK && r.sequence==1);
    }
    disk=baseline; disk.fail_sync=true;
    assert(pn_journal_save(&io,newer,sizeof newer)==PN_IO);
    assert(pn_journal_load(&io,&r)==PN_OK && r.sequence==2);
    disk=baseline; assert(pn_journal_save(&io,newer,sizeof newer)==PN_OK);
    disk_t two=disk;
    for (size_t byte=0;byte<24;byte++) {
        for (unsigned bit=0;bit<8;bit++) {
            disk=two; disk.bytes[1][byte]^=(uint8_t)(1u<<bit);
            pn_status_t result=pn_journal_load(&io,&r);
            if (byte==4 || byte==5) assert(result==PN_UNSUPPORTED);
            else assert(result==PN_OK && r.sequence==1);
        }
    }
    disk=two; disk.bytes[1][16]^=1;
    assert(pn_journal_load(&io,&r)==PN_OK && r.sequence==1);
    disk.bytes[0][16]^=1; assert(pn_journal_load(&io,&r)==PN_CORRUPT);
    assert(pn_journal_save(&io,old,3)==PN_CORRUPT);
    disk=baseline;
    const uint8_t last_sequence[]={ 0x50,0x4e,0x4a,0x52,0x01,0x00,0x03,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x01,0x02,0x03,0xb5,0xab,0x56,0x5f };
    memcpy(disk.bytes[0],last_sequence,sizeof last_sequence); disk.sizes[0]=sizeof last_sequence;
    assert(pn_journal_load(&io,&r)==PN_OK && r.sequence==UINT64_MAX);
    assert(pn_journal_save(&io,newer,4)==PN_LIMIT && !disk.exists[1]);
    disk=baseline; disk.fail_read=true;
    assert(pn_journal_save(&io,newer,4)==PN_IO);
    assert(!disk.exists[1]);
    assert(pn_journal_save(&io,old,PN_JOURNAL_PAYLOAD_MAX+1)==PN_LIMIT);
    assert(pn_journal_load(NULL,&r)==PN_INVALID);
    disk=(disk_t){.cutoff=SIZE_MAX};
    pn_txt_progress_t progress={.source_size=1000,.source_offset=600,.encoding=PN_TEXT_UTF8,.paragraph_version=1};
    progress.book.sha256[0]=42;
    assert(pn_txt_progress_save(&io,&progress)==PN_OK);
    pn_txt_progress_t restored={0};
    assert(pn_txt_progress_load(&io,&progress.book,&restored)==PN_OK && restored.source_offset==600 && restored.source_size==1000);
    pn_book_id_t other=progress.book; other.sha256[31]=1;
    assert(pn_txt_progress_load(&io,&other,&restored)==PN_STALE_JOB && restored.source_offset==600);
    progress.source_offset=1001; assert(pn_txt_progress_save(&io,&progress)==PN_INVALID);
    progress.source_offset=0; progress.paragraph_version=0; assert(pn_txt_progress_save(&io,&progress)==PN_INVALID);
    char dir[]="/tmp/pn-journal-XXXXXX"; assert(mkdtemp(dir));
    char a[384],b[384]; snprintf(a,sizeof a,"%s/a",dir); snprintf(b,sizeof b,"%s/b",dir);
    pn_media_t media; pn_media_init(&media); assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t lease; assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
    pn_journal_files_t files; pn_journal_io_t real;
    assert(pn_journal_files_init(&files,&media,&lease,a,b,&real)==PN_OK);
    assert(pn_journal_save(&real,old,3)==PN_OK);
    assert(pn_journal_save(&real,newer,4)==PN_OK);
    pn_journal_files_t reopened; pn_journal_io_t reopened_io;
    assert(pn_journal_files_init(&reopened,&media,&lease,a,b,&reopened_io)==PN_OK);
    assert(pn_journal_load(&reopened_io,&r)==PN_OK && r.sequence==2 && memcmp(r.payload,newer,4)==0);
    FILE *f=fopen(b,"wb"); assert(f); assert(fwrite("broken",1,6,f)==6); assert(fclose(f)==0);
    assert(pn_journal_load(&reopened_io,&r)==PN_OK && r.sequence==1);
    assert(pn_media_detach(&media)==PN_OK); assert(pn_journal_load(&real,&r)==PN_STALE_MEDIA);
    assert(pn_media_release(&media,&lease)==PN_OK);
    assert(pn_media_attach(&media,2)==PN_OK); assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    assert(pn_journal_files_init(&files,&media,&lease,a,b,&real)==PN_OK);
    assert(pn_journal_save(&real,old,3)==PN_INVALID);
    assert(pn_journal_load(&real,&r)==PN_OK);
    assert(pn_media_release(&media,&lease)==PN_OK); assert(unlink(a)==0 && unlink(b)==0 && rmdir(dir)==0);
    puts("journal: interruption, corruption, reopen and leases passed"); return 0;
}
