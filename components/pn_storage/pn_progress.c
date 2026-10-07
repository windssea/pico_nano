/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：将TXT源位置、内容身份和解析策略写入有版本的载荷。
 * English: versioned payload for TXT source position, content identity and parsing policy.
 * 冻结：不存永久页码；字符边界由解析器保证，不跨身份恢复。
 * Frozen: no permanent page numbers; parser ensures character boundaries; never restore across identities.
 */
#include "pn_progress.h"
#include <string.h>
static void put(uint8_t *out, uint64_t value, unsigned n) {
    for (unsigned i=0;i<n;i++) out[i]=(uint8_t)(value>>(8*i));
}
static uint64_t get(const uint8_t *in, unsigned n) {
    uint64_t value=0; for (unsigned i=0;i<n;i++) value|=(uint64_t)in[i]<<(8*i); return value;
}
static bool valid(const pn_txt_progress_t *p) {
    return p && p->source_offset<=p->source_size && p->paragraph_version &&
        p->encoding>=PN_TEXT_UTF8 && p->encoding<=PN_TEXT_GBK;
}
pn_status_t pn_txt_progress_save(const pn_journal_io_t *io, const pn_txt_progress_t *p) {
    if (!valid(p)) return PN_INVALID;
    uint8_t bytes[64]={0}; memcpy(bytes,"PNTP",4); put(bytes+4,1,2); put(bytes+6,p->encoding,2);
    memcpy(bytes+8,p->book.sha256,32); put(bytes+40,p->source_offset,8);
    put(bytes+48,p->source_size,8); put(bytes+56,p->paragraph_version,4);
    return pn_journal_save(io,bytes,sizeof bytes);
}
pn_status_t pn_txt_progress_load(const pn_journal_io_t *io, const pn_book_id_t *expected, pn_txt_progress_t *p) {
    if (!expected || !p) return PN_INVALID;
    pn_record_t record; pn_status_t status=pn_journal_load(io,&record); if (status!=PN_OK) return status;
    const uint8_t *bytes=record.payload;
    if (record.size!=64 || memcmp(bytes,"PNTP",4)!=0) return PN_CORRUPT;
    if (get(bytes+4,2)!=1) return PN_UNSUPPORTED;
    if (get(bytes+60,4)!=0) return PN_UNSUPPORTED;
    pn_txt_progress_t result={.source_offset=get(bytes+40,8),.source_size=get(bytes+48,8),
        .encoding=(pn_text_encoding_t)get(bytes+6,2),.paragraph_version=(uint32_t)get(bytes+56,4)};
    memcpy(result.book.sha256,bytes+8,32);
    if (!valid(&result)) return PN_CORRUPT;
    if (memcmp(result.book.sha256,expected->sha256,32)!=0) return PN_STALE_JOB;
    *p=result; return PN_OK;
}
