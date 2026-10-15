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
#include "test_device_boot.c"
#undef main
#undef pn_device_transfer_state
#undef pn_device_transfer_close
#undef pn_device_transfer_take_network
#undef pn_device_time_poll
#undef pn_device_time_state
#undef pn_device_time_close
#undef pn_device_time_commit
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
static bool feedback_cancelled(void *ctx){(void)ctx;return true;}
int main(void){
    assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    assert(read_pico_init(&hardware)==ESP_OK);pn_media_init(&sd_media);pn_media_init(&data_media);pn_media_init(&wallpaper_media);
    int vcom;assert(read_pico_pmu_vcom_get(&vcom)==ESP_OK);epd_set_vcom((uint16_t)vcom);
    assert(pn_media_attach(&sd_media,1)==PN_OK);
    assert(pn_settings_ui_open(&settings_ui,&pool,NULL,NULL,present,NULL)==PN_OK);
    assert(pn_settings_ui_event(&settings_ui,PN_SETUI_REFRESH,present,NULL)==PN_OK && pn_settings_ui_screen(&settings_ui)==2);
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
    // 同屏重绘复用界面引擎，字形缓存命中增加，无新增TF租约。/ Repaint reuses the UI engine, gains glyph hits and adds no TF lease.
    void *ui_engine=management_font.impl;size_t leases=pn_media_active(&sd_media);uint64_t hits=pool.cache_stats[PN_CACHE_GLYPH].hits;
    assert(ui_engine && management_scope.active && draw_shelf(PN_REFRESH_GL16)==PN_OK && management_font.impl==ui_engine && pn_media_active(&sd_media)==leases && pool.cache_stats[PN_CACHE_GLYPH].hits>hits);
    // 完整TF源的缓存消费者先关闭，释放源后可继续使用应急字体。/ Close cached TF-source consumers first; emergency fonts remain usable after source release.
    char tf_path[]="/tmp/pn-management-font-XXXXXX";int tf_fd=mkstemp(tf_path);assert(tf_fd>=0);FILE *tf_file=fdopen(tf_fd,"wb");assert(tf_file);
    pn_text_source_t tf_source=pn_font_builtin_source();uint8_t tf_bytes[4096];
    for(uint64_t at=0;at<tf_source.size;){size_t n;assert(tf_source.read_at(tf_source.ctx,at,tf_bytes,sizeof tf_bytes,&n)==PN_OK && n && fwrite(tf_bytes,1,n,tf_file)==n);at+=n;}assert(!fclose(tf_file));
    management_fonts_close();assert(pn_font_system_open(&pool,&sd_media,tf_path)==PN_OK && pn_font_system_available());
    assert(draw_shelf(PN_REFRESH_GL16)==PN_OK && management_font.impl && pn_media_active(&sd_media)>0);
    system_font_release();assert(!management_font.impl && !management_scope.active && !pn_font_system_available() && !pn_media_active(&sd_media));
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
    assert(active_step(PN_APP_OPEN,now_ms())==PN_OK);
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
    status_page=false;handle_key(PN_KEY_2,PN_KEY_SHORT);assert(!reader_active() && home_mode && !*selected_path);
    assert(stop_reader());
    // 真实main返回/重开路径保持对象；强制停止归还热会话READ租约。
    // Actual main return/reopen preserves the object; forced stop returns the warm READ lease.
    strcpy(selected_path,book);selected_format=PN_BOOK_TXT;selected_identified=false;
    home_mode=shelf_mode=status_page=false;
    assert(pn_reader_app_open_on_media(&reader,&pool,&sd_media,book,NULL,NULL,44,0)==PN_OK);
    assert(active_step(PN_APP_OPEN,now_ms())==PN_OK && active_step(PN_APP_NEXT,now_ms())==PN_OK);
    void *retained=reader.impl;pn_txt_progress_t location;assert(pn_reader_app_progress(&reader,&location)==PN_OK);
    assert(leave_reader() && !reader_active() && hot_session.txt.impl==retained);
    start_reader_impl();assert(reader.impl==retained && hot_session.hits==1 && !hot_session.media);
    pn_txt_progress_t reopened;assert(pn_reader_app_progress(&reader,&reopened)==PN_OK && reopened.source_offset==location.source_offset);
    assert(leave_reader() && hot_session.media && stop_reader() && !hot_session.media);
    // 索引实际需要独占写时释放热源和完整系统字库，保留原索引契约。
    // When an index actually needs exclusive writes, release warm sources and the complete system face, preserving the original index contract.
    tf_file=fopen(tf_path,"wb");assert(tf_file);tf_source=pn_font_builtin_source();
    for(uint64_t at=0;at<tf_source.size;){size_t n;assert(tf_source.read_at(tf_source.ctx,at,tf_bytes,sizeof tf_bytes,&n)==PN_OK && n && fwrite(tf_bytes,1,n,tf_file)==n);at+=n;}assert(!fclose(tf_file));
    assert(pn_font_system_open(&pool,&sd_media,tf_path)==PN_OK);
    assert(pn_reader_app_open_on_media(&reader,&pool,&sd_media,book,NULL,NULL,44,0)==PN_OK && active_step(PN_APP_OPEN,now_ms())==PN_OK && leave_reader());
    char folder[]="/tmp/pn-hot-index-XXXXXX",cache_path[256],indexed_book[256],books_path[256];assert(mkdtemp(folder));
    snprintf(books_path,sizeof books_path,"%s/books",folder);assert(!mkdir(books_path,0700));
    snprintf(cache_path,sizeof cache_path,"%s/cache",folder);snprintf(indexed_book,sizeof indexed_book,"%s/books/a.txt",folder);story=fopen(indexed_book,"wb");assert(story && fputs("Original index test.",story)>0 && !fclose(story));
    assert(pn_catalog_index_init(&catalog_index,&pool,&sd_media,books_path,cache_path)==PN_OK);pn_media_lease_t index_lease;
    assert(pn_media_acquire(&sd_media,PN_MEDIA_READ,&index_lease)==PN_OK && pn_catalog_index_page(&catalog_index,&index_lease,"",false,0,NULL,6,shelf_page)==PN_EMPTY && pn_media_release(&sd_media,&index_lease)==PN_OK);
    assert(catalog_build_idle()==PN_OK && !hot_session.media && !pn_font_system_available() && !pn_media_active(&sd_media));
    // 索引已就绪后，空闲与读页不得再次归还系统字库。/ Once the index is ready, idle work and paging must not release the system face again.
    assert(pn_font_system_open(&pool,&sd_media,tf_path)==PN_OK);pn_text_source_t retained_system=pn_font_builtin_source();
    assert(catalog_build_idle()==PN_EMPTY && pn_font_system_available() && pn_font_builtin_source().ctx==retained_system.ctx);
    assert(pn_media_acquire(&sd_media,PN_MEDIA_READ,&index_lease)==PN_OK && pn_catalog_index_page(&catalog_index,&index_lease,"",false,0,NULL,6,shelf_page)==PN_OK && pn_media_release(&sd_media,&index_lease)==PN_OK);
    assert(catalog_build_idle()==PN_EMPTY && pn_font_system_available() && pn_font_builtin_source().ctx==retained_system.ctx);
    system_font_release();pn_catalog_index_close(&catalog_index);assert(!remove(tf_path));
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
    free(original);free(logical);
    pn_free(page_copy);pn_free(shelf_page);pn_free(shelf_covers);pn_free(recent_snapshot);pn_free(favorites);
    pn_font_close(&management_font);pn_w_font_scope_end(&management_scope);
    assert(!pool.used && !pool.live);return 0;
}
