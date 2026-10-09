/* 目录推屏屏障、层级分组和实际跳转。/ TOC presentation barriers, groups and actual jumps. */
#include "pn_toc_ui.h"
#include <assert.h>
#include <stdio.h>
typedef struct {bool fail;unsigned calls;} screen_t;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)frame;(void)profile;screen_t *s=ctx;s->calls++;return s->fail?PN_IO:PN_OK;}
static bool same(const pn_epub_progress_t *a,const pn_epub_progress_t *b){return a->location.position.element==b->location.position.element && a->location.position.run==b->location.position.run && a->location.position.offset==b->location.position.offset;}
int main(int argc,char **argv){
    pn_toc_ui_t ui={0};assert(pn_toc_ui_hit(&ui,100,200)==-1);pn_toc_ui_close(&ui);if(argc==1)return 0;assert(argc==3);
    pn_pool_t pool;assert(!pn_pool_init(&pool,2*1024*1024,NULL,NULL,NULL));pn_epub_app_t app={0};screen_t screen={0};pn_epub_progress_t origin,after;uint64_t now=1;
    assert(pn_epub_app_open(&app,&pool,argv[1],argv[2],NULL,44,0)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&origin)==PN_OK);
    screen.fail=true;assert(pn_toc_ui_open(&ui,&app,present,&screen)==PN_IO && ui.active && !ui.presented);unsigned calls=screen.calls;
    assert(pn_toc_ui_event(&ui,PN_TOC_UI_ROW+1,now++,present,&screen)==PN_BUSY && screen.calls==calls && pn_epub_app_progress(&app,&after)==PN_OK && same(&origin,&after));
    screen.fail=false;assert(pn_toc_ui_event(&ui,PN_TOC_UI_RETRY,now++,present,&screen)==PN_OK && ui.presented && ui.count==6 && ui.total==8);
    assert(pn_toc_ui_hit(&ui,100,170)==PN_TOC_UI_ROW && pn_toc_ui_hit(&ui,100,262)==-1);
    assert(pn_toc_ui_event(&ui,PN_TOC_UI_ROW,now++,present,&screen)==PN_EMPTY && ui.active && pn_epub_app_progress(&app,&after)==PN_OK && same(&after,&origin));
    assert(pn_toc_ui_event(&ui,PN_TOC_UI_NEXT,now++,present,&screen)==PN_OK && ui.start==6 && ui.count==2);
    assert(pn_toc_ui_event(&ui,PN_TOC_UI_NEXT,now++,present,&screen)==PN_EMPTY && ui.start==6);
    assert(pn_toc_ui_event(&ui,PN_TOC_UI_PREVIOUS,now++,present,&screen)==PN_OK && ui.start==0);
    screen.fail=true;assert(pn_toc_ui_event(&ui,PN_TOC_UI_ROW+2,now++,present,&screen)==PN_IO && ui.active && !ui.presented && pn_epub_app_progress(&app,&after)==PN_OK && same(&after,&origin));
    screen.fail=false;assert(pn_toc_ui_event(&ui,PN_TOC_UI_RETRY,now++,present,&screen)==PN_OK);
    assert(pn_toc_ui_event(&ui,PN_TOC_UI_ROW+2,now++,present,&screen)==PN_OK && !ui.active && pn_epub_app_progress(&app,&after)==PN_OK && !same(&origin,&after));
    origin=after;assert(pn_toc_ui_open(&ui,&app,present,&screen)==PN_OK && pn_toc_ui_event(&ui,PN_TOC_UI_BACK,now++,present,&screen)==PN_OK && !ui.active && pn_epub_app_progress(&app,&after)==PN_OK && same(&origin,&after));
    pn_toc_ui_close(&ui);assert(pn_epub_app_close(&app,now++)==PN_OK && !pool.used && !pool.live);puts("TOC UI: presented barrier, groups/page bounds, failed jump retention, real jump and back preserve progress passed");return 0;
}
