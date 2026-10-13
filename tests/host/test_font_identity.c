/* SPDX-License-Identifier: Apache-2.0
 * 中文：内置SHA一致、句柄缓存与换卡失效。/ English: builtin SHA equivalence, handle caching and media invalidation. */
#define _POSIX_C_SOURCE 200809L
#include "pn_font.h"
#include "pn_identity.h"
#include "pn_text_file.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void){
    pn_text_source_t sources[]={pn_font_reading_source(),pn_font_builtin_source(),pn_font_fallback_source()};pn_book_id_t expected,actual;
    for(unsigned j=0;j<3;j++){assert(!pn_identity_stream(sources[j].ctx,sources[j].size,sources[j].read_at,&expected));assert(!pn_font_source_identity(&sources[j],&actual));assert(!memcmp(&actual,&expected,sizeof actual));}
    char path[]="/tmp/pn-font-sha-XXXXXX";int fd=mkstemp(path);assert(fd>=0 && write(fd,"abcd",4)==4 && !close(fd));
    pn_media_t media;pn_media_init(&media);pn_media_attach(&media,1);pn_media_lease_t lease;assert(!pn_media_acquire(&media,PN_MEDIA_READ,&lease));pn_text_file_t file={0};pn_text_source_t source;
    assert(!pn_text_file_open(&file,&media,&lease,path,&source));assert(!pn_font_source_identity(&source,&expected));uint64_t reads=file.io_reads;
    assert(!pn_font_source_identity(&source,&actual) && file.io_reads==reads && file.identity_hits==1 && !memcmp(&actual,&expected,sizeof actual));
    assert(!pn_text_file_close(&file));FILE *f=fopen(path,"wb");assert(f && fwrite("efgh",1,4,f)==4 && !fclose(f));
    assert(!pn_text_file_open(&file,&media,&lease,path,&source) && !file.identity_known);assert(!pn_font_source_identity(&source,&actual) && memcmp(&actual,&expected,sizeof actual));
    pn_media_detach(&media);expected=actual;assert(pn_font_source_identity(&source,&actual)==PN_STALE_MEDIA && !memcmp(&actual,&expected,sizeof actual));assert(!pn_text_file_close(&file));pn_media_release(&media,&lease);unlink(path);return 0;
}
