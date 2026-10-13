/* SPDX-License-Identifier: Apache-2.0
 * 中文：覆盖区快照与反馈复用一次显示；不额外刷新状态行。
 * English: regional snapshot and feedback share one presentation, without an extra status-line refresh. */
#include "pn_reader_toolbar.h"
#include <string.h>
pn_status_t pn_reader_toolbar_present(pn_pool_t *pool,pn_font_t *font,pn_frame_t *frame,unsigned unavailable,const char *notice,pn_reader_present_fn present,void *ctx){
    if(!pool || !font || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342 || !present)return PN_INVALID;
    // 包含圆角顶边和描边的8px余量。/ Include an 8px margin for the rounded top and stroke.
    const unsigned top=PN_TOOLBAR_Y-8,rows=1216-top;const size_t bytes=342u*rows;
    uint8_t *saved=pn_alloc_tagged(pool,PN_MEM_FRAME,bytes);if(!saved)return PN_NO_MEMORY;
    for(unsigned y=0;y<rows;y++)memcpy(saved+y*342u,frame->pixels+(size_t)(top+y)*frame->stride,342);
    pn_status_t status=pn_reader_toolbar_render(font,frame,unavailable);
    if(status==PN_OK && notice && *notice){
        int original=font->pixels;status=pn_font_size(font,24);
        pn_frame_rect(frame,32,1170,620,46,15);
        if(status==PN_OK)status=pn_w_text(font,frame,notice,32,1200,620,PN_ALIGN_CENTER);
        pn_status_t restored=pn_font_size(font,original);if(status==PN_OK)status=restored;
    }
    if(status==PN_OK)status=present(ctx,frame,PN_REFRESH_GL16);
    for(unsigned y=0;y<rows;y++)memcpy(frame->pixels+(size_t)(top+y)*frame->stride,saved+y*342u,342);
    pn_free(saved);return status;
}
