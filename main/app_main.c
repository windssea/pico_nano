/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设备TXT接入：共享阅读器、只读VCOM、触摸、内部非破坏挂载。
 * English: device TXT wiring with shared reader, read-only VCOM, touch and non-destructive internal mounting.
 * 冻结：不格式化/擦NVS，不写SY标定；未读有效VCOM不推屏。
 * Frozen: no formatting/NVS erase or SY calibration writes; no presentation without valid VCOM.
 */
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include "pn_toc_ui.h"
#include "pn_reader_input.h"
#include "pn_shelf_view.h"
#include "pn_tap.h"
#include "pn_bookmark_ui.h"
#include "pn_style_ui.h"
#include "pn_font_ui.h"
#include "device_text.h"
#include "device_transfer.h"
#include "pn_transfer_view.h"
#include "read_pico.h"
#include "read_pico_pmu.h"
#include "esp_littlefs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>
static const char *TAG="pico_nano";
static read_pico_handle_t hardware;
static pn_pool_t pool;
static pn_media_t sd_media;
static pn_media_t data_media;
static pn_reader_app_t reader;
static pn_epub_app_t epub;
static pn_toc_ui_t toc;
static pn_book_format_t selected_format=PN_BOOK_TXT;
static pn_bookmark_ui_t bookmarks;
static pn_style_ui_t styles;
static pn_font_ui_t fonts;
static bool panel_known,data_ready,locked,status_page,shelf_mode;
static pn_catalog_page_t *shelf_page;
static pn_recent_snapshot_t *recent_snapshot;
static bool recent_mode,selected_identified;
static size_t recent_start;
static pn_book_id_t selected_expected;
static char selected_path[PN_CATALOG_PATH_MAX];
static const char *book_directory="/sdcard";
static uint64_t card_instance=1;
static pn_device_transfer_t transfer;
static bool reading_menu,transfer_return,transfer_lock;
static pn_transfer_view_t transfer_view,last_transfer_view;
static uint64_t last_transfer_draw;
static void *psram_alloc(void *ctx,size_t size){(void)ctx;return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
static void psram_free(void *ctx,void *ptr){(void)ctx;heap_caps_free(ptr);}
static uint64_t now_ms(void){return (uint64_t)(esp_timer_get_time()/1000);}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)ctx;
    if(profile==PN_REFRESH_DU)return PN_UNSUPPORTED;
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++)epd_draw_pixel(x,y,(uint8_t)(pn_frame_get(frame,x,y)<<4),hardware.framebuffer);
    read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FULL);epd_poweron();
    enum EpdDrawMode mode=profile==PN_REFRESH_GC16?MODE_GC16:MODE_GL16;
    enum EpdDrawError result;
    if(!panel_known){epd_clear();result=epd_hl_update_screen_from_white(&hardware.hl,MODE_GC16,25);}
    else result=epd_hl_update_screen_full(&hardware.hl,mode,25);
    epd_poweroff();panel_known=result==EPD_DRAW_SUCCESS;
    if(!panel_known)ESP_LOGE(TAG,"Panel present failed: %u",(unsigned)result);
    return panel_known?PN_OK:PN_IO;
}
static bool reader_active(void){return reader.impl || epub.impl;}
static pn_status_t active_step(pn_reader_action_t action,uint64_t now){return epub.impl?pn_epub_app_step(&epub,action,now,present,NULL):pn_reader_app_step(&reader,action,now,present,NULL);}
static pn_status_t active_close(uint64_t now){return epub.impl?pn_epub_app_close(&epub,now):pn_reader_app_close(&reader,now);}
static void message(const char *title,const char *detail){
    reading_menu=false;
    pn_toc_ui_close(&toc);
    pn_bookmark_ui_cancel(&bookmarks);
    pn_style_ui_close(&styles);pn_font_ui_close(&fonts);
    status_page=true;shelf_mode=false;
    uint8_t *pixels=pn_alloc(&pool,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,32);
    if(status==PN_OK){pn_frame_clear(&frame,15);status=pn_device_text(&font,&frame,"小纸 Pico",32,64);}
    if(status==PN_OK)status=pn_device_text(&font,&frame,title,32,210);
    if(status==PN_OK)status=pn_device_text(&font,&frame,detail,32,330);
    if(status==PN_OK && !locked)status=pn_device_text(&font,&frame,"点下方重试",32,1140);
    if(status==PN_OK){pn_frame_rect(&frame,32,1070,620,1,8);status=present(NULL,&frame,PN_REFRESH_GC16);}
    pn_font_close(&font);pn_free(pixels);if(status!=PN_OK)ESP_LOGE(TAG,"Message unavailable: %d",(int)status);
}
static bool mount_data(void){
    esp_vfs_littlefs_conf_t config={.base_path="/data",.partition_label="data",.format_if_mount_failed=false,.read_only=false,.dont_mount=false,.grow_on_mount=false};
    esp_err_t error=esp_vfs_littlefs_register(&config);
    if(error!=ESP_OK){ESP_LOGE(TAG,"Internal data mount failed: %s; preserving partition",esp_err_to_name(error));return false;}
    size_t total=0,used=0;error=esp_littlefs_info("data",&total,&used);if(error!=ESP_OK){(void)esp_vfs_littlefs_unregister("data");return false;}
    if(!data_media.available && pn_media_attach(&data_media,1)!=PN_OK)return false;
    ESP_LOGI(TAG,"Internal data mounted: %u/%u bytes",(unsigned)used,(unsigned)total);return true;
}
static bool stop_reader(void){
    if(!reader_active()){pn_toc_ui_close(&toc);pn_bookmark_ui_cancel(&bookmarks);pn_style_ui_close(&styles);pn_font_ui_close(&fonts);return true;}
    pn_status_t status=active_close(now_ms());
    if(status!=PN_OK && reader_active()){ESP_LOGE(TAG,"Save barrier failed: %d; session retained",(int)status);message("保存失败","仍保留当前位置\n修复内部存储后重试");return false;}
    pn_toc_ui_close(&toc);
    pn_bookmark_ui_cancel(&bookmarks);pn_style_ui_close(&styles);pn_font_ui_close(&fonts);return true;
}
static void show_shelf(const char *cursor,bool previous){
    if(!shelf_page){shelf_page=pn_alloc(&pool,sizeof *shelf_page);if(!shelf_page){message("内存不足","稍后重试");return;}}
    pn_catalog_page_t *next=pn_alloc(&pool,sizeof *next);if(!next){message("内存不足","稍后重试");return;}
    pn_media_lease_t lease={0};pn_status_t status;
    if(recent_mode){
        if(!recent_snapshot)recent_snapshot=pn_alloc(&pool,sizeof *recent_snapshot);
        status=recent_snapshot?pn_media_acquire(&data_media,PN_MEDIA_READ,&lease):PN_NO_MEMORY;
        pn_journal_files_t files;pn_journal_io_t io;
        if(status==PN_OK)status=pn_recent_files(&files,&data_media,&lease,"/data/progress",&io);
        if(status==PN_OK)status=pn_recent_load(&io,&pool,recent_snapshot);
        if(lease.ticket)(void)pn_media_release(&data_media,&lease);
        if(status==PN_EMPTY){memset(recent_snapshot,0,sizeof *recent_snapshot);status=PN_OK;}
        size_t start=!*cursor?0:previous?(recent_start>=6?recent_start-6:0):recent_start+6;
        if(status==PN_OK && start>=recent_snapshot->count && start){pn_free(next);return;}
        if(status==PN_OK){status=pn_catalog_recent_page(recent_snapshot,start,next);if(status==PN_OK)recent_start=start;}
        if(status!=PN_OK){recent_mode=false;pn_free(next);message("历史读取失败","记录保留，点下方回书架");return;}
    }else{
        status=pn_media_acquire(&sd_media,PN_MEDIA_READ,&lease);
        if(status==PN_OK)status=previous?pn_catalog_page_before(&sd_media,&lease,book_directory,cursor,next):pn_catalog_page(&sd_media,&lease,book_directory,cursor,next);
        if(lease.ticket)(void)pn_media_release(&sd_media,&lease);
    }
    if(status==PN_OK && (next->count || !*cursor))*shelf_page=*next;
    pn_free(next);
    uint8_t *pixels=pn_alloc(&pool,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    if(status==PN_OK && !pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216))status=PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,24);
    if(status==PN_OK)status=pn_shelf_render_mode_with_transfer(shelf_page,&font,&frame,-1,recent_mode,true);
    if(status==PN_OK)status=present(NULL,&frame,PN_REFRESH_GC16);
    pn_font_close(&font);pn_free(pixels);
    if(status!=PN_OK){message("读取书架失败","当前位置仍保留，重试");return;}
    status_page=false;shelf_mode=true;locked=false;
}
static void start_reader(void){
    if(transfer.impl)return;
    if(!stop_reader())return;
    if(!data_ready){data_ready=mount_data();if(!data_ready){message("内部存储不可用","保留数据，未格式化\n请先初始化数据分区");return;}}
    read_pico_sd_info_t card={0};(void)read_pico_sd_get_info(&card);
    if(card.present && !card.mounted){esp_err_t mounted=read_pico_sd_remount();if(mounted!=ESP_OK){message("TF卡不可用","未格式化\n检查卡后重试");return;}(void)read_pico_sd_get_info(&card);}
    if(!card.mounted){message("请插入TF卡","书籍放 books 目录\n字体可放 fonts/reader.ttf");return;}
    if(!sd_media.available){pn_status_t s=pn_media_attach(&sd_media,card_instance++);if(s!=PN_OK){message("卡仍被占用","等待旧文件关闭后重试");return;}}
    if(!*selected_path){struct stat info;book_directory=stat("/sdcard/books",&info)==0 && S_ISDIR(info.st_mode)?"/sdcard/books":"/sdcard";show_shelf("",false);return;}
    const char *font=access("/sdcard/fonts/reader.ttf",R_OK)==0?"/sdcard/fonts/reader.ttf":NULL;
    pn_status_t status=selected_format==PN_BOOK_EPUB?pn_epub_app_open_on_media(&epub,&pool,&sd_media,selected_path,font,"/data/progress",44,now_ms()):pn_reader_app_open_on_media(&reader,&pool,&sd_media,selected_path,font,"/data/progress",44,now_ms());
    if(status==PN_OK && selected_identified){pn_book_id_t actual;status=epub.impl?pn_epub_app_identity(&epub,&actual):pn_reader_app_identity(&reader,&actual);if(status==PN_OK && memcmp(actual.sha256,selected_expected.sha256,32))status=PN_STALE_JOB;}
    if(status==PN_OK)status=active_step(PN_APP_OPEN,now_ms());
    if(status==PN_STALE_JOB){if(stop_reader()){recent_mode=false;selected_identified=false;selected_path[0]=0;message("书籍内容已改变","从全部书架重新选择\n历史记录仍保留");}return;}
    if(status!=PN_OK){ESP_LOGE(TAG,"Reader open/present failed: %d",(int)status);if(stop_reader())message("打开书籍失败","检查书籍或字体后重试");return;}
    locked=false;status_page=false;shelf_mode=false;
}
static void paint_transfer(bool menu){
    uint8_t *pixels=pn_alloc(&pool,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,32);
    if(status==PN_OK)status=menu?pn_reading_menu_render(reader_active(),&font,&frame):pn_transfer_view_render(&transfer_view,&font,&frame);
    if(status==PN_OK)status=present(NULL,&frame,panel_known?PN_REFRESH_GL16:PN_REFRESH_GC16);
    pn_font_close(&font);pn_free(pixels);
    if(status==PN_OK){status_page=true;shelf_mode=false;if(!menu)last_transfer_view=transfer_view;}
    else if(!menu)last_transfer_view.phase=(pn_transfer_view_phase_t)-1;
}
static void begin_transfer(void){
    if(transfer.impl)return;
    if(reader_active()){
        pn_status_t status=epub.impl?pn_epub_app_identity(&epub,&selected_expected):pn_reader_app_identity(&reader,&selected_expected);
        if(status!=PN_OK){message("身份读取失败","保留原书，稍后重试");return;}
        selected_identified=true;
    }
    if(!stop_reader())return;
    pn_device_wifi_config_t config={.mode=PN_WIFI_AP};
    pn_status_t status=pn_device_transfer_open(&transfer,&sd_media,"/sdcard",&config);
    if(status!=PN_OK){message("传输未开启","保留阅读位置，请重试");return;}
    reading_menu=false;transfer_return=false;transfer_lock=false;memset(&transfer_view,0,sizeof transfer_view);transfer_view.phase=PN_TVIEW_STARTING;
    paint_transfer(false);last_transfer_draw=now_ms();
}
static void tick_transfer(uint64_t now){
    pn_device_transfer_state_t state;if(pn_device_transfer_state(&transfer,&state)!=PN_OK)return;
    memset(&transfer_view,0,sizeof transfer_view);transfer_view.released=state.released;transfer_view.busy=state.work.busy;
    transfer_view.phase=state.phase==PN_DTRANSFER_READY?PN_TVIEW_READY:state.phase==PN_DTRANSFER_STOPPING?PN_TVIEW_STOPPING:state.phase==PN_DTRANSFER_STARTING?PN_TVIEW_STARTING:PN_TVIEW_FAILED;
    memcpy(transfer_view.ssid,state.network.ssid,sizeof transfer_view.ssid);memcpy(transfer_view.password,state.network.ap_password,sizeof transfer_view.password);memcpy(transfer_view.pin,state.pin,sizeof transfer_view.pin);
    if(state.network.address[0])snprintf(transfer_view.address,sizeof transfer_view.address,"http://%s",state.network.address);
    if(state.released && transfer_return && pn_device_transfer_close(&transfer)==PN_OK){
        reading_menu=false;if(transfer_lock){locked=true;message("已锁屏","传输已停止\n再按电源键继续阅读");}else start_reader();return;
    }
    if(now-last_transfer_draw>=1000 && memcmp(&transfer_view,&last_transfer_view,sizeof transfer_view)){paint_transfer(false);last_transfer_draw=now;}
}
static void device_task(void *arg){
    (void)arg;esp_err_t error=nvs_flash_init();if(error!=ESP_OK){ESP_LOGE(TAG,"NVS unavailable: %s; preserving data",esp_err_to_name(error));vTaskDelete(NULL);return;}
    error=read_pico_init(&hardware);if(error!=ESP_OK){ESP_LOGE(TAG,"Board init failed: %s",esp_err_to_name(error));read_pico_deinit(&hardware);vTaskDelete(NULL);return;}
    int vcom=0;if(!hardware.pmu_ready || read_pico_pmu_vcom_get(&vcom)!=ESP_OK){ESP_LOGE(TAG,"Factory VCOM unavailable; display withheld");read_pico_deinit(&hardware);vTaskDelete(NULL);return;}
    epd_set_vcom((uint16_t)vcom);
    size_t available=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    size_t budget=available>256u*1024u?available-256u*1024u:0;if(budget>6u*1024u*1024u)budget=6u*1024u*1024u;
    if(pn_pool_init(&pool,budget,psram_alloc,psram_free,NULL)!=0){read_pico_deinit(&hardware);vTaskDelete(NULL);return;}
    pn_media_init(&sd_media);pn_media_init(&data_media);read_pico_pmu_drain_events();
    if(read_pico_pmu_report_ready()!=ESP_OK){ESP_LOGE(TAG,"PMU running handshake failed; display withheld");read_pico_deinit(&hardware);vTaskDelete(NULL);return;}
    start_reader();pn_reader_input_t input={0};pn_tap_t tap={0};uint64_t last_card=0,last_key=0,last_ui_retry=0,boot=now_ms();
    for(;;){uint64_t now=now_ms();
        if(transfer.impl)tick_transfer(now);
        if(!transfer.impl && now-last_card>=250){last_card=now;read_pico_sd_info_t card={0};(void)read_pico_sd_get_info(&card);
            if(sd_media.available && !card.mounted){if(reader_active()){if(epub.impl)(void)pn_epub_app_media_lost(&epub);else (void)pn_reader_app_media_lost(&reader);(void)stop_reader();}else (void)pn_media_detach(&sd_media);selected_path[0]=0;pn_reader_input_cancel(&input);message("卡已移除","保留上次阅读位置\n插卡后点下方重试");}}
        if(now-last_key>=100){last_key=now;if(read_pico_pmu_take_key_short() && now-boot>=1000){pn_reader_input_cancel(&input);
                if(transfer.impl){transfer_return=true;transfer_lock=true;(void)pn_device_transfer_request_stop(&transfer);}
                else if(locked){locked=false;start_reader();}else if(stop_reader()){locked=true;message("已锁屏","再按电源键继续阅读\n当前尚未进入浅睡");}}}
        if(reader_active() && !locked){pn_status_t saved=epub.impl?pn_epub_app_tick(&epub,now_ms()):pn_reader_app_tick(&reader,now_ms());if(saved!=PN_OK && saved!=PN_BUSY)ESP_LOGW(TAG,"Pending progress save: %d",(int)saved);}
        if(bookmarks.mode!=PN_BUI_CLOSED && !bookmarks.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_bookmark_ui_present(&bookmarks,present,NULL);}
        if(toc.active && !toc.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_toc_ui_present(&toc,present,NULL);}
        if(styles.active && !styles.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_style_ui_present(&styles,present,NULL);}
        if(styles.request_fonts && !locked){styles.request_fonts=false;(void)pn_font_ui_open(&fonts,&pool,epub.impl?NULL:&reader,epub.impl?&epub:NULL,NULL,present,NULL);}
        if(fonts.active && !fonts.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_font_ui_present(&fonts,present,NULL);}
        if(hardware.touch_ready){cst836u_touch_t touch={0};esp_err_t read=cst836u_read(hardware.touch,&touch);pn_reader_action_t action;
            int hit=-1,selection=-1;
            if(transfer.impl)hit=pn_transfer_view_hit(&transfer_view,touch.x,touch.y);
            else if(reading_menu)hit=pn_reading_menu_hit(touch.x,touch.y);
            else if(fonts.active)hit=pn_font_ui_hit(&fonts,touch.x,touch.y);
            else if(toc.active)hit=pn_toc_ui_hit(&toc,touch.x,touch.y);
            else if(styles.active)hit=pn_style_ui_hit(&styles,touch.x,touch.y);
            else if(bookmarks.mode!=PN_BUI_CLOSED)hit=pn_bookmark_ui_hit(&bookmarks,touch.x,touch.y);
            else if(shelf_mode && shelf_page)hit=pn_shelf_hit_with_transfer(shelf_page,touch.x,touch.y,true);
            else if(reader_active() && !status_page && touch.y<80){
                if(epub.impl)hit=pn_epub_app_header_hit(&epub,touch.x,touch.y);
                else if(touch.x<240)hit=8;
                else if(touch.x>=250 && touch.x<380)hit=10;
                else if(touch.x>=390 && touch.x<510 && pn_reader_app_bookmark_can_return(&reader))hit=11;
                else if(touch.x>=520 && touch.x<652)hit=12;
            }
            else if(status_page && touch.y>=1070)hit=9;
            if(pn_tap_feed(&tap,touch.count,hit,read==ESP_OK,&selection) && !locked){
                if(transfer.impl){if(selection==PN_TRANSFER_VIEW_STOP){transfer_return=true;(void)pn_device_transfer_request_stop(&transfer);}}
                else if(reading_menu){if(selection==PN_READING_MENU_TRANSFER)begin_transfer();else if(selection==PN_READING_MENU_SHELF){reading_menu=false;if(stop_reader()){selected_path[0]=0;show_shelf("",false);}}else if(selection==PN_READING_MENU_RESUME){reading_menu=false;if(reader_active()){if(active_step(PN_APP_OPEN,now_ms())==PN_OK){status_page=false;shelf_mode=false;}}else start_reader();}}
                else if(shelf_mode && selection==PN_SHELF_TRANSFER)begin_transfer();
                else if(fonts.active){(void)pn_font_ui_event(&fonts,selection,now_ms(),present,NULL);if(!fonts.active){pn_font_ui_close(&fonts);if(styles.active)(void)pn_style_ui_present(&styles,present,NULL);}}
                else if(toc.active){(void)pn_toc_ui_event(&toc,selection,now_ms(),present,NULL);}
                else if(selection==13 && epub.impl && bookmarks.mode==PN_BUI_CLOSED && !styles.active){(void)pn_toc_ui_open(&toc,&epub,present,NULL);}
                else if(styles.active){pn_status_t status=pn_style_ui_event(&styles,selection,now_ms(),present,NULL);if(status!=PN_OK && status!=PN_EMPTY && status!=PN_BUSY)ESP_LOGW(TAG,"Style UI: %d",(int)status);}
                else if(selection==12 && reader_active() && bookmarks.mode==PN_BUI_CLOSED && !shelf_mode && !status_page){if(epub.impl)(void)pn_style_ui_open_epub(&styles,&epub,present,NULL);else (void)pn_style_ui_open(&styles,&reader,present,NULL);}
                else if(bookmarks.mode!=PN_BUI_CLOSED){pn_status_t status=pn_bookmark_ui_event(&bookmarks,selection,NULL,now_ms(),present,NULL);if(status!=PN_OK && status!=PN_EMPTY && status!=PN_BUSY)ESP_LOGW(TAG,"Bookmark UI: %d",(int)status);}
                else if(selection==10){if(epub.impl)(void)pn_bookmark_ui_open_epub(&bookmarks,&epub,present,NULL);else (void)pn_bookmark_ui_open(&bookmarks,&reader,present,NULL);}
                else if(selection==11){if(epub.impl)(void)pn_epub_app_bookmark_return(&epub,now_ms(),present,NULL);else (void)pn_reader_app_bookmark_return(&reader,now_ms(),present,NULL);}
                else if(shelf_mode && selection==PN_SHELF_TOGGLE){recent_mode=!recent_mode;show_shelf("",false);}
                else if(shelf_mode && selection==PN_SHELF_CONTINUE){recent_mode=true;show_shelf("",false);if(shelf_mode && shelf_page && shelf_page->count){if((shelf_page->items[0].format!=PN_BOOK_TXT && shelf_page->items[0].format!=PN_BOOK_EPUB))message("格式尚未接入","历史记录仍保留");else{selected_format=shelf_page->items[0].format;strcpy(selected_path,shelf_page->items[0].path);selected_identified=shelf_page->items[0].identified;selected_expected=shelf_page->items[0].expected;start_reader();}}}
                else if(shelf_mode && selection<6 && shelf_page){
                    if(shelf_page->items[selection].format==PN_BOOK_TXT || shelf_page->items[selection].format==PN_BOOK_EPUB){selected_format=shelf_page->items[selection].format;strcpy(selected_path,shelf_page->items[selection].path);selected_identified=shelf_page->items[selection].identified;selected_expected=shelf_page->items[selection].expected;start_reader();}
                    else message("格式尚未接入","书籍仍保留，选择TXT或EPUB");
                }else if(shelf_mode && shelf_page && shelf_page->count && (selection==PN_SHELF_NEXT || selection==PN_SHELF_PREVIOUS)){
                    char cursor[PN_CATALOG_NAME_MAX];strcpy(cursor,shelf_page->items[selection==PN_SHELF_NEXT?shelf_page->count-1:0].name);show_shelf(cursor,selection==PN_SHELF_PREVIOUS);
                }else if(selection==8){reading_menu=true;paint_transfer(true);}
                else if(selection==9){selected_path[0]=0;start_reader();}
                pn_reader_input_cancel(&input);
            }
            if(!transfer.impl && !reading_menu && !shelf_mode && !status_page && bookmarks.mode==PN_BUI_CLOSED && !styles.active && !fonts.active && !toc.active && pn_reader_input_feed(&input,touch.count,touch.x,touch.y,read==ESP_OK,&action) && !locked){
                if(reader_active() && !status_page){pn_status_t status=active_step(action,now_ms());if(status!=PN_OK && status!=PN_EMPTY){ESP_LOGW(TAG,"Reader action: %d",(int)status);message("操作未完成","当前位置仍保留\n重试或按电源键返回");}}
                else start_reader();}}
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void app_main(void){
    // 字体/解析独占一个较大栈的worker，不在小型启动栈上运行。/ Run font/parsing on one larger-stack worker, not the small boot stack.
    if(xTaskCreatePinnedToCore(device_task,"pn_device",32768,NULL,5,NULL,1)!=pdPASS)ESP_LOGE(TAG,"Device worker creation failed");
}
