/* SPDX-License-Identifier: Apache-2.0
 * 中文：区域快照在显示失败与分配失败后仍保持原正文；成功仅显示一次。
 * English: regional snapshots retain the original body after display/allocation failure; success presents once. */
#include "pn_reader_toolbar.h"
#include "pn_hardware.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static const uint8_t *original;static unsigned calls;static bool fail;
static pn_status_t show(void *ctx,const pn_frame_t *f,pn_refresh_t mode){
    (void)ctx;calls++;assert(mode==PN_REFRESH_GL16 && !memcmp(f->pixels,original,(PN_TOOLBAR_Y-8)*342u));
    assert(memcmp(f->pixels,original,PN_HW_FRAME_BYTES));return fail?PN_IO:PN_OK;
}
int main(void){
    pn_pool_t pool;assert(!pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL));
    uint8_t *pixels=malloc(PN_HW_FRAME_BYTES),*copy=malloc(PN_HW_FRAME_BYTES);assert(pixels && copy);original=copy;
    pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,PN_HW_FRAME_BYTES,684,1216));
    for(unsigned i=0;i<PN_HW_FRAME_BYTES;i++)pixels[i]=copy[i]=(uint8_t)(i*19u);
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,34)==PN_OK);
    // 正文的末行必须完整位于正文更新区；进度圆点不能跨入该区。/ The last text line stays in the body update; the progress knob must not cross into it.
    assert(PN_READER_TOP+PN_READER_HEIGHT<=1152);
    for(int basis=0;basis<=10000;basis+=5000){
        pn_frame_clear(&frame,15);assert(pn_reader_footer_progress(&font,NULL,&frame,"","",basis)==PN_OK);
        for(int y=PN_READER_FOOTER_Y;y<1152;y++)for(int x=0;x<684;x++)assert(pn_frame_get(&frame,x,y)==15);
        bool ink=false;for(int y=1152;y<1170;y++)for(int x=0;x<684;x++)ink|=pn_frame_get(&frame,x,y)!=15;assert(ink);
    }
    memcpy(pixels,copy,PN_HW_FRAME_BYTES);
    assert(pn_reader_toolbar_present(&pool,&font,&frame,1,"保存失败，当前页保留",show,NULL)==PN_OK && calls==1 && !memcmp(copy,pixels,PN_HW_FRAME_BYTES));
    fail=true;assert(pn_reader_toolbar_present(&pool,&font,&frame,1,NULL,show,NULL)==PN_IO && calls==2 && !memcmp(copy,pixels,PN_HW_FRAME_BYTES));
    pool.fail_at=pool.attempts+1;assert(pn_reader_toolbar_present(&pool,&font,&frame,1,NULL,show,NULL)==PN_NO_MEMORY && calls==2 && !memcmp(copy,pixels,PN_HW_FRAME_BYTES));
    assert(pool.memory[PN_MEM_FRAME].peak<120u*1024u);pool.fail_at=0;pn_font_close(&font);assert(!pool.used && !pool.live);free(pixels);free(copy);return 0;
}
