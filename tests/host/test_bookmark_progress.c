/* 中文：添加书签/退出菜单不能改变正文位置或页脚进度。/ English: adding bookmarks and leaving their menu must preserve location and footer progress. */
#include "pn_bookmark_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct {uint64_t footer;} screen_t;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t mode){
    (void)mode;screen_t *s=ctx;s->footer=1469598103934665603ull;
    for(int y=1160;y<1216;y++)for(int x=420;x<684;x++)s->footer=(s->footer^pn_frame_get(frame,x,y))*1099511628211ull;
    return PN_OK;
}
int main(int argc,char **argv){
    assert(argc==4 || argc==5);pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    pn_epub_app_t app={0};screen_t screen={0};uint64_t now=1;
    assert(pn_epub_app_open(&app,&pool,argv[1],argv[3],argv[2],32,0)==PN_OK);
    assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK);
    size_t target=argc==5?(size_t)strtoul(argv[4],NULL,10):1;
    assert(pn_epub_app_section_jump(&app,target,now++,present,&screen)==PN_OK);
    pn_epub_progress_t before,after;unsigned basis_before,basis_after;size_t section,count;
    assert(pn_epub_app_progress(&app,&before)==PN_OK);
    assert(pn_epub_app_percent(&app,&basis_before,&section,&count)==PN_OK && basis_before>0);uint64_t footer=screen.footer;
    pn_bookmark_ui_t ui={0};assert(pn_bookmark_ui_open_epub(&ui,&app,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_ADD,NULL,now++,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_BACK,NULL,now++,present,&screen)==PN_OK && ui.mode==PN_BUI_CLOSED);
    assert(pn_epub_app_progress(&app,&after)==PN_OK && !memcmp(&before,&after,sizeof before));
    assert(pn_epub_app_percent(&app,&basis_after,&section,&count)==PN_OK && basis_after==basis_before);
    printf("before=%u after=%u footer_before=%llu footer_after=%llu\n",basis_before,basis_after,(unsigned long long)footer,(unsigned long long)screen.footer);fflush(stdout);
    assert(screen.footer==footer);
    assert(pn_bookmark_ui_open_epub(&ui,&app,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_SELECT,NULL,now++,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_JUMP,NULL,now++,present,&screen)==PN_OK);
    assert(pn_epub_app_percent(&app,&basis_after,&section,&count)==PN_OK && basis_after==basis_before && screen.footer==footer);
    assert(pn_epub_app_close(&app,now++)==PN_OK && !pool.used && !pool.live);return 0;
}
