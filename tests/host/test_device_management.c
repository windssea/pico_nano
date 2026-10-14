/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实设备路由管理键与面板底图复用，硬件用现有启动桩。/ English: real device management routing and shelf-overlay reuse with existing boot stubs. */
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
    pn_free(page_copy);pn_free(shelf_page);pn_free(shelf_covers);pn_free(recent_snapshot);pn_free(favorites);
    pn_font_close(&management_font);pn_w_font_scope_end(&management_scope);
    assert(!pool.used && !pool.live);return 0;
}
