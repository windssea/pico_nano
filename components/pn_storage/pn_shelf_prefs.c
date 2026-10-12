/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 书架设置与翻页设置分开保存，旧记录不迁移。/ Store shelf and page-turn preferences separately without migrating old records.
 */
#include "pn_shelf_prefs.h"
#include <stdio.h>
#include <string.h>
pn_shelf_prefs_t pn_shelf_prefs_default(void){return (pn_shelf_prefs_t){3,PN_SHELF_DEFAULT};}
static bool valid(const pn_shelf_prefs_t *p){return p && (p->columns==3 || p->columns==4) && p->style<PN_SHELF_STYLE_COUNT;}
pn_status_t pn_shelf_prefs_files(pn_journal_files_t *f,pn_media_t *m,const pn_media_lease_t *l,const char *root,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];int n=snprintf(a,sizeof a,"%s/shelf.a",root);
    if(n<0 || (size_t)n>=sizeof a)return PN_LIMIT;
    n=snprintf(b,sizeof b,"%s/shelf.b",root);if(n<0 || (size_t)n>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(f,m,l,a,b,io);
}
pn_status_t pn_shelf_prefs_load(const pn_journal_io_t *io,pn_shelf_prefs_t *out){
    if(!io || !out)return PN_INVALID;
    pn_record_t r;pn_status_t s=pn_journal_load(io,&r);if(s!=PN_OK)return s;
    if(r.size!=8 || memcmp(r.payload,"PNSH",4))return PN_CORRUPT;
    if(r.payload[4]!=1 || r.payload[7])return PN_UNSUPPORTED;
    pn_shelf_prefs_t p={r.payload[5],r.payload[6]};if(!valid(&p))return PN_CORRUPT;
    *out=p;return PN_OK;
}
pn_status_t pn_shelf_prefs_save(const pn_journal_io_t *io,const pn_shelf_prefs_t *p){
    if(!io || !valid(p))return PN_INVALID;
    uint8_t data[8]={'P','N','S','H',1,p->columns,p->style,0};return pn_journal_save(io,data,sizeof data);
}
