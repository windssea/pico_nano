/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：受限TTF度量与4bpp字体绘制，包含常驻UI子集源。
 * English: bounded TTF metrics and 4bpp font drawing with a resident UI subset source.
 * 冻结：字体源保持有效；owner串行使用；不写设置或硬件。
 * Frozen: font source remains valid; serialized owner calls; no settings or hardware writes.
 */
#include "pn_font.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
static pn_status_t check_source(void *ctx){return *(bool *)ctx?PN_OK:PN_STALE_MEDIA;}
int main(void){
    pn_pool_t pool;assert(pn_pool_init(&pool,1024*1024,NULL,NULL,NULL)==0);
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();
    assert(pn_font_open(&font,&pool,&source,44)==PN_OK);
    pn_font_info_t info={0};assert(pn_font_info(&font,&info)==PN_OK && info.glyphs>1000 && info.family[0] && info.weight<=1000 && !info.variable);
    int32_t latin,chinese;assert(pn_font_advance(&font,'W',&latin)==PN_OK && latin>0);
    assert(pn_font_advance(&font,0x4e2d,&chinese)==PN_OK && chinese>0);
    int ascent,descent;assert(pn_font_vertical(&font,&ascent,&descent)==PN_OK && ascent>0 && descent>=0);
    uint8_t bytes[100*100/2];pn_frame_t frame;assert(pn_frame_bind(&frame,bytes,sizeof bytes,100,100));pn_frame_clear(&frame,15);
    assert(pn_font_draw(&font,&frame,0x4e2d,20*64,60,PN_FONT_GRAY)==PN_OK);
    unsigned ink=0,gray=0;for(int y=0;y<100;y++)for(int x=0;x<100;x++){uint8_t v=pn_frame_get(&frame,x,y);ink+=v<15;gray+=v>0 && v<15;}
    assert(ink>100 && gray>0);
    pn_frame_clear(&frame,15);assert(pn_font_draw(&font,&frame,0x4e2d,20*64,60,PN_FONT_BINARY)==PN_OK);
    for(int y=0;y<100;y++)for(int x=0;x<100;x++){uint8_t v=pn_frame_get(&frame,x,y);assert(v==0 || v==15);}
    assert(pn_font_draw(&font,&frame,'W',INT32_MAX,INT_MAX,PN_FONT_GRAY)==PN_OK);
    assert(pn_font_advance(&font,0x10ffff,&latin)==PN_EMPTY);
    assert(pn_font_size(&font,0)==PN_INVALID);assert(pn_font_size(&font,56)==PN_OK);
    pn_font_close(&font);pn_font_close(&font);assert(!pool.live && !pool.used);
    size_t requests=pool.attempts;
    for(size_t fail=1;fail<=requests;fail++){
        pn_pool_t injected;assert(pn_pool_init(&injected,1024*1024,NULL,NULL,NULL)==0);injected.fail_at=fail;
        pn_font_t f={0};pn_status_t status=pn_font_open(&f,&injected,&source,44);
        if(status==PN_OK)(void)pn_font_draw(&f,&frame,0x4e2d,0,60,PN_FONT_GRAY);
        pn_font_close(&f);assert(!injected.live && !injected.used);
    }
    bool available=true;pn_text_source_t guarded=source;guarded.ctx=&available;guarded.validate=check_source;
    pn_pool_t guarded_pool;assert(pn_pool_init(&guarded_pool,1024*1024,NULL,NULL,NULL)==0);pn_font_t g={0};
    assert(pn_font_open(&g,&guarded_pool,&guarded,44)==PN_OK);available=false;int32_t unchanged=123;
    assert(pn_font_advance(&g,'A',&unchanged)==PN_STALE_MEDIA && unchanged==123);
    assert(pn_font_draw(&g,&frame,'A',0,60,PN_FONT_GRAY)==PN_STALE_MEDIA);
    info.glyphs=999;assert(pn_font_info(&g,&info)==PN_STALE_MEDIA && info.glyphs==999);
    pn_font_close(&g);assert(!guarded_pool.used && !guarded_pool.live);
    pn_pool_t tiny;assert(pn_pool_init(&tiny,64,NULL,NULL,NULL)==0);font=(pn_font_t){0};
    assert(pn_font_open(&font,&tiny,&source,44)==PN_NO_MEMORY && !tiny.live);
    printf("font: native metrics, gray/binary rendering, clipping and %zu allocation faults passed\n",requests);return 0;
}
