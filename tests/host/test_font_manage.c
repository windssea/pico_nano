/* 字体管理页：列表/坏字体/详情/全局默认/删除确认/占用/删除后阅读回退。/ Font management: list/bad font/detail/global default/delete confirm/busy/reading fallback after deletion. */
#include "pn_font_manage.h"
#include "pn_reader_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int presents;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)ctx;(void)frame;(void)profile;presents++;return PN_OK;}
static const char *capture;
static pn_status_t save_frame(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)ctx;(void)profile;FILE *f=fopen(capture,"wb");assert(f);fprintf(f,"P5\n684 1216\n15\n");
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++)fputc(pn_frame_get(frame,x,y),f);
    assert(!fclose(f));return PN_OK;
}
static bool global(pn_media_t *state,const char *dir,pn_pool_t *pool,pn_font_preferences_t *out){
    pn_media_lease_t lease;assert(pn_media_acquire(state,PN_MEDIA_READ,&lease)==PN_OK);pn_journal_files_t files;pn_journal_io_t io;
    assert(pn_font_preferences_files(&files,state,&lease,dir,NULL,&io)==PN_OK);pn_status_t s=pn_font_preferences_load(&io,pool,NULL,out);
    assert(pn_media_release(state,&lease)==PN_OK);return s==PN_OK;
}
int main(int argc,char **argv){
    assert(argc==5);const char *fonts=argv[1],*state=argv[2],*book=argv[3];capture=argv[4];
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t sd,internal;pn_media_init(&sd);pn_media_init(&internal);assert(pn_media_attach(&sd,1)==PN_OK && pn_media_attach(&internal,2)==PN_OK);
    char a[600],c[600];snprintf(a,sizeof a,"%s/a-ui.ttf",fonts);snprintf(c,sizeof c,"%s/c-bad.ttf",fonts);
    pn_font_manage_t ui={0};pn_font_preferences_t g;
    assert(pn_font_manage_open(&ui,&pool,&sd,fonts,&internal,state,present,NULL)==PN_OK && ui.active && ui.presented && ui.screen==PN_FMU_LIST);
    assert(pn_font_manage_hit(&ui,100,360)==PN_FMU_ROW && pn_font_manage_hit(&ui,100,600)==PN_FMU_ROW+2 && pn_font_manage_hit(&ui,100,740)==-1 && pn_font_manage_hit(&ui,300,1150)==PN_FMU_BACK);
    /* 坏字体留在列表。/ A bad font stays on the list. */
    assert(pn_font_manage_event(&ui,PN_FMU_ROW+2,present,NULL)!=PN_OK && ui.screen==PN_FMU_LIST);
    /* 详情→设为全局默认→重复无操作。/ Detail → set global default → repeat is a no-op. */
    assert(pn_font_manage_event(&ui,PN_FMU_ROW,save_frame,NULL)==PN_OK && ui.screen==PN_FMU_DETAIL);
    assert(pn_font_manage_hit(&ui,100,1000)==PN_FMU_DEFAULT && pn_font_manage_hit(&ui,500,1000)==PN_FMU_DELETE && pn_font_manage_hit(&ui,300,1150)==PN_FMU_BACK);
    assert(!global(&internal,state,&pool,&g));
    assert(pn_font_manage_event(&ui,PN_FMU_DEFAULT,present,NULL)==PN_OK && ui.last==PN_OK);
    assert(global(&internal,state,&pool,&g) && g.primary.kind==PN_FONT_FILE && !strcmp(g.primary.path,a) && g.fallback.kind==PN_FONT_RESIDENT);
    assert(pn_font_manage_event(&ui,PN_FMU_DEFAULT,present,NULL)==PN_EMPTY);
    /* 删除：取消回详情；他人持有READ时删除失败、文件保留。/ Delete: cancel returns to detail; deletion fails and keeps the file while another READ is held. */
    assert(pn_font_manage_event(&ui,PN_FMU_DELETE,present,NULL)==PN_OK && ui.screen==PN_FMU_CONFIRMING && pn_font_manage_hit(&ui,500,1150)==PN_FMU_CONFIRM);
    assert(pn_font_manage_event(&ui,PN_FMU_CANCEL,present,NULL)==PN_OK && ui.screen==PN_FMU_DETAIL);
    pn_media_lease_t reader_lease;assert(pn_media_acquire(&sd,PN_MEDIA_READ,&reader_lease)==PN_OK);
    assert(pn_font_manage_event(&ui,PN_FMU_DELETE,present,NULL)==PN_OK && pn_font_manage_event(&ui,PN_FMU_CONFIRM,present,NULL)==PN_BUSY && ui.screen==PN_FMU_LIST && access(a,F_OK)==0);
    assert(pn_media_release(&sd,&reader_lease)==PN_OK);
    assert(pn_font_manage_event(&ui,PN_FMU_ROW,present,NULL)==PN_OK && pn_font_manage_event(&ui,PN_FMU_DELETE,present,NULL)==PN_OK && pn_font_manage_event(&ui,PN_FMU_CONFIRM,present,NULL)==PN_OK && ui.last==PN_OK);
    assert(access(a,F_OK)!=0 && access(c,F_OK)==0 && pn_font_manage_hit(&ui,100,600)==-1 && pn_font_manage_hit(&ui,100,480)==PN_FMU_ROW+1);
    /* 删除不清选择记录。/ Deletion keeps the selection record. */
    assert(global(&internal,state,&pool,&g) && !strcmp(g.primary.path,a));
    assert(pn_font_manage_event(&ui,PN_FMU_BACK,present,NULL)==PN_OK && !ui.active);pn_font_manage_close(&ui);pn_font_manage_close(&ui);assert(!pool.used && !pool.live);
    /* 删除后打开书：用启动默认字体并标记。/ Opening a book after deletion uses the startup default and flags it. */
    pn_reader_app_t app={0};assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,1)==PN_OK && pn_reader_app_font_unavailable(&app));
    assert(pn_reader_app_step(&app,PN_APP_OPEN,2,present,NULL)==PN_OK && pn_reader_app_close(&app,3)==PN_OK && !pool.live);
    assert(global(&internal,state,&pool,&g) && !strcmp(g.primary.path,a));
    /* 无内部存储：不能设默认。/ Without internal storage the default cannot be set. */
    assert(pn_font_manage_open(&ui,&pool,&sd,fonts,NULL,NULL,present,NULL)==PN_OK && pn_font_manage_event(&ui,PN_FMU_ROW,present,NULL)==PN_OK && pn_font_manage_event(&ui,PN_FMU_DEFAULT,present,NULL)==PN_UNSUPPORTED);pn_font_manage_close(&ui);
    /* 打开时逐分配失败完全回收。/ Every allocation failure while opening fully recovers. */
    pn_pool_t probe;assert(!pn_pool_init(&probe,6u*1024u*1024u,NULL,NULL,NULL));assert(pn_font_manage_open(&ui,&probe,&sd,fonts,&internal,state,present,NULL)==PN_OK);size_t attempts=probe.attempts;pn_font_manage_close(&ui);
    for(size_t fail=1;fail<=attempts;fail++){assert(!pn_pool_init(&probe,6u*1024u*1024u,NULL,NULL,NULL));probe.fail_at=fail;pn_status_t s=pn_font_manage_open(&ui,&probe,&sd,fonts,&internal,state,present,NULL);if(s==PN_OK)pn_font_manage_close(&ui);else assert(!ui.impl);assert(!probe.used && !probe.live);}
    assert(!pool.used && !pool.live);printf("font manage ok presents=%d attempts=%zu\n",presents,attempts);return 0;
}
