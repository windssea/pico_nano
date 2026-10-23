/* SPDX-License-Identifier: Apache-2.0
 * 中文：无损、奇数边界、源变化、取消及预算退路。/ English: lossless pixels, odd edges, changed sources, cancellation and budget fallback. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_image_cache.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
static bool cancelled(void *ctx){(void)ctx;return true;}
int main(void){
    char path[]="/tmp/pn-image-cache-XXXXXX";int fd=mkstemp(path);assert(fd>=0 && write(fd,"verified image source",21)==21 && !close(fd));
    pn_pool_t pool;assert(!pn_pool_init(&pool,1024u*1024u,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);pn_media_lease_t lease={0};assert(!pn_media_attach(&media,1) && !pn_media_acquire(&media,PN_MEDIA_READ,&lease));
    pn_text_file_t file={0};pn_text_source_t source;pn_book_id_t identity;assert(!pn_text_file_open(&file,&media,&lease,path,&source));assert(!pn_text_file_identity_pooled(&pool,&source,&identity));
    pn_epub_image_cache_t c={0};pn_epub_image_cache_init(&c,&pool);pn_style_t style=pn_style_default(44);
    uint8_t in[7*520],out[7*520];pn_frame_t a={in,1033,7,520},b={out,1033,7,520};
    for(unsigned pattern=0;pattern<4;pattern++){
        for(size_t i=0;i<sizeof in;i++)in[i]=pattern==0?0xff:pattern==1?(uint8_t)(i%251):pattern==2?(uint8_t)((i/127)%16*17u):(uint8_t)(i*73u+(i>>3));
        memset(out,0xa5,sizeof out);pn_epub_image_cache_store(&c,&file,"OPS/cover.png",&style,&a);assert(c.entry);
        assert(!pn_epub_image_cache_restore(&c,&file,"OPS/cover.png",&style,&b));
        for(int y=0;y<a.height;y++){for(int x=0;x<a.width;x++)assert(pn_frame_get(&a,x,y)==pn_frame_get(&b,x,y));assert((out[y*520+516]&240u)==0xa0u);assert(out[y*520+519]==0xa5);}
    }
    assert(pn_epub_image_cache_restore(&c,&file,"OPS/other.png",&style,&b)==PN_EMPTY);pn_style_t changed=style;changed.background=7;assert(pn_epub_image_cache_restore(&c,&file,"OPS/cover.png",&changed,&b)==PN_EMPTY);
    pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,NULL);assert(pn_epub_image_cache_restore(&c,&file,"OPS/cover.png",&style,&b)==PN_CANCELLED);pn_cancel_leave(&scope);
    pn_cache_pin(&c.owner);assert(!pn_cache_reclaim(&pool,SIZE_MAX) && c.entry);pn_cache_unpin(&c.owner);assert(pn_cache_reclaim(&pool,SIZE_MAX)>0 && !c.entry);
    pool.fail_at=pool.attempts+1;pn_epub_image_cache_store(&c,&file,"OPS/cover.png",&style,&a);assert(!c.entry);pool.fail_at=0;
    size_t cap=pool.cache_caps[PN_CACHE_COVER];pool.cache_caps[PN_CACHE_COVER]=16;pn_epub_image_cache_store(&c,&file,"OPS/cover.png",&style,&a);assert(!c.entry);pool.cache_caps[PN_CACHE_COVER]=cap;
    pn_epub_image_cache_store(&c,&file,"OPS/cover.png",&style,&a);assert(c.entry);FILE *changed_file=fopen(path,"r+b");assert(changed_file && fwrite("X",1,1,changed_file)==1 && !fclose(changed_file));assert(pn_epub_image_cache_restore(&c,&file,"OPS/cover.png",&style,&b)==PN_STALE_JOB);
    pn_epub_image_cache_close(&c);assert(!pn_text_file_close(&file));assert(!pn_media_release(&media,&lease));assert(!pool.used && !pool.live);assert(!unlink(path));puts("epub image cache: lossless, source validation, edges, cancellation and bounded fallback passed");return 0;
}
