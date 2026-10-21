/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源TXT应用控制器，连接文件、字体、显示确认与保存。
 * English: shared TXT application controller connecting files, fonts, presentation confirmation and saving.
 * 冻结：一个owner；不格式化，不执行硬件电源操作；关闭失败保留会话。
 * Frozen: one owner; no formatting or hardware power operations; retain session on close failure.
 */
#define _POSIX_C_SOURCE 200809L
#include "test_remove_indexes.h"
#include "pn_reader_app.h"
#include "pn_font_preferences.h"
#include "pn_font_set.h"
#include "pn_font_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
typedef struct {bool fail;unsigned calls;unsigned ink;unsigned tag;pn_refresh_t profile;} screen_t;
static pn_status_t present(void *ctx,const pn_frame_t *f,pn_refresh_t profile){screen_t *s=ctx;s->profile=profile;s->calls++;s->ink=0;s->tag=pn_frame_get(f,17,17);for(int y=0;y<f->height;y++)for(int x=0;x<f->width;x++)s->ink+=pn_frame_get(f,x,y)<15;return s->fail?PN_IO:PN_OK;}
/* 最终目标页使用真实候选字体，并留可观察像素；测试不伪造呈现成功。/ The destination draws the actual candidate font and observable pixels; tests never fake presentation success. */
static pn_status_t destination(void *ctx,pn_font_t *ui,pn_font_t *body,pn_frame_t *frame){
 (void)ui;if(ctx && *(bool *)ctx)return PN_IO;pn_frame_clear(frame,15);pn_frame_rect(frame,16,16,4,4,9);return pn_font_draw(body,frame,'A',32*64,120,PN_FONT_GRAY);
}
int main(void){
    char dir[]="/tmp/pn-app-XXXXXX";assert(mkdtemp(dir));char book[384],state[384];snprintf(book,sizeof book,"%s/book.txt",dir);snprintf(state,sizeof state,"%s/state",dir);
    FILE *f=fopen(book,"wb");assert(f);for(unsigned i=0;i<300;i++)assert(fputs("Read Pico keeps your place. Read a book every day.\n",f)>=0);assert(fclose(f)==0);
    pn_pool_t pool;assert(pn_pool_init(&pool,1024*1024,NULL,NULL,NULL)==0);pn_reader_app_t app={0};screen_t screen={0};pn_txt_progress_t progress;
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,0)==PN_OK);
    char fallback_path[384];snprintf(fallback_path,sizeof fallback_path,"%s/fallback.ttf",dir);pn_text_source_t font_source=pn_font_builtin_source();
    f=fopen(fallback_path,"wb");assert(f);uint8_t chunk[4096];for(uint64_t at=0;at<font_source.size;){size_t n;assert(font_source.read_at(font_source.ctx,at,chunk,sizeof chunk,&n)==PN_OK && n && fwrite(chunk,1,n,f)==n);at+=n;}assert(!fclose(f));
    assert(pn_reader_app_fallback_font(&app,"/missing-font.ttf")==PN_IO && pn_reader_app_fallback_font(&app,fallback_path)==PN_OK);
    assert(pn_reader_app_progress(&app,&progress)==PN_EMPTY);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,1,present,&screen)==PN_OK && screen.ink>1000 && screen.profile==PN_REFRESH_GC16);
    assert(pn_reader_app_fallback_font(&app,fallback_path)==PN_BUSY);
    assert(pn_reader_app_font_inherit(&app,1,present,&screen)==PN_OK && pn_reader_app_font_is_inherited(&app));
    assert(pn_reader_app_progress(&app,&progress)==PN_OK && progress.source_offset==0);
    screen.fail=true;assert(pn_reader_app_step(&app,PN_APP_NEXT,2,present,&screen)==PN_IO && screen.profile==PN_REFRESH_GL16);
    assert(pn_reader_app_progress(&app,&progress)==PN_OK && progress.source_offset==0);
    screen.fail=false;assert(pn_reader_app_step(&app,PN_APP_NEXT,3,present,&screen)==PN_OK && screen.profile==PN_REFRESH_GC16);
    assert(pn_reader_app_progress(&app,&progress)==PN_OK && progress.source_offset>0);uint64_t second=progress.source_offset;
    assert(pn_reader_app_step(&app,PN_APP_LARGER,4,present,&screen)==PN_OK);
    assert(pn_reader_app_progress(&app,&progress)==PN_OK && progress.source_offset==second);
    assert(pn_reader_app_close(&app,5)==PN_OK && !app.impl && pool.live==0 && pool.used==0);
    pn_media_t preferences_media;pn_media_init(&preferences_media);assert(pn_media_attach(&preferences_media,1)==PN_OK);pn_media_lease_t preferences_lease;assert(pn_media_acquire(&preferences_media,PN_MEDIA_READ,&preferences_lease)==PN_OK);
    pn_font_preferences_t preferences={0};preferences.primary.kind=PN_FONT_FILE;strcpy(preferences.primary.path,fallback_path);preferences.primary.size=font_source.size;assert(pn_identity_file(&preferences_media,&preferences_lease,fallback_path,PN_FONT_REFERENCE_MAX_BYTES,&preferences.primary.identity)==PN_OK);
    assert(pn_media_release(&preferences_media,&preferences_lease)==PN_OK && pn_media_acquire(&preferences_media,PN_MEDIA_WRITE,&preferences_lease)==PN_OK);pn_journal_files_t preferences_files;pn_journal_io_t preferences_io;assert(pn_font_preferences_files(&preferences_files,&preferences_media,&preferences_lease,state,NULL,&preferences_io)==PN_OK && pn_font_preferences_save(&preferences_io,&pool,NULL,&preferences)==PN_OK);
    assert(pn_reader_app_open(&app,&pool,book,"/missing-boot-font.ttf",state,44,6)==PN_OK);

    assert(pn_reader_app_step(&app,PN_APP_OPEN,7,present,&screen)==PN_OK);
    assert(pn_reader_app_progress(&app,&progress)==PN_OK && progress.source_offset==second);
    pn_font_preferences_t candidate={0};screen.fail=true;assert(pn_reader_app_font_preview(&app,&candidate,8,present,&screen)==PN_IO && pn_reader_app_progress(&app,&progress)==PN_OK && progress.source_offset==second);screen.fail=false;
    assert(pn_reader_app_font_preview(&app,&candidate,8,present,&screen)==PN_OK && pn_reader_app_progress(&app,&progress)==PN_OK && progress.source_offset==second);
    assert(pn_reader_app_font_cancel(&app,8,present,&screen)==PN_OK && pn_reader_app_progress(&app,&progress)==PN_OK && progress.source_offset==second);
    assert(pn_reader_app_font_apply(&app,&preferences,8,present,&screen)==PN_OK && pn_reader_app_progress(&app,&progress)==PN_OK && progress.source_offset==second);
    // 新字体只呈现最终设置页；绘制/显示失败不接受，成功只一次回调且锚点不变。/ A new font presents only the destination; paint/display failures cannot accept it, and success calls once without moving the anchor.
    unsigned calls=screen.calls;bool failed_paint=true;pn_font_preferences_t resident={0},observed;char view_directory[PN_FONT_REFERENCE_PATH_MAX];
    assert(pn_reader_app_font_apply_view(&app,&resident,destination,&failed_paint,8,present,&screen)==PN_IO && screen.calls==calls && !pn_reader_app_last_confirmed(&app));
    screen.fail=true;assert(pn_reader_app_font_apply_view(&app,&resident,destination,NULL,8,present,&screen)==PN_IO && screen.calls==calls+1);screen.fail=false;
    assert(pn_reader_app_fonts_get(&app,&observed,view_directory,sizeof view_directory)==PN_OK && observed.primary.kind==PN_FONT_FILE);
    calls=screen.calls;assert(pn_reader_app_font_apply_view(&app,&resident,destination,NULL,8,present,&screen)==PN_OK && screen.calls==calls+1 && screen.tag==9);
    assert(pn_reader_app_progress(&app,&progress)==PN_OK && progress.source_offset==second);
    calls=screen.calls;assert(pn_reader_app_font_cancel_view(&app,destination,NULL,8,present,&screen)==PN_OK && screen.calls==calls+1 && screen.tag==9);
    assert(pn_reader_app_font_apply(&app,&preferences,8,present,&screen)==PN_OK);
    // 全局保存不改逐书覆盖；继承失败不落盘，成功保留阅读锚点。/ Global saves preserve overrides; failed inheritance does not persist, successful inheritance preserves anchors.
    pn_font_preferences_t global_fonts={0},loaded_fonts;assert(pn_reader_app_font_default(&app,&global_fonts)==PN_OK);
    pn_journal_files_t inspect_files;pn_journal_io_t inspect_io;assert(pn_font_preferences_files(&inspect_files,&preferences_media,&preferences_lease,state,&progress.book,&inspect_io)==PN_OK);
    assert(pn_font_preferences_load(&inspect_io,&pool,&progress.book,&loaded_fonts)==PN_OK && loaded_fonts.primary.kind==PN_FONT_FILE);
    screen.fail=true;assert(pn_reader_app_font_inherit(&app,8,present,&screen)==PN_IO);screen.fail=false;
    assert(pn_font_preferences_load(&inspect_io,&pool,&progress.book,&loaded_fonts)==PN_OK && !loaded_fonts.inherit);
    assert(pn_reader_app_font_inherit(&app,8,present,&screen)==PN_OK && pn_reader_app_progress(&app,&progress)==PN_OK && progress.source_offset==second);
    assert(pn_font_preferences_load(&inspect_io,&pool,&progress.book,&loaded_fonts)==PN_OK && loaded_fonts.inherit);
    char font_directory[1024];assert(pn_reader_app_fonts_get(&app,&loaded_fonts,font_directory,sizeof font_directory)==PN_OK && !loaded_fonts.inherit && loaded_fonts.primary.kind==PN_FONT_RESIDENT);
    // 实际菜单取消覆盖：失败仍留单书选择；先显示等待，成功再呈现设置页并存继承标记。/ Real menu clears overrides: failure preserves the book choice; show busy feedback before the successful destination and inheritance commit.
    assert(pn_reader_app_font_apply(&app,&resident,8,present,&screen)==PN_OK && !pn_reader_app_font_is_inherited(&app));
    assert(pn_reader_app_font_default(&app,&preferences)==PN_OK);
    pn_font_ui_t chooser={0};assert(pn_font_ui_open(&chooser,&pool,&app,NULL,dir,present,&screen)==PN_OK);
    assert(pn_font_ui_hit(&chooser,100,180)==PN_FUI_RESTORE_DEFAULT && pn_font_ui_hit(&chooser,600,270)==PN_FUI_RESIDENT);
    pn_font_ui_return_to(&chooser,destination,NULL);screen.fail=true;
    assert(pn_font_ui_event(&chooser,PN_FUI_RESTORE_DEFAULT,8,present,&screen)==PN_IO && chooser.active && !pn_reader_app_font_is_inherited(&app));screen.fail=false;
    assert(pn_font_preferences_load(&inspect_io,&pool,&progress.book,&loaded_fonts)==PN_OK && !loaded_fonts.inherit);
    assert(pn_font_ui_event(&chooser,PN_FUI_RETRY,8,present,&screen)==PN_OK);
    calls=screen.calls;assert(pn_font_ui_event(&chooser,PN_FUI_RESTORE_DEFAULT,8,present,&screen)==PN_OK && !chooser.active && chooser.return_presented && screen.calls==calls+2 && screen.tag==9 && pn_reader_app_font_is_inherited(&app));
    assert(pn_font_preferences_load(&inspect_io,&pool,&progress.book,&loaded_fonts)==PN_OK && loaded_fonts.inherit);pn_font_ui_close(&chooser);
    assert(pn_reader_app_font_default(&app,&preferences)==PN_OK && pn_reader_app_font_apply(&app,&preferences,8,present,&screen)==PN_OK);
    // 在字体校验后注入全局保存故障，关闭只能重试原请求。/ Inject global-save failure after font validation; close must retry the original request.
    pn_media_t fault_media;pn_media_init(&fault_media);pn_media_lease_t fault_lease;assert(pn_media_attach(&fault_media,1)==PN_OK && pn_media_acquire(&fault_media,PN_MEDIA_READ,&fault_lease)==PN_OK);
    pn_font_set_t fault_set={0};size_t allocation_start=pool.attempts;assert(pn_font_set_open(&fault_set,&pool,&fault_media,&fault_lease,&global_fonts,44)==PN_OK);size_t validation_allocations=pool.attempts-allocation_start;pn_font_set_close(&fault_set);
    allocation_start=pool.attempts;assert(pn_font_preferences_load(&preferences_io,&pool,NULL,&loaded_fonts)==PN_OK);size_t read_allocations=pool.attempts-allocation_start;
    pool.fail_at=pool.attempts+validation_allocations+read_allocations+1;assert(pn_reader_app_font_default(&app,&global_fonts)==PN_NO_MEMORY);pool.fail_at=0;
    assert(pn_reader_app_font_default(&app,&preferences)==PN_BUSY && pn_reader_app_font_inherit(&app,8,present,&screen)==PN_BUSY);
    pool.fail_at=pool.attempts+1;assert(pn_reader_app_close(&app,8)==PN_NO_MEMORY && app.impl);pool.fail_at=0;assert(pn_reader_app_close(&app,8)==PN_OK && !pool.used && !pool.live);
    assert(pn_font_preferences_load(&preferences_io,&pool,NULL,&loaded_fonts)==PN_OK && loaded_fonts.primary.kind==PN_FONT_RESIDENT);
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,8)==PN_OK && pn_reader_app_step(&app,PN_APP_OPEN,8,present,&screen)==PN_OK);
    assert(pn_reader_app_fonts_get(&app,&loaded_fonts,font_directory,sizeof font_directory)==PN_OK && loaded_fonts.primary.kind==PN_FONT_FILE);
    assert(pn_reader_app_font_default(&app,&preferences)==PN_OK && pn_media_release(&fault_media,&fault_lease)==PN_OK && pn_media_detach(&fault_media)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_BEGINNING,8,present,&screen)==PN_OK);
    char hash[65];for(size_t i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",progress.book.sha256[i]);
    char paths[2][768];snprintf(paths[0],sizeof paths[0],"%s/%s.a",state,hash);snprintf(paths[1],sizeof paths[1],"%s/%s.b",state,hash);
    uint8_t backup[2][PN_JOURNAL_RECORD_MAX];size_t sizes[2]={0};bool exists[2]={0};
    for(unsigned i=0;i<2;i++){f=fopen(paths[i],"rb");if(f){exists[i]=true;sizes[i]=fread(backup[i],1,sizeof backup[i],f);assert(fclose(f)==0);}f=fopen(paths[i],"wb");assert(f);assert(fputs("broken",f)>=0);assert(fclose(f)==0);}
    assert(pn_reader_app_close(&app,9)==PN_CORRUPT && app.impl && pool.live>0);
    for(unsigned i=0;i<2;i++){if(exists[i]){f=fopen(paths[i],"wb");assert(f);assert(fwrite(backup[i],1,sizes[i],f)==sizes[i]);assert(fclose(f)==0);}else assert(unlink(paths[i])==0);}
    assert(pn_reader_app_close(&app,10)==PN_OK && !pool.live);
    pn_journal_files_t book_preferences_files;pn_journal_io_t book_preferences_io;pn_font_preferences_t inherit_fonts={.inherit=true};assert(pn_font_preferences_files(&book_preferences_files,&preferences_media,&preferences_lease,state,&progress.book,&book_preferences_io)==PN_OK && pn_font_preferences_save(&book_preferences_io,&pool,&progress.book,&inherit_fonts)==PN_OK);
    preferences.primary.identity.sha256[0]^=1;assert(pn_font_preferences_save(&preferences_io,&pool,NULL,&preferences)==PN_OK);assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,11)==PN_OK && pn_reader_app_font_unavailable(&app) && pn_reader_app_step(&app,PN_APP_OPEN,11,present,&screen)==PN_OK && pn_reader_app_close(&app,11)==PN_OK && !pool.live);preferences.primary.identity.sha256[0]^=1;assert(pn_font_preferences_save(&preferences_io,&pool,NULL,&preferences)==PN_OK);assert(pn_media_release(&preferences_media,&preferences_lease)==PN_OK && pn_media_detach(&preferences_media)==PN_OK);
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,11)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,12,present,&screen)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_NEXT,13,present,&screen)==PN_OK);
    assert(pn_reader_app_close(&app,14)==PN_OK);
    f=fopen(book,"r+b");assert(f);assert(fputc('X',f)=='X');assert(fclose(f)==0);
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,15)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,16,present,&screen)==PN_OK);
    assert(pn_reader_app_progress(&app,&progress)==PN_OK && progress.source_offset==0);
    assert(pn_reader_app_close(&app,17)==PN_OK && !pool.live);
    pn_media_t shared;pn_media_init(&shared);assert(pn_media_attach(&shared,7)==PN_OK);
    assert(pn_reader_app_open_on_media(&app,&pool,&shared,book,NULL,state,44,18)==PN_OK);
    pn_media_lease_t exclusive;assert(pn_media_acquire(&shared,PN_MEDIA_WRITE,&exclusive)==PN_BUSY);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,19,present,&screen)==PN_OK);
    uint64_t refresh_now=20;
    pn_style_t legacy;assert(pn_reader_app_style_get(&app,&legacy)==PN_OK);legacy.gl_before_clear=0;
    assert(pn_reader_app_style_apply(&app,&legacy,refresh_now++,present,&screen)==PN_OK);
    for(unsigned p=0;p<3;p++){
        const unsigned periods[]={6,14,24};unsigned period=periods[p];pn_reader_app_refresh_period(&app,period);pn_reader_app_refresh_reset(&app);
        for(unsigned i=1;i<=period;i++){
            assert(pn_reader_app_step(&app,PN_APP_OPEN,refresh_now++,present,&screen)==PN_OK && screen.profile==PN_REFRESH_GL16);
            assert(pn_reader_app_step(&app,i%2?PN_APP_NEXT:PN_APP_PREVIOUS,refresh_now++,present,&screen)==PN_OK);
            assert(screen.profile==(i==period?PN_REFRESH_GC16:PN_REFRESH_GL16));
        }
    }
    // 下调周期保留已计页数；显示owner深清回执可重置，旧排版字段仍无效。/ Lowering the period preserves counted turns; a confirmed physical clean can reset it, with the legacy style field ignored.
    pn_reader_app_refresh_period(&app,14);
    for(unsigned i=1;i<=5;i++)assert(pn_reader_app_step(&app,i%2?PN_APP_NEXT:PN_APP_PREVIOUS,refresh_now++,present,&screen)==PN_OK && screen.profile==PN_REFRESH_GL16);
    pn_reader_app_refresh_period(&app,6);
    assert(pn_reader_app_step(&app,PN_APP_PREVIOUS,refresh_now++,present,&screen)==PN_OK && screen.profile==PN_REFRESH_GC16);
    pn_reader_app_refresh_reset(&app);
    assert(pn_reader_app_step(&app,PN_APP_NEXT,refresh_now++,present,&screen)==PN_OK && screen.profile==PN_REFRESH_GL16);
    // 保存成功提示不能产生失败重试按钮。/ A successful save notice must not produce an error Retry button.
    pn_font_ui_t font_menu={0};assert(pn_font_ui_open(&font_menu,&pool,&app,NULL,dir,present,&screen)==PN_OK);
    assert(pn_font_ui_event(&font_menu,PN_FUI_RESIDENT,refresh_now++,present,&screen)==PN_OK && font_menu.mode==1);
    assert(pn_font_ui_event(&font_menu,PN_FUI_DEFAULT,refresh_now++,present,&screen)==PN_OK);
    assert(pn_font_ui_hit(&font_menu,600,60)!=PN_FUI_RETRY);pn_font_ui_close(&font_menu);
    assert(pn_reader_app_media_lost(&app)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_NEXT,refresh_now++,present,&screen)==PN_STALE_MEDIA);
    assert(pn_reader_app_close(&app,refresh_now++)==PN_OK && !pool.live && pn_media_active(&shared)==0);
    pn_test_remove_indexes(state);DIR *d=opendir(state);assert(d);struct dirent *entry;while((entry=readdir(d))){if(entry->d_name[0]=='.')continue;char path[768];snprintf(path,sizeof path,"%s/%s",state,entry->d_name);assert(unlink(path)==0);}assert(closedir(d)==0);assert(rmdir(state)==0 && unlink(book)==0 && unlink(fallback_path)==0 && rmdir(dir)==0);
    puts("reader app: native page, failed presentation, reflow anchor, durable reopen and cleanup passed");return 0;
}
