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
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static uint32_t crc32(const uint8_t *data, size_t size) {
    uint32_t crc=UINT32_MAX;
    for (size_t i=0;i<size;i++) {
        crc^=data[i];
        for (unsigned bit=0;bit<8;bit++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
static uint64_t get_le(const uint8_t *p, unsigned n) {
    uint64_t v=0;
    for (unsigned i=0;i<n;i++) v|=(uint64_t)p[i]<<(8*i);
    return v;
}
static void put_le(uint8_t *p, uint64_t v, unsigned n) {
    for (unsigned i=0;i<n;i++) p[i]=(uint8_t)(v>>(8*i));
}
static pn_status_t decode(const uint8_t *data, size_t n, pn_record_t *r) {
    if (n<20 || n>PN_JOURNAL_RECORD_MAX || memcmp(data,"PNJR",4)!=0) return PN_CORRUPT;
    if (get_le(data+4,2)!=1) return PN_UNSUPPORTED;
    size_t size=(size_t)get_le(data+6,2);
    if (size>PN_JOURNAL_PAYLOAD_MAX || n!=20+size || get_le(data+8,8)==0 ||
        get_le(data+16+size,4)!=crc32(data,16+size)) return PN_CORRUPT;
    memset(r,0,sizeof *r); r->size=size; r->sequence=get_le(data+8,8);
    memcpy(r->payload,data+16,size); return PN_OK;
}
static pn_status_t latest(const pn_journal_io_t *io, pn_record_t *out, unsigned *slot) {
    if (!io || !io->read || !out || !slot) return PN_INVALID;
    pn_record_t records[2]; pn_status_t state[2];
    uint8_t bytes[PN_JOURNAL_RECORD_MAX];
    for (unsigned i=0;i<2;i++) {
        size_t n=0; state[i]=io->read(io->ctx,i,bytes,sizeof bytes,&n);
        if (state[i]==PN_OK) state[i]=decode(bytes,n,&records[i]);
        if (state[i]!=PN_OK && state[i]!=PN_EMPTY && state[i]!=PN_CORRUPT) return state[i];
    }
    if (state[0]!=PN_OK && state[1]!=PN_OK)
        return state[0]==PN_EMPTY && state[1]==PN_EMPTY ? PN_EMPTY : PN_CORRUPT;
    unsigned pick=state[0]==PN_OK ? 0:1;
    if (state[0]==PN_OK && state[1]==PN_OK) {
        if (records[0].sequence==records[1].sequence &&
            (records[0].size!=records[1].size || memcmp(records[0].payload,records[1].payload,records[0].size)!=0)) return PN_CORRUPT;
        if (records[1].sequence>records[0].sequence) pick=1;
    }
    *out=records[pick]; *slot=pick; return PN_OK;
}
pn_status_t pn_journal_load(const pn_journal_io_t *io, pn_record_t *record) {
    unsigned slot=0; return latest(io,record,&slot);
}
pn_status_t pn_journal_save(const pn_journal_io_t *io, const uint8_t *payload, size_t size) {
    if (!io || !io->read || !io->write_sync || (!payload && size)) return PN_INVALID;
    if (size>PN_JOURNAL_PAYLOAD_MAX) return PN_LIMIT;
    pn_record_t old; unsigned active=1; pn_status_t status=latest(io,&old,&active);
    if (status!=PN_OK && status!=PN_EMPTY) return status;
    if (status==PN_OK && old.sequence==UINT64_MAX) return PN_LIMIT;
    uint64_t sequence=status==PN_OK ? old.sequence+1:1;
    uint8_t bytes[PN_JOURNAL_RECORD_MAX]={0};
    memcpy(bytes,"PNJR",4); put_le(bytes+4,1,2); put_le(bytes+6,size,2); put_le(bytes+8,sequence,8);
    if (size) memcpy(bytes+16,payload,size);
    put_le(bytes+16+size,crc32(bytes,16+size),4);
    unsigned target=1-active; status=io->write_sync(io->ctx,target,bytes,size+20);
    if (status!=PN_OK) return status;
    uint8_t check[PN_JOURNAL_RECORD_MAX]; size_t n=0;
    status=io->read(io->ctx,target,check,sizeof check,&n);
    if (status!=PN_OK) return status;
    if (n!=size+20 || memcmp(bytes,check,n)!=0) return PN_CORRUPT;
    return PN_OK;
}
static pn_status_t allowed(pn_journal_files_t *files, bool writing) {
    pn_status_t s=pn_media_validate(files->media,&files->lease);
    if (s!=PN_OK) return s;
    if (files->lease.access==PN_MEDIA_USB || (writing && files->lease.access!=PN_MEDIA_WRITE)) return PN_INVALID;
    return PN_OK;
}
static pn_status_t file_read(void *ctx, unsigned slot, uint8_t *out, size_t cap, size_t *size) {
    pn_journal_files_t *files=ctx; pn_status_t s=allowed(files,false);
    if (s!=PN_OK) return s;
    FILE *f=fopen(files->paths[slot],"rb");
    if (!f) return errno==ENOENT ? PN_EMPTY:PN_IO;
    size_t n=fread(out,1,cap,f); int extra=fgetc(f);
    bool error=ferror(f)!=0; if (fclose(f)!=0) error=true;
    s=allowed(files,false); if (s!=PN_OK) return s;
    if (error) return PN_IO;
    if (extra!=EOF) return PN_CORRUPT;
    *size=n; return PN_OK;
}
static pn_status_t file_write(void *ctx, unsigned slot, const uint8_t *data, size_t n) {
    pn_journal_files_t *files=ctx; pn_status_t s=allowed(files,true);
    if (s!=PN_OK) return s;
    FILE *f=fopen(files->paths[slot],"wb"); if (!f) return PN_IO;
    bool error=fwrite(data,1,n,f)!=n;
    if (fflush(f)!=0) error=true;
    if (!error && fsync(fileno(f))!=0) error=true;
    if (fclose(f)!=0) error=true;
    s=allowed(files,true); if (s!=PN_OK) return s;
    return error ? PN_IO:PN_OK;
}
pn_status_t pn_journal_files_init(pn_journal_files_t *files, pn_media_t *media,
    const pn_media_lease_t *lease, const char *a, const char *b, pn_journal_io_t *io) {
    if (!files || !media || !lease || !a || !b || !io || !*a || !*b || strcmp(a,b)==0) return PN_INVALID;
    if (strlen(a)>=PN_JOURNAL_PATH_MAX || strlen(b)>=PN_JOURNAL_PATH_MAX) return PN_LIMIT;
    pn_status_t s=pn_media_validate(media,lease); if (s!=PN_OK) return s;
    if (lease->access==PN_MEDIA_USB) return PN_INVALID;
    files->media=media; files->lease=*lease; strcpy(files->paths[0],a); strcpy(files->paths[1],b);
    *io=(pn_journal_io_t){files,file_read,file_write}; return PN_OK;
}
