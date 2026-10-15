/* SPDX-License-Identifier: Apache-2.0
 * 中文：字形回收不关闭face、不改变输出，低预算与失败后仍可重绘。/ English: glyph eviction preserves faces and output, including redraw after low-budget failures. */
#include "pn_font.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <ft2build.h>
#include FT_FREETYPE_H
static size_t outline_loads;
FT_Error __real_FT_Load_Glyph(FT_Face,FT_UInt,FT_Int32);
FT_Error __wrap_FT_Load_Glyph(FT_Face face,FT_UInt index,FT_Int32 flags){outline_loads++;return __real_FT_Load_Glyph(face,index,flags);}
/// 不同字号的菜单字符反复交替，不应互相驱逐字宽。/ Alternating menu characters at different sizes must not evict each other's advances.
static void mixed_sizes(pn_font_t *font){
    const uint32_t characters[]={'3','I','Z','H','>'};const int sizes[]={24,26,30,36,52};int32_t advances[5];
    for(unsigned i=0;i<5;i++){assert(pn_font_size(font,sizes[i])==PN_OK);assert(pn_font_advance(font,characters[i],&advances[i])==PN_OK);}
    size_t loaded=outline_loads;
    for(unsigned pass=0;pass<12;pass++)for(unsigned i=0;i<5;i++){int32_t actual;assert(pn_font_size(font,sizes[i])==PN_OK);assert(pn_font_advance(font,characters[i],&actual)==PN_OK && actual==advances[i]);}
    assert(outline_loads==loaded);assert(pn_font_size(font,44)==PN_OK);
}
int main(void){
    pn_pool_t pool;assert(!pn_pool_init(&pool,4u*1024u*1024u,NULL,NULL,NULL));
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(!pn_font_open(&font,&pool,&source,44));
    mixed_sizes(&font);
    uint8_t pixels[5000],expected[5000];pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,sizeof pixels,100,100));
    int32_t width,again;assert(!pn_font_advance(&font,'W',&width));pn_frame_clear(&frame,15);assert(!pn_font_draw(&font,&frame,'W',10*64,60,PN_FONT_GRAY));memcpy(expected,pixels,sizeof pixels);
    assert(!pn_font_draw(&font,&frame,'W',10*64,60,PN_FONT_GRAY));pn_cache_snapshot(&pool);
    assert(pool.cache_stats[PN_CACHE_GLYPH].bytes && pool.cache_stats[PN_CACHE_GLYPH].hits);
    size_t bytes=pool.cache_stats[PN_CACHE_GLYPH].bytes,used=pool.used;void *engine=font.impl;
    assert(pn_cache_reclaim(&pool,SIZE_MAX)==bytes && pool.used==used-bytes && font.impl==engine);
    assert(!pool.cache_stats[PN_CACHE_GLYPH].bytes && pool.cache_stats[PN_CACHE_GLYPH].evictions);
    // 保留较小前台余量，直接绘制不能依赖缓存成功。/ Keep small foreground headroom; direct drawing cannot depend on successful caching.
    pool.limit=pool.used+256u*1024u;
    assert(!pn_font_advance(&font,'W',&again) && width==again);pn_frame_clear(&frame,15);assert(!pn_font_draw(&font,&frame,'W',10*64,60,PN_FONT_GRAY) && !memcmp(expected,pixels,sizeof pixels));
    pool.limit=4u*1024u*1024u;assert(!pn_font_advance(&font,'W',&again));pn_frame_clear(&frame,15);assert(!pn_font_draw(&font,&frame,'W',10*64,60,PN_FONT_GRAY));
    // 在已有缓存时逐项注入绘制分配失败，借用槽不得被重试回收。/ Inject rendering allocation failures with warm caches; retry must never reclaim borrowed slots.
    for(unsigned i=1;i<=12;i++){pool.fail_at=pool.attempts+i;pn_status_t s=pn_font_draw(&font,&frame,0x4e2d,10*64,60,PN_FONT_GRAY);assert(s==PN_OK || s==PN_NO_MEMORY);pool.fail_at=0;assert(!pn_font_draw(&font,&frame,'W',10*64,60,PN_FONT_GRAY));}
    pn_font_close(&font);pn_cache_snapshot(&pool);assert(!pool.used && !pool.live && !pool.caches && !pool.cache_stats[PN_CACHE_GLYPH].bytes);
    puts("glyph cache: eviction preserves face/metrics/pixels, low-budget direct rendering, warm-cache faults and release passed");return 0;
}
