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
#include "pn_reader_chrome.h"
#include "pn_widgets.h"
#include "pn_search_ui.h"
#include "pn_jump_ui.h"
#include "pn_focus.h"
#include "pn_shelf_view.h"
#include "pn_wallpaper.h"
#include "pn_wallpaper_ui.h"
#include "pn_font_manage.h"
#include "pn_network_store.h"
#include "pn_settings_ui.h"
#include "pn_key.h"
#include "device_sleep.h"
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
#include "esp_random.h"
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
static pn_media_t wallpaper_media;
static bool wallpaper_ready;
static pn_wallpaper_ui_t wallpaper_ui;
static pn_font_manage_t font_manage;
static pn_settings_ui_t settings_ui;
static uint8_t input_flags; ///< 已保存翻页标志 / Saved page-turn flags
static bool input_flags_loaded;
static pn_key_t keys={.key=-1};
static bool index_mode; ///< 字母跳转页 / Letter-index page
static int shelf_focus=-1; ///< 三键书架焦点（-1无）/ Three-key shelf focus (-1 none)
static bool back_to_settings; ///< 子页从设置页进入，返回时回设置页而不是书架 / The sub-page was opened from Settings, so Back returns there instead of the shelf
static bool search_mode; ///< 搜索输入页 / Search input page
static pn_search_ui_t search_ui; ///< 搜索页输入状态 / Search page input state
static char search_query[PN_CATALOG_QUERY_MAX+1]; ///< 生效中的搜索词，空表示不过滤 / Active search query, empty means unfiltered
static char jump_letter; ///< 下次书架查询从此字母起 / The next shelf query starts at this letter
static uint64_t last_input; ///< 最近一次触摸/按键，用于自动锁屏 / Latest touch or key, for auto-lock
#define AUTO_LOCK_MS (5u*60u*1000u)
static void lock_and_sleep(const char *hint);
static char saved_network[PN_NETWORK_SSID_MAX+1]; ///< 已保存网络名，不缓存口令 / Saved network name; the password is not cached
static pn_reader_app_t reader;
static pn_epub_app_t epub;
static pn_toc_ui_t toc;
static pn_book_format_t selected_format=PN_BOOK_TXT;
static pn_bookmark_ui_t bookmarks;
static pn_style_ui_t styles;
static pn_jump_ui_t jump_ui; ///< 进度跳转面板 / Progress jump panel
static pn_font_ui_t fonts;
static bool panel_known,data_ready,locked,status_page,shelf_mode;
static pn_catalog_page_t *shelf_page;
static pn_recent_snapshot_t *recent_snapshot;
static bool recent_mode,selected_identified;
static size_t recent_start;
static bool list_mode; ///< 书架列表模式（每页5本）/ Shelf list mode (five per page)
/// 当前书架每页条数。/ Entries per shelf page right now.
static size_t shelf_per_page(void){return list_mode || *search_query?5u:6u;}
static pn_book_id_t selected_expected;
static char selected_path[PN_CATALOG_PATH_MAX];
static const char *book_directory="/sdcard";
static uint64_t card_instance=1;
static pn_device_transfer_t transfer;
static bool reading_menu,transfer_return,transfer_lock;
static pn_transfer_view_t transfer_view,last_transfer_view;
static uint64_t last_transfer_draw;
static pn_shelf_covers_t *shelf_covers;
static bool covers_dirty,touch_held;
static uint8_t cover_salt[16];
static const char *const cover_cache="/sdcard/.readpico/covers";
static void *psram_alloc(void *ctx,size_t size){(void)ctx;return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
static void psram_free(void *ctx,void *ptr){(void)ctx;heap_caps_free(ptr);}
static uint64_t now_ms(void){return (uint64_t)(esp_timer_get_time()/1000);}
/// 高压轨空闲保活毫秒；断电需等放电、再上电要数十毫秒，连续翻页期间不应反复开关。/ Idle keep-alive for the HV rails; power-off waits for discharge and power-on takes tens of ms, so do not toggle them between consecutive page turns.
#define RAILS_IDLE_MS 8000u
/// 连续软刷新(GL16)达到该次数后升级为一次整屏GC16，压掉累积灰底（同官方示例每14次）。/ Promote one full GC16 after this many consecutive soft (GL16) refreshes to clear the accumulated gray floor (14, as in the official demo).
#define SOFT_REFRESH_PER_GC16 14
/// 供数不足后退回的安全像素时钟(MHz)。/ Safe pixel clock (MHz) used after a line-queue underrun.
#define PCLK_SAFE_MHZ READ_PICO_EPD_PCLK_MIN_MHZ
/// 轨到期时刻(ms)，0表示已断电。/ Rail expiry time (ms); 0 means the rails are off.
static uint64_t rails_deadline_ms;
/// 自上次GC16以来的软刷新次数。/ Soft refreshes since the last GC16.
static unsigned soft_refreshes;
/// 立即断开高压轨；未上电时无操作。/ Drop the HV rails now; no-op when already off.
static void rails_release(void){if(rails_deadline_ms){rails_deadline_ms=0;epd_poweroff();}}
/// 主循环调用：轨空闲满期后断电。/ Called by the main loop: power the rails down once the idle period has elapsed.
static void rails_idle_check(uint64_t now){if(rails_deadline_ms && now>=rails_deadline_ms)rails_release();}
/// 阅读工具栏是否显示，以及最近一次呈现帧的副本（关闭工具栏时原样恢复）。/ Whether the reading toolbar is showing, and a copy of the latest presented frame (restored as is when the toolbar closes).
static bool toolbar_open;
static uint8_t *page_copy;
static bool ring_pending; ///< 下一次呈现在焦点位置画焦点环（仅一次）/ The next presentation draws the focus ring (once)
static pn_focus_item_t ring_item; ///< 焦点环位置 / Where the focus ring goes
static pn_focus_t focus_nav; ///< 当前页的三键焦点表 / Three-key focus table of the current page
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)ctx;
    if(!toolbar_open && frame->width==684 && frame->height==1216 && frame->stride>=342){
        if(!page_copy)page_copy=pn_alloc(&pool,342u*1216u);
        if(page_copy && frame->pixels!=page_copy)for(int y=0;y<1216;y++)memcpy(page_copy+(size_t)y*342,frame->pixels+(size_t)y*frame->stride,342);
    }
    if(ring_pending){pn_frame_t view=*frame;pn_focus_draw(&view,&ring_item);ring_pending=false;} // 页面副本在上面已取，故副本不含焦点环 / The page copy was taken above, so it never contains the ring
    if(profile==PN_REFRESH_DU)return PN_UNSUPPORTED;
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++)epd_draw_pixel(x,y,(uint8_t)(pn_frame_get(frame,x,y)<<4),hardware.framebuffer);
    read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FULL);epd_poweron();
    enum EpdDrawMode mode=profile==PN_REFRESH_GC16?MODE_GC16:MODE_GL16;
    // 软刷新累计满额时改用GC16，目标帧与区域不变。/ When the soft-refresh budget is spent, switch to GC16 with the same target frame and area.
    if(mode==MODE_GC16)soft_refreshes=0;else if(++soft_refreshes>=SOFT_REFRESH_PER_GC16){mode=MODE_GC16;soft_refreshes=0;}
    enum EpdDrawError result;
    if(!panel_known){epd_clear();result=epd_hl_update_screen_from_white(&hardware.hl,MODE_GC16,25);soft_refreshes=0;}
    else result=epd_hl_update_screen_full(&hardware.hl,mode,25);
    if(result&EPD_DRAW_EMPTY_LINE_QUEUE){
        // 供数不足：退回安全时钟，白场重建参考帧，目标帧保留在framebuffer。/ Underrun: fall back to the safe clock and rebuild the reference frame from white; the target stays in the framebuffer.
        ESP_LOGW(TAG,"Line queue underrun, pclk back to %d MHz",PCLK_SAFE_MHZ);
        read_pico_epd_set_pclk(PCLK_SAFE_MHZ);read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FULL);
        epd_clear();result=epd_hl_update_screen_from_white(&hardware.hl,MODE_GC16,25);soft_refreshes=0;
    }
    panel_known=result==EPD_DRAW_SUCCESS;
    // 成功则保活到空闲期满；失败立即断电。/ Keep the rails up until idle expiry on success; drop them at once on failure.
    rails_deadline_ms=now_ms()+RAILS_IDLE_MS;if(!panel_known)rails_release();
    if(!panel_known)ESP_LOGE(TAG,"Panel present failed: %u",(unsigned)result);
    return panel_known?PN_OK:PN_IO;
}
static bool reader_active(void){return reader.impl || epub.impl;}
static pn_status_t active_step(pn_reader_action_t action,uint64_t now){return epub.impl?pn_epub_app_step(&epub,action,now,present,NULL):pn_reader_app_step(&reader,action,now,present,NULL);}
static pn_status_t active_close(uint64_t now){return epub.impl?pn_epub_app_close(&epub,now):pn_reader_app_close(&reader,now);}
static void message(const char *title,const char *detail){
    reading_menu=false;back_to_settings=false;
    pn_toc_ui_close(&toc);
    pn_bookmark_ui_cancel(&bookmarks);
    pn_style_ui_close(&styles);pn_font_ui_close(&fonts);pn_wallpaper_ui_close(&wallpaper_ui);pn_font_manage_close(&font_manage);pn_settings_ui_close(&settings_ui);
    status_page=true;shelf_mode=false;index_mode=false;search_mode=false;
    uint8_t *pixels=pn_alloc(&pool,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,28);
    // 状态页：产品名、标题、按行说明，底部圆角“重试”按钮（点击区y>=1070）。/ Status page: product name, title, line-by-line explanation and a rounded Retry button at the bottom (touch area y>=1070).
    if(status==PN_OK){pn_frame_clear(&frame,15);status=pn_w_text(&font,&frame,"小纸 Pico",32,44,620,PN_ALIGN_LEFT);}
    if(status==PN_OK)status=pn_font_size(&font,44);
    if(status==PN_OK)status=pn_w_text_lines(&font,&frame,title,32,250,620,2,56,NULL);
    if(status==PN_OK)status=pn_font_size(&font,32);
    for(int line=0,at=0;status==PN_OK && detail[at] && line<8;line++){
        char part[128];int n=0;while(detail[at] && detail[at]!='\n' && n<(int)sizeof part-1)part[n++]=detail[at++];
        part[n]=0;if(detail[at]=='\n')at++;
        status=pn_w_text(&font,&frame,part,32,380+line*56,620,PN_ALIGN_LEFT);
    }
    if(status==PN_OK && !locked){status=pn_font_size(&font,34);if(status==PN_OK)status=pn_w_button(&font,&frame,"重试",32,1090,620,96,PN_W_SELECTED);}
    if(status==PN_OK)status=present(NULL,&frame,PN_REFRESH_GC16);
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
/// 非破坏挂载内部壁纸分区；失败只回退默认锁屏，不格式化。/ Non-destructively mount the internal wallpaper partition; failures only fall back to the default lock screen, never formatting.
static bool mount_wallpaper(void){
    if(wallpaper_ready)return true;
    esp_vfs_littlefs_conf_t config={.base_path="/wallpaper",.partition_label="wallpaper",.format_if_mount_failed=false,.read_only=false,.dont_mount=false,.grow_on_mount=false};
    esp_err_t error=esp_vfs_littlefs_register(&config);
    if(error!=ESP_OK){ESP_LOGW(TAG,"Wallpaper partition unavailable: %s; using default lock screen",esp_err_to_name(error));return false;}
    if(!wallpaper_media.available && pn_media_attach(&wallpaper_media,1)!=PN_OK){(void)esp_vfs_littlefs_unregister("wallpaper");return false;}
    wallpaper_ready=true;return true;
}
/// 锁屏页：读内部有效壁纸记录，任何失败用系统默认图，再失败退回文字页。/ Lock page: load the valid internal wallpaper record, use the system default on any failure, and fall back to the text page last.
static void show_lock(const char *hint){
    reading_menu=false;pn_toc_ui_close(&toc);pn_bookmark_ui_cancel(&bookmarks);pn_style_ui_close(&styles);pn_font_ui_close(&fonts);pn_wallpaper_ui_close(&wallpaper_ui);pn_font_manage_close(&font_manage);pn_settings_ui_close(&settings_ui);
    status_page=true;shelf_mode=false;index_mode=false;search_mode=false;
    uint8_t *pixels=pn_alloc(&pool,PN_WALLPAPER_BYTES);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_lock_selection_t selection={.mode=PN_LOCK_DEFAULT,.hint=true};
    pn_status_t status=pn_frame_bind(&frame,pixels,PN_WALLPAPER_BYTES,PN_WALLPAPER_WIDTH,PN_WALLPAPER_HEIGHT)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK && mount_wallpaper()){pn_wallpaper_store_t store;pn_media_lease_t lease;
        if(pn_wallpaper_store_init(&store,&wallpaper_media,"/wallpaper/lock.a","/wallpaper/lock.b")==PN_OK && pn_media_acquire(&wallpaper_media,PN_MEDIA_READ,&lease)==PN_OK){
            pn_status_t loaded=pn_wallpaper_load(&store,&lease,&selection,&frame);(void)pn_media_release(&wallpaper_media,&lease);
            if(loaded!=PN_OK){if(loaded!=PN_EMPTY)ESP_LOGW(TAG,"Wallpaper record unusable: %d; default lock screen",(int)loaded);selection=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=true};}}}
    if(status==PN_OK && selection.mode==PN_LOCK_BOOK){
        // 当前书封面：只有正在读的书且封面可解码时使用，否则退回系统默认。/ Current-book cover: used only while a book is open and its cover decodes; otherwise fall back to the system default.
        bool covered=false;
        if(selected_path[0] && (selected_format==PN_BOOK_TXT || selected_format==PN_BOOK_EPUB)){
            pn_catalog_item_t *item=pn_alloc(&pool,sizeof *item);pn_media_lease_t lease;
            if(item){memset(item,0,sizeof *item);item->format=selected_format;snprintf(item->path,sizeof item->path,"%s",selected_path);
                if(pn_media_acquire(&sd_media,PN_MEDIA_READ,&lease)==PN_OK){
                    size_t free_bytes=pool.limit>pool.used?pool.limit-pool.used:0,reserve=512u*1024u;
                    pn_status_t cover=pn_cover_render(&pool,&sd_media,&lease,item,cover_salt,free_bytes>reserve?free_bytes-reserve:1,&frame);(void)pn_media_release(&sd_media,&lease);
                    covered=cover==PN_OK;if(!covered)ESP_LOGW(TAG,"Lock cover unavailable: %d; default lock screen",(int)cover);}
                pn_free(item);}}
        if(!covered)selection=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=true};
    }
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,32);
    if(status==PN_OK)status=pn_lock_render(&selection,&font,hint,&frame);
    if(status==PN_OK)status=present(NULL,&frame,PN_REFRESH_GC16);
    pn_font_close(&font);pn_free(pixels);
    if(status!=PN_OK){ESP_LOGE(TAG,"Lock screen unavailable: %d",(int)status);message("已锁屏",hint);}
}
static bool stop_reader(void){
    if(!reader_active()){pn_toc_ui_close(&toc);pn_bookmark_ui_cancel(&bookmarks);pn_style_ui_close(&styles);pn_jump_ui_close(&jump_ui);pn_font_ui_close(&fonts);return true;}
    pn_status_t status=active_close(now_ms());
    if(status!=PN_OK && reader_active()){ESP_LOGE(TAG,"Save barrier failed: %d; session retained",(int)status);message("保存失败","仍保留当前位置\n修复内部存储后重试");return false;}
    pn_toc_ui_close(&toc);
    pn_bookmark_ui_cancel(&bookmarks);pn_style_ui_close(&styles);pn_jump_ui_close(&jump_ui);pn_font_ui_close(&fonts);return true;
}
/// 从PMU快照读电量百分比；PMU未就绪、读失败或SOC无效时返回-1（书架不显示电量，不猜）。
/// Battery percentage from the PMU quick snapshot; -1 when the PMU is not ready, the read fails or the SOC is invalid (the shelf then shows no battery rather than a guess).
static int battery_percent(void){
    if(!read_pico_pmu_ready() || read_pico_pmu_poll()!=ESP_OK)return -1;
    const pmu_snapshot_t *snapshot=read_pico_pmu_get();
    if(!snapshot || !(snapshot->qb_flags&0x02u) || snapshot->qb_soc>1000u)return -1;
    return (snapshot->qb_soc+5u)/10u;
}
/// 只按当前页与封面槽绘制书架，不扫描目录。/ Draw the shelf from the current page and cover slots only, without scanning.
static pn_status_t draw_shelf(pn_refresh_t profile){
    uint8_t *pixels=pn_alloc(&pool,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,24);
    if(status==PN_OK){pn_w_set_battery(battery_percent());pn_shelf_options_t options={.battery_percent=-1,.search=true,.import_tile=true,.layout_toggle=true,.list_mode=list_mode || (!recent_mode && *search_query),.query=!recent_mode && *search_query};status=pn_shelf_render_with_font_file_ex(shelf_page,&font,&frame,shelf_focus,recent_mode,true,shelf_covers,&pool,&sd_media,access("/sdcard/fonts/reader.ttf",R_OK)==0?"/sdcard/fonts/reader.ttf":NULL,&options);}
    if(status==PN_OK)status=present(NULL,&frame,profile);
    pn_font_close(&font);pn_free(pixels);return status;
}
/// 空闲时解码一本封面；整页完成后一次GL16重绘，避免逐本闪屏。/ Decode one cover per idle tick; redraw once with GL16 after the page completes to avoid per-book flashing.
static void tick_covers(void){
    size_t free_bytes=pool.limit>pool.used?pool.limit-pool.used:0,reserve=512u*1024u;bool changed=false;
    pn_status_t status=pn_shelf_covers_step(shelf_covers,shelf_page,&pool,&sd_media,cover_cache,cover_salt,free_bytes>reserve?free_bytes-reserve:1,&changed);
    if(changed)covers_dirty=true;
    if(status==PN_EMPTY && covers_dirty){covers_dirty=false;if(draw_shelf(PN_REFRESH_GL16)!=PN_OK)ESP_LOGW(TAG,"Cover redraw failed");}
    else if(status!=PN_OK && status!=PN_EMPTY && status!=PN_BUSY){ESP_LOGW(TAG,"Cover queue stopped: %d",(int)status);pn_shelf_covers_reset(shelf_covers,NULL);covers_dirty=false;}
}
/// 取最近阅读第一本作为“继续阅读”卡；读取失败或没有记录时不显示卡。/ Use the first recent entry as the continue-reading card; show none when unreadable or empty.
static void refresh_last(void){
    if(!shelf_covers)return;
    pn_catalog_page_t *tmp=pn_alloc(&pool,sizeof *tmp);bool found=false;
    if(tmp && data_ready){
        if(!recent_snapshot)recent_snapshot=pn_alloc(&pool,sizeof *recent_snapshot);
        pn_media_lease_t lease={0};pn_status_t status=recent_snapshot?pn_media_acquire(&data_media,PN_MEDIA_READ,&lease):PN_NO_MEMORY;
        pn_journal_files_t files;pn_journal_io_t io;
        if(status==PN_OK)status=pn_recent_files(&files,&data_media,&lease,"/data/progress",&io);
        if(status==PN_OK)status=pn_recent_load(&io,&pool,recent_snapshot);
        if(lease.ticket)(void)pn_media_release(&data_media,&lease);
        // 书架条目的“进度 · 格式”行用同一份记录。/ The shelf entries' progress line uses the same snapshot.
        if(status==PN_OK && shelf_page)pn_catalog_apply_recent(shelf_page,recent_snapshot);
        if(status==PN_OK && recent_snapshot->count && pn_catalog_recent_page(recent_snapshot,0,tmp)==PN_OK && tmp->count){pn_shelf_covers_set_last(shelf_covers,&tmp->items[0]);found=true;}
    }
    if(!found)pn_shelf_covers_set_last(shelf_covers,NULL);
    pn_free(tmp);
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
        size_t start=!*cursor?0:previous?(recent_start>=shelf_per_page()?recent_start-shelf_per_page():0):recent_start+shelf_per_page();
        if(status==PN_OK && start>=recent_snapshot->count && start){pn_free(next);return;}
        if(status==PN_OK){status=pn_catalog_recent_page_n(recent_snapshot,start,shelf_per_page(),next);if(status==PN_OK)recent_start=start;}
        if(status!=PN_OK){recent_mode=false;pn_free(next);message("历史读取失败","记录保留，点下方回书架");return;}
    }else{
        status=pn_media_acquire(&sd_media,PN_MEDIA_READ,&lease);
        if(status==PN_OK)status=*search_query?(previous?pn_catalog_search_page_before_n(&sd_media,&lease,book_directory,search_query,cursor,shelf_per_page(),next):pn_catalog_search_page_n(&sd_media,&lease,book_directory,search_query,cursor,shelf_per_page(),next)):jump_letter?pn_catalog_page_from_n(&sd_media,&lease,book_directory,jump_letter,shelf_per_page(),next):previous?pn_catalog_page_before_n(&sd_media,&lease,book_directory,cursor,shelf_per_page(),next):pn_catalog_page_n(&sd_media,&lease,book_directory,cursor,shelf_per_page(),next);
        jump_letter=0;
        if(lease.ticket)(void)pn_media_release(&sd_media,&lease);
    }
    if(status==PN_OK && (next->count || !*cursor)){*shelf_page=*next;shelf_focus=-1;}
    pn_free(next);
    // 换页先用缓存封面，首帧即完整；未命中的留给空闲逐条解码。/ Use cached covers on page change so the first paint is complete; misses decode later per idle tick.
    if(status==PN_OK && !shelf_covers)shelf_covers=pn_alloc(&pool,sizeof *shelf_covers);
    if(status==PN_OK && shelf_covers){bool changed;refresh_last();pn_shelf_covers_reset(shelf_covers,shelf_page);covers_dirty=false;
        if(pn_shelf_covers_cached(shelf_covers,shelf_page,&pool,&sd_media,cover_cache,&changed)!=PN_OK)pn_shelf_covers_reset(shelf_covers,NULL);}
    if(status==PN_OK)status=draw_shelf(PN_REFRESH_GC16);
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
/// 读取已保存网络（含口令），调用方用后清零。/ Read the saved network including its password; callers wipe it after use.
static pn_status_t load_network(pn_network_credentials_t *out){
    if(!data_ready)data_ready=mount_data();
    if(!data_ready)return PN_IO;
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(&data_media,PN_MEDIA_READ,&lease);if(status!=PN_OK)return status;
    pn_journal_files_t files;pn_journal_io_t io;status=pn_network_files(&files,&data_media,&lease,"/data/progress",&io);
    if(status==PN_OK)status=pn_network_load(&io,out);
    (void)pn_media_release(&data_media,&lease);return status;
}
static void refresh_network_name(void){pn_network_credentials_t c={0};pn_status_t status=load_network(&c);if(status==PN_OK)strcpy(saved_network,c.ssid);else if(status==PN_EMPTY)saved_network[0]=0;pn_network_wipe(&c);}
/// 主任务保存网页提交的网络，再回报给网页。/ The main task saves the network submitted by the web page, then reports back.
static void handle_network_request(void){
    pn_network_credentials_t c={0};bool forget=false;
    if(pn_device_transfer_take_network(&transfer,&c,&forget)!=PN_OK)return;
    if(forget)pn_network_wipe(&c);
    pn_status_t status=data_ready?PN_OK:PN_IO;pn_media_lease_t lease;
    if(status==PN_OK)status=pn_media_acquire(&data_media,PN_MEDIA_WRITE,&lease);
    if(status==PN_OK){pn_journal_files_t files;pn_journal_io_t io;status=pn_network_files(&files,&data_media,&lease,"/data/progress",&io);if(status==PN_OK)status=pn_network_save(&io,&c);(void)pn_media_release(&data_media,&lease);}
    pn_network_wipe(&c);refresh_network_name();
    if(status!=PN_OK)ESP_LOGW(TAG,"Network save failed: %d",(int)status);
    (void)pn_device_transfer_network_result(&transfer,status,saved_network);
}
static void paint_transfer(bool menu){
    uint8_t *pixels=pn_alloc(&pool,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,32);
    if(status==PN_OK)status=menu?pn_reading_menu_render_lan(reader_active(),*saved_network?saved_network:NULL,&font,&frame):pn_transfer_view_render(&transfer_view,&font,&frame);
    if(status==PN_OK)status=present(NULL,&frame,panel_known?PN_REFRESH_GL16:PN_REFRESH_GC16);
    pn_font_close(&font);pn_free(pixels);
    if(status==PN_OK){status_page=true;shelf_mode=false;if(!menu)last_transfer_view=transfer_view;}
    else if(!menu)last_transfer_view.phase=(pn_transfer_view_phase_t)-1;
}
/// 进入壁纸设置：先保存并关闭正文释放内存，退出后回原书或书架。/ Enter wallpaper settings: save and close the book first to free memory, then return to the book or shelf on exit.
static void begin_wallpaper(void){
    if(transfer.impl || wallpaper_ui.impl)return;
    if(!stop_reader())return;
    reading_menu=false;pn_wallpaper_store_t store;
    bool store_ok=mount_wallpaper() && pn_wallpaper_store_init(&store,&wallpaper_media,"/wallpaper/lock.a","/wallpaper/lock.b")==PN_OK;
    pn_status_t status=pn_wallpaper_ui_open(&wallpaper_ui,&pool,&sd_media,"/sdcard/wallpapers",store_ok?&store:NULL,present,NULL);
    if(status!=PN_OK){message("壁纸设置未打开","保留原锁屏，稍后重试");return;}
    status_page=true;shelf_mode=false;index_mode=false;search_mode=false;
}
/// 进入字体管理：先关闭正文和其字体源，删除/设默认在无消费者时进行。/ Enter font management after closing the book and its font sources, so deletion and defaults happen without consumers.
static void begin_fonts(void){
    if(transfer.impl || font_manage.impl || wallpaper_ui.impl)return;
    if(!stop_reader())return;
    reading_menu=false;
    if(!data_ready)data_ready=mount_data();
    pn_status_t status=pn_font_manage_open(&font_manage,&pool,&sd_media,"/sdcard/fonts",data_ready?&data_media:NULL,data_ready?"/data/progress":NULL,present,NULL);
    if(status!=PN_OK){message("字体管理未打开","字体与选择均保留，稍后重试");return;}
    status_page=true;shelf_mode=false;index_mode=false;search_mode=false;
}
/// 设置页：壁纸/字体入口与翻页开关；进入前同样保存并关闭正文。/ Settings: wallpaper/font entries and page-turn switches; the book is saved and closed first as well.
static void begin_settings(void){
    if(transfer.impl || settings_ui.impl || font_manage.impl || wallpaper_ui.impl)return;
    if(!stop_reader())return;
    reading_menu=false;
    if(!data_ready)data_ready=mount_data();
    pn_status_t status=pn_settings_ui_open(&settings_ui,&pool,data_ready?&data_media:NULL,data_ready?"/data/progress":NULL,present,NULL);
    if(status!=PN_OK){message("设置未打开","原设置保留，稍后重试");return;}
    refresh_network_name();if(*saved_network)(void)pn_settings_ui_set_lan(&settings_ui,saved_network,present,NULL);
    status_page=true;shelf_mode=false;index_mode=false;search_mode=false;
}
static void end_settings(void){input_flags=settings_ui.flags;pn_settings_ui_close(&settings_ui);if(*selected_path)start_reader();else show_shelf("",false);}
static void end_fonts(void){pn_font_manage_close(&font_manage);if(back_to_settings){back_to_settings=false;begin_settings();return;}if(*selected_path)start_reader();else show_shelf("",false);}
static void end_wallpaper(void){pn_wallpaper_ui_close(&wallpaper_ui);if(back_to_settings){back_to_settings=false;begin_settings();return;}if(*selected_path)start_reader();else show_shelf("",false);}
static void begin_transfer(bool station){
    if(transfer.impl)return;
    if(reader_active()){
        pn_status_t status=epub.impl?pn_epub_app_identity(&epub,&selected_expected):pn_reader_app_identity(&reader,&selected_expected);
        if(status!=PN_OK){message("身份读取失败","保留原书，稍后重试");return;}
        selected_identified=true;
    }
    if(!stop_reader())return;
    pn_device_wifi_config_t config={.mode=PN_WIFI_AP};
    if(station){pn_network_credentials_t c={0};pn_status_t loaded=load_network(&c);
        if(loaded!=PN_OK || !c.ssid[0]){pn_network_wipe(&c);message("没有已保存的网络","可在热点传书网页中保存");return;}
        config.mode=PN_WIFI_STA;strcpy(config.ssid,c.ssid);strcpy(config.password,c.password);pn_network_wipe(&c);}
    pn_status_t status=pn_device_transfer_open(&transfer,&sd_media,"/sdcard",&config);
    {volatile uint8_t *secret=(volatile uint8_t *)config.password;for(size_t i=0;i<sizeof config.password;i++)secret[i]=0;}
    if(status!=PN_OK){message("传输未开启","保留阅读位置，请重试");return;}
    if(!data_ready)data_ready=mount_data();
    (void)pn_device_transfer_network_result(&transfer,PN_EMPTY,saved_network);
    reading_menu=false;transfer_return=false;transfer_lock=false;memset(&transfer_view,0,sizeof transfer_view);transfer_view.phase=PN_TVIEW_STARTING;
    paint_transfer(false);last_transfer_draw=now_ms();
}
static void tick_transfer(uint64_t now){
    pn_device_transfer_state_t state;if(pn_device_transfer_state(&transfer,&state)!=PN_OK)return;
    handle_network_request();
    memset(&transfer_view,0,sizeof transfer_view);transfer_view.released=state.released;transfer_view.busy=state.work.busy;transfer_view.station=state.network.mode==PN_WIFI_STA;
    transfer_view.phase=state.phase==PN_DTRANSFER_READY?PN_TVIEW_READY:state.phase==PN_DTRANSFER_STOPPING?PN_TVIEW_STOPPING:state.phase==PN_DTRANSFER_STARTING?PN_TVIEW_STARTING:PN_TVIEW_FAILED;
    memcpy(transfer_view.ssid,state.network.ssid,sizeof transfer_view.ssid);memcpy(transfer_view.password,state.network.ap_password,sizeof transfer_view.password);memcpy(transfer_view.pin,state.pin,sizeof transfer_view.pin);
    if(state.network.address[0])snprintf(transfer_view.address,sizeof transfer_view.address,"http://%s",state.network.address);
    if(state.released && transfer_return && pn_device_transfer_close(&transfer)==PN_OK){
        reading_menu=false;if(transfer_lock)lock_and_sleep("传输已停止，按电源键继续阅读");else start_reader();return;
    }
    if(now-last_transfer_draw>=1000 && memcmp(&transfer_view,&last_transfer_view,sizeof transfer_view)){paint_transfer(false);last_transfer_draw=now;}
}
/// 锁屏后浅睡，电源键唤醒即解锁回原书；唤醒源不可用时保持锁屏等待轮询。/ Light-sleep after locking and unlock back to the book on a power-key wake; if the wake source is unavailable stay locked and keep polling.
static void lock_and_sleep(const char *hint){
    locked=true;show_lock(hint);
    rails_release(); // 入睡前必须断开高压轨与VCOM。/ HV rails and VCOM must be down before sleeping.
    if(pn_device_sleep_until_key()){locked=false;last_input=now_ms();start_reader();}
}
/// 字母跳转页。/ Letter-index page.
/// 打开搜索输入页；沿用字母页的状态约定（不在书架模式）。/ Open the search input page, following the letter-index page's state conventions (not in shelf mode).
static void show_search(void){
    uint8_t *pixels=pn_alloc(&pool,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,40);
    if(status==PN_OK)status=pn_search_ui_render(&search_ui,&font,&frame);
    if(status==PN_OK)status=present(NULL,&frame,PN_REFRESH_GL16);
    pn_font_close(&font);pn_free(pixels);
    if(status==PN_OK){search_mode=true;shelf_mode=false;status_page=true;}
}
static void show_index(void){
    uint8_t *pixels=pn_alloc(&pool,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,40);
    if(status==PN_OK)status=pn_shelf_index_render(&font,&frame);
    if(status==PN_OK)status=present(NULL,&frame,PN_REFRESH_GL16);
    pn_font_close(&font);pn_free(pixels);
    if(status==PN_OK){index_mode=true;shelf_mode=false;status_page=true;}
}
/// 屏下三键：阅读KEY1/3翻页、KEY2菜单、长按KEY2回书架；书架KEY1/3翻书目页；菜单KEY2返回。/ Touch keys: reading KEY1/3 turn pages, KEY2 opens the menu and a long KEY2 returns to the shelf; shelf KEY1/3 page the catalog; menu KEY2 goes back.
/// 当前书不可用的工具栏入口：TXT没有目录，搜索尚未实现。/ Toolbar entries unavailable for the current book: TXT has no TOC and search is not implemented.
static unsigned tool_unavailable(void){return (epub.impl?0u:1u)|4u;}
/// 在最近一页上叠加工具栏并呈现；失败时保持原页面。/ Overlay the toolbar on the latest page and present it; the page stays on failure.
static void toolbar_show(void){
    if(toolbar_open || !reader_active() || status_page || !page_copy)return;
    uint8_t *scratch=pn_alloc(&pool,342u*1216u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=scratch && pn_frame_bind(&frame,scratch,342u*1216u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK){memcpy(scratch,page_copy,342u*1216u);status=pn_font_open(&font,&pool,&builtin,34);}
    if(status==PN_OK)status=pn_reader_toolbar_render(&font,&frame,tool_unavailable());
    if(status==PN_OK){toolbar_open=true;status=present(NULL,&frame,PN_REFRESH_GL16);if(status!=PN_OK)toolbar_open=false;}
    pn_font_close(&font);pn_free(scratch);
    if(status!=PN_OK)ESP_LOGW(TAG,"Toolbar: %d",(int)status);
}
/// 关闭工具栏并恢复原页面；强刷传PN_REFRESH_GC16。/ Close the toolbar and restore the page; pass PN_REFRESH_GC16 for a full refresh.
static void toolbar_close(pn_refresh_t profile){
    toolbar_open=false;if(!page_copy)return;
    pn_frame_t frame;if(pn_frame_bind(&frame,page_copy,342u*1216u,684,1216) && present(NULL,&frame,profile)!=PN_OK)ESP_LOGW(TAG,"Toolbar close failed");
}
static int active_hit(int x,int y);
static void apply_selection(int selection);
/// 三键焦点导航适用的页面：触摸页与工具栏等（书架和阅读正文各有自己的按键规则）。/ Pages three-key focus navigation applies to: touch pages and the toolbar (the shelf and the reading text have their own key rules).
static bool focus_page(void){
    return transfer.impl || wallpaper_ui.impl || font_manage.impl || settings_ui.impl || search_mode || index_mode || reading_menu || fonts.active || toc.active || jump_ui.active || styles.active || bookmarks.mode!=PN_BUI_CLOSED || toolbar_open || status_page;
}
static int focus_hit(void *ctx,int x,int y){(void)ctx;return active_hit(x,y);}
/// 重新扫描当前页的焦点表，尽量保持原焦点。/ Rescan the page's focus table, keeping the focus where possible.
static void focus_rescan(void){
    int keep=-1;const pn_focus_item_t *current=pn_focus_current(&focus_nav);if(current)keep=current->code;
    static const int skip[]={PN_TOOL_CLOSE};
    pn_focus_scan(&focus_nav,focus_hit,NULL,skip,1);
    for(size_t i=0;keep>=0 && i<focus_nav.count;i++)if(focus_nav.items[i].code==keep)focus_nav.index=(int)i;
}
/// 在当前页上画焦点环并呈现一次；基底取最近一次呈现的页面副本。/ Present the page once with the focus ring on top, based on the latest presented page copy.
static void focus_show(void){
    const pn_focus_item_t *item=pn_focus_current(&focus_nav);
    if(!item || !page_copy)return;
    ring_item=*item;ring_pending=true;
    if(toolbar_open){toolbar_open=false;toolbar_show();ring_pending=false;return;}
    uint8_t *scratch=pn_alloc(&pool,342u*1216u);pn_frame_t frame;
    if(scratch && pn_frame_bind(&frame,scratch,342u*1216u,684,1216)){memcpy(scratch,page_copy,342u*1216u);if(present(NULL,&frame,PN_REFRESH_GL16)!=PN_OK)ESP_LOGW(TAG,"Focus ring present failed");}
    ring_pending=false;pn_free(scratch);
}
/// 当前是否是阻断确认弹窗（删除字体/删除书签），是则给出确认与取消的命中码。/ Whether a blocking confirmation dialog (delete font / delete bookmark) is showing, giving its confirm and cancel hit codes.
static bool dialog_codes(int *confirm,int *cancel){
    if(font_manage.impl && font_manage.screen==PN_FMU_CONFIRMING){*confirm=PN_FMU_CONFIRM;*cancel=PN_FMU_CANCEL;return true;}
    if(bookmarks.mode==PN_BUI_DELETE){*confirm=PN_BUI_CONFIRM;*cancel=PN_BUI_CANCEL;return true;}
    return false;
}
/// 弹窗三键（docs/UI_UX.md第7节）：KEY1取消，KEY3在取消与确认间切换焦点，KEY2确认当前焦点；默认焦点是取消，不会自动确认。
/// Dialog keys (docs/UI_UX.md section 7): KEY1 cancels, KEY3 switches focus between Cancel and Confirm and KEY2 confirms the focused button; the default focus is Cancel and nothing is ever confirmed automatically.
static void dialog_key(int key,pn_key_event_t event,int confirm,int cancel){
    if(event!=PN_KEY_SHORT)return;
    focus_rescan();
    const pn_focus_item_t *item=pn_focus_current(&focus_nav);
    bool on_confirm=item && item->code==confirm;
    if(key==PN_KEY_1){focus_nav.index=-1;apply_selection(cancel);return;}
    if(key==PN_KEY_2){focus_nav.index=-1;apply_selection(on_confirm?confirm:cancel);return;}
    int target=on_confirm?cancel:confirm;
    for(size_t i=0;i<focus_nav.count;i++)if(focus_nav.items[i].code==target)focus_nav.index=(int)i;
    focus_show();
}
/// KEY1/KEY3移动焦点，KEY2确认焦点项（没有焦点时工具栏的KEY2仍是关闭）。/ KEY1/KEY3 move the focus and KEY2 confirms it (with no focus KEY2 still closes the toolbar).
static void focus_key(int key,pn_key_event_t event){
    if(event!=PN_KEY_SHORT)return;
    focus_rescan();
    if(key==PN_KEY_2){
        const pn_focus_item_t *item=pn_focus_current(&focus_nav);
        if(item){int code=item->code;focus_nav.index=-1;apply_selection(code);}
        else if(toolbar_open)toolbar_close(PN_REFRESH_GL16);
        return;
    }
    if(pn_focus_move(&focus_nav,key==PN_KEY_3?1:-1)>=0)focus_show();
}
/// 打开书架第index项（触摸与KEY2共用）。/ Open shelf entry index (shared by touch and KEY2).
static void open_shelf_item(int index){
    if(!shelf_page || index<0 || (size_t)index>=shelf_page->count)return;
    const pn_catalog_item_t *item=&shelf_page->items[index];
    if(item->format==PN_BOOK_TXT || item->format==PN_BOOK_EPUB){selected_format=item->format;strcpy(selected_path,item->path);selected_identified=item->identified;selected_expected=item->expected;start_reader();}
    else message("格式尚未接入","书籍仍保留，选择TXT或EPUB");
}
static void handle_key(int key,pn_key_event_t event){
    {int confirm=0,cancel=0;if(dialog_codes(&confirm,&cancel)){dialog_key(key,event,confirm,cancel);return;}}
    if(focus_page()){focus_key(key,event);return;}
    if(transfer.impl || wallpaper_ui.impl || font_manage.impl || settings_ui.impl)return;
    if(bookmarks.mode!=PN_BUI_CLOSED || styles.active || fonts.active || toc.active || jump_ui.active)return;
    if(toolbar_open){if(key==PN_KEY_2 && event==PN_KEY_SHORT)toolbar_close(PN_REFRESH_GL16);return;}
    if(reading_menu){
        if(event==PN_KEY_SHORT && key==PN_KEY_2){reading_menu=false;if(reader_active()){if(active_step(PN_APP_OPEN,now_ms())==PN_OK){status_page=false;shelf_mode=false;}}else start_reader();}
        return;
    }
    if(shelf_mode && shelf_page){
        if(event!=PN_KEY_SHORT)return;
        // KEY2：先把焦点放到当前页第一项，再按一次打开焦点项（docs/UI_UX.md第7节）；设置从底栏进入。/ KEY2: first put focus on the page's first entry, press again to open it (docs/UI_UX.md section 7); Settings is reached from the bottom bar.
        if(key==PN_KEY_2){
            if(!shelf_page->count)return;
            if(shelf_focus<0){shelf_focus=0;if(draw_shelf(PN_REFRESH_GL16)!=PN_OK)ESP_LOGW(TAG,"Shelf focus redraw failed");}
            else{int index=shelf_focus;shelf_focus=-1;open_shelf_item(index);}
            return;
        }
        if(!shelf_page->count)return;
        char cursor[PN_CATALOG_NAME_MAX];strcpy(cursor,shelf_page->items[key==PN_KEY_3?shelf_page->count-1:0].name);show_shelf(cursor,key==PN_KEY_1);return;
    }
    if(!reader_active() || status_page)return;
    if(key==PN_KEY_2){
        if(event==PN_KEY_LONG){if(stop_reader()){selected_path[0]=0;show_shelf("",false);}}
        else toolbar_show();
        return;
    }
    if(event!=PN_KEY_SHORT || (input_flags&PN_INPUT_NO_KEYS))return;
    pn_status_t status=active_step(key==PN_KEY_1?PN_APP_PREVIOUS:PN_APP_NEXT,now_ms());
    if(status!=PN_OK && status!=PN_EMPTY){ESP_LOGW(TAG,"Key action: %d",(int)status);message("操作未完成","当前位置仍保留\n重试或按电源键返回");}
}
/// 按当前活动页面扫描命中码（触摸与三键焦点共用）。/ Hit code of the active page at a point (shared by touch and three-key focus).
static int active_hit(int x,int y){
    int hit=-1;
    if(transfer.impl)hit=pn_transfer_view_hit(&transfer_view,x,y);
    else if(wallpaper_ui.impl)hit=pn_wallpaper_ui_hit(&wallpaper_ui,x,y);
    else if(font_manage.impl)hit=pn_font_manage_hit(&font_manage,x,y);
    else if(settings_ui.impl)hit=pn_settings_ui_hit(&settings_ui,x,y);
    else if(search_mode)hit=pn_search_ui_hit(x,y);
    else if(index_mode){char letter=pn_shelf_index_hit(x,y);hit=letter?(int)(unsigned char)letter:-1;}
    else if(reading_menu)hit=pn_reading_menu_hit_lan(x,y,*saved_network!=0);
    else if(fonts.active)hit=pn_font_ui_hit(&fonts,x,y);
    else if(toc.active)hit=pn_toc_ui_hit(&toc,x,y);
    else if(jump_ui.active)hit=pn_jump_ui_hit(&jump_ui,x,y);
    else if(styles.active)hit=pn_style_ui_hit(&styles,x,y);
    else if(bookmarks.mode!=PN_BUI_CLOSED)hit=pn_bookmark_ui_hit(&bookmarks,x,y);
    else if(shelf_mode && shelf_page){pn_shelf_options_t options={.battery_percent=-1,.search=true,.import_tile=true,.layout_toggle=true,.list_mode=list_mode || (!recent_mode && *search_query),.query=!recent_mode && *search_query};hit=pn_shelf_hit_ex(shelf_page,x,y,&options);if(hit<0 && pn_w_tabbar_hit(3,1104,112,x,y)==1)hit=PN_SHELF_TRANSFER;}
    else if(toolbar_open)hit=pn_reader_toolbar_hit(x,y,tool_unavailable());
    else if(reader_active() && !status_page && y>=1144 && x<420 && (epub.impl?pn_epub_app_bookmark_can_return(&epub):pn_reader_app_bookmark_can_return(&reader)))hit=11;
    else if(reader_active() && !status_page && y>=1144 && x>=420)hit=14;
    else if(status_page && y>=1070)hit=9;
    return hit;
}
/// 执行一次已确认的选择（点击或KEY2确认）。/ Perform one confirmed selection (a tap or a KEY2 confirmation).
static void apply_selection(int selection){
    if(transfer.impl){if(selection==PN_TRANSFER_VIEW_STOP){transfer_return=true;(void)pn_device_transfer_request_stop(&transfer);}}
    else if(wallpaper_ui.impl){pn_status_t status=pn_wallpaper_ui_event(&wallpaper_ui,selection,present,NULL);if(status!=PN_OK && status!=PN_EMPTY)ESP_LOGW(TAG,"Wallpaper UI: %d",(int)status);if(!wallpaper_ui.active)end_wallpaper();}
    else if(font_manage.impl){pn_status_t status=pn_font_manage_event(&font_manage,selection,present,NULL);if(status!=PN_OK && status!=PN_EMPTY)ESP_LOGW(TAG,"Font management: %d",(int)status);if(!font_manage.active)end_fonts();}
    else if(search_mode){
        if(selection==PN_SEARCH_BACK){search_mode=false;show_shelf("",false);}
        else if(selection==PN_SEARCH_DONE){snprintf(search_query,sizeof search_query,"%s",search_ui.query);search_mode=false;recent_mode=false;show_shelf("",false);}
        else{
            bool changed=selection==PN_SEARCH_DELETE?pn_search_ui_delete(&search_ui):selection==PN_SEARCH_CLEAR?(*search_ui.query?(search_ui.query[0]=0,true):false):pn_search_ui_append(&search_ui,(char)selection);
            if(changed)show_search();
        }
    }
    else if(index_mode){index_mode=false;if(selection!='<'){jump_letter=(char)selection;recent_mode=false;}show_shelf("",false);}
    else if(settings_ui.impl){pn_status_t status=pn_settings_ui_event(&settings_ui,selection,present,NULL);if(status!=PN_OK && status!=PN_EMPTY && status!=PN_LIMIT)ESP_LOGW(TAG,"Settings: %d",(int)status);
        if(settings_ui.request){int request=settings_ui.request;input_flags=settings_ui.flags;pn_settings_ui_close(&settings_ui);back_to_settings=request==PN_SETUI_WALLPAPER || request==PN_SETUI_FONTS;if(request==PN_SETUI_WALLPAPER)begin_wallpaper();else if(request==PN_SETUI_LAN)begin_transfer(true);else begin_fonts();}
        else if(!settings_ui.active)end_settings();}
    else if(toolbar_open){
        if(selection==PN_TOOL_CLOSE)toolbar_close(PN_REFRESH_GL16);
        else if(selection==PN_TOOL_REFRESH)toolbar_close(PN_REFRESH_GC16);
        else if(selection==PN_TOOL_TOC){toolbar_open=false;if(epub.impl)(void)pn_toc_ui_open(&toc,&epub,present,NULL);}
        else if(selection==PN_TOOL_BOOKMARKS){toolbar_open=false;if(epub.impl)(void)pn_bookmark_ui_open_epub(&bookmarks,&epub,present,NULL);else (void)pn_bookmark_ui_open(&bookmarks,&reader,present,NULL);}
        else if(selection==PN_TOOL_TYPESET){toolbar_open=false;if(epub.impl)(void)pn_style_ui_open_epub(&styles,&epub,present,NULL);else (void)pn_style_ui_open(&styles,&reader,present,NULL);}
        else if(selection==PN_TOOL_SHELF){toolbar_open=false;if(stop_reader()){selected_path[0]=0;show_shelf("",false);}}
    }
    else if(reading_menu){if(selection==PN_READING_MENU_LAN)begin_transfer(true);else if(selection==PN_READING_MENU_SETTINGS)begin_settings();else if(selection==PN_READING_MENU_TRANSFER)begin_transfer(false);else if(selection==PN_READING_MENU_SHELF){reading_menu=false;if(stop_reader()){selected_path[0]=0;show_shelf("",false);}}else if(selection==PN_READING_MENU_RESUME){reading_menu=false;if(reader_active()){if(active_step(PN_APP_OPEN,now_ms())==PN_OK){status_page=false;shelf_mode=false;}}else start_reader();}}
    else if(shelf_mode && selection==PN_SHELF_LAYOUT){list_mode=!list_mode;shelf_focus=-1;show_shelf("",false);}
                else if(shelf_mode && selection==PN_SHELF_IMPORT && !recent_mode)begin_transfer(false);
    else if(shelf_mode && selection==PN_SHELF_TRANSFER)begin_transfer(false);
    else if(shelf_mode && selection==PN_SHELF_SEARCH){if(*search_query && !recent_mode){search_query[0]=0;show_shelf("",false);}else{pn_search_ui_open(&search_ui,NULL);show_search();}}
    else if(shelf_mode && selection==PN_SHELF_INDEX && !recent_mode && !*search_query)show_index();
    else if(shelf_mode && selection==PN_SHELF_MENU)begin_settings();
    else if(jump_ui.active){pn_status_t status=pn_jump_ui_event(&jump_ui,selection,now_ms(),present,NULL);if(status!=PN_OK && status!=PN_EMPTY && status!=PN_BUSY)ESP_LOGW(TAG,"Jump UI: %d",(int)status);}
    else if(fonts.active){(void)pn_font_ui_event(&fonts,selection,now_ms(),present,NULL);if(!fonts.active){pn_font_ui_close(&fonts);if(styles.active)(void)pn_style_ui_present(&styles,present,NULL);}}
    else if(toc.active){(void)pn_toc_ui_event(&toc,selection,now_ms(),present,NULL);}
    else if(selection==14 && reader_active() && !shelf_mode && !status_page && bookmarks.mode==PN_BUI_CLOSED && !styles.active && !toc.active && !fonts.active){pn_status_t status=epub.impl?pn_jump_ui_open_epub(&jump_ui,&epub,present,NULL):pn_jump_ui_open(&jump_ui,&reader,present,NULL);if(status!=PN_OK)ESP_LOGW(TAG,"Jump UI open: %d",(int)status);}
    else if(selection==13 && epub.impl && bookmarks.mode==PN_BUI_CLOSED && !styles.active){(void)pn_toc_ui_open(&toc,&epub,present,NULL);}
    else if(styles.active){pn_status_t status=pn_style_ui_event(&styles,selection,now_ms(),present,NULL);if(status!=PN_OK && status!=PN_EMPTY && status!=PN_BUSY)ESP_LOGW(TAG,"Style UI: %d",(int)status);}
    else if(selection==12 && reader_active() && bookmarks.mode==PN_BUI_CLOSED && !shelf_mode && !status_page){if(epub.impl)(void)pn_style_ui_open_epub(&styles,&epub,present,NULL);else (void)pn_style_ui_open(&styles,&reader,present,NULL);}
    else if(bookmarks.mode!=PN_BUI_CLOSED){pn_status_t status=pn_bookmark_ui_event(&bookmarks,selection,NULL,now_ms(),present,NULL);if(status!=PN_OK && status!=PN_EMPTY && status!=PN_BUSY)ESP_LOGW(TAG,"Bookmark UI: %d",(int)status);}
    else if(selection==10){if(epub.impl)(void)pn_bookmark_ui_open_epub(&bookmarks,&epub,present,NULL);else (void)pn_bookmark_ui_open(&bookmarks,&reader,present,NULL);}
    else if(selection==11){if(epub.impl)(void)pn_epub_app_bookmark_return(&epub,now_ms(),present,NULL);else (void)pn_reader_app_bookmark_return(&reader,now_ms(),present,NULL);}
    else if(shelf_mode && (selection==PN_SHELF_TAB_ALL || selection==PN_SHELF_TAB_RECENT || selection==PN_SHELF_HOME)){bool want_recent=selection==PN_SHELF_TAB_RECENT;if(selection==PN_SHELF_HOME || want_recent!=recent_mode){recent_mode=want_recent;search_query[0]=0;show_shelf("",false);}}
    else if(shelf_mode && selection==PN_SHELF_CONTINUE){
        // 继续阅读卡直接打开最近一本，不改变当前书架模式。/ The continue card opens the latest book directly without changing the shelf mode.
        if(shelf_covers && shelf_covers->has_last){const pn_catalog_item_t *last=&shelf_covers->last;
            if(last->format!=PN_BOOK_TXT && last->format!=PN_BOOK_EPUB)message("格式尚未接入","历史记录仍保留");
            else{selected_format=last->format;strcpy(selected_path,last->path);selected_identified=last->identified;selected_expected=last->expected;start_reader();}}
    }
    else if(shelf_mode && selection<6 && shelf_page)open_shelf_item(selection);else if(shelf_mode && shelf_page && shelf_page->count && (selection==PN_SHELF_NEXT || selection==PN_SHELF_PREVIOUS)){
        char cursor[PN_CATALOG_NAME_MAX];strcpy(cursor,shelf_page->items[selection==PN_SHELF_NEXT?shelf_page->count-1:0].name);show_shelf(cursor,selection==PN_SHELF_PREVIOUS);
    }else if(selection==8){reading_menu=true;refresh_network_name();paint_transfer(true);}
    else if(selection==9){selected_path[0]=0;start_reader();}
}
static void device_task(void *arg){
    (void)arg;esp_err_t error=nvs_flash_init();if(error!=ESP_OK){ESP_LOGE(TAG,"NVS unavailable: %s; preserving data",esp_err_to_name(error));vTaskDelete(NULL);return;}
    error=read_pico_init(&hardware);if(error!=ESP_OK){ESP_LOGE(TAG,"Board init failed: %s",esp_err_to_name(error));read_pico_deinit(&hardware);vTaskDelete(NULL);return;}
    int vcom=0;if(!hardware.pmu_ready || read_pico_pmu_vcom_get(&vcom)!=ESP_OK){ESP_LOGE(TAG,"Factory VCOM unavailable; display withheld");read_pico_deinit(&hardware);vTaskDelete(NULL);return;}
    epd_set_vcom((uint16_t)vcom);
    size_t available=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    size_t budget=available>256u*1024u?available-256u*1024u:0;if(budget>6u*1024u*1024u)budget=6u*1024u*1024u;
    if(pn_pool_init(&pool,budget,psram_alloc,psram_free,NULL)!=0){read_pico_deinit(&hardware);vTaskDelete(NULL);return;}
    pn_media_init(&sd_media);pn_media_init(&data_media);pn_media_init(&wallpaper_media);read_pico_pmu_drain_events();esp_fill_random(cover_salt,sizeof cover_salt);
    if(read_pico_pmu_report_ready()!=ESP_OK){ESP_LOGE(TAG,"PMU running handshake failed; display withheld");read_pico_deinit(&hardware);vTaskDelete(NULL);return;}
    start_reader();pn_reader_input_t input={0};pn_tap_t tap={0};int hold_hit=-1;uint64_t hold_start=0,hold_last=0;bool hold_fired=false;uint64_t last_card=0,last_key=0,last_ui_retry=0,boot=now_ms();
    for(;;){uint64_t now=now_ms();
        rails_idle_check(now);
        if(transfer.impl)tick_transfer(now);
        {static uint64_t last_battery;if(now-last_battery>=60000 || !last_battery){last_battery=now;pn_w_set_battery(battery_percent());}} // 状态带电量每分钟刷新一次 / Refresh the status-band battery once a minute
        if(!input_flags_loaded && data_ready && pn_settings_load_flags(&data_media,"/data/progress",&input_flags)==PN_OK)input_flags_loaded=true;
        input.config=(pn_reader_input_config_t){.left_hand=(input_flags&PN_INPUT_LEFT_HAND)!=0,.no_swipe=(input_flags&PN_INPUT_NO_SWIPE)!=0,.no_edge_tap=(input_flags&PN_INPUT_NO_EDGE_TAP)!=0,.no_keys=(input_flags&PN_INPUT_NO_KEYS)!=0};
        if(shelf_mode && shelf_covers && shelf_page && !transfer.impl && !wallpaper_ui.impl && !font_manage.impl && !settings_ui.impl && !locked && !status_page && !reading_menu && !touch_held)tick_covers();
        if(!transfer.impl && now-last_card>=250){last_card=now;read_pico_sd_info_t card={0};(void)read_pico_sd_get_info(&card);
            if(sd_media.available && !card.mounted){if(reader_active()){if(epub.impl)(void)pn_epub_app_media_lost(&epub);else (void)pn_reader_app_media_lost(&reader);(void)stop_reader();}else (void)pn_media_detach(&sd_media);selected_path[0]=0;pn_reader_input_cancel(&input);message("卡已移除","保留上次阅读位置\n插卡后点下方重试");}}
        if(now-last_key>=100){last_key=now;if(read_pico_pmu_take_key_short() && now-boot>=1000){pn_reader_input_cancel(&input);
                if(transfer.impl){transfer_return=true;transfer_lock=true;(void)pn_device_transfer_request_stop(&transfer);}
                else if(locked){locked=false;last_input=now;start_reader();}else if(stop_reader())lock_and_sleep("再按电源键继续阅读");}}
        // 无操作满5分钟自动锁屏；传输中由其停止流程负责。/ Auto-lock after five idle minutes; transfers handle their own stop flow.
        if(!locked && !transfer.impl && now-last_input>=AUTO_LOCK_MS && now-boot>=AUTO_LOCK_MS){pn_reader_input_cancel(&input);if(stop_reader())lock_and_sleep("再按电源键继续阅读");else last_input=now;}
        if(reader_active() && !locked){pn_status_t saved=epub.impl?pn_epub_app_tick(&epub,now_ms()):pn_reader_app_tick(&reader,now_ms());if(saved!=PN_OK && saved!=PN_BUSY)ESP_LOGW(TAG,"Pending progress save: %d",(int)saved);}
        if(bookmarks.mode!=PN_BUI_CLOSED && !bookmarks.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_bookmark_ui_present(&bookmarks,present,NULL);}
        if(toc.active && !toc.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_toc_ui_present(&toc,present,NULL);}
        if(styles.active && !styles.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_style_ui_present(&styles,present,NULL);}
        if(jump_ui.active && !jump_ui.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_jump_ui_present(&jump_ui,present,NULL);}
        if(styles.request_fonts && !locked){styles.request_fonts=false;(void)pn_font_ui_open(&fonts,&pool,epub.impl?NULL:&reader,epub.impl?&epub:NULL,NULL,present,NULL);}
        if(fonts.active && !fonts.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_font_ui_present(&fonts,present,NULL);}
        if(hardware.touch_ready){cst836u_touch_t touch={0};esp_err_t read=cst836u_read(hardware.touch,&touch);pn_reader_action_t action;touch_held=read==ESP_OK && touch.count>0;if(touch_held)last_input=now_ms();
            int hit=active_hit(touch.x,touch.y),selection=-1;
            // 步进键长按：按住满600 ms后每180 ms再走一档；松手时不再多走一档（docs/UI_UX.md第5节）。
            // Stepper hold: after 600 ms held, step again every 180 ms; releasing does not add one more step (docs/UI_UX.md section 5).
            {bool repeatable=(styles.active && hit>=PN_SUI_FIELD && hit<PN_SUI_FIELD+PN_SUI_FIELDS*2) || (jump_ui.active && hit>=PN_JUI_STEP && hit<PN_JUI_STEP+4);
             uint64_t tick=now_ms();
             if(touch_held && repeatable && hit==hold_hit){if(tick-hold_start>=600 && tick-hold_last>=180 && !locked){hold_last=tick;hold_fired=true;apply_selection(hit);}}
             else{hold_hit=touch_held && repeatable?hit:-1;hold_start=tick;hold_last=0;if(touch_held)hold_fired=false;}}
            if(pn_tap_feed(&tap,touch.count,hit,read==ESP_OK,&selection) && !locked){
                if(hold_fired)hold_fired=false;else apply_selection(selection);
                pn_reader_input_cancel(&input);
            }
            {int key=-1;pn_key_event_t event=pn_key_feed(&keys,touch.count,touch.x,touch.y,read==ESP_OK,now_ms(),&key);if(event!=PN_KEY_NONE && !locked){pn_reader_input_cancel(&input);handle_key(key,event);}}
            if(!transfer.impl && !wallpaper_ui.impl && !font_manage.impl && !settings_ui.impl && !reading_menu && !shelf_mode && !status_page && bookmarks.mode==PN_BUI_CLOSED && !styles.active && !jump_ui.active && !fonts.active && !toc.active && !toolbar_open && pn_reader_input_feed(&input,touch.count,touch.x,touch.y,read==ESP_OK,&action) && !locked){
                if(action==PN_APP_TOOLS)toolbar_show();
                else if(reader_active() && !status_page){pn_status_t status=active_step(action,now_ms());if(status!=PN_OK && status!=PN_EMPTY){ESP_LOGW(TAG,"Reader action: %d",(int)status);message("操作未完成","当前位置仍保留\n重试或按电源键返回");}}
                else start_reader();}}
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void app_main(void){
    // 字体/解析独占一个较大栈的worker，不在小型启动栈上运行。/ Run font/parsing on one larger-stack worker, not the small boot stack.
    if(xTaskCreatePinnedToCore(device_task,"pn_device",32768,NULL,5,NULL,1)!=pdPASS)ESP_LOGE(TAG,"Device worker creation failed");
}
