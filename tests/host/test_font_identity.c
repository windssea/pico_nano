/* SPDX-License-Identifier: Apache-2.0
 * 中文：内置SHA一致、句柄缓存与换卡失效。/ English: builtin SHA equivalence, handle caching and media invalidation. */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "pn_font.h"
#include "pn_identity.h"
#include "pn_text_file.h"
#include "pn_font_asset.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static bool cancelled(void *ctx){unsigned *remaining=ctx;if(*remaining)--*remaining;return !*remaining;}
int main(void){
    pn_text_source_t sources[]={pn_font_reading_source(),pn_font_builtin_source(),pn_font_fallback_source()};pn_book_id_t expected,actual;
    for(unsigned j=0;j<3;j++){assert(!pn_identity_stream(sources[j].ctx,sources[j].size,sources[j].read_at,&expected));assert(!pn_font_source_identity(&sources[j],&actual));assert(!memcmp(&actual,&expected,sizeof actual));}
    char path[]="/tmp/pn-font-sha-XXXXXX";int fd=mkstemp(path);assert(fd>=0 && write(fd,"abcd",4)==4 && !close(fd));
    pn_media_t media;pn_media_init(&media);pn_media_attach(&media,1);pn_media_lease_t lease;assert(!pn_media_acquire(&media,PN_MEDIA_READ,&lease));pn_text_file_t file={0};pn_text_source_t source;
    assert(!pn_text_file_open(&file,&media,&lease,path,&source));assert(!pn_font_source_identity(&source,&expected));uint64_t reads=file.io_reads;
    assert(!pn_font_source_identity(&source,&actual) && file.io_reads==reads && file.identity_hits==1 && !memcmp(&actual,&expected,sizeof actual));
    // 资源初次核验必须绑定实际font源，排版键不再扫描第二遍。/ Initial asset verification must bind its real font source so layout keys do not rescan it.
    pn_pool_t pool;assert(!pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL));
    char font_path[4096];assert(realpath("../assets/fonts/read-pico-ui.ttf",font_path));pn_font_asset_t asset={0};pn_text_source_t font_source;
    assert(!pn_font_asset_open(&asset,&pool,&media,&lease,font_path,0,44));assert(!pn_font_asset_source(&asset,&font_source));
    pn_text_file_t *asset_file=font_source.ctx;reads=asset_file->io_reads;assert(asset_file->identity_known);
    assert(!pn_font_source_identity(&font_source,&actual) && asset_file->io_reads==reads);
    pn_font_reference_t reference;assert(!pn_font_asset_details(&asset,&reference,NULL));
    assert(!pn_font_asset_close(&asset) && !pool.used && !pool.live);
    pn_text_file_t selected={0};pn_text_source_t selected_source;
    assert(!pn_font_reference_open_file(&pool,&reference,&media,&lease,&selected,&selected_source) && selected.identity_known);
    reads=selected.io_reads;assert(!pn_font_source_identity(&selected_source,&actual) && selected.io_reads==reads);assert(!pn_text_file_close(&selected));
    // 完整系统字库按源流读，缺卡/损坏时仍能取内置UI源。/ Stream the external UI face and keep resident UI available after media loss/corruption.
    assert(!pn_font_system_retained());assert(!pn_font_system_open(&pool,&media,font_path));pn_text_source_t system=pn_font_builtin_source();assert(system.ctx && system.validate && pn_font_system_retained());
    assert(pn_font_reading_source().ctx==system.ctx);
    pn_text_file_t *system_file=system.ctx;reads=system_file->io_reads;
    assert(!pn_font_system_open(&pool,&media,font_path) && system_file->io_reads==reads);
    // 已取消的同路径/替换请求不关闭已核验源；取消发生在源校验内也不得当成失效重开。
    // Cancelled same-path/replacement requests keep the verified source, including cancellation during its validation.
    size_t retained_used=pool.used,retained_leases=pn_media_active(&media);
    for(unsigned mode=0;mode<3;mode++){
        unsigned remaining=mode==1?2:1;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,&remaining);
        assert(pn_font_system_open(&pool,&media,mode==2?path:font_path)==PN_CANCELLED);pn_cancel_leave(&scope);
        assert(pn_font_system_available() && pn_font_builtin_source().ctx==system.ctx && system_file->io_reads==reads && pool.used==retained_used && pn_media_active(&media)==retained_leases);
    }
    pn_font_t ui={0};assert(!pn_font_open(&ui,&pool,&system,24));pn_font_close(&ui);
    assert(!pn_font_system_close());assert(!pn_font_builtin_source().ctx && !pn_font_system_retained());
    assert(pn_font_system_open(&pool,&media,path)!=PN_OK && !pn_font_builtin_source().ctx && !pool.used && !pool.live);
    assert(!pn_text_file_close(&file));FILE *f=fopen(path,"wb");assert(f && fwrite("efgh",1,4,f)==4 && !fclose(f));
    assert(!pn_text_file_open(&file,&media,&lease,path,&source) && !file.identity_known);assert(!pn_font_source_identity(&source,&actual) && memcmp(&actual,&expected,sizeof actual));
    assert(!pn_font_system_open(&pool,&media,font_path));pn_media_detach(&media);assert(pn_font_system_retained() && !pn_font_system_available());
    assert(!pn_font_builtin_source().ctx && !pn_font_system_close() && !pool.used && !pool.live);
    expected=actual;assert(pn_font_source_identity(&source,&actual)==PN_STALE_MEDIA && !memcmp(&actual,&expected,sizeof actual));assert(!pn_text_file_close(&file));pn_media_release(&media,&lease);unlink(path);return 0;
}
