/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界缺字回退选择，度量和绘制不分离来源。
 * English: bounded missing-glyph selection sharing metric and rendering sources.
 * 冻结：不吞IO/媒体/分配错误，不关闭借用主字体。
 * Frozen: preserve I/O/media/allocation errors; never close the borrowed primary.
 */
#include "pn_font_chain.h"
#include <string.h>
pn_status_t pn_font_chain_init(pn_font_chain_t *c,pn_pool_t *pool,pn_font_t *primary,const pn_text_source_t *fallback){
    if(!c || !pool || !primary || !primary->impl || (fallback && !fallback->read_at))return PN_INVALID;
    if(c->primary || c->fonts[0].impl || c->fonts[1].impl)return PN_BUSY;
    *c=(pn_font_chain_t){.primary=primary,.pool=pool,.count=fallback?2u:1u};
    if(fallback)c->sources[0]=*fallback;
    c->sources[c->count-1]=pn_font_builtin_source();return PN_OK;
}
void pn_font_chain_suspend(pn_font_chain_t *c){if(c)for(unsigned i=0;i<2;i++)pn_font_close(&c->fonts[i]);}
void pn_font_chain_clear(pn_font_chain_t *c){if(c){pn_font_chain_suspend(c);memset(c,0,sizeof *c);}}
static pn_status_t slot(pn_font_chain_t *c,unsigned i,pn_font_t **font){
    if(!c || !c->primary || !c->primary->impl || !c->pool || !c->count || c->count>2 || i>=c->count)return PN_INVALID;
    pn_status_t status=c->fonts[i].impl?(c->fonts[i].pixels==c->primary->pixels?PN_OK:pn_font_size(&c->fonts[i],c->primary->pixels)):pn_font_open(&c->fonts[i],c->pool,&c->sources[i],c->primary->pixels);
    if(status==PN_OK)*font=&c->fonts[i];
    return status;
}
pn_status_t pn_font_chain_choose(pn_font_chain_t *c,uint32_t cp,pn_font_choice_t *out){
    if(!c || !c->primary || !c->primary->impl || !out || !c->count || c->count>2)return PN_INVALID;
    pn_font_choice_t choice={.font=c->primary};pn_status_t status=pn_font_advance(choice.font,cp,&choice.advance_64);
    for(unsigned i=0;status==PN_EMPTY && i<c->count;i++){status=slot(c,i,&choice.font);if(status==PN_OK)status=pn_font_advance(choice.font,cp,&choice.advance_64);}
    if(status==PN_OK)status=pn_font_vertical(choice.font,&choice.ascent,&choice.descent);
    if(status==PN_OK)*out=choice;
    return status;
}
pn_status_t pn_font_chain_advance(void *ctx,uint32_t cp,int32_t *out){if(!out)return PN_INVALID;pn_font_choice_t choice;pn_status_t status=pn_font_chain_choose(ctx,cp,&choice);if(status==PN_OK)*out=choice.advance_64;return status;}
pn_status_t pn_font_chain_vertical(pn_font_chain_t *c,int *ascent,int *descent){
    if(!c || !c->primary || !c->primary->impl || !ascent || !descent || !c->count || c->count>2)return PN_INVALID;
    int a,d;pn_status_t status=pn_font_vertical(c->primary,&a,&d);if(status!=PN_OK)return status;
    for(unsigned i=0;i<c->count;i++){pn_font_t *font;status=slot(c,i,&font);if(status!=PN_OK)return status;int x,y;status=pn_font_vertical(font,&x,&y);if(status!=PN_OK)return status;if(x>a)a=x;if(y>d)d=y;}
    *ascent=a;*descent=d;return PN_OK;
}
pn_status_t pn_font_chain_draw(pn_font_chain_t *c,pn_frame_t *frame,uint32_t cp,int32_t x,int baseline,pn_font_render_t mode){pn_font_choice_t choice;pn_status_t status=pn_font_chain_choose(c,cp,&choice);return status==PN_OK?pn_font_draw(choice.font,frame,cp,x,baseline,mode):status;}
