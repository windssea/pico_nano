/* 字体管理页：列表/坏字体/详情/全局默认/删除确认/占用/删除后阅读回退。/ Font management: list/bad font/detail/global default/delete confirm/busy/reading fallback after deletion. */
#include "pn_font_manage.h"
#include "pn_reader_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int presents;
static uint64_t list_pixels; ///< 最近列表像素摘要 / Last list pixel checksum
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)ctx;(void)frame;(void)profile;presents++;return PN_OK;}
typedef struct {pn_font_manage_t *ui;bool first;} busy_probe_t;
// 取消等待回执不进入详情，也不改变全局字体。/ Cancelling the busy receipt must not enter details or change the global font.
static pn_status_t busy_probe(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    busy_probe_t *p=ctx;if(!p->first){p->first=true;assert(pn_font_manage_hit(p->ui,100,480)==-1);assert(pn_font_manage_event(p->ui,PN_FMU_ROW,present,NULL)==PN_BUSY);return PN_CANCELLED;}
    return present(NULL,frame,profile);
}
static pn_status_t checksum(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)ctx;(void)profile;list_pixels=0;for(int y=0;y<1216;y++)for(int x=0;x<684;x++)list_pixels=list_pixels*33+pn_frame_get(frame,x,y);return PN_OK;}
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
    for(unsigned i=0;i<8;i++){pn_status_t sampled=pn_font_manage_samples_idle(&ui,1000+i,present,NULL);assert(sampled==PN_OK || sampled==PN_BUSY || sampled==PN_EMPTY);if(sampled==PN_EMPTY)break;}
    /* 暂时移走两个样本字体，重绘仍保持小样像素，证明不重复依赖外置源。/ Temporarily move both sample fonts; unchanged tile pixels prove repainting does not reread external sources. */
    assert(pn_font_manage_present(&ui,checksum,NULL)==PN_OK);uint64_t first_pixels=list_pixels;
    char tile_paths[2][1200],tile_hidden[2][1200];const char *tile_names[]={"a-ui.ttf","b-fallback.ttf"};
    for(unsigned i=0;i<2;i++){snprintf(tile_paths[i],sizeof tile_paths[i],"%s/%s",fonts,tile_names[i]);snprintf(tile_hidden[i],sizeof tile_hidden[i],"%s/.sample-%u",fonts,i);assert(rename(tile_paths[i],tile_hidden[i])==0);}
    assert(pn_font_manage_present(&ui,checksum,NULL)==PN_OK && list_pixels==first_pixels);
    for(unsigned i=0;i<2;i++)assert(rename(tile_hidden[i],tile_paths[i])==0);
    // 顶部两行当前字体右侧是重置按钮；TF卡字体从434开始。/ The two current-font rows carry reset buttons on the right; TF-card fonts start at 434.
    assert(pn_font_manage_hit(&ui,600,230)==PN_FMU_RESET_SYSTEM && pn_font_manage_hit(&ui,600,330)==PN_FMU_RESET_READING && pn_font_manage_hit(&ui,100,230)==-1);
    // 尚未选择外置字体时重置无操作。/ Reset is a no-op while no external font is chosen.
    assert(pn_font_manage_event(&ui,PN_FMU_RESET_SYSTEM,present,NULL)==PN_EMPTY && pn_font_manage_event(&ui,PN_FMU_RESET_READING,present,NULL)==PN_EMPTY);
    assert(pn_font_manage_hit(&ui,100,480)==PN_FMU_ROW && pn_font_manage_hit(&ui,100,680)==PN_FMU_ROW+2 && pn_font_manage_hit(&ui,100,840)==-1 && pn_font_manage_hit(&ui,100,60)==PN_FMU_BACK && pn_font_manage_hit(&ui,100,1120)==PN_FMU_PREVIOUS && pn_font_manage_hit(&ui,600,1120)==PN_FMU_NEXT);
    /* 坏字体留在列表。/ A bad font stays on the list. */
    assert(pn_font_manage_event(&ui,PN_FMU_ROW+2,present,NULL)!=PN_OK && ui.screen==PN_FMU_LIST);
    /* 详情→设为全局默认→重复无操作。/ Detail → set global default → repeat is a no-op. */
    busy_probe_t busy_check={.ui=&ui};assert(pn_font_manage_event(&ui,PN_FMU_ROW,busy_probe,&busy_check)==PN_CANCELLED && busy_check.first && ui.presented && ui.screen==PN_FMU_LIST && !global(&internal,state,&pool,&g));
    assert(pn_font_manage_event(&ui,PN_FMU_ROW,save_frame,NULL)==PN_OK && ui.screen==PN_FMU_DETAIL);
    assert(pn_font_manage_hit(&ui,100,1000)==PN_FMU_DEFAULT && pn_font_manage_hit(&ui,500,1000)==PN_FMU_DELETE && pn_font_manage_hit(&ui,300,1100)==PN_FMU_SYSTEM && pn_font_manage_hit(&ui,500,1100)==PN_FMU_BACK);
    /* 设为系统字体→记录指向该文件；再按恢复默认（未选择）。/ Set as system font → the record names this file; pressing again restores the default (unset). */
    {char chosen[1024],dir[1024];strcpy(dir,a);*strrchr(dir,'/')=0;
     assert(pn_font_preferences_system_path(&internal,state,&pool,dir,chosen,sizeof chosen)==PN_EMPTY);
     assert(pn_font_manage_event(&ui,PN_FMU_SYSTEM,present,NULL)==PN_OK && ui.last==PN_OK && ui.system_changed && ui.screen==PN_FMU_DETAIL);
     assert(pn_font_preferences_system_path(&internal,state,&pool,dir,chosen,sizeof chosen)==PN_OK && !strcmp(chosen,a));
     assert(pn_font_manage_event(&ui,PN_FMU_SYSTEM,present,NULL)==PN_OK && pn_font_preferences_system_path(&internal,state,&pool,dir,chosen,sizeof chosen)==PN_EMPTY);}
    assert(!global(&internal,state,&pool,&g));
    assert(pn_font_manage_event(&ui,PN_FMU_DEFAULT,present,NULL)==PN_OK && ui.last==PN_OK);
    assert(global(&internal,state,&pool,&g) && g.primary.kind==PN_FONT_FILE && !strcmp(g.primary.path,a) && g.fallback.kind==PN_FONT_RESIDENT);
    // 已是默认时再按即恢复系统内置字体；再按又设回该文件。/ Pressing again on the current default restores the built-in system font; pressing once more sets the file again.
    assert(pn_font_manage_event(&ui,PN_FMU_DEFAULT,present,NULL)==PN_OK && ui.last==PN_OK);
    assert(global(&internal,state,&pool,&g) && g.primary.kind==PN_FONT_RESIDENT);
    assert(pn_font_manage_event(&ui,PN_FMU_DEFAULT,present,NULL)==PN_OK && ui.last==PN_OK);
    assert(global(&internal,state,&pool,&g) && g.primary.kind==PN_FONT_FILE && !strcmp(g.primary.path,a));
    /* 删除：取消回详情；他人持有READ时删除失败、文件保留。/ Delete: cancel returns to detail; deletion fails and keeps the file while another READ is held. */
    assert(pn_font_manage_event(&ui,PN_FMU_DELETE,present,NULL)==PN_OK && ui.screen==PN_FMU_CONFIRMING && pn_font_manage_hit(&ui,500,1150)==PN_FMU_CONFIRM);
    assert(pn_font_manage_event(&ui,PN_FMU_CANCEL,present,NULL)==PN_OK && ui.screen==PN_FMU_DETAIL);
    pn_media_lease_t reader_lease;assert(pn_media_acquire(&sd,PN_MEDIA_READ,&reader_lease)==PN_OK);
    assert(pn_font_manage_event(&ui,PN_FMU_DELETE,present,NULL)==PN_OK && pn_font_manage_event(&ui,PN_FMU_CONFIRM,present,NULL)==PN_BUSY && ui.screen==PN_FMU_LIST && access(a,F_OK)==0);
    assert(pn_media_release(&sd,&reader_lease)==PN_OK);
    assert(pn_font_manage_event(&ui,PN_FMU_ROW,present,NULL)==PN_OK && pn_font_manage_event(&ui,PN_FMU_DELETE,present,NULL)==PN_OK && pn_font_manage_event(&ui,PN_FMU_CONFIRM,present,NULL)==PN_OK && ui.last==PN_OK);
    assert(access(a,F_OK)!=0 && access(c,F_OK)==0 && pn_font_manage_hit(&ui,100,680)==-1 && pn_font_manage_hit(&ui,100,580)==PN_FMU_ROW+1);
    /* 删除不清选择记录。/ Deletion keeps the selection record. */
    assert(global(&internal,state,&pool,&g) && !strcmp(g.primary.path,a));
    assert(pn_font_manage_event(&ui,PN_FMU_BACK,present,NULL)==PN_OK && !ui.active);pn_font_manage_close(&ui);pn_font_manage_close(&ui);assert(!pool.used && !pool.live);
    /* 删除后打开书：用启动默认字体并标记。/ Opening a book after deletion uses the startup default and flags it. */
    pn_reader_app_t app={0};assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,1)==PN_OK && pn_reader_app_font_unavailable(&app));
    assert(pn_reader_app_step(&app,PN_APP_OPEN,2,present,NULL)==PN_OK && pn_reader_app_close(&app,3)==PN_OK && !pool.live);
    assert(global(&internal,state,&pool,&g) && !strcmp(g.primary.path,a));
    /* 列表顶部“重置”把阅读字体恢复为固件字体。/ The list-top reset restores the reading font to the firmware face. */
    assert(pn_font_manage_open(&ui,&pool,&sd,fonts,&internal,state,present,NULL)==PN_OK && pn_font_manage_event(&ui,PN_FMU_RESET_READING,present,NULL)==PN_OK && ui.screen==PN_FMU_LIST);
    assert(global(&internal,state,&pool,&g) && g.primary.kind==PN_FONT_RESIDENT && pn_font_manage_event(&ui,PN_FMU_RESET_READING,present,NULL)==PN_EMPTY);pn_font_manage_close(&ui);assert(!pool.live);
    /* 无内部存储：不能设默认。/ Without internal storage the default cannot be set. */
    assert(pn_font_manage_open(&ui,&pool,&sd,fonts,NULL,NULL,present,NULL)==PN_OK && pn_font_manage_event(&ui,PN_FMU_ROW,present,NULL)==PN_OK && pn_font_manage_event(&ui,PN_FMU_DEFAULT,present,NULL)==PN_UNSUPPORTED);pn_font_manage_close(&ui);
    /* 改名的鸿蒙字库仍由家族识别保护，触摸和直接命令都拒绝删除。/ Renamed HarmonyOS remains protected by family for touch and direct commands. */
    char protected_path[600];snprintf(protected_path,sizeof protected_path,"%s/d-renamed.ttf",fonts);
    pn_text_source_t harmony=pn_font_builtin_source();FILE *protected_file=fopen(protected_path,"wb");assert(protected_file);uint8_t bytes[4096];
    for(uint64_t off=0;off<harmony.size;){size_t n;assert(harmony.read_at(harmony.ctx,off,bytes,sizeof bytes,&n)==PN_OK && n && fwrite(bytes,1,n,protected_file)==n);off+=n;}assert(!fclose(protected_file));
    assert(pn_font_manage_open(&ui,&pool,&sd,fonts,&internal,state,present,NULL)==PN_OK);
    assert(pn_font_manage_event(&ui,PN_FMU_ROW+2,present,NULL)==PN_OK && ui.screen==PN_FMU_DETAIL);
    assert(pn_font_manage_hit(&ui,500,1000)==-1 && pn_font_manage_event(&ui,PN_FMU_DELETE,present,NULL)==PN_UNSUPPORTED && ui.screen==PN_FMU_DETAIL && access(protected_path,F_OK)==0);
    assert(pn_font_manage_event(&ui,PN_FMU_CONFIRM,present,NULL)==PN_EMPTY && access(protected_path,F_OK)==0);pn_font_manage_close(&ui);assert(!pool.live);
    /* 打开时逐分配失败完全回收。/ Every allocation failure while opening fully recovers. */
    pn_pool_t probe;assert(!pn_pool_init(&probe,6u*1024u*1024u,NULL,NULL,NULL));assert(pn_font_manage_open(&ui,&probe,&sd,fonts,&internal,state,present,NULL)==PN_OK);size_t attempts=probe.attempts;pn_font_manage_close(&ui);
    for(size_t fail=1;fail<=attempts;fail++){assert(!pn_pool_init(&probe,6u*1024u*1024u,NULL,NULL,NULL));probe.fail_at=fail;pn_status_t s=pn_font_manage_open(&ui,&probe,&sd,fonts,&internal,state,present,NULL);if(s==PN_OK)pn_font_manage_close(&ui);else assert(!ui.impl);assert(!probe.used && !probe.live);}
    assert(!pool.used && !pool.live);printf("font manage ok presents=%d attempts=%zu\n",presents,attempts);return 0;
}
