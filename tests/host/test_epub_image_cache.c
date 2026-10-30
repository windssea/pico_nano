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
static pn_status_t reject(void *ctx,const char *path){unsigned *calls=ctx;assert(!strcmp(path,"OPS/inline.png"));(*calls)++;return PN_CORRUPT;}
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
    // 奇偶起点/宽度和四边裁切均逐像素核对，padding与区域外始终保留。/ Check odd/even starts, widths and clipping on all sides pixel by pixel, preserving padding and everything outside.
    static const pn_image_rect_t rects[]={{1,1,15,4},{2,1,15,4},{1,0,16,7},{2,0,16,7},{-3,-2,18,6},{1027,3,18,8},{2000,0,10,4}};
    for(size_t k=0;k<sizeof rects/sizeof rects[0];k++){
        pn_image_rect_t r=rects[k];memset(out,0xa5,sizeof out);pn_book_id_t key;assert(pn_epub_image_cache_underlay(&b,r,&key)==(r.x>=a.width?PN_EMPTY:PN_OK));
        if(r.x<a.width){pn_book_id_t actual;out[519]^=255;assert(!pn_epub_image_cache_underlay(&b,r,&actual) && !memcmp(&actual,&key,sizeof key));out[519]^=255;
            int x=r.x>0?r.x:0,y=r.y>0?r.y:0;uint8_t old=pn_frame_get(&b,x,y);pn_frame_pixel(&b,x,y,old^1);assert(!pn_epub_image_cache_underlay(&b,r,&actual) && memcmp(&actual,&key,sizeof key));pn_frame_pixel(&b,x,y,old);}
        pn_epub_image_cache_store_region(&c,&file,"OPS/inline.png",&style,&a,r,&key);
        pn_status_t s=pn_epub_image_cache_restore_region_guarded(&c,&file,"OPS/inline.png",&style,&b,r,&key,NULL,NULL);
        if(r.x>=a.width){assert(s==PN_EMPTY);continue;}assert(s==PN_OK);
        for(int y=0;y<a.height;y++)for(int x=0;x<a.width;x++)assert(pn_frame_get(&b,x,y)==(x>=r.x && x<r.x+r.width && y>=r.y && y<r.y+r.height?pn_frame_get(&a,x,y):(x&1?10:5)));
        for(int y=0;y<a.height;y++){assert((out[y*520+516]&240u)==0xa0u);assert(out[y*520+519]==0xa5);}
        pn_image_rect_t other=r;other.x++;assert(pn_epub_image_cache_restore_region_guarded(&c,&file,"OPS/inline.png",&style,&b,other,&key,NULL,NULL)==PN_EMPTY);
        pn_book_id_t changed_key=key;changed_key.sha256[0]^=1;assert(pn_epub_image_cache_restore_region_guarded(&c,&file,"OPS/inline.png",&style,&b,r,&changed_key,NULL,NULL)==PN_EMPTY);
        unsigned calls=0;memset(out,0xa5,sizeof out);assert(pn_epub_image_cache_restore_region_guarded(&c,&file,"OPS/inline.png",&style,&b,r,&key,reject,&calls)==PN_CORRUPT && calls==1);
        for(size_t i=0;i<sizeof out;i++)assert(out[i]==0xa5);
        pn_cancel_enter(&scope,cancelled,NULL);assert(pn_epub_image_cache_restore_region_guarded(&c,&file,"OPS/inline.png",&style,&b,r,&key,NULL,NULL)==PN_CANCELLED);assert(pn_epub_image_cache_underlay(&b,r,&key)==PN_CANCELLED);pn_cancel_leave(&scope);
    }
    pn_cache_pin(&c.owner);assert(!pn_cache_reclaim(&pool,SIZE_MAX) && c.entry);pn_cache_unpin(&c.owner);assert(pn_cache_reclaim(&pool,SIZE_MAX)>0 && !c.entry);
    pool.fail_at=pool.attempts+1;pn_epub_image_cache_store(&c,&file,"OPS/cover.png",&style,&a);assert(!c.entry);pool.fail_at=0;
    size_t cap=pool.cache_caps[PN_CACHE_COVER];pool.cache_caps[PN_CACHE_COVER]=16;pn_epub_image_cache_store(&c,&file,"OPS/cover.png",&style,&a);assert(!c.entry);pool.cache_caps[PN_CACHE_COVER]=cap;
    pn_epub_image_cache_store(&c,&file,"OPS/cover.png",&style,&a);assert(c.entry);FILE *changed_file=fopen(path,"r+b");assert(changed_file && fwrite("X",1,1,changed_file)==1 && !fclose(changed_file));assert(pn_epub_image_cache_restore(&c,&file,"OPS/cover.png",&style,&b)==PN_STALE_JOB);
    pn_epub_image_cache_close(&c);assert(!pn_text_file_close(&file));assert(!pn_media_release(&media,&lease));assert(!pool.used && !pool.live);assert(!unlink(path));puts("epub image cache: lossless, source validation, edges, cancellation and bounded fallback passed");return 0;
}
