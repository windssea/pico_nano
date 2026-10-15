/* SPDX-License-Identifier: Apache-2.0
 * 中文：区域快照在显示失败与分配失败后仍保持原正文；成功仅显示一次。
 * English: regional snapshots retain the original body after display/allocation failure; success presents once. */
#include "pn_reader_toolbar.h"
#include "pn_hardware.h"
#include "pn_widgets.h"
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
    // 独立像素检查阅读线的空隙、模式、边界和无副作用关闭。 / Independent pixel checks cover guide gaps, modes, bounds and no-op disabling.
    pn_frame_clear(&frame,15);memcpy(copy,pixels,PN_HW_FRAME_BYTES);
    pn_reader_guide_row(&frame,0,3,100);pn_reader_guide_row(&frame,1,3,-1);pn_reader_guide_row(&frame,2,3,1216);assert(!memcmp(copy,pixels,PN_HW_FRAME_BYTES));
    pn_reader_guide_row(&frame,1,0,100);assert(pn_frame_get(&frame,18,100)==10 && pn_frame_get(&frame,19,100)==15 && pn_frame_get(&frame,31,100)==10 && pn_frame_get(&frame,0,99)==15);
    pn_reader_guide_row(&frame,2,0,101);assert(pn_frame_get(&frame,1,101)==10 && pn_frame_get(&frame,2,101)==15 && pn_frame_get(&frame,13,101)==10);
    // 深浅：中7、深4、黑0，越界按浅10。/ Darkness: medium 7, dark 4, black 0, out of range falls back to light 10.
    pn_reader_guide_row(&frame,1,1,102);pn_reader_guide_row(&frame,1,2,103);pn_reader_guide_row(&frame,1,3,104);pn_reader_guide_row(&frame,1,9,105);
    assert(pn_frame_get(&frame,0,102)==7 && pn_frame_get(&frame,0,103)==4 && pn_frame_get(&frame,0,104)==0 && pn_frame_get(&frame,0,105)==10 && pn_frame_get(&frame,19,104)==15);
    for(unsigned i=0;i<PN_HW_FRAME_BYTES;i++)pixels[i]=copy[i]=(uint8_t)(i*19u);
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,34)==PN_OK);
    // 状态带仅改顶部留白，变化不触碰正文，字体大小复原。/ Status changes only the reserved top band, preserving body pixels and font size.
    pn_w_set_clock("12:34");pn_w_set_battery(87);assert(pn_reader_status_render(&font,&frame)==PN_OK && font.pixels==34);
    assert(!memcmp(pixels+PN_READER_TOP*342u,copy+PN_READER_TOP*342u,PN_HW_FRAME_BYTES-PN_READER_TOP*342u));
    memcpy(copy,pixels,PN_HW_FRAME_BYTES);pn_w_set_clock("12:35");pn_w_set_battery(86);assert(pn_reader_status_render(&font,&frame)==PN_OK);
    assert(memcmp(pixels,copy,PN_READER_TOP*342u) && !memcmp(pixels+PN_READER_TOP*342u,copy+PN_READER_TOP*342u,PN_HW_FRAME_BYTES-PN_READER_TOP*342u));
    pn_w_set_clock(NULL);pn_w_set_battery(-1);assert(pn_reader_status_render(&font,&frame)==PN_OK && font.pixels==34);
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
