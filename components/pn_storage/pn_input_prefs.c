/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：PNIP v1载荷：魔数、版本、标志，共6字节。/ English: PNIP v1 payload: magic, version and flags, six bytes total.
 */
#include "pn_input_prefs.h"
#include <stdio.h>
#include <string.h>
pn_status_t pn_input_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];int n=snprintf(a,sizeof a,"%s/input.a",root);
    if(n<0 || (size_t)n>=sizeof a)return PN_LIMIT;
    n=snprintf(b,sizeof b,"%s/input.b",root);
    if(n<0 || (size_t)n>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(files,media,lease,a,b,io);
}
pn_status_t pn_input_prefs_load(const pn_journal_io_t *io,uint8_t *flags){
    if(!io || !flags)return PN_INVALID;
    pn_record_t record;pn_status_t status=pn_journal_load(io,&record);
    if(status!=PN_OK)return status;
    if(record.size!=6 || memcmp(record.payload,"PNIP",4))return PN_CORRUPT;
    if(record.payload[4]!=1 || (record.payload[5]&~PN_INPUT_FLAGS_ALL))return PN_UNSUPPORTED;
    *flags=record.payload[5];return PN_OK;
}
pn_status_t pn_input_prefs_save(const pn_journal_io_t *io,uint8_t flags){
    if(!io || (flags&~PN_INPUT_FLAGS_ALL))return PN_INVALID;
    uint8_t payload[6]={'P','N','I','P',1,flags};return pn_journal_save(io,payload,sizeof payload);
}
