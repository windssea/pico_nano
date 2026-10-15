/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实目录控制器的跨页、部分行、分组、失败跳转、位置屏障、尾部当前项及低预算。
 * English: real TOC motion checks cross-page/partial rows, groups, failed jumps, position barriers, current tail entries and low budgets. */
#include "pn_toc_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {bool fail,phase_fail;unsigned normal,stages[6];} screen_t;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)frame;(void)profile;screen_t *s=ctx;s->normal++;return s->fail?PN_IO:PN_OK;}
static pn_status_t motion(void *ctx,pn_list_stage_t stage,const pn_frame_t *frame,pn_follow_box_t area,const pn_follow_job_t *job){screen_t *s=ctx;(void)frame;assert(area.x==32 && area.y==148 && area.width==620 && area.height==880);s->stages[stage]++;if(stage==PN_LIST_PHASE){bool fail=s->phase_fail;s->phase_fail=false;return pn_follow_complete(job->owner,job,!fail);}return PN_OK;}
static bool same(const pn_epub_progress_t *a,const pn_epub_progress_t *b){return !strcmp(a->location.path,b->location.path) && a->location.position.element==b->location.position.element && a->location.position.run==b->location.position.run && a->location.position.offset==b->location.position.offset;}
static void pointer(pn_toc_ui_t *u,screen_t *s,unsigned count,int y,uint64_t now,pn_status_t expected){pn_status_t status;assert(pn_toc_ui_pointer(u,count,400,y,true,now,present,s,&status) && status==expected);}
int main(int argc,char **argv){
    assert(argc==3);pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_epub_app_t app={0};screen_t screen={0};pn_toc_ui_t ui={0};pn_epub_progress_t before,after;
    assert(pn_epub_app_open(&app,&pool,argv[1],argv[2],NULL,44,0)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,1,present,&screen)==PN_OK && pn_epub_app_progress(&app,&before)==PN_OK);
    screen.fail=true;assert(pn_toc_ui_open_motion(&ui,&app,&pool,present,&screen,motion,&screen)==PN_IO && ui.active && !ui.presented && ui.motion_ctx && !pn_toc_ui_motion_stats(&ui).active && pn_toc_ui_tick(&ui,2)==PN_EMPTY);unsigned calls=screen.normal;
    assert(pn_toc_ui_event(&ui,PN_TOC_UI_ROW+1,3,present,&screen)==PN_BUSY && screen.normal==calls);screen.fail=false;assert(pn_toc_ui_event(&ui,PN_TOC_UI_RETRY,4,present,&screen)==PN_OK && ui.motion_ctx && ui.total==54 && ui.current==1);
    pointer(&ui,&screen,1,900,100,PN_OK);pointer(&ui,&screen,1,500,1100,PN_OK);pointer(&ui,&screen,1,250,2100,PN_OK);assert(pn_toc_ui_motion_stats(&ui).offset==650 && ui.start==7 && ui.count==10 && pn_toc_ui_hit(&ui,400,158)==PN_TOC_UI_ROW+7 && pn_toc_ui_hit(&ui,400,1027)==PN_TOC_UI_ROW+17);
    pn_status_t status;assert(pn_toc_ui_pointer(&ui,2,0,0,true,2200,present,&screen,&status));pointer(&ui,&screen,0,0,2210,PN_OK);
    for(unsigned i=0;i<8;i++){assert(pn_toc_ui_tick(&ui,2300+i)==PN_OK && pn_epub_app_progress(&app,&after)==PN_OK && same(&before,&after));}
    assert(pn_toc_ui_tick(&ui,2520)==PN_OK && screen.stages[PN_LIST_REFERENCE]==1 && screen.stages[PN_LIST_SETTLE]==1 && ui.presented && !pn_toc_ui_motion_stats(&ui).active && !pn_epub_app_last_confirmed(&app));
    pointer(&ui,&screen,1,310,2600,PN_OK);pointer(&ui,&screen,0,0,2610,PN_EMPTY);assert(ui.active && ui.notice && pn_epub_app_progress(&app,&after)==PN_OK && same(&before,&after));
    screen.fail=true;pointer(&ui,&screen,1,500,2700,PN_OK);pointer(&ui,&screen,0,0,2710,PN_IO);assert(ui.active && !ui.presented && pn_epub_app_progress(&app,&after)==PN_OK && same(&before,&after));
    screen.fail=false;assert(pn_toc_ui_event(&ui,PN_TOC_UI_RETRY,2800,present,&screen)==PN_OK);pointer(&ui,&screen,1,500,2810,PN_OK);pointer(&ui,&screen,0,0,2820,PN_OK);assert(!ui.active && !ui.motion_ctx && pn_epub_app_progress(&app,&after)==PN_OK && strcmp(after.location.path,"OPS/c09.xhtml")==0 && !same(&before,&after));
    assert(pn_epub_app_toc_jump(&app,53,2900,present,&screen)==PN_OK && pn_epub_app_progress(&app,&before)==PN_OK);
    assert(pn_toc_ui_open_motion(&ui,&app,&pool,present,&screen,motion,&screen)==PN_OK && ui.current==53 && ui.start==44 && ui.selected==9 && pn_toc_ui_selected_command(&ui)==PN_TOC_UI_ROW+53 && pn_toc_ui_hit(&ui,400,200)==PN_TOC_UI_ROW+44);
    assert(pn_toc_ui_event(&ui,PN_TOC_UI_PREVIOUS,3000,present,&screen)==PN_OK && ui.start==34 && pn_toc_ui_event(&ui,PN_TOC_UI_CURRENT,3010,present,&screen)==PN_OK && ui.start==44 && ui.selected==9);
    assert(pn_toc_ui_event(&ui,PN_TOC_UI_BACK,3020,present,&screen)==PN_OK && !ui.active && !ui.motion_ctx && pn_epub_app_progress(&app,&after)==PN_OK && same(&before,&after));pn_toc_ui_close(&ui);assert(pn_epub_app_close(&app,3030)==PN_OK && !pool.used && !pool.live);
    assert(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)==PN_OK && pn_epub_app_open(&app,&pool,argv[1],argv[2],NULL,44,0)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,1,present,&screen)==PN_OK);
    assert(pn_toc_ui_open_motion(&ui,&app,&pool,present,&screen,motion,&screen)==PN_OK && !ui.motion_ctx && pn_toc_ui_event(&ui,PN_TOC_UI_NEXT,2,present,&screen)==PN_OK && ui.start==10);pn_toc_ui_close(&ui);assert(pn_epub_app_close(&app,3)==PN_OK && !pool.used && !pool.live);
    puts("TOC motion: actual receipt-only initialization, cross-page partial-row hit, position-preserving drag/settle/groups, failed and successful chapter jumps, tail/current/paging alignment and low-budget cleanup passed");return 0;
}
