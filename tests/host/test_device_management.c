/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实设备路由管理键与面板底图复用，硬件用现有启动桩。/ English: real device management routing and shelf-overlay reuse with existing boot stubs. */
#define main boot_fixture_main
#include "test_device_boot.c"
#undef main
#include "../../main/app_main.c"
int main(void){
    assert(!pn_pool_init(&pool,3u*1024u*1024u,NULL,NULL,NULL));
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
    uint8_t top[342*16];assert(shown_px);memcpy(top,shown_px,sizeof top);
    open_actions(0);assert(actions_mode && !memcmp(top,shown_px,sizeof top));
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
    for(unsigned i=0;i<6;i++){pn_frame_rect(&probe_frame,100+(int)i*4,300,4,4,0);assert(present(NULL,&probe_frame,PN_REFRESH_GL16)==PN_OK);}
    assert(clear_calls==clean_before && gc_updates==gc_before);atomic_store(&reading_presentation,false);
    // 管理第24次实际更新才GC；不变帧不占预算，周期不插入额外clear。/ GC on management update 24; unchanged frames spend no budget and periodic refresh adds no clear.
    assert(present(NULL,&probe_frame,PN_REFRESH_GC16)==PN_OK);gc_before=gc_updates;
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
    pn_free(probe_pixels);
    pn_free(shown_px);pn_free(page_copy);pn_free(shelf_page);pn_free(shelf_covers);pn_free(recent_snapshot);pn_free(favorites);
    assert(!pool.used && !pool.live);return 0;
}
