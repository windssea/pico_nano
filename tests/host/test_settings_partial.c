/* SPDX-License-Identifier: Apache-2.0
 * 中文：设置控件增量与整页重画逐字节一致；保存/呈现失败与外部更新保留完整恢复。
 * English: byte-exact incremental controls versus complete settings painting, with full recovery after persistence/presentation failures or external updates. */
#define _POSIX_C_SOURCE 200809L
#include "pn_settings_ui.h"
#include "pn_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {BYTES=342*1216};
static uint8_t captured[BYTES],incremental[BYTES];
static unsigned draws,presents;static bool reject,fail_draw;
extern pn_status_t __real_pn_font_draw(pn_font_t *,pn_frame_t *,uint32_t,int32_t,int,pn_font_render_t);
pn_status_t __wrap_pn_font_draw(pn_font_t *font,pn_frame_t *frame,uint32_t cp,int32_t x,int base,pn_font_render_t mode){draws++;if(fail_draw){fail_draw=false;return PN_IO;}return __real_pn_font_draw(font,frame,cp,x,base,mode);}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)ctx;assert(profile==PN_REFRESH_GL16 || profile==PN_REFRESH_GC16);assert(frame->width==684 && frame->height==1216);
    for(int y=0;y<1216;y++)memcpy(captured+(size_t)y*342,frame->pixels+(size_t)y*frame->stride,342);
    presents++;return reject?PN_IO:PN_OK;
}
static unsigned check(pn_settings_ui_t *ui,int command,pn_status_t expected){
    draws=0;pn_status_t status=pn_settings_ui_event(ui,command,present,NULL);if(status!=expected)fprintf(stderr,"command=%d status=%d expected=%d flags=%u\n",command,(int)status,(int)expected,(unsigned)ui->flags);assert(status==expected);unsigned local=draws;assert(ui->presented);
    memcpy(incremental,captured,BYTES);draws=0;assert(pn_settings_ui_present(ui,present,NULL)==PN_OK);unsigned full=draws;
    if(memcmp(captured,incremental,BYTES)){for(size_t i=0;i<BYTES;i++)if(captured[i]!=incremental[i]){fprintf(stderr,"command=%d first byte=%zu local=%02x full=%02x\n",command,i,incremental[i],captured[i]);break;}abort();}
    printf("settings command=%d local_draws=%u full_draws=%u\n",command,local,full);fflush(stdout);return local;
}
int main(void){
    char root[]="/tmp/pn-settings-partial-XXXXXX";assert(mkdtemp(root));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_pool_t pool;assert(!pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL));pn_w_font_scope_t scope={0};assert(pn_w_font_scope_begin(&scope,&pool,NULL)==PN_OK);
    pn_settings_ui_t ui={0};assert(pn_settings_ui_open(&ui,&pool,&media,root,present,NULL)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_KEYS,present,NULL)==PN_OK);
    unsigned mask=PN_INPUT_NO_SWIPE|PN_INPUT_NO_EDGE_TAP|PN_INPUT_NO_KEYS;
    for(unsigned round=0;round<6;round++)for(int i=0;i<4;i++){if(((ui.flags^(1u<<i))&mask)==mask)continue;assert(check(&ui,PN_SETUI_TOGGLE+i,PN_OK)==0);}
    // 未改变的选择不重画、不再提交显示；错误提示必须完整擦除。/ An unchanged selection neither paints nor resubmits; error notices must be fully erased.
    assert(pn_settings_ui_event(&ui,PN_SETUI_REFRESH,present,NULL)==PN_OK);
    for(int i=0;i<3;i++){unsigned n=check(&ui,PN_SETUI_LEVEL+i,PN_OK);if(i)assert(n>0 && n<100);}
    unsigned before=presents;draws=0;assert(pn_settings_ui_event(&ui,PN_SETUI_LEVEL+2,present,NULL)==PN_OK && presents==before && !draws);
    assert(pn_settings_ui_event(&ui,PN_SETUI_APPEARANCE,present,NULL)==PN_OK);
    assert(check(&ui,PN_SETUI_COLUMNS+1,PN_OK)<100);
    for(unsigned round=0;round<3;round++)for(int i=0;i<4;i++)assert(check(&ui,PN_SETUI_STYLE+i,PN_OK)<30);
    assert(check(&ui,PN_SETUI_COLUMNS,PN_OK)<100);
    pn_media_lease_t writer;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&writer)==PN_OK);
    assert(check(&ui,PN_SETUI_COLUMNS+1,PN_BUSY)>100 && ui.shelf.columns==3);assert(pn_media_release(&media,&writer)==PN_OK);
    assert(check(&ui,PN_SETUI_COLUMNS+1,PN_OK)>100 && ui.shelf.columns==4);
    // 保存成功但面板失败：保留已保存值，下次选择或显式重画恢复整页。/ Persistence success with display failure retains saved values; the next selection or explicit repaint restores a complete page.
    reject=true;assert(pn_settings_ui_event(&ui,PN_SETUI_STYLE,present,NULL)==PN_IO && !ui.presented && ui.shelf.style==0);reject=false;
    assert(check(&ui,PN_SETUI_STYLE+1,PN_OK)>100);
    pn_settings_about_t about={0};strcpy(about.version,"test version");pn_settings_ui_set_about(&ui,&about);
    assert(check(&ui,PN_SETUI_STYLE+2,PN_OK)>100);
    pn_settings_time_t time={0};strcpy(time.datetime,"2026-10-15 02:30");pn_settings_ui_set_time(&ui,&time);
    assert(check(&ui,PN_SETUI_STYLE+3,PN_OK)>100);
    // 局部文字绘制失败不提交残缺帧，已保存值仍由下一次全页恢复显示。
    // Failed local text painting never submits an incomplete frame; the next full repaint shows the persisted value.
    before=presents;fail_draw=true;assert(pn_settings_ui_event(&ui,PN_SETUI_STYLE,present,NULL)==PN_IO && !ui.presented && ui.shelf.style==0 && presents==before);
    assert(check(&ui,PN_SETUI_STYLE,PN_OK)>100);
    assert(pn_settings_ui_event(&ui,PN_SETUI_KEYS,present,NULL)==PN_OK);
    pn_w_set_battery(85);pn_w_set_clock("02:30");pn_w_set_wifi(true);assert(check(&ui,PN_SETUI_TOGGLE,PN_OK)<60);
    pn_w_set_battery(10);pn_w_set_clock("1");pn_w_set_wifi(false);assert(check(&ui,PN_SETUI_TOGGLE,PN_OK)<60);
    pn_w_set_battery(-1);pn_w_set_clock(NULL);assert(check(&ui,PN_SETUI_TOGGLE,PN_OK)<60);
    assert(check(&ui,PN_SETUI_TOGGLE,PN_OK)==0);
    for(int i=1;i<4;i++)if(!(ui.flags&(1u<<i)))(void)check(&ui,PN_SETUI_TOGGLE+i,((ui.flags^(1u<<i))&mask)==mask?PN_LIMIT:PN_OK);
    // 强刷与失败重画沿原全页路径，完整帧不可被部分更新掩盖。/ Deep refresh and failed repaint keep the full-page path rather than masking an incomplete frame.
    before=presents;assert(pn_settings_ui_event(&ui,PN_SETUI_FULL_REFRESH,present,NULL)==PN_OK && presents==before+1);
    pn_settings_ui_close(&ui);pn_w_font_scope_end(&scope);assert(!pool.used && !pool.live);puts("Settings partial: complete pixels, zero-glyph toggles, limited segments/previews, no-op and failure/external-update full recovery passed");return 0;
}
