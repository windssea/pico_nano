/* SPDX-License-Identifier: Apache-2.0
 * 中文：精确命中矩形与独立按下像素；不调用硬件或选择命令。
 * English: exact hit rectangles and independent press pixels, without hardware or selection commands. */
#include "pn_feedback.h"
#include "pn_cancel.h"
#include <string.h>
static bool box_valid(pn_feedback_box_t b){return b.x>=0 && b.y>=0 && b.width>=16 && b.height>=16 && b.width<=PN_UI_WIDTH-b.x && b.height<=PN_UI_HEIGHT-b.y;}
pn_status_t pn_feedback_region(pn_feedback_hit_fn hit,void *ctx,int x,int y,pn_feedback_box_t *out,int *code){
    if(!hit || !out || !code || x<0 || x>=PN_UI_WIDTH || y<0 || y>=PN_UI_HEIGHT)return PN_INVALID;
    int wanted=hit(ctx,x,y);if(wanted<0)return PN_EMPTY;
    int l=x,r=x+1,t=y,b=y+1;
    while(l>0 && hit(ctx,l-1,y)==wanted)l--;
    while(r<PN_UI_WIDTH && hit(ctx,r,y)==wanted)r++;
    while(t>0 && hit(ctx,x,t-1)==wanted)t--;
    while(b<PN_UI_HEIGHT && hit(ctx,x,b)==wanted)b++;
    pn_feedback_box_t box={l,t,r-l,b-t};
    if(!box_valid(box))return PN_EMPTY;
    if(((size_t)box.width+1u)/2u*(size_t)box.height>PN_FEEDBACK_MAX_PIXELS/2u){
        // 大卡片只在同一命中码内给出底部提示，不快刷整张封面/背景。
        // Indicate large cards inside the same hit code at the bottom, never fast-refresh the whole cover/background.
        if(box.width==PN_UI_WIDTH || box.height==PN_UI_HEIGHT)return PN_EMPTY;
        box.y=b-16;box.height=16;t=box.y;
    }
    // 不把相同码的背景/分离区域合成大框；逐像素确认是同一个矩形。
    // Never merge backgrounds/disjoint regions sharing a code; confirm a uniform rectangle pixel by pixel.
    for(int py=t;py<b;py++){pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;for(int px=l;px<r;px++)if(hit(ctx,px,py)!=wanted)return PN_EMPTY;}
    *out=box;*code=wanted;return PN_OK;
}
pn_status_t pn_feedback_discard(pn_feedback_t *f){
    if(!f)return PN_INVALID;
    if(f->borrowed)return PN_BUSY;
    pn_free(f->original);pn_free(f->target);memset(f,0,sizeof *f);return PN_OK;
}
pn_status_t pn_feedback_prepare(pn_feedback_t *f,pn_pool_t *pool,pn_feedback_box_t box,uint64_t view,int hit,pn_feedback_read_fn read,void *ctx){
    if(!f || !pool || !read || !view || hit<0 || !box_valid(box))return PN_INVALID;
    if(f->active || f->borrowed || f->original || f->target)return PN_BUSY;
    pn_status_t cancelled=pn_cancel_checkpoint();if(cancelled!=PN_OK)return cancelled;
    size_t bytes=((size_t)box.width+1u)/2u*(size_t)box.height;
    if(bytes>PN_FEEDBACK_MAX_PIXELS/2u)return PN_EMPTY;
    f->original=pn_alloc_optional(pool,PN_MEM_UI,bytes);if(f->original)f->target=pn_alloc_optional(pool,PN_MEM_UI,bytes);
    if(!f->target){(void)pn_feedback_discard(f);return PN_EMPTY;}
    f->patch=(pn_feedback_patch_t){.box=box,.view=view,.hit=hit};
    if(!pn_frame_bind(&f->patch.frame,f->original,bytes,box.width,box.height)){(void)pn_feedback_discard(f);return PN_INVALID;}
    pn_frame_clear(&f->patch.frame,PN_UI_PAPER);
    // 回调也属于短借用，不能重入释放或替换当前存储。/ Snapshot callbacks are short borrows too, never reenter release/replacement.
    f->borrowed=true;pn_status_t s=read(ctx,box,&f->patch.frame);f->borrowed=false;
    if(s==PN_OK)s=pn_cancel_checkpoint();
    if(s!=PN_OK){(void)pn_feedback_discard(f);return s;}
    memcpy(f->target,f->original,bytes);f->patch.frame.pixels=f->target;
    pn_w_round_stroke(&f->patch.frame,2,2,box.width-4,box.height-4,10,3.0f,PN_UI_INK);
    f->active=true;return PN_OK;
}
pn_status_t pn_feedback_present(pn_feedback_t *f,bool pressed,pn_feedback_present_fn present,void *ctx){
    if(!f || !f->active || !present)return PN_INVALID;
    if(f->borrowed)return PN_BUSY;
    pn_status_t cancelled=pn_cancel_checkpoint();if(cancelled!=PN_OK)return cancelled;
    f->patch.pressed=pressed;f->patch.frame.pixels=pressed?f->target:f->original;
    f->borrowed=true;pn_status_t s=present(ctx,&f->patch);f->borrowed=false;return s;
}
