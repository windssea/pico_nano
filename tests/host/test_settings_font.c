/* SPDX-License-Identifier: Apache-2.0
 * 中文：设置字体跨页移交、热字形、失败归还、回收与源交接，像素与原冷打开一致。
 * English: settings-face transfer, warm glyphs, failure returns, reclamation and source handoff, with pixels matching cold opens. */
#include "pn_settings_ui.h"
#include "pn_cancel.h"
#include "pn_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t reference[415872],pixels[415872];
static unsigned opens,presents;static uint64_t ticks;static pn_status_t present_status=PN_OK;
static uint64_t clock_tick(void *ctx){(void)ctx;return ++ticks;}
static void profile(void *ctx,const pn_profile_event_t *event){(void)ctx;if(!strcmp(event->phase,"font.open"))opens++;}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t mode){(void)ctx;(void)mode;assert(frame->width==684 && frame->height==1216 && frame->stride==342);memcpy(pixels,frame->pixels,sizeof pixels);presents++;return present_status;}
static bool cancelled(void *ctx){(void)ctx;return true;}
typedef struct {pn_text_source_t source;pn_status_t status;unsigned validations;} controlled_t;
static pn_status_t validate(void *ctx){controlled_t *s=ctx;s->validations++;return s->status;}
static pn_status_t read_at(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *got){controlled_t *s=ctx;return s->source.read_at(s->source.ctx,at,out,cap,got);}
int main(int argc,char **argv){
    pn_pool_t pool,other;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==0);assert(pn_pool_init(&other,2u*1024u*1024u,NULL,NULL,NULL)==0);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    if(argc>1)assert(pn_font_system_open(&pool,&media,argv[1])==PN_OK);
    pn_profile_port(clock_tick,profile,NULL);
    pn_settings_ui_t ui={0};pn_font_t slot={0};
    assert(pn_settings_ui_open(&ui,&pool,NULL,NULL,present,NULL)==PN_OK && ui.presented);memcpy(reference,pixels,sizeof pixels);pn_settings_ui_close(&ui);
    assert(pn_settings_ui_open_cached(&ui,&pool,NULL,NULL,&slot,present,NULL)==PN_OK && ui.presented && !slot.impl && !memcmp(reference,pixels,sizeof pixels));pn_settings_ui_close(&ui);void *engine=slot.impl;assert(engine);
    unsigned before=opens;uint64_t hits=pool.cache_stats[PN_CACHE_GLYPH].hits;
    for(unsigned n=0;n<12;n++){assert(pn_settings_ui_open_cached(&ui,&pool,NULL,NULL,&slot,present,NULL)==PN_OK && ui.presented && !slot.impl && !memcmp(reference,pixels,sizeof pixels));pn_settings_ui_close(&ui);assert(slot.impl==engine);}
    unsigned warm_opens=opens-before;assert(!warm_opens && pool.cache_stats[PN_CACHE_GLYPH].hits>hits);
    // 分配失败、跨池和取消都保留唯一引擎；关闭在取消scope内仍可归还。
    // Allocation failure, a foreign pool and cancellation retain the sole engine; close can return it even inside a cancellation scope.
    for(unsigned fail=1;fail<=2;fail++){pool.fail_at=pool.attempts+fail;assert(pn_settings_ui_open_cached(&ui,&pool,NULL,NULL,&slot,present,NULL)==PN_NO_MEMORY && !ui.impl && slot.impl==engine);pool.fail_at=0;}
    assert(pn_settings_ui_open_cached(&ui,&other,NULL,NULL,&slot,present,NULL)==PN_INVALID && !ui.impl && slot.impl==engine && !other.used && !other.live);
    pn_cancel_scope_t cancel;pn_cancel_enter(&cancel,cancelled,NULL);assert(pn_settings_ui_open_cached(&ui,&pool,NULL,NULL,&slot,present,NULL)==PN_CANCELLED && !ui.impl && slot.impl==engine);pn_cancel_leave(&cancel);
    present_status=PN_IO;assert(pn_settings_ui_open_cached(&ui,&pool,NULL,NULL,&slot,present,NULL)==PN_OK && !ui.presented && !slot.impl);present_status=PN_OK;
    assert(pn_settings_ui_present(&ui,present,NULL)==PN_OK && !memcmp(reference,pixels,sizeof pixels));pn_cancel_enter(&cancel,cancelled,NULL);pn_settings_ui_close(&ui);pn_cancel_leave(&cancel);assert(slot.impl==engine);
    pn_cache_reclaim(&pool,SIZE_MAX);before=opens;assert(pn_settings_ui_open_cached(&ui,&pool,NULL,NULL,&slot,present,NULL)==PN_OK && ui.presented && !memcmp(reference,pixels,sizeof pixels));pn_settings_ui_close(&ui);assert(slot.impl==engine && opens==before);
    // 防御性处理被违规填满的归还槽，不覆盖或泄漏另一引擎。
    // Defensively handle a return slot filled against the contract, without overwriting or leaking the other engine.
    assert(pn_settings_ui_open_cached(&ui,&pool,NULL,NULL,&slot,present,NULL)==PN_OK);pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&slot,&pool,&source,24)==PN_OK);void *replacement=slot.impl;pn_settings_ui_close(&ui);assert(slot.impl==replacement);pn_font_close(&slot);
    if(argc>1){assert(pn_media_detach(&media)==PN_OK);assert(pn_font_system_close()==PN_OK);}
    // 移交操作自身不检查文件或分配；源失效在正常字体使用时仍被拒绝。
    // Moving itself neither validates files nor allocates; normal font use still rejects an invalid source.
    controlled_t state={pn_font_builtin_source(),PN_OK,0};pn_text_source_t controlled={&state,state.source.size,read_at,validate};pn_font_t from={0},to={0};assert(pn_font_open(&from,&pool,&controlled,24)==PN_OK);unsigned checked=state.validations;size_t attempts=pool.attempts;engine=from.impl;
    assert(pn_font_move(&to,&from,&other)==PN_INVALID && from.impl==engine && !to.impl);
    assert(pn_font_move(&from,&from,&pool)==PN_INVALID && pn_font_move(NULL,&from,&pool)==PN_INVALID);
    state.status=PN_STALE_MEDIA;assert(pn_font_move(&to,&from,&pool)==PN_OK && !from.impl && to.impl==engine && state.validations==checked && pool.attempts==attempts);
    assert(pn_font_source_ready(&to)==PN_STALE_MEDIA);assert(pn_font_move(&from,&to,&pool)==PN_OK && from.impl==engine && !to.impl);assert(pn_font_move(&to,&from,&pool)==PN_OK);assert(pn_font_move(&to,&from,&pool)==PN_BUSY);pn_font_close(&to);assert(pn_font_move(&to,&from,&pool)==PN_EMPTY);
    pn_profile_port(NULL,NULL,NULL);assert(!pool.used && !pool.live && !pool.caches && !other.used && !other.live && !pn_media_active(&media));
    printf("settings font: 12 warm opens without font creation, matching pixels, faults/cancel/reclaim; warm_opens=%u presents=%u peak=%zu used=%zu live=%zu\n",warm_opens,presents,pool.peak,pool.used,pool.live);return 0;
}
