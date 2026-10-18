/* 实际EPUB页、字体、显示失败和关闭/重开。/ Actual EPUB/font pages, failed presentation and close/reopen. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_app.h"
#include "pn_style_ui.h"
#include "pn_bookmark_ui.h"
#include "pn_font_preferences.h"
#include "pn_font_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct {bool fail;pn_pool_t *save_fault;unsigned calls,tag;pn_refresh_t profile;uint64_t hash;} screen_t;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    screen_t *s=ctx;s->profile=profile;s->calls++;s->tag=pn_frame_get(frame,17,17);if(s->fail)return PN_IO;uint64_t hash=1469598103934665603ULL;for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++){hash^=pn_frame_get(frame,x,y);hash*=1099511628211ULL;}s->hash=hash;if(s->save_fault){s->save_fault->fail_at=s->save_fault->attempts+1;s->save_fault=NULL;}return PN_OK;
}
static bool same(const pn_epub_progress_t *a,const pn_epub_progress_t *b){return !memcmp(a->book.sha256,b->book.sha256,32) && !strcmp(a->location.path,b->location.path) && a->location.position.element==b->location.position.element && a->location.position.offset==b->location.position.offset && a->location.position.run==b->location.position.run && a->location.position.kind==b->location.position.kind;}
/* 最终目标页使用真实候选字体，并留可观察像素；测试不伪造呈现成功。/ The destination draws the actual candidate font and observable pixels; tests never fake presentation success. */
static pn_status_t destination(void *ctx,pn_font_t *ui,pn_font_t *body,pn_frame_t *frame){
 (void)ui;if(ctx && *(bool *)ctx)return PN_IO;pn_frame_clear(frame,15);pn_frame_rect(frame,16,16,4,4,9);return pn_font_draw(body,frame,'A',32*64,120,PN_FONT_GRAY);
}
int main(int argc,char **argv){
    pn_epub_app_t app={0};assert(pn_epub_app_close(&app,0)==PN_OK && !pn_epub_app_last_confirmed(&app));if(argc==1)return 0;assert(argc==4);
    pn_pool_t pool;assert(!pn_pool_init(&pool,2*1024*1024,NULL,NULL,NULL));screen_t screen={.fail=true};pn_epub_progress_t first,current,after;uint64_t now=1;
    assert(pn_epub_app_open(&app,&pool,argv[1],argv[3],argv[2],44,0)==PN_OK);
    assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_IO && !pn_epub_app_last_confirmed(&app) && pn_epub_app_progress(&app,&first)==PN_EMPTY);
    assert(pn_epub_app_fallback_font(&app,"/missing-font.ttf")==PN_IO);
    assert(pn_epub_app_fallback_font(&app,argv[3])==PN_OK);
    uint64_t mark_id;assert(pn_epub_app_bookmark_add(&app,"未显示",&mark_id)==PN_EMPTY);
    screen.fail=false;assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&first)==PN_OK);uint64_t first_hash=screen.hash;assert(pn_epub_app_bookmark_add(&app,"第一页",&mark_id)==PN_OK && mark_id==1);
    assert(pn_epub_app_suspend(&app,now++)==PN_OK && app.impl);
    assert(pn_epub_app_resume(&app,now++,present,&screen)==PN_OK && screen.hash==first_hash && pn_epub_app_progress(&app,&current)==PN_OK && same(&first,&current));
    assert(pn_epub_app_suspend(&app,now++)==PN_OK);
    screen.fail=true;assert(pn_epub_app_resume(&app,now++,present,&screen)==PN_IO && pn_epub_app_progress(&app,&current)==PN_OK && same(&first,&current));
    screen.fail=false;assert(pn_epub_app_suspend(&app,now++)==PN_OK && pn_epub_app_resume(&app,now++,present,&screen)==PN_OK);
    screen.fail=true;assert(pn_epub_app_step(&app,PN_APP_NEXT,now++,present,&screen)==PN_IO && pn_epub_app_progress(&app,&current)==PN_OK && same(&first,&current));
    screen.fail=false;assert(pn_epub_app_step(&app,PN_APP_NEXT,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&current)==PN_OK && !same(&first,&current) && screen.hash!=first_hash);
    screen.fail=true;assert(pn_epub_app_bookmark_jump(&app,mark_id,now++,present,&screen)==PN_IO && !pn_epub_app_bookmark_can_return(&app) && pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));
    screen.fail=false;assert(pn_epub_app_bookmark_jump(&app,mark_id,now++,present,&screen)==PN_OK && pn_epub_app_bookmark_can_return(&app) && pn_epub_app_progress(&app,&after)==PN_OK && same(&first,&after));
    screen.fail=true;assert(pn_epub_app_bookmark_return(&app,now++,present,&screen)==PN_IO && pn_epub_app_bookmark_can_return(&app));
    screen.fail=false;assert(pn_epub_app_bookmark_return(&app,now++,present,&screen)==PN_OK && !pn_epub_app_bookmark_can_return(&app) && pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));
    assert(pn_epub_app_bookmark_jump(&app,mark_id,now++,present,&screen)==PN_OK && pn_epub_app_bookmark_can_return(&app));
    screen.fail=true;assert(pn_epub_app_step(&app,PN_APP_NEXT,now++,present,&screen)==PN_IO && pn_epub_app_bookmark_can_return(&app));screen.fail=false;
    assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK && pn_epub_app_bookmark_can_return(&app));
    assert(pn_epub_app_step(&app,PN_APP_NEXT,now++,present,&screen)==PN_OK && !pn_epub_app_bookmark_can_return(&app));
    assert(pn_epub_app_bookmark_return(&app,now++,present,&screen)==PN_EMPTY);
    assert(pn_epub_app_bookmark_rename(&app,mark_id,"首章")==PN_OK);
    pn_epub_bookmark_t marks[6];size_t mark_count;bool mark_more;assert(pn_epub_app_bookmark_list(&app,0,marks,6,&mark_count,&mark_more)==PN_OK && mark_count==1 && !mark_more && !strcmp(marks[0].label,"首章"));
    pn_bookmark_ui_t bm={0};assert(pn_bookmark_ui_open_epub(&bm,&app,present,&screen)==PN_OK && bm.count==1);
    assert(pn_bookmark_ui_event(&bm,PN_BUI_SELECT,NULL,now++,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&bm,PN_BUI_EDIT,NULL,now++,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&bm,PN_BUI_CLEAR,NULL,now++,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&bm,PN_BUI_TEXT,"首章",now++,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&bm,PN_BUI_SAVE,NULL,now++,present,&screen)==PN_OK && bm.mode==PN_BUI_LIST);
    assert(pn_bookmark_ui_event(&bm,PN_BUI_SELECT,NULL,now++,present,&screen)==PN_OK);
    screen.fail=true;assert(pn_bookmark_ui_event(&bm,PN_BUI_REMOVE,NULL,now++,present,&screen)==PN_IO && !bm.presented);
    assert(pn_bookmark_ui_event(&bm,PN_BUI_CONFIRM,NULL,now++,present,&screen)==PN_BUSY);
    screen.fail=false;assert(pn_bookmark_ui_event(&bm,PN_BUI_RETRY,NULL,now++,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&bm,PN_BUI_CANCEL,NULL,now++,present,&screen)==PN_OK && bm.mode==PN_BUI_ACTIONS);
    assert(pn_bookmark_ui_event(&bm,PN_BUI_JUMP,NULL,now++,present,&screen)==PN_OK && bm.mode==PN_BUI_CLOSED && pn_epub_app_bookmark_can_return(&app));
    assert(pn_epub_app_bookmark_return(&app,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));
    pn_font_preferences_t font_candidate={0};screen.fail=true;assert(pn_epub_app_font_preview(&app,&font_candidate,now++,present,&screen)==PN_IO && pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));screen.fail=false;
    assert(pn_epub_app_font_preview(&app,&font_candidate,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));
    assert(pn_epub_app_font_cancel(&app,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));
    pn_font_ui_t chooser={0};screen.fail=true;assert(pn_font_ui_open(&chooser,&pool,NULL,&app,NULL,present,&screen)==PN_IO && !chooser.presented);assert(pn_font_ui_event(&chooser,PN_FUI_APPLY,now++,present,&screen)==PN_BUSY);screen.fail=false;
    assert(pn_font_ui_event(&chooser,PN_FUI_RETRY,now++,present,&screen)==PN_OK);
    assert(pn_font_ui_event(&chooser,PN_FUI_SELECT,now++,present,&screen)==PN_OK && chooser.mode==1);
    assert(pn_font_ui_event(&chooser,PN_FUI_PREVIEW,now++,present,&screen)==PN_OK && chooser.preview && pn_epub_app_progress(&app,&after)==PN_OK && same(&after,&current));
    assert(pn_font_ui_event(&chooser,PN_FUI_FORM,now++,present,&screen)==PN_OK);
    assert(pn_font_ui_event(&chooser,PN_FUI_CANCEL,now++,present,&screen)==PN_OK && !chooser.active);pn_font_ui_close(&chooser);
    // 单帧目标页仍遵守绘制/呈现/保存屏障，失败后可重试原字体请求。/ A single destination frame retains paint/display/save barriers and allows retrying the original font request.
    pn_font_preferences_t before_view,resident_view={0};char view_directory[PN_FONT_REFERENCE_PATH_MAX];assert(pn_epub_app_fonts_get(&app,&before_view,view_directory,sizeof view_directory)==PN_OK);
    unsigned calls=screen.calls;bool failed_paint=true;assert(pn_epub_app_font_apply_view(&app,&resident_view,destination,&failed_paint,now++,present,&screen)==PN_IO && screen.calls==calls && !pn_epub_app_last_confirmed(&app));
    screen.fail=true;assert(pn_epub_app_font_apply_view(&app,&resident_view,destination,NULL,now++,present,&screen)==PN_IO && screen.calls==calls+1);screen.fail=false;
    assert(pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));
    calls=screen.calls;screen.save_fault=&pool;assert(pn_epub_app_font_apply_view(&app,&resident_view,destination,NULL,now++,present,&screen)==PN_NO_MEMORY && pn_epub_app_last_confirmed(&app) && screen.calls==calls+1 && screen.tag==9);pool.fail_at=0;
    calls=screen.calls;assert(pn_epub_app_font_apply_view(&app,&resident_view,destination,NULL,now++,present,&screen)==PN_OK && screen.calls==calls+1 && screen.tag==9);
    calls=screen.calls;assert(pn_epub_app_font_cancel_view(&app,destination,NULL,now++,present,&screen)==PN_OK && screen.calls==calls+1 && screen.tag==9);
    assert(pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));
    assert(pn_epub_app_font_apply(&app,&before_view,now++,present,&screen)==PN_OK);
    pn_style_t style;assert(pn_epub_app_style_get(&app,&style)==PN_OK);style.pixels=46;style.line_percent=155;style.gap_percent=50;style.indent_em=2;style.tracking_percent=10;style.margin=40;style.reading_line=1;style.reading_line_offset=-2;
    assert(pn_epub_app_style_apply(&app,&style,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));
    pn_style_t draft=style;draft.tracking_percent=20;assert(pn_epub_app_style_preview(&app,&draft,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));
    assert(pn_epub_app_style_cancel(&app,now++,present,&screen)==PN_OK && pn_epub_app_style_get(&app,&draft)==PN_OK && draft.tracking_percent==style.tracking_percent);
    pn_style_ui_t ui={0};screen.fail=true;assert(pn_style_ui_open_epub(&ui,&app,present,&screen)==PN_IO && !ui.presented && pn_style_ui_event(&ui,PN_SUI_APPLY,now++,present,&screen)==PN_BUSY);
    screen.fail=false;assert(pn_style_ui_event(&ui,PN_SUI_RETRY,now++,present,&screen)==PN_OK);
    assert(pn_style_ui_event(&ui,PN_SUI_FIELD+7,now++,present,&screen)==PN_EMPTY && ui.draft.indent_em==2);
    for(unsigned i=0;i<PN_SUI_FIELDS;i++){pn_status_t stepped=pn_style_ui_event(&ui,PN_SUI_FIELD+(int)i*2+(i==3?0:1),now++,present,&screen);assert(stepped==PN_OK);} /* 档位项首尾循环，到最宽的边距再加回到最窄 / option fields wrap, so the widest margin steps back to the narrowest */
    assert(pn_style_ui_event(&ui,PN_SUI_PREVIEW,now++,present,&screen)==PN_OK && ui.preview && pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));
    assert(pn_style_ui_event(&ui,PN_SUI_FORM,now++,present,&screen)==PN_OK && pn_style_ui_event(&ui,PN_SUI_CANCEL,now++,present,&screen)==PN_OK && !ui.active);
    assert(pn_epub_app_style_get(&app,&draft)==PN_OK && !memcmp(&draft,&style,sizeof style));
    pn_style_t candidate=style;candidate.pixels=48;screen.fail=true;
    assert(pn_epub_app_style_apply(&app,&candidate,now++,present,&screen)==PN_IO && pn_epub_app_style_get(&app,&candidate)==PN_OK && candidate.pixels==46 && pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));screen.fail=false;
    char hash[65],blocked[768];for(unsigned i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",current.book.sha256[i]);snprintf(blocked,sizeof blocked,"%s/%s.epub.b",argv[2],hash);assert(!mkdir(blocked,0700));
    assert(pn_epub_app_close(&app,now++)==PN_IO && app.impl);assert(!rmdir(blocked));assert(pn_epub_app_close(&app,now++)==PN_OK && !app.impl && !pool.used && !pool.live);
    assert(pn_epub_app_open(&app,&pool,argv[1],argv[3],argv[2],44,now++)==PN_OK && pn_epub_app_style_get(&app,&candidate)==PN_OK && !memcmp(&candidate,&style,sizeof style));
    assert(pn_epub_app_bookmark_list(&app,0,marks,6,&mark_count,&mark_more)==PN_OK && mark_count==1 && !strcmp(marks[0].label,"首章"));
    assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));
    size_t toc_count;pn_toc_entry_t entry;assert(pn_epub_app_toc_count(&app,&toc_count)==PN_OK && toc_count==2 && pn_epub_app_toc_get(&app,0,&entry)==PN_OK && !strcmp(entry.fragment,"p100"));
    screen.fail=true;assert(pn_epub_app_toc_jump(&app,0,now++,present,&screen)==PN_IO && pn_epub_app_progress(&app,&after)==PN_OK && same(&after,&current));screen.fail=false;
    assert(pn_epub_app_toc_jump(&app,1,now++,present,&screen)==PN_EMPTY && !pn_epub_app_last_confirmed(&app));
    assert(pn_epub_app_toc_jump(&app,0,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&current)==PN_OK && !same(&after,&current));
    assert(pn_epub_app_media_lost(&app)==PN_OK && pn_epub_app_step(&app,PN_APP_NEXT,now++,present,&screen)==PN_STALE_MEDIA && pn_epub_app_progress(&app,&after)==PN_OK && same(&after,&current));
    assert(pn_epub_app_bookmark_add(&app,"拔卡",&mark_id)==PN_STALE_MEDIA && pn_epub_app_bookmark_list(&app,0,marks,6,&mark_count,&mark_more)==PN_STALE_MEDIA && !mark_count);
    // 最近记录损坏端口也阻止关闭，不丢已保存位置。/ A failed recent-record port also blocks closing without losing saved position.
    char recent_b[768],backup[768];snprintf(recent_b,sizeof recent_b,"%s/recent.a",argv[2]);snprintf(backup,sizeof backup,"%s/recent.saved",argv[2]);
    assert(!rename(recent_b,backup) && !mkdir(recent_b,0700));assert(pn_epub_app_close(&app,now++)==PN_IO && app.impl);
    assert(!rmdir(recent_b) && !rename(backup,recent_b));
    assert(pn_epub_app_close(&app,now++)==PN_OK && !pool.used && !pool.live);
    // 保存失败后再预览另一草稿，关闭仍重试原应用意图。/ After a failed save and another preview, close retries the original apply intent.
    assert(pn_epub_app_open(&app,&pool,argv[1],argv[3],argv[2],44,now++)==PN_OK);
    assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK);
    snprintf(blocked,sizeof blocked,"%s/%s.style.b",argv[2],hash);assert(!mkdir(blocked,0700));
    candidate=style;candidate.tracking_percent=15;
    assert(pn_epub_app_style_apply(&app,&candidate,now++,present,&screen)==PN_IO && pn_epub_app_last_confirmed(&app));
    draft=candidate;draft.tracking_percent=25;
    assert(pn_epub_app_style_preview(&app,&draft,now++,present,&screen)==PN_OK);
    assert(pn_epub_app_close(&app,now++)==PN_IO && app.impl);assert(!rmdir(blocked));
    assert(pn_epub_app_close(&app,now++)==PN_OK && !pool.used && !pool.live);
    assert(pn_epub_app_open(&app,&pool,argv[1],argv[3],argv[2],44,now++)==PN_OK);
    assert(pn_epub_app_style_get(&app,&draft)==PN_OK && draft.tracking_percent==15);
    assert(pn_epub_app_close(&app,now++)==PN_OK && !pool.used && !pool.live);
    // 保存故障后预览另一份字体，关闭仍保存原应用意图。/ Preview other fonts after a save failure; close retains original apply intent.
    assert(pn_epub_app_open(&app,&pool,argv[1],argv[3],argv[2],44,now++)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK);
    screen.save_fault=&pool;pn_font_preferences_t chosen_fonts={0};assert(pn_epub_app_font_apply(&app,&chosen_fonts,now++,present,&screen)==PN_NO_MEMORY && pn_epub_app_last_confirmed(&app));pool.fail_at=0;
    pn_media_t probe_media;pn_media_init(&probe_media);assert(pn_media_attach(&probe_media,1)==PN_OK);pn_media_lease_t probe_lease;assert(pn_media_acquire(&probe_media,PN_MEDIA_READ,&probe_lease)==PN_OK);
    pn_font_preferences_t other_fonts={0};other_fonts.primary.kind=PN_FONT_FILE;strcpy(other_fonts.primary.path,argv[3]);struct stat probe_stat;assert(!stat(argv[3],&probe_stat));other_fonts.primary.size=(uint64_t)probe_stat.st_size;assert(pn_identity_file(&probe_media,&probe_lease,argv[3],PN_FONT_REFERENCE_MAX_BYTES,&other_fonts.primary.identity)==PN_OK);assert(pn_media_release(&probe_media,&probe_lease)==PN_OK && pn_media_detach(&probe_media)==PN_OK);
    assert(pn_epub_app_font_preview(&app,&other_fonts,now++,present,&screen)==PN_OK);
    assert(pn_epub_app_font_apply(&app,&other_fonts,now++,present,&screen)==PN_BUSY && !pn_epub_app_last_confirmed(&app));
    assert(pn_epub_app_font_cancel(&app,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&after)==PN_OK);
    screen.save_fault=&pool;assert(pn_epub_app_font_apply(&app,&chosen_fonts,now++,present,&screen)==PN_NO_MEMORY && pn_epub_app_last_confirmed(&app));pool.fail_at=0;
    assert(pn_epub_app_font_preview(&app,&other_fonts,now++,present,&screen)==PN_OK);

    pool.fail_at=pool.attempts+1;assert(pn_epub_app_close(&app,now++)==PN_NO_MEMORY && app.impl);pool.fail_at=0;assert(pn_epub_app_close(&app,now++)==PN_OK && !pool.used && !pool.live);
    assert(pn_epub_app_open(&app,&pool,argv[1],"/missing-bootstrap.ttf",argv[2],44,now++)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK && pn_epub_app_close(&app,now++)==PN_OK);
    pn_media_init(&probe_media);assert(pn_media_attach(&probe_media,1)==PN_OK && pn_media_acquire(&probe_media,PN_MEDIA_WRITE,&probe_lease)==PN_OK);pn_journal_files_t selected_files;pn_journal_io_t selected_io;assert(pn_font_preferences_files(&selected_files,&probe_media,&probe_lease,argv[2],&current.book,&selected_io)==PN_OK);pn_font_preferences_t selected_read;assert(pn_font_preferences_load(&selected_io,&pool,&current.book,&selected_read)==PN_OK && selected_read.primary.kind==PN_FONT_RESIDENT);
    pn_font_preferences_t reset_fonts={.inherit=true};assert(pn_font_preferences_save(&selected_io,&pool,&current.book,&reset_fonts)==PN_OK);assert(pn_media_release(&probe_media,&probe_lease)==PN_OK && pn_media_detach(&probe_media)==PN_OK);
    // 全局选择覆盖启动默认，逐书选择优先，身份不匹配拒绝打开。/ Global selections override boot defaults, per-book selections win and identity mismatches reject opening.
    pn_media_t font_media;pn_media_init(&font_media);assert(pn_media_attach(&font_media,1)==PN_OK);pn_media_lease_t font_lease;assert(pn_media_acquire(&font_media,PN_MEDIA_READ,&font_lease)==PN_OK);
    pn_font_preferences_t fonts={0};fonts.primary.kind=PN_FONT_FILE;strcpy(fonts.primary.path,argv[3]);struct stat font_stat;assert(!stat(argv[3],&font_stat));fonts.primary.size=(uint64_t)font_stat.st_size;assert(pn_identity_file(&font_media,&font_lease,argv[3],PN_FONT_REFERENCE_MAX_BYTES,&fonts.primary.identity)==PN_OK);
    assert(pn_epub_app_open(&app,&pool,argv[1],argv[3],NULL,44,0)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK);
    pn_style_t legacy;assert(pn_epub_app_style_get(&app,&legacy)==PN_OK);legacy.gl_before_clear=0;
    assert(pn_epub_app_style_apply(&app,&legacy,now++,present,&screen)==PN_OK);
    for(unsigned p=0;p<3;p++){
        const unsigned periods[]={6,14,24};unsigned period=periods[p];pn_epub_app_refresh_period(&app,period);pn_epub_app_refresh_reset(&app);
        for(unsigned i=1;i<=period;i++){
            assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK && screen.profile==PN_REFRESH_GL16);
            assert(pn_epub_app_step(&app,i%2?PN_APP_NEXT:PN_APP_PREVIOUS,now++,present,&screen)==PN_OK);
            assert(screen.profile==(i==period?PN_REFRESH_GC16:PN_REFRESH_GL16));
        }
    }
    assert(pn_epub_app_close(&app,now++)==PN_OK && !pool.used && !pool.live);
    assert(pn_media_release(&font_media,&font_lease)==PN_OK && pn_media_acquire(&font_media,PN_MEDIA_WRITE,&font_lease)==PN_OK);
    pn_journal_files_t global_files,book_files;pn_journal_io_t global_io,book_io;assert(pn_font_preferences_files(&global_files,&font_media,&font_lease,argv[2],NULL,&global_io)==PN_OK && pn_font_preferences_files(&book_files,&font_media,&font_lease,argv[2],&current.book,&book_io)==PN_OK);
    assert(pn_font_preferences_save(&global_io,&pool,NULL,&fonts)==PN_OK);
    assert(pn_epub_app_open(&app,&pool,argv[1],"/missing-boot-font.ttf",argv[2],44,now++)==PN_OK);
    assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK && pn_epub_app_close(&app,now++)==PN_OK);
    // 全局和逐书分别提交，继承事务保存标记而非展开后的文件引用。/ Global and per-book commits stay separate; inheritance stores a marker rather than expanded file references.
    assert(pn_epub_app_open(&app,&pool,argv[1],argv[3],argv[2],44,now++)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK);
    assert(pn_epub_app_font_apply(&app,&chosen_fonts,now++,present,&screen)==PN_OK);
    assert(pn_epub_app_font_default(&app,&fonts)==PN_OK && pn_font_preferences_load(&book_io,&pool,&current.book,&selected_read)==PN_OK && !selected_read.inherit && selected_read.primary.kind==PN_FONT_RESIDENT);
    assert(pn_epub_app_progress(&app,&current)==PN_OK);screen.fail=true;assert(pn_epub_app_font_inherit(&app,now++,present,&screen)==PN_IO);screen.fail=false;
    assert(pn_font_preferences_load(&book_io,&pool,&current.book,&selected_read)==PN_OK && !selected_read.inherit);
    screen.save_fault=&pool;assert(pn_epub_app_font_inherit(&app,now++,present,&screen)==PN_NO_MEMORY && pn_epub_app_last_confirmed(&app));pool.fail_at=0;
    assert(pn_epub_app_font_apply(&app,&fonts,now++,present,&screen)==PN_BUSY);
    assert(pn_epub_app_font_cancel(&app,now++,present,&screen)==PN_OK && pn_font_preferences_load(&book_io,&pool,&current.book,&selected_read)==PN_OK && !selected_read.inherit);
    assert(pn_epub_app_font_inherit(&app,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&after)==PN_OK && same(&current,&after));
    assert(pn_font_preferences_load(&book_io,&pool,&current.book,&selected_read)==PN_OK && selected_read.inherit);
    assert(pn_epub_app_font_apply(&app,&chosen_fonts,now++,present,&screen)==PN_OK);
    screen.save_fault=&pool;assert(pn_epub_app_font_inherit(&app,now++,present,&screen)==PN_NO_MEMORY);pool.fail_at=0;
    assert(pn_epub_app_font_preview(&app,&chosen_fonts,now++,present,&screen)==PN_OK);
    pool.fail_at=pool.attempts+1;assert(pn_epub_app_close(&app,now++)==PN_NO_MEMORY && app.impl);pool.fail_at=0;
    assert(pn_epub_app_close(&app,now++)==PN_OK && !pool.used && !pool.live);
    assert(pn_font_preferences_load(&book_io,&pool,&current.book,&selected_read)==PN_OK && selected_read.inherit);
    assert(pn_epub_app_open(&app,&pool,argv[1],"/missing-bootstrap.ttf",argv[2],44,now++)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK);

    assert(pn_epub_app_close(&app,now++)==PN_OK && !pool.used && !pool.live);
    fonts.primary.identity.sha256[0]^=1;assert(pn_font_preferences_save(&global_io,&pool,NULL,&fonts)==PN_OK);
    // 所选字体被替换：外置字体首屏后后台复核，失配才回退常驻字体并标记；记录保持原样。
    // Replaced selection: the external font is verified after the first frame; only a mismatch falls back to the resident face and flags it; the record stays intact.
    assert(pn_epub_app_open(&app,&pool,argv[1],argv[3],argv[2],44,now++)==PN_OK);
    screen.fail=false;assert(pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK && pn_epub_app_progress(&app,&first)==PN_OK);
    pn_status_t verify_status;while((verify_status=pn_epub_app_verify_step(&app))==PN_BUSY){}
    bool unavailable=pn_epub_app_font_unavailable(&app);
    // 字体降级的呈现失败保留同一位置与恢复标志，重试成功后才清除。/ A failed degraded-font repaint retains the position/recovery flag until a successful retry.
    screen.fail=true;assert(pn_epub_app_font_recovery_pending(&app) && pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_IO && pn_epub_app_font_recovery_pending(&app));
    assert(pn_epub_app_progress(&app,&after)==PN_OK && same(&first,&after));screen.fail=false;
    pn_status_t reopen=pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen);
    assert(!pn_epub_app_font_recovery_pending(&app) && pn_epub_app_progress(&app,&after)==PN_OK && same(&first,&after));
    pn_status_t closed=pn_epub_app_close(&app,now++);
    assert(verify_status==PN_STALE_JOB && unavailable && reopen==PN_OK && closed==PN_OK && !pool.used && !pool.live);
    {pn_font_preferences_t kept;assert(pn_font_preferences_load(&global_io,&pool,NULL,&kept)==PN_OK && !memcmp(&kept.primary.identity,&fonts.primary.identity,sizeof kept.primary.identity));}
    pn_font_preferences_t per_book={0};assert(pn_font_preferences_save(&book_io,&pool,&current.book,&per_book)==PN_OK);
    assert(pn_epub_app_open(&app,&pool,argv[1],"/missing-boot-font.ttf",argv[2],44,now++)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen)==PN_OK && pn_epub_app_close(&app,now++)==PN_OK);
    assert(pn_media_release(&font_media,&font_lease)==PN_OK && pn_media_detach(&font_media)==PN_OK);
    pn_pool_t baseline;assert(!pn_pool_init(&baseline,2*1024*1024,NULL,NULL,NULL));assert(pn_epub_app_open(&app,&baseline,argv[1],argv[3],NULL,44,0)==PN_OK);size_t attempts=baseline.attempts;assert(pn_epub_app_close(&app,1)==PN_OK && !baseline.used && !baseline.live);
    for(size_t i=1;i<=attempts;i++){pn_pool_t fault;assert(!pn_pool_init(&fault,2*1024*1024,NULL,NULL,NULL));fault.fail_at=i;pn_status_t status=pn_epub_app_open(&app,&fault,argv[1],argv[3],NULL,44,0);if(status==PN_OK)assert(pn_epub_app_close(&app,1)==PN_OK);assert(!app.impl && !fault.used && !fault.live);}

    printf("epub app: real font/image frames, failed display unchanged, per-book typesetting, close barrier/reopen/media loss; peak=%zu used=0 live=0\\n",pool.peak);return 0;
}
