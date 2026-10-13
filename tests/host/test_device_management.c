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
    pn_free(shown_px);pn_free(page_copy);pn_free(shelf_page);pn_free(shelf_covers);pn_free(recent_snapshot);pn_free(favorites);
    assert(!pool.used && !pool.live);return 0;
}
