/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实设备路由管理键与面板底图复用，硬件用现有启动桩。/ English: real device management routing and shelf-overlay reuse with existing boot stubs. */
#define _POSIX_C_SOURCE 200809L
#define main boot_fixture_main
#define pn_device_transfer_state boot_transfer_state
#define pn_device_transfer_close boot_transfer_close
#define pn_device_transfer_take_network boot_transfer_take_network
#define pn_device_time_poll boot_time_poll
#define pn_device_time_state boot_time_state
#define pn_device_time_close boot_time_close
#define pn_device_time_commit boot_time_commit
#define pn_device_ota_state boot_ota_state
#include "device_ota.h"
#include "test_device_boot.c"
#undef main
#undef pn_device_transfer_state
#undef pn_device_transfer_close
#undef pn_device_transfer_take_network
#undef pn_device_time_poll
#undef pn_device_time_state
#undef pn_device_time_close
#undef pn_device_time_commit
#undef pn_device_ota_state
static pn_device_ota_state_t ota_result;
static pn_status_t ota_state_result=PN_OK;
pn_status_t pn_device_ota_state(const pn_device_ota_t *o,pn_device_ota_state_t *s){assert(o->impl);*s=ota_result;return ota_state_result;}
static pn_device_time_state_t time_result;
static pn_status_t commit_result=PN_IO;
static unsigned time_commits;
pn_status_t pn_device_time_poll(pn_device_time_t *t,uint64_t n){assert(t->impl);(void)n;return PN_OK;}
pn_status_t pn_device_time_state(const pn_device_time_t *t,pn_device_time_state_t *s){assert(t->impl);*s=time_result;return PN_OK;}
pn_status_t pn_device_time_close(pn_device_time_t *t){assert(time_result.released);t->impl=NULL;return PN_OK;}
pn_status_t pn_device_time_commit(const pn_device_time_state_t *s,uint32_t *u){assert(s->released && s->phase==PN_TIME_SUCCEEDED);time_commits++;if(commit_result==PN_OK)*u=s->utc;return commit_result;}
pn_status_t pn_device_transfer_state(pn_device_transfer_t *s,pn_device_transfer_state_t *out){assert(s->impl);*out=(pn_device_transfer_state_t){.released=true,.phase=PN_DTRANSFER_STOPPED};return PN_OK;}
pn_status_t pn_device_transfer_close(pn_device_transfer_t *s){assert(s->impl);s->impl=NULL;return PN_OK;}
pn_status_t pn_device_transfer_take_network(pn_device_transfer_t *s,pn_network_credentials_t *c,bool *f){(void)s;(void)c;(void)f;return PN_EMPTY;}
#include "../../main/app_main.c"
#include "device_reader_step.h"
static bool feedback_cancelled(void *ctx){(void)ctx;return true;}
typedef struct {unsigned calls,limit;} font_cancel_t;
static bool font_cancel_after(void *ctx){font_cancel_t *c=ctx;return ++c->calls>=c->limit;}
static void queued_root_pointer(unsigned count,int x,int y){
    pn_device_input_event_t input={.kind=PN_DASYNC_TOUCH,.count=count,.x=x,.y=y,.valid=true};pn_async_event_t event={.bytes=sizeof input,.view=atomic_load(&async_port.input.view)};memcpy(event.payload,&input,sizeof input);
    assert((count==1 && pn_w_navigation_hit(x,y)>=0?pn_async_send_root(&async_port.input,&event):pn_async_send(&async_port.input,&event))==PN_OK);
}
static void queued_root_release(void){queued_root_pointer(0,0,0);queued_root_pointer(0,0,0);}
static void root_merge_checks(void){
    unsigned shown=presents;
    // 实际Settings根页的命令作为独立预期；合并过程不绘制、不执行内容或按键命令。
    // Actual Settings-root commands are independent expectations; merging never paints or executes content/key commands.
    for(unsigned mode=0;mode<5;mode++){
        pn_async_init(&async_port.input);pn_async_set_view(&async_port.input,8,true);pn_tap_press_t press={0};int selected=-1;
        assert(pn_tap_press_feed(&press,1,PN_SETUI_HOME,true,&selected) && selected==PN_SETUI_HOME);
        if(mode!=1)queued_root_release();
        if(mode==2){queued_root_pointer(2,420,1150);queued_root_pointer(1,420,1150);queued_root_release();}
        if(mode==3){queued_root_pointer(1,100,700);queued_root_pointer(1,420,1150);queued_root_release();}
        queued_root_pointer(1,250,1150);
        if(mode!=1){queued_root_release();queued_root_pointer(1,420,1150);queued_root_release();}
        if(mode==4){pn_async_set_root(&async_port.input,false);pn_async_set_root(&async_port.input,true);}
        pn_async_scope_t scope;pn_async_begin(&async_port.input,&scope,false);
        touch_held=true;int merged=root_navigation_merge(&press,&scope,selected);assert(merged==((mode==1 || mode==4)?PN_SETUI_HOME:PN_SETUI_TRANSFER));assert(touch_held==(mode==1));
        assert(presents==shown && pn_async_take(&async_port.input,&(pn_async_event_t){0})==PN_EMPTY);pn_async_end(&scope);
    }
    pn_async_init(&async_port.input);pn_async_set_view(&async_port.input,8,true);queued_root_release();queued_root_pointer(1,420,1150);
    pn_device_input_event_t power={.kind=PN_DASYNC_POWER};pn_async_event_t priority={.bytes=sizeof power,.navigation=true,.urgent=true};memcpy(priority.payload,&power,sizeof power);assert(pn_async_send(&async_port.input,&priority)==PN_OK);
    pn_async_scope_t scope;pn_async_begin(&async_port.input,&scope,false);pn_tap_press_t press={.down=true,.claimed=true};
    assert(root_navigation_merge(&press,&scope,PN_SETUI_HOME)==-1 && presents==shown && async_port.input.count==3);
    assert(pn_async_take(&async_port.input,&priority)==PN_OK && priority.navigation && priority.urgent);pn_async_end(&scope);pn_async_init(&async_port.input);
}
/// 退出阅读只提交一次完整整屏GC16，失败归还波形，未知参考沿原GC16恢复。/ Reading exit submits full-screen original GC16 once, restores the waveform on failure, and retains GC16 recovery for unknown references.
static void reading_exit_refresh_checks(void){
    pn_device_refresh_t saved=maintenance;bool known=panel_known,clean=atomic_exchange(&full_clean_completed,false);const void *wave=hardware.hl.waveform;
    uint8_t *pixels=pn_alloc(&pool,415872);pn_frame_t frame;assert(pixels && pn_frame_bind(&frame,pixels,415872,684,1216));
    pn_frame_clear(&frame,15);pn_frame_rect(&frame,24,250,400,40,0);
    hardware.hl.waveform=&E0470_FOLLOW_WAVEFORM;panel_known=true;maintenance.management=23;memset(maintenance.bands,6,sizeof maintenance.bands);
    unsigned calls=screen_full_calls,shown=presents,clears=clear_calls,areas=area_full_calls+area_diff_calls,gcs=gc_updates;
    assert(present(NULL,&frame,PN_REFRESH_EXIT_GC16)==PN_OK);
    assert(screen_full_calls==calls+1 && presents==shown+1 && clear_calls==clears && area_full_calls+area_diff_calls==areas);
    assert(last_screen_wave==&E0470_WAVEFORM && last_screen_mode==MODE_GC16 && hardware.hl.waveform==&E0470_FOLLOW_WAVEFORM && panel_known);
    assert(gc_updates==gcs+1 && maintenance.management==0 && atomic_load(&full_clean_completed));
    // 即使目标像素未变仍执行这次退出更新；普通GL16不消耗额外提交。/ Perform the explicit exit update even for unchanged pixels; ordinary GL16 still skips a redundant submission.
    calls=screen_full_calls;assert(present(NULL,&frame,PN_REFRESH_EXIT_GC16)==PN_OK && screen_full_calls==calls+1);
    shown=presents;assert(present(NULL,&frame,PN_REFRESH_GL16)==PN_OK && presents==shown);
    panel_fault=true;calls=screen_full_calls;assert(present(NULL,&frame,PN_REFRESH_EXIT_GC16)==PN_IO && screen_full_calls==calls+1 && !panel_known && hardware.hl.waveform==&E0470_FOLLOW_WAVEFORM);panel_fault=false;
    clears=clear_calls;shown=presents;assert(present(NULL,&frame,PN_REFRESH_EXIT_GC16)==PN_OK && clear_calls==clears+1 && presents==shown+1 && panel_known && hardware.hl.waveform==&E0470_FOLLOW_WAVEFORM);
    pn_free(pixels);hardware.hl.waveform=wave;maintenance=saved;panel_known=known;atomic_store(&full_clean_completed,clean);
}
int main(void){
    assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    assert(read_pico_init(&hardware)==ESP_OK);pn_media_init(&sd_media);pn_media_init(&data_media);pn_media_init(&wallpaper_media);
    int vcom;assert(read_pico_pmu_vcom_get(&vcom)==ESP_OK);epd_set_vcom((uint16_t)vcom);
    assert(pn_media_attach(&sd_media,1)==PN_OK);
    reading_exit_refresh_checks();
    assert(pn_settings_ui_open(&settings_ui,&pool,NULL,NULL,present,NULL)==PN_OK);
    // 网络结束时若绘制被取消，空闲重试遵守节流、取消与锁屏门控。 / Retry cancelled network-terminal draws subject to idle throttling, cancellation and lock gates.
    uint64_t retry_at=0;settings_ui.presented=false;unsigned retry_frames=presents;
    retry_settings_ui(999,&retry_at);assert(presents==retry_frames && !settings_ui.presented);
    locked=true;retry_settings_ui(1000,&retry_at);assert(presents==retry_frames && !retry_at);locked=false;
    pn_cancel_scope_t cancelled_scope;pn_cancel_enter(&cancelled_scope,feedback_cancelled,NULL);
    retry_settings_ui(1000,&retry_at);assert(presents==retry_frames && !retry_at);pn_cancel_leave(&cancelled_scope);
    retry_settings_ui(1000,&retry_at);assert(settings_ui.presented && retry_at==1000);retry_frames=presents;
    retry_settings_ui(2000,&retry_at);assert(presents==retry_frames && retry_at==1000);
    // 写Flash与取消收口期间，主动更新和空闲补画都不能推屏；资源归还后恢复。/ Neither explicit updates nor idle retries may paint during flash writes or cancellation cleanup; resume after release.
    assert(pn_settings_ui_event(&settings_ui,PN_SETUI_ABOUT,present,NULL)==PN_OK && pn_settings_ui_event(&settings_ui,PN_SETUI_UPDATE,present,NULL)==PN_OK);
    pn_settings_update_t installing={.running=true};pn_settings_ui_set_update(&settings_ui,&installing);assert(pn_settings_ui_present(&settings_ui,present,NULL)==PN_OK);retry_frames=presents;
    ota.impl=&ota_result;ota_result=(pn_device_ota_state_t){.phase=PN_DEVICE_OTA_DOWNLOADING,.total=100,.bytes=30};settings_ui.presented=false;
    assert(ota_display_frozen());update_ota_ui(true);retry_settings_ui(3000,&retry_at);assert(presents==retry_frames && !settings_ui.presented);
    ota_result.phase=PN_DEVICE_OTA_STOPPING;update_ota_ui(true);retry_settings_ui(4000,&retry_at);assert(presents==retry_frames);
    ota_state_result=PN_IO;assert(ota_display_frozen());ota_state_result=PN_OK;
    ota_result.radio_released=true;assert(!ota_display_frozen());update_ota_ui(false);retry_settings_ui(5000,&retry_at);assert(settings_ui.presented && presents==retry_frames+1);ota.impl=NULL;
    assert(pn_settings_ui_event(&settings_ui,PN_SETUI_BACK,present,NULL)==PN_OK && pn_settings_ui_event(&settings_ui,PN_SETUI_BACK,present,NULL)==PN_OK);
    assert(root_navigation_hit(80,1150)==PN_SETUI_HOME && root_navigation_hit(100,700)==-1);
    root_merge_checks();
    assert(pn_settings_ui_event(&settings_ui,PN_SETUI_REFRESH,present,NULL)==PN_OK && pn_settings_ui_screen(&settings_ui)==2);
    assert(root_navigation_hit(80,1150)==-1);
    handle_key(PN_KEY_1,PN_KEY_LONG);assert(pn_settings_ui_screen(&settings_ui)==2);
    handle_key(PN_KEY_1,PN_KEY_SHORT);assert(settings_ui.active && pn_settings_ui_screen(&settings_ui)==0);
    focus_rescan();bool found=false;for(size_t i=0;i<focus_nav.count;i++)found|=focus_nav.items[i].code==PN_SETUI_FONTS;assert(found);
    settings_ui.flags=PN_INPUT_LEFT_HAND;
    handle_key(PN_KEY_2,PN_KEY_SHORT);assert(!settings_ui.impl && home_mode && input_flags==PN_INPUT_LEFT_HAND);
    // 操作面板只改下半屏；右键明确确认弹窗，左键取消保留面板。/ The sheet preserves the upper frame; dialog Back cancels and keeps the sheet.
    shelf_page= shelf_page?shelf_page:pn_alloc(&pool,sizeof *shelf_page);assert(shelf_page);memset(shelf_page,0,sizeof *shelf_page);shelf_page->count=1;
    strcpy(shelf_page->items[0].name,"Original.txt");strcpy(shelf_page->items[0].path,"/original.txt");shelf_page->items[0].format=PN_BOOK_TXT;
    home_mode=false;shelf_mode=true;status_page=false;
    assert(draw_shelf(PN_REFRESH_GL16)==PN_OK);
    // 固定底栏按下即开设置；跨页持有/滑动/多点/释放均不再次执行。/ Fixed navigation opens Settings on press; holding across pages, movement, multitouch and release never execute again.
    pn_tap_press_t nav_press={0};unsigned nav_shown=presents;bool nav_data_ready=data_ready,nav_data_available=data_media.available;
    // 真实设备入口合并首页/传书/设置，只呈现最终设置一次；批次里的最终释放同时恢复空闲资格。
    // The real device entry merges Home/Transfer/Settings, presents only Settings once and restores idle eligibility from the drained release.
    pn_async_init(&async_port.input);pn_async_set_view(&async_port.input,17,true);queued_root_release();queued_root_pointer(1,420,1150);queued_root_release();queued_root_pointer(1,600,1150);queued_root_release();
    pn_async_scope_t merged_scope;pn_async_begin(&async_port.input,&merged_scope,false);async_port.input_active=true;pn_tap_press_t merged_press={0};
    assert(root_navigation_touch(&merged_press,1,80,1150,true,&merged_scope));
    assert(settings_ui.impl && pn_settings_ui_screen(&settings_ui)==0 && presents==nav_shown+1 && !touch_held && !merged_press.down && !merged_press.blocked && !async_port.input.count);
    assert(pn_cancel_checkpoint()==PN_OK);pn_async_end(&merged_scope);async_port.input_active=false;pn_settings_ui_close(&settings_ui);shelf_mode=true;status_page=false;
    assert(draw_shelf(PN_REFRESH_GL16)==PN_OK);nav_shown=presents;
    assert(root_navigation_hit(600,1150)==PN_SHELF_MENU && root_navigation_touch(&nav_press,1,600,1150,true,NULL));
    assert(settings_ui.impl && pn_settings_ui_screen(&settings_ui)==0 && presents>nav_shown);nav_shown=presents;
    assert(root_navigation_touch(&nav_press,1,80,1150,true,NULL) && settings_ui.impl && presents==nav_shown);
    assert(root_navigation_touch(&nav_press,2,80,1150,true,NULL) && presents==nav_shown);
    pn_device_input_event_t stale_release={.kind=PN_DASYNC_TOUCH,.valid=true,.count=0};
    root_navigation_cancel(&nav_press,&stale_release);assert(nav_press.claimed);
    root_navigation_cancel(&nav_press,&stale_release);
    assert(!nav_press.down && !nav_press.claimed && settings_ui.impl && presents==nav_shown);
    assert(!root_navigation_touch(&nav_press,1,100,700,true,NULL));
    assert(root_navigation_touch(&nav_press,1,80,1150,true,NULL) && presents==nav_shown);
    assert(root_navigation_touch(&nav_press,0,0,0,true,NULL) && root_navigation_touch(&nav_press,0,0,0,true,NULL));
    locked=true;assert(root_navigation_hit(80,1150)==-1);locked=false;
    time_sync.impl=(void *)1;assert(root_navigation_hit(80,1150)==-1);time_sync.impl=NULL;
    pn_settings_ui_close(&settings_ui);shelf_mode=true;status_page=false;
    // 恢复后续传输故障夹具原来的未挂载背景，不弱化产品租约规则。/ Restore the later transfer fault fixture's original unmounted background without weakening product leases.
    if(!nav_data_available && data_media.available)assert(pn_media_detach(&data_media)==PN_OK);
    data_ready=nav_data_ready;
    // 同屏重绘复用界面引擎，字形缓存命中增加，无新增TF租约；先失效整页缓存以走真实重绘路径。
    // Repaint reuses the UI engine, gains glyph hits and adds no TF lease; invalidate whole-page reuse so the real repaint runs.
    pn_menu_cache_invalidate(&menu_cache);
    void *ui_engine=management_font.impl;size_t leases=pn_media_active(&sd_media);uint64_t hits=pool.cache_stats[PN_CACHE_GLYPH].hits;
    assert(ui_engine && management_scope.active && draw_shelf(PN_REFRESH_GL16)==PN_OK && management_font.impl==ui_engine && pn_media_active(&sd_media)==leases && pool.cache_stats[PN_CACHE_GLYPH].hits>hits);
    // 完整TF源的缓存消费者先关闭，释放源后可继续使用应急字体。/ Close cached TF-source consumers first; emergency fonts remain usable after source release.
    char tf_path[]="/tmp/pn-management-font-XXXXXX";int tf_fd=mkstemp(tf_path);assert(tf_fd>=0);FILE *tf_file=fdopen(tf_fd,"wb");assert(tf_file);
    pn_text_source_t tf_source=pn_font_builtin_source();uint8_t tf_bytes[4096];
    for(uint64_t at=0;at<tf_source.size;){size_t n;assert(tf_source.read_at(tf_source.ctx,at,tf_bytes,sizeof tf_bytes,&n)==PN_OK && n && fwrite(tf_bytes,1,n,tf_file)==n);at+=n;}assert(!fclose(tf_file));
    management_fonts_close();assert(pn_font_system_open(&pool,&sd_media,tf_path)==PN_OK && pn_font_system_available());
    assert(draw_shelf(PN_REFRESH_GL16)==PN_OK && management_font.impl && pn_media_active(&sd_media)>0);
    // 在有效性检查、入口或内部验证时取消都保留UI引擎/源/租约；失败后重试不重新读字体。
    // Cancellation during availability, entry or internal validation preserves the UI/source/lease; retry does not reread the font.
    pn_text_source_t retained_ui_source=pn_font_builtin_source();void *retained_ui_engine=management_font.impl;
    pn_text_file_t *retained_ui_file=retained_ui_source.ctx;uint64_t retained_ui_reads=retained_ui_file->io_reads;size_t retained_ui_used=pool.used,retained_ui_leases=pn_media_active(&sd_media);
    for(unsigned limit=1;limit<=4;limit++){
        font_cancel_t interrupted={.limit=limit};pn_cancel_scope_t scope;pn_cancel_enter(&scope,font_cancel_after,&interrupted);system_font_ready();pn_cancel_leave(&scope);
        assert(interrupted.calls>=limit && management_font.impl==retained_ui_engine && pn_font_builtin_source().ctx==retained_ui_source.ctx && pool.used==retained_ui_used && pn_media_active(&sd_media)==retained_ui_leases && retained_ui_file->io_reads==retained_ui_reads);
        assert(pn_font_system_open(&pool,&sd_media,tf_path)==PN_OK);assert(management_font.impl==retained_ui_engine && pn_font_builtin_source().ctx==retained_ui_source.ctx && retained_ui_file->io_reads==retained_ui_reads);
    }
    // 设置正持有TF界面引擎时，源交接先关闭设置并归还，再释放全部消费者。
    // Source handoff closes Settings and returns its TF UI engine before releasing all consumers.
    void *shared_ui=management_font.impl;bool prior_shelf=shelf_mode,prior_status=status_page,prior_data=data_ready;
    begin_settings();assert(settings_ui.impl && !management_font.impl);
    pn_settings_ui_close(&settings_ui);assert(management_font.impl==shared_ui);shelf_mode=prior_shelf;status_page=prior_status;
    pn_cancel_scope_t settings_cancel;pn_cancel_enter(&settings_cancel,feedback_cancelled,NULL);begin_settings();pn_cancel_leave(&settings_cancel);
    assert(!settings_ui.impl && management_font.impl==shared_ui && shelf_mode==prior_shelf && status_page==prior_status);
    // 真实入口的挂载替身会附加内部介质；恢复本用例原来的无内部存储背景。
    // The real entry's mount stub attaches internal media; restore this test's original no-storage background.
    if(!prior_data){assert(!pn_media_active(&data_media));(void)pn_media_detach(&data_media);data_ready=false;}
    assert(pn_settings_ui_open_cached(&settings_ui,&pool,NULL,NULL,&management_font,present,NULL)==PN_OK);
    system_font_release();assert(!settings_ui.impl && !management_font.impl && !management_scope.active && !pn_font_system_available() && !pn_media_active(&sd_media));
    assert(draw_shelf(PN_REFRESH_GL16)==PN_OK);assert(!remove(tf_path));
    // 逻辑前16行是每个板级行的前8字节；面板底图来自后缓冲逆变换。/ The logical top 16 rows are the first 8 bytes of each board row; the sheet base is restored from the back buffer.
    static uint8_t top[684*8];for(int py=0;py<684;py++)memcpy(top+py*8,hardware.hl.back_fb+(size_t)py*608,8);
    open_actions(0);assert(actions_mode);for(int py=0;py<684;py++)assert(!memcmp(top+py*8,hardware.hl.back_fb+(size_t)py*608,8));
    actions.confirming=true;handle_key(PN_KEY_1,PN_KEY_SHORT);assert(actions_mode && !actions.confirming);
    actions.confirming=true;actions.notice=NULL;handle_key(PN_KEY_3,PN_KEY_SHORT);
    assert(actions_mode && actions.confirming && actions.notice); // 不存在的测试文件删除失败，证明右键送达确认而非焦点切换。/ Failed deletion of the nonexistent fixture proves Confirm delivery, rather than focus movement.
    actions_mode=false;
    // 管理轻更新不沿用阅读的6页清屏间隔；显式强刷仍生效。/ Management updates ignore the reading six-page cleaning interval; explicit cleaning still works.
    input_flags=(input_flags&~PN_INPUT_REFRESH_MASK)|0x10;
    pn_frame_t probe_frame;uint8_t *probe_pixels=pn_alloc(&pool,PN_HW_FRAME_BYTES);assert(probe_pixels);
    assert(pn_frame_bind(&probe_frame,probe_pixels,PN_HW_FRAME_BYTES,PN_HW_WIDTH,PN_HW_HEIGHT));
    pn_frame_clear(&probe_frame,15);assert(present(NULL,&probe_frame,PN_REFRESH_GC16)==PN_OK);
    unsigned clean_before=clear_calls;
    for(unsigned i=0;i<12;i++){pn_frame_rect(&probe_frame,20+(int)i*4,200,4,4,0);assert(present(NULL,&probe_frame,PN_REFRESH_GL16)==PN_OK);}
    assert(clear_calls==clean_before);
    unsigned gc_before=gc_updates;
    assert(present(NULL,&probe_frame,PN_REFRESH_GC16)==PN_OK && clear_calls==clean_before && gc_updates==gc_before+1);
    // 逻辑周期由阅读控制器决定，显示帧不再次计页；显式刷新与未知参考保留深清。/ Logical periods belong to the reader; display frames never count again, with deep cleaning kept for explicit/unknown references.
    atomic_store(&reading_presentation,true);gc_before=gc_updates;
    unsigned full_before=area_full_calls,diff_before=area_diff_calls;
    for(unsigned i=0;i<6;i++){pn_frame_rect(&probe_frame,100+(int)i*4,300,4,4,0);assert(present(NULL,&probe_frame,PN_REFRESH_GL16)==PN_OK);}
    assert(clear_calls==clean_before && gc_updates==gc_before);atomic_store(&reading_presentation,false);
    // 页脚未变：每页一次正文带全像素更新，不再单独画页脚。/ Footer unchanged: one full body-band update per page, no separate footer draw.
    assert(area_full_calls==full_before+6 && area_diff_calls==diff_before);
    assert(last_full_area.y==0 && last_full_area.height==1152);
    // 页脚变化：合并为一次整屏更新。/ Footer changed: merged into one full-screen update.
    atomic_store(&reading_presentation,true);full_before=area_full_calls;pn_frame_rect(&probe_frame,200,1180,8,8,0);
    assert(present(NULL,&probe_frame,PN_REFRESH_GL16)==PN_OK && area_full_calls==full_before+1 && area_diff_calls==diff_before && last_full_area.height==1216);
    atomic_store(&reading_presentation,true);footer_fault=true;pn_frame_rect(&probe_frame,300,1190,4,4,0);
    assert(present(NULL,&probe_frame,PN_REFRESH_GL16)==PN_IO && !panel_known);footer_fault=false;atomic_store(&reading_presentation,false);
    // 管理第24次实际更新才GC；不变帧不占预算，周期不插入额外clear。/ GC on management update 24; unchanged frames spend no budget and periodic refresh adds no clear.
    assert(present(NULL,&probe_frame,PN_REFRESH_GC16)==PN_OK);gc_before=gc_updates;
    clean_before=clear_calls;
    for(unsigned i=0;i<24;i++){
        for(unsigned repeat=0;repeat<3;repeat++)assert(present(NULL,&probe_frame,PN_REFRESH_GL16)==PN_OK);
        pn_frame_rect(&probe_frame,160+(int)i*4,400,4,4,0);assert(present(NULL,&probe_frame,PN_REFRESH_GL16)==PN_OK);
        assert(gc_updates==gc_before+(i==23));
    }
    assert(clear_calls==clean_before);
    atomic_store(&deep_clean_pending,true);assert(present(NULL,&probe_frame,PN_REFRESH_GL16)==PN_OK && clear_calls==clean_before+1);
    clean_before++;
    atomic_store(&deep_clean_pending,true);assert(present(NULL,&probe_frame,PN_REFRESH_GC16)==PN_OK && clear_calls==clean_before+1);
    panel_known=false;assert(present(NULL,&probe_frame,PN_REFRESH_GL16)==PN_OK && clear_calls==clean_before+2);
    atomic_store(&full_clean_completed,false);atomic_store(&deep_clean_pending,true);panel_fault=true;
    assert(present(NULL,&probe_frame,PN_REFRESH_GL16)==PN_IO && !panel_known && atomic_load(&deep_clean_pending) && !atomic_load(&full_clean_completed));
    panel_fault=false;assert(present(NULL,&probe_frame,PN_REFRESH_GL16)==PN_OK && panel_known && !atomic_load(&deep_clean_pending) && atomic_load(&full_clean_completed));
    // 内容转换提前GC16，保持同一帧、一次扫描且不插入白场；小文字变化不触发。
    // Content transitions promote to GC16 for the same target in one scan without a white prepass; small text stays GL16.
    pn_frame_clear(&probe_frame,7);assert(present(NULL,&probe_frame,PN_REFRESH_GC16)==PN_OK);
    pn_frame_clear(&probe_frame,15);gc_before=gc_updates;clean_before=clear_calls;
    atomic_store(&reading_presentation,true);assert(present(NULL,&probe_frame,PN_REFRESH_GL16)==PN_OK);
    assert(gc_updates==gc_before+1 && clear_calls==clean_before);atomic_store(&reading_presentation,false);
    pn_free(probe_pixels);
    // 异步关闭完成属于已提交的前台返回；残留抬手事件不能取消唯一的返回帧。/ Completed asynchronous shutdown commits a foreground return; a trailing touch release must not cancel its only return frame.
    pn_async_init(&async_port.input);pn_async_scope_t stop_scope;pn_async_begin(&async_port.input,&stop_scope,true);
    pn_device_input_event_t release={.kind=PN_DASYNC_TOUCH};pn_async_event_t event={.bytes=sizeof release};memcpy(event.payload,&release,sizeof release);
    assert(pn_async_send(&async_port.input,&event)==PN_OK && pn_cancel_checkpoint()==PN_CANCELLED);
    transfer.impl=(void *)1;transfer_return=true;management_home_pending=true;selected_path[0]=0;
    gc_before=gc_updates;full_before=area_full_calls;tick_transfer(now_ms());
    assert(!transfer.impl && (gc_updates>gc_before || area_full_calls>full_before));pn_async_end(&stop_scope);
    // 前台返回不能吞掉已到达的紧急导航。/ Foreground return must not swallow pending urgent navigation.
    pn_async_init(&async_port.input);event.navigation=true;event.urgent=true;assert(pn_async_send(&async_port.input,&event)==PN_OK);
    transfer.impl=(void *)1;transfer_return=true;management_home_pending=true;gc_before=gc_updates;full_before=area_full_calls;
    tick_transfer(now_ms());assert(!transfer.impl && gc_updates==gc_before && area_full_calls==full_before);
    // 无线未释放不提交；PMU失败保留旧显示，成功才更新时基；对时禁止第二个无线owner。 / Never commit before radio release; preserve display on PMU failure, update only on success, and reject a second radio owner.
    pn_async_init(&async_port.input);time_sync.impl=(void *)1;time_result=(pn_device_time_state_t){.phase=PN_TIME_STOPPING};clock_known=false;time_poll_at=0;
    tick_time_sync(1000);assert(time_sync.impl && !time_commits && !clock_known);begin_transfer(false);assert(time_sync.impl && !transfer.impl);
    time_result=(pn_device_time_state_t){.phase=PN_TIME_SUCCEEDED,.released=true,.utc=1780000000u,.acquired_ms=1000};tick_time_sync(1100);assert(!time_sync.impl && time_commits==1 && !clock_known);
    time_sync.impl=(void *)1;commit_result=PN_OK;tick_time_sync(1200);assert(!time_sync.impl && time_commits==2 && clock_known && clock_base_sec==1780000000u);
    time_sync.impl=(void *)1;time_result=(pn_device_time_state_t){.phase=PN_TIME_CANCELLED,.released=true,.error=PN_CANCELLED};tick_time_sync(1300);assert(!time_sync.impl && time_commits==2 && clock_base_sec==1780000000u);
    time_sync.impl=(void *)1;time_result=(pn_device_time_state_t){.phase=PN_TIME_SUCCEEDED,.released=true,.utc=1780000000u,.acquired_ms=1000};clock_us=31000000;tick_time_sync(31000);assert(time_commits==3 && clock_base_sec==1780000030u);
    // USB会话阻止无线开启与直接导航；首页键必须先结束USB。
    // USB blocks wireless start/direct navigation; Home must request USB teardown first.
    usb=(pn_device_usb_t){.open=true,.phase=PN_USB_READY};begin_transfer(false);assert(!transfer.impl);
    handle_key(PN_KEY_2,PN_KEY_SHORT);assert(usb.open && usb_home && usb_return && usb.stop_requested);
    usb=(pn_device_usb_t){0};
    // 排版页中间键保存并留在原书；翻页错误不强制弹出工具。/ Middle key saves typesetting within the book; turn failures do not force tools open.
    char book[]="/tmp/pn-device-style-XXXXXX";int fd=mkstemp(book);assert(fd>=0);FILE *story=fdopen(fd,"wb");assert(story);
    for(unsigned i=0;i<60;i++)assert(fputs("文字阅读保持当前位置。\n",story)>0);
    assert(!fclose(story));
    pn_async_init(&async_port.input);home_mode=shelf_mode=status_page=actions_mode=false;
    strcpy(selected_path,book);selected_format=PN_BOOK_TXT;selected_identified=false;
    assert(pn_reader_app_open_on_media(&reader,&pool,&sd_media,book,NULL,NULL,44,0)==PN_OK);
    assert(fixture_reader_step(PN_APP_OPEN,now_ms())==PN_OK);
    // 长按只打开阅读设置，短按关闭工具回正文；之后短按正文才返回首页。
    // Long opens reading settings only, short closes tools into the body, and a subsequent body short goes Home.
    void *key_reader=reader.impl;handle_key(PN_KEY_2,PN_KEY_LONG);assert(toolbar_open && reader.impl==key_reader && !home_mode && !shelf_mode);
    handle_key(PN_KEY_2,PN_KEY_SHORT);assert(!toolbar_open && reader.impl==key_reader && !home_mode);
    assert(pn_style_ui_open(&styles,&reader,present,NULL)==PN_OK);styles.draft.pixels=46;
    panel_fault=true;handle_key(PN_KEY_2,PN_KEY_SHORT);assert(styles.active && reader.impl==key_reader && !home_mode);panel_fault=false;
    assert(pn_style_ui_present(&styles,present,NULL)==PN_OK);
    handle_key(PN_KEY_2,PN_KEY_SHORT);assert(reader.impl && !styles.active && !home_mode);
    pn_style_t saved;assert(pn_reader_app_style_get(&reader,&saved)==PN_OK && saved.pixels==46);
    toolbar_open=false;report_reader_result(PN_LIMIT);assert(!toolbar_open && reader.impl && (reader_notice || progress_save_failed));
    // 前面的USB故障用例可留下离线内部介质；本用例验证正常首页返回，先恢复真实启动的可读存储条件。
    // Earlier USB-failure cases may leave internal media offline; this case checks a normal Home return with readable storage as after a real boot.
    if(!data_media.available)assert(pn_media_attach(&data_media,1)==PN_OK);
    status_page=false;unsigned exit_calls=screen_full_calls,exit_clears=clear_calls;
    handle_key(PN_KEY_2,PN_KEY_SHORT);assert(!reader_active() && home_mode && !*selected_path);
    assert(screen_full_calls==exit_calls+1 && clear_calls==exit_clears && last_screen_wave==&E0470_WAVEFORM && last_screen_mode==MODE_GC16);
    assert(stop_reader());
    // 真实main返回/重开路径保持对象；强制停止归还热会话READ租约。
    // Actual main return/reopen preserves the object; forced stop returns the warm READ lease.
    strcpy(selected_path,book);selected_format=PN_BOOK_TXT;selected_identified=false;
    home_mode=shelf_mode=status_page=false;
    assert(pn_reader_app_open_on_media(&reader,&pool,&sd_media,book,NULL,NULL,44,0)==PN_OK);
    assert(fixture_reader_step(PN_APP_OPEN,now_ms())==PN_OK && fixture_reader_step(PN_APP_NEXT,now_ms())==PN_OK);
    void *retained=reader.impl;pn_txt_progress_t location;assert(pn_reader_app_progress(&reader,&location)==PN_OK);
    recent_loaded=true;assert(leave_reader() && !reader_active() && hot_session.txt.impl==retained && !recent_loaded);
    start_reader_impl();assert(reader.impl==retained && hot_session.hits==1 && !hot_session.media);
    pn_txt_progress_t reopened;assert(pn_reader_app_progress(&reader,&reopened)==PN_OK && reopened.source_offset==location.source_offset);
    assert(leave_reader() && hot_session.media && stop_reader() && !hot_session.media);
    // 索引只写自身缓存，构建和取消都不能驱逐热源或完整系统字库。
    // Index writes touch only its own cache; building/cancellation must not evict warm sources or the complete system face.
    tf_file=fopen(tf_path,"wb");assert(tf_file);tf_source=pn_font_builtin_source();
    for(uint64_t at=0;at<tf_source.size;){size_t n;assert(tf_source.read_at(tf_source.ctx,at,tf_bytes,sizeof tf_bytes,&n)==PN_OK && n && fwrite(tf_bytes,1,n,tf_file)==n);at+=n;}assert(!fclose(tf_file));
    assert(pn_font_system_open(&pool,&sd_media,tf_path)==PN_OK);
    assert(pn_reader_app_open_on_media(&reader,&pool,&sd_media,book,NULL,NULL,44,0)==PN_OK && fixture_reader_step(PN_APP_OPEN,now_ms())==PN_OK && leave_reader());
    char folder[]="/tmp/pn-hot-index-XXXXXX",cache_path[256],indexed_book[256],books_path[256];assert(mkdtemp(folder));
    snprintf(books_path,sizeof books_path,"%s/books",folder);assert(!mkdir(books_path,0700));
    snprintf(cache_path,sizeof cache_path,"%s/cache",folder);snprintf(indexed_book,sizeof indexed_book,"%s/books/a.txt",folder);story=fopen(indexed_book,"wb");assert(story && fputs("Original index test.",story)>0 && !fclose(story));
    assert(pn_catalog_index_init(&catalog_index,&pool,&sd_media,books_path,cache_path)==PN_OK);pn_media_lease_t index_lease;
    assert(pn_media_acquire(&sd_media,PN_MEDIA_READ,&index_lease)==PN_OK && pn_catalog_index_page(&catalog_index,&index_lease,"",false,0,NULL,6,shelf_page)==PN_EMPTY && pn_media_release(&sd_media,&index_lease)==PN_OK);
    void *index_hot=hot_session.txt.impl;pn_text_source_t retained_system=pn_font_builtin_source();
    assert(catalog_build_idle()==PN_OK && hot_session.media && hot_session.txt.impl==index_hot && pn_font_system_available() && pn_font_builtin_source().ctx==retained_system.ctx);
    pn_catalog_index_invalidate(&catalog_index);pn_cancel_scope_t index_cancel;pn_cancel_enter(&index_cancel,feedback_cancelled,NULL);
    assert(catalog_build_idle()==PN_CANCELLED && hot_session.media && hot_session.txt.impl==index_hot);pn_cancel_leave(&index_cancel);
    assert(pn_font_system_available() && pn_font_builtin_source().ctx==retained_system.ctx);
    assert(catalog_build_idle()==PN_OK && hot_session.txt.impl==index_hot && pn_font_builtin_source().ctx==retained_system.ctx);
    // 索引已就绪后，空闲与读页不得再次归还系统字库。/ Once the index is ready, idle work and paging must not release the system face again.
    assert(pn_font_system_open(&pool,&sd_media,tf_path)==PN_OK && pn_font_builtin_source().ctx==retained_system.ctx);
    assert(catalog_build_idle()==PN_EMPTY && pn_font_system_available() && pn_font_builtin_source().ctx==retained_system.ctx);
    assert(pn_media_acquire(&sd_media,PN_MEDIA_READ,&index_lease)==PN_OK && pn_catalog_index_page(&catalog_index,&index_lease,"",false,0,NULL,6,shelf_page)==PN_OK && pn_media_release(&sd_media,&index_lease)==PN_OK);
    assert(catalog_build_idle()==PN_EMPTY && pn_font_system_available() && pn_font_builtin_source().ctx==retained_system.ctx);
    assert(pn_hot_session_drop(&hot_session)==PN_OK);system_font_release();pn_catalog_index_close(&catalog_index);assert(!remove(tf_path));
    selected_path[0]=0;assert(!unlink(book));
    // 真正产品适配的短反馈/恢复：原波形身份与FULL时序归还，失败和欠载恢复原图。
    // Exercise the real product feedback/restore adapter: return waveform identity/FULL timing and restore original pixels after failure/underrun.
    pn_async_init(&async_port.input);async_port.panel=present_panel;async_port.feedback=feedback_panel;pn_device_async_view(&async_port,8,false,0);atomic_store(&reading_presentation,false);
    home_mode=shelf_mode=status_page=actions_mode=false;assert(pn_settings_ui_open(&settings_ui,&pool,NULL,NULL,present,NULL)==PN_OK);status_page=true;
    uint8_t *original=malloc(sizeof stub_back),*logical=malloc(sizeof stub_back);assert(original && logical);memcpy(original,stub_back,sizeof stub_back);const void *wave=hardware.hl.waveform;
    pn_frame_t normal;assert(pn_frame_bind(&normal,logical,sizeof stub_back,684,1216));assert(pn_device_frame_restore(original,sizeof stub_back,&normal)==PN_OK);
    unsigned management_before=maintenance.management;unsigned revision=async_port.revision;
    feedback_begin(100,600);assert(press_feedback.active && memcmp(stub_back,original,sizeof stub_back) && hardware.hl.waveform==wave && last_scan==READ_PICO_EPD_SCAN_FULL && maintenance.management==management_before && async_port.revision==revision);
    feedback_restore();assert(!press_feedback.active && !memcmp(stub_back,original,sizeof stub_back) && maintenance.management==management_before);
    assert(feedback_idle.pending && feedback_idle_tick(feedback_idle.due-1,false)==PN_EMPTY && feedback_idle_tick(feedback_idle.due+1,true)==PN_EMPTY);
    maintenance.bands[8]=maintenance.bands[9]=maintenance.bands[10]=3;maintenance.bands[5]=2;size_t attempts=pool.attempts;unsigned areas=area_full_calls;
    clock_us=(int64_t)(feedback_idle.due+1)*1000;assert(feedback_idle_tick(now_ms(),false)==PN_OK && !feedback_idle.pending && feedback_idle.completed==1 && pool.attempts==attempts && area_full_calls==areas+1);
    assert(last_full_area.y==512 && last_full_area.height==192 && maintenance.management==management_before && !maintenance.bands[8] && !maintenance.bands[9] && !maintenance.bands[10] && maintenance.bands[5]==2 && !memcmp(stub_back,original,sizeof stub_back) && async_port.revision==revision);
    feedback_begin(100,600);feedback_restore();panel_fault=true;clock_us=(int64_t)(feedback_idle.due+1)*1000;
    assert(feedback_idle_tick(now_ms(),false)==PN_IO && feedback_idle.pending && !panel_known && settle_recoverable);panel_fault=false;
    clock_us=(int64_t)(feedback_idle.due+1)*1000;assert(feedback_idle_tick(now_ms(),false)==PN_OK && !feedback_idle.pending && panel_known && !memcmp(stub_back,original,sizeof stub_back));
    feedback_begin(100,600);feedback_restore();panel_fault=true;clock_us=(int64_t)(feedback_idle.due+1)*1000;assert(feedback_idle_tick(now_ms(),false)==PN_IO);
    pn_cancel_scope_t abandoned;pn_cancel_enter(&abandoned,feedback_cancelled,NULL);assert(present(NULL,&normal,PN_REFRESH_GL16)==PN_CANCELLED && !settle_recoverable);pn_cancel_leave(&abandoned);
    pn_frame_rect(&normal,0,0,80,80,0);assert(present(NULL,&normal,PN_REFRESH_GL16)==PN_IO && !settle_recoverable);areas=area_full_calls;
    clock_us=(int64_t)(feedback_idle.due+1)*1000;assert(feedback_idle_tick(now_ms(),false)==PN_EMPTY && area_full_calls==areas);panel_fault=false;
    assert(pn_device_frame_restore(original,sizeof stub_back,&normal)==PN_OK && present(NULL,&normal,PN_REFRESH_GL16)==PN_OK && !feedback_idle.pending);
    feedback_fault=true;feedback_begin(100,600);assert(!press_feedback.active && panel_known && !memcmp(stub_back,original,sizeof stub_back) && hardware.hl.waveform==wave && last_scan==READ_PICO_EPD_SCAN_FULL && !maintenance.management);
    atomic_store(&overlap_disabled,false);feedback_underrun=true;feedback_begin(100,600);assert(!press_feedback.active && panel_known && atomic_load(&overlap_disabled) && last_pclk==PCLK_SAFE_MHZ && !memcmp(stub_back,original,sizeof stub_back));
    panel_fault=true;feedback_begin(100,600);assert(!press_feedback.active && !panel_known);feedback_begin(100,600);assert(!press_feedback.active);panel_fault=false;assert(pn_settings_ui_present(&settings_ui,present,NULL)==PN_OK && panel_known && !memcmp(stub_back,original,sizeof stub_back));
    feedback_begin(100,600);assert(press_feedback.active);assert(pn_device_frame_restore(original,sizeof stub_back,&normal)==PN_OK);
    pn_cancel_scope_t cancelled;pn_cancel_enter(&cancelled,feedback_cancelled,NULL);assert(present(NULL,&normal,PN_REFRESH_GL16)==PN_CANCELLED && press_feedback.active);
    feedback_restore();assert(!press_feedback.active && panel_known && !memcmp(stub_back,original,sizeof stub_back));pn_cancel_leave(&cancelled);
    feedback_begin(100,600);assert(press_feedback.active);assert(pn_settings_ui_event(&settings_ui,PN_SETUI_KEYS,present,NULL)==PN_OK && !press_feedback.active && !feedback_idle.pending);
    panel_known=false;feedback_begin(100,600);assert(!press_feedback.active);assert(pn_settings_ui_present(&settings_ui,present,NULL)==PN_OK);
    pn_settings_ui_close(&settings_ui);
    // 实际设备底图/三键出口的焦点回执：失败不复用未确认像素，下一次完整恢复。
    // Focus receipts through actual device references/keys: failed pixels are never reused; the next attempt restores fully.
    if(!shelf_page)shelf_page=pn_alloc(&pool,sizeof *shelf_page);
    assert(shelf_page);memset(shelf_page,0,sizeof *shelf_page);shelf_page->count=6;shelf_page->total=18;shelf_page->more=true;
    for(size_t i=0;i<6;i++){snprintf(shelf_page->items[i].name,sizeof shelf_page->items[i].name,"Book %zu.txt",i);snprintf(shelf_page->items[i].path,sizeof shelf_page->items[i].path,"/books/%zu.txt",i);shelf_page->items[i].format=PN_BOOK_TXT;}
    pn_shelf_covers_reset(shelf_covers,shelf_page);shelf_mode=true;home_mode=false;status_page=false;actions_mode=false;recent_mode=false;fav_mode=false;covers_dirty=false;shelf_focus=-1;
    assert(draw_shelf(PN_REFRESH_GL16)==PN_OK && shelf_draw_valid);unsigned shown=presents;handle_key(PN_KEY_2,PN_KEY_SHORT);assert(shelf_focus==0 && shelf_draw_valid && presents==shown+1);
    pn_frame_t expected;assert(pn_frame_bind(&expected,logical,415872,684,1216));pn_font_t face={0};assert(management_font_begin(&face,24)==PN_OK);pn_shelf_options_t options=shelf_options();if(favorites_valid)options.favorite_items=favorites;
    assert(pn_shelf_render_with_font_file_ex(shelf_page,&face,&expected,0,false,true,shelf_covers,&pool,&sd_media,NULL,&options)==PN_OK && pn_device_frame_copy(&expected,original,415872)==PN_OK && !memcmp(stub_back,original,415872));management_font_end(&face);
    // 同一输入第二次绘制命中整页帧：不再重绘，但必须呈现同一已确认像素。/ A second draw with the same inputs hits the whole-page frame: no repaint, same confirmed pixels.
    shelf_focus=0;assert(draw_shelf(PN_REFRESH_GL16)==PN_OK);
    uint64_t menu_hits=pool.cache_stats[PN_CACHE_MENU].hits;uint8_t *cached_snapshot=malloc(415872);assert(cached_snapshot);memcpy(cached_snapshot,stub_back,415872);
    assert(draw_shelf(PN_REFRESH_GL16)==PN_OK && pool.cache_stats[PN_CACHE_MENU].hits>menu_hits && !memcmp(cached_snapshot,stub_back,415872));free(cached_snapshot);
    shown=presents;assert(draw_shelf_focus(0)==PN_OK && presents==shown);
    shelf_focus=1;pn_cancel_scope_t focus_cancel;pn_cancel_enter(&focus_cancel,feedback_cancelled,NULL);assert(draw_shelf_focus(0)==PN_CANCELLED && !shelf_draw_valid);pn_cancel_leave(&focus_cancel);assert(draw_shelf_focus(0)==PN_OK && shelf_draw_valid);
    panel_fault=true;shelf_focus=2;assert(draw_shelf_focus(1)==PN_IO && !shelf_draw_valid && !panel_known);panel_fault=false;unsigned cleaned=clear_calls;shelf_focus=3;assert(draw_shelf_focus(2)==PN_OK && shelf_draw_valid && panel_known && clear_calls==cleaned+1);
    assert(present(NULL,&normal,PN_REFRESH_GL16)==PN_OK && shelf_draw_revision!=async_port.revision);shelf_focus=4;assert(draw_shelf_focus(3)==PN_OK && shelf_draw_revision==async_port.revision);
    assert(management_font_begin(&face,24)==PN_OK);options=shelf_options();if(favorites_valid)options.favorite_items=favorites;
    assert(pn_shelf_render_with_font_file_ex(shelf_page,&face,&expected,4,false,true,shelf_covers,&pool,&sd_media,NULL,&options)==PN_OK && pn_device_frame_copy(&expected,original,415872)==PN_OK && !memcmp(stub_back,original,415872));management_font_end(&face);
    pn_w_set_clock("23:45");shelf_focus=5;assert(draw_shelf_focus(4)==PN_OK && !strcmp(shelf_draw_status.clock,"23:45"));
    management_fonts_close();assert(!shelf_draw_valid);shelf_focus=0;assert(draw_shelf_focus(5)==PN_OK && shelf_draw_valid);
    list_mode=true;shelf_focus=-1;assert(draw_shelf(PN_REFRESH_GL16)==PN_OK);shelf_focus=0;assert(draw_shelf_focus(-1)==PN_OK);assert(management_font_begin(&face,24)==PN_OK);options=shelf_options();if(favorites_valid)options.favorite_items=favorites;
    assert(pn_shelf_render_with_font_file_ex(shelf_page,&face,&expected,0,false,true,shelf_covers,&pool,&sd_media,NULL,&options)==PN_OK && pn_device_frame_copy(&expected,original,415872)==PN_OK && !memcmp(stub_back,original,415872));management_font_end(&face);
    home_mode=true;list_mode=false;shelf_focus=-1;assert(draw_shelf(PN_REFRESH_GL16)==PN_OK);shown=presents;handle_key(PN_KEY_3,PN_KEY_SHORT);assert(shelf_focus==0 && presents==shown);handle_key(PN_KEY_3,PN_KEY_SHORT);assert(shelf_focus==1 && presents==shown+1);
    // 四列后两项不被旧阅读书签快捷码10/11截走；不支持格式仍应到达开书提示路由。
    // The last two four-column slots must bypass legacy reader bookmark codes 10/11 and reach the unsupported-format opening notice.
    home_mode=false;shelf_page->count=12;
    for(int slot=10;slot<=11;slot++){
        shelf_page->items[slot].format=PN_BOOK_PDF;shelf_mode=true;status_page=false;shown=presents;
        apply_selection(slot);assert(status_page && !shelf_mode && presents>shown && !reader_active());
    }
    free(original);free(logical);
    pn_free(page_copy);pn_free(shelf_page);pn_free(shelf_covers);pn_free(recent_snapshot);pn_free(favorites);pn_menu_cache_close(&menu_cache);
    pn_font_close(&management_font);pn_w_font_scope_end(&management_scope);
    assert(!pool.used && !pool.live);return 0;
}
