/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设备TXT接入：共享阅读器、只读VCOM、触摸、内部非破坏挂载。
 * English: device TXT wiring with shared reader, read-only VCOM, touch and non-destructive internal mounting.
 * 冻结：不格式化/擦NVS，不写SY标定；未读有效VCOM不推屏。
 * Frozen: no formatting/NVS erase or SY calibration writes; no presentation without valid VCOM.
 */
#include "pn_brand.h"
#include "pn_profile.h"
#include "pn_catalog_index.h"
#include "pn_brand_image.h"
#include "pn_reader_app.h"
#include "pn_turn_transition.h"
#include "pn_epub_app.h"
#include "pn_toc_ui.h"
#include "pn_reader_input.h"
#include "pn_reader_chrome.h"
#include "device_card_presence.h"
#include "pn_reader_toolbar.h"
#include "pn_widgets.h"
#include "pn_search_ui.h"
#include "pn_jump_ui.h"
#include "pn_transfer_hub.h"
#include "pn_book_actions.h"
#include "esp_vfs_fat.h"
#include "esp_app_desc.h"
#include "pn_focus.h"
#include "pn_sd_layout.h"
#include "pn_shelf_view.h"
#include "pn_wallpaper.h"
#include "pn_wallpaper_ui.h"
#include "pn_font_manage.h"
#include "pn_network_store.h"
#include "pn_settings_ui.h"
#include "pn_key.h"
#include "device_sleep.h"
#include "device_async.h"
#include "device_identity.h"
#include "pn_hardware.h"
#include "device_sd.h"
#include "pn_tap.h"
#include "pn_bookmark_ui.h"
#include "pn_style_ui.h"
#include "pn_font_ui.h"
#include "device_text.h"
#include "device_frame.h"
#include "device_power.h"
#include "device_transfer.h"
#include "device_time.h"
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
#include <time.h>
static const char *TAG="pico_nano";
static read_pico_handle_t hardware;
static pn_pool_t pool;
static pn_media_t sd_media;
static pn_device_sd_t sd_recovery; ///< TF挂载请求的异步完成状态 / Asynchronous completion state of the TF mount request
static pn_media_t data_media;
static pn_media_t wallpaper_media;
static bool wallpaper_ready;
static pn_wallpaper_ui_t wallpaper_ui;
static pn_wallpaper_mask_t lock_mask; ///< 锁屏时临时遮罩 / Temporary mask used only while locked
static pn_font_manage_t font_manage;
static pn_settings_ui_t settings_ui;
static pn_device_time_t time_sync;
static char time_notice[96];
static bool time_lock_pending;
static uint64_t time_poll_at;
static uint8_t input_flags; ///< 已保存翻页标志 / Saved page-turn flags
static bool input_flags_loaded;
static pn_shelf_prefs_t shelf_prefs={3,PN_SHELF_DEFAULT}; ///< 已保存书架外观 / Saved shelf appearance
static bool shelf_prefs_loaded;
static pn_key_t keys={.key=-1};
static bool home_mode=true; ///< 默认阅读首页，阅读返回保留来源 / Default reading home, preserving the reading return origin
static bool index_mode; ///< 字母跳转页 / Letter-index page
static int shelf_focus=-1; ///< 三键书架焦点（-1无）/ Three-key shelf focus (-1 none)
static bool back_to_settings; ///< 子页从设置页进入，返回时回设置页而不是书架 / The sub-page was opened from Settings, so Back returns there instead of the shelf
static bool fav_mode; ///< 书架“收藏”分类 / Shelf Favorites category
static size_t fav_start;static pn_favorites_t *favorites;
static bool actions_mode; ///< 书籍操作面板 / Book actions sheet
static pn_book_actions_t actions;
static bool hub_mode; ///< 传书一级页 / Transfer root tab
static pn_transfer_hub_info_t hub_info;static char hub_storage[64];
static bool search_mode; ///< 搜索输入页 / Search input page
static pn_search_ui_t search_ui; ///< 搜索页输入状态 / Search page input state
static char search_query[PN_CATALOG_QUERY_MAX+1]; ///< 生效中的搜索词，空表示不过滤 / Active search query, empty means unfiltered
static char jump_letter; ///< 下次书架查询从此字母起 / The next shelf query starts at this letter
static uint64_t last_input; ///< 最近一次触摸/按键，用于自动锁屏 / Latest touch or key, for auto-lock
#define AUTO_LOCK_MS (5u*60u*1000u)
static void lock_and_sleep(const char *hint);
static bool prepare_lock(void);
static void resume_reader(void);
static bool reader_active(void);
static bool reader_suspended; ///< 热挂起会话 / Warm suspended session
static bool management_home_pending; ///< 传输停止后回阅读首页 / Return home after transfer shutdown
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
static bool opening; ///< 开书控制门，产品owner串行管理 / Opening gate owned by the serial product owner
static atomic_bool deep_clean_pending; ///< 显式清屏意图，由显示owner消费 / Explicit cleaning intent consumed by the display owner
static pn_catalog_index_t catalog_index;
static bool recent_loaded;static uint64_t recent_epoch;
static pn_catalog_page_t *shelf_page;
static pn_recent_snapshot_t *recent_snapshot;
static bool recent_mode,selected_identified;
static size_t recent_start;
static bool list_mode; ///< 书架列表模式（每页5本）/ Shelf list mode (five per page)
/// 当前书架每页条数。/ Entries per shelf page right now.
static size_t shelf_per_page(void){return home_mode?3u:list_mode || (!recent_mode && !fav_mode && *search_query)?5u:(size_t)shelf_prefs.columns*2u;}
static pn_book_id_t selected_expected;
static char selected_path[PN_CATALOG_PATH_MAX];
static pn_catalog_item_t last_read_book; ///< 已成功关闭阅读的书快照，不借用正文句柄 / Snapshot of a successfully closed book, without borrowing a reader handle
static bool last_read_known; ///< 已有可信在读书身份 / A trusted in-progress book identity is available
static const char *const book_directory=PN_SD_MOUNT "/" PN_SD_BOOKS;
static uint64_t card_instance=1;
static pn_device_transfer_t transfer;
static bool reading_menu,transfer_return,transfer_lock;
static pn_transfer_view_t transfer_view,last_transfer_view;
static uint64_t last_transfer_draw;
static pn_cache_owner_t cover_cache_owner; ///< 封面预算owner / Cover-budget owner
static bool covers_inactive; ///< 非活动且不再借用时可回收 / Reclaim only when inactive and no longer borrowed
static pn_shelf_covers_t *shelf_covers;
static bool covers_dirty,touch_held;
static size_t cover_bytes(void *ctx){(void)ctx;return pn_alloc_size(shelf_covers);}
static void cover_drop(void *ctx){(void)ctx;if(covers_inactive){pn_free(shelf_covers);shelf_covers=NULL;covers_dirty=false;}}

static uint8_t cover_salt[16];
static const char *const cover_cache=PN_SD_MOUNT "/" PN_SD_COVER_CACHE;
/// 最近记录只作展示过滤；用户自行整理同名书后可关联新路径，原记录不因缺文件被清除。
/// Filter recent entries only for display; user-organized books can resolve by basename at the new path, without deleting missing-file records.
static void filter_recent_available(pn_recent_snapshot_t *snapshot){
    size_t kept=0;const char *prefix=PN_SD_MOUNT "/" PN_SD_BOOKS "/";
    for(size_t i=0;i<snapshot->count;i++){
        pn_recent_item_t *item=&snapshot->items[i];char path[PN_RECENT_PATH_MAX];
        const char *name=strrchr(item->path,'/');name=name?name+1:item->path;
        int n=snprintf(path,sizeof path,"%s%s",prefix,name);struct stat info;
        if(n<0 || (size_t)n>=sizeof path || stat(path,&info) || !S_ISREG(info.st_mode) || info.st_size<0 || (uint64_t)info.st_size!=item->source_size)continue;
        if(kept!=i)snapshot->items[kept]=*item;
        strcpy(snapshot->items[kept++].path,path);
    }
    snapshot->count=kept;
}
static void *psram_alloc(void *ctx,size_t size){(void)ctx;return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
static void psram_free(void *ctx,void *ptr){(void)ctx;heap_caps_free(ptr);}
static uint64_t now_ms(void){return (uint64_t)(esp_timer_get_time()/1000);}
#if defined(ESP_PLATFORM)
static void profile_task_stacks(const char *);
static uint64_t profile_clock(void *ctx){(void)ctx;return (uint64_t)esp_timer_get_time();}
static void profile_emit(void *ctx,const pn_profile_event_t *e){
    (void)ctx;
    bool timing=!strncmp(e->phase,"epub.model-",11) || !strncmp(e->phase,"epub.raster",11) || !strncmp(e->phase,"epub.layout",11) || !strcmp(e->phase,"epub.zip") || !strcmp(e->phase,"epub.publication") || !strncmp(e->phase,"storage.",8) || !strncmp(e->phase,"display.",8);
#ifndef CONFIG_PN_PROFILE
    if(e->status==PN_EMPTY || (!timing && strcmp(e->phase,"epub.open") && strcmp(e->phase,"content.stream-sha") && strcmp(e->phase,"epub.prepare") && strcmp(e->phase,"epub.next") && strcmp(e->phase,"epub.previous") && strcmp(e->phase,"epub.prefetch") && strcmp(e->phase,"font.layout-key")))return;
#endif
    ESP_LOGI(TAG,"profile phase=%s elapsed_us=%llu status=%d begin=%zu end=%zu peak=%zu attempts=%zu backend=device",e->phase,(unsigned long long)e->elapsed_us,(int)e->status,e->begin_bytes,e->end_bytes,e->peak_bytes,e->attempts);
#ifndef CONFIG_PN_PROFILE
    // 高频子阶段只记耗时，避免重复堆/栈遍历和串口输出拖慢响应。/ Frequent subphases log timing only, avoiding repeated heap/stack scans and serial overhead.
    if(timing)return;
#endif
    const unsigned caps[]={MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT};
    for(unsigned j=0;j<2;j++)ESP_LOGI(TAG,"heap phase=%s region=%s free=%zu minimum=%zu largest=%zu",e->phase,j?"psram":"internal",heap_caps_get_free_size(caps[j]),heap_caps_get_minimum_free_size(caps[j]),heap_caps_get_largest_free_block(caps[j]));
    #ifdef CONFIG_PN_PROFILE
    if(e->pool)for(unsigned j=0;j<PN_MEM_COUNT;j++){const pn_memory_stats_t *m=&e->pool->memory[j];ESP_LOGI(TAG,"memory phase=%s tag=%s used=%zu peak=%zu live=%zu allocations=%zu failures=%zu",e->phase,pn_memory_tag_name((pn_memory_tag_t)j),m->used,m->peak,m->live,m->allocations,m->failures);}
    #endif
    ESP_LOGI(TAG,"stack phase=%s owner=product minimum_bytes=%u",e->phase,(unsigned)uxTaskGetStackHighWaterMark(NULL));
    profile_task_stacks(e->phase);
    if(e->pool)for(unsigned k=0;k<PN_CACHE_COUNT;k++){const pn_cache_stats_t *c=&e->pool->cache_stats[k];ESP_LOGI(TAG,"cache phase=%s kind=%u bytes=%zu peak=%zu hits=%zu misses=%zu denied=%zu evictions=%zu reclaimed=%zu",e->phase,k,c->bytes,c->peak,c->hits,c->misses,c->denied,c->evictions,c->reclaimed);}
}
#endif
/// 高压轨空闲保活毫秒；断电需等放电、再上电要数十毫秒，连续翻页期间不应反复开关。/ Idle keep-alive for the HV rails; power-off waits for discharge and power-on takes tens of ms, so do not toggle them between consecutive page turns.
#define RAILS_IDLE_MS 8000u
/// 供数不足后退回的安全像素时钟(MHz)。/ Safe pixel clock (MHz) used after a line-queue underrun.
#define PCLK_SAFE_MHZ READ_PICO_EPD_PCLK_MIN_MHZ
/// 轨到期时刻(ms)，0表示已断电。/ Rail expiry time (ms); 0 means the rails are off.
static uint64_t rails_deadline_ms;
/// 显示owner发布清理回执，产品owner下次操作消费。/ Display owner publishes a cleaning receipt for the next product operation.
static atomic_bool full_clean_completed;
static atomic_bool reading_presentation; ///< 此次显示的用途，独立于输入页面状态 / Purpose of this presentation, independent of input view state
/// 一旦出现供数欠载就停用扫描期重叠，避免PSRAM争用反复触发恢复。/ Stop scan-time overlap after any line-queue underrun so PSRAM contention cannot keep triggering recovery.
static atomic_bool overlap_disabled;
/// 立即断开高压轨；未上电时无操作。/ Drop the HV rails now; no-op when already off.
static void rails_release(void){if(rails_deadline_ms){rails_deadline_ms=0;epd_poweroff();}pn_device_power_busy(PN_POWER_RAILS,false);}
/// 主循环调用：轨空闲满期后断电。/ Called by the main loop: power the rails down once the idle period has elapsed.
static void rails_idle_check(uint64_t now){if(rails_deadline_ms && now>=rails_deadline_ms)rails_release();}
/// 阅读工具栏是否显示，以及最近一次呈现帧的副本（关闭工具栏时原样恢复）。/ Whether the reading toolbar is showing, and a copy of the latest presented frame (restored as is when the toolbar closes).
static bool toolbar_open;
static uint8_t *page_copy;
static const char *reader_notice; ///< 下一次工具栏携带错误反馈 / Error feedback included in the next toolbar
static bool progress_save_failed; ///< 后台保存失败，不冒充翻页失败 / Background save failure is distinct from turn failure
static pn_focus_t focus_nav; ///< 当前页的三键焦点表 / Three-key focus table of the current page
/* 局部刷新：GL16时只推变化的外接矩形（逻辑坐标，epdiy按旋转换算）；变化超过45%或没有已知参考则整屏。
 * 已显示参考就是epdiy后缓冲（每次绘制后与前缓冲一致），不再另存一份406KiB逻辑副本。
 * Partial refresh: for GL16 push only the bounding box of what changed (logical coordinates, rotated by epdiy); more than 45% changed or no known reference means a full update.
 * The shown reference is epdiy's back buffer (equal to the front buffer after every draw), so no separate 406 KiB logical copy is kept. */
#define PARTIAL_MAX_PERCENT 45
static pn_device_async_t async_port;
#if defined(ESP_PLATFORM)
static void profile_task_stacks(const char *phase){
    if(async_port.input_task_handle)ESP_LOGI(TAG,"stack phase=%s owner=input minimum_bytes=%u",phase,(unsigned)uxTaskGetStackHighWaterMark(async_port.input_task_handle));
    if(async_port.display_task_handle)ESP_LOGI(TAG,"stack phase=%s owner=display minimum_bytes=%u",phase,(unsigned)uxTaskGetStackHighWaterMark(async_port.display_task_handle));
}
#endif
static pn_status_t present_panel(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)ctx;
    uint64_t prepared_at=now_ms();
    (void)prepared_at; // 无日志主机桩仍编译同源路径。/ Compile the same path with logging disabled in host stubs.
    if(profile==PN_REFRESH_DU)return PN_UNSUPPORTED;
    // 先转换到前缓冲，再与后缓冲（已显示参考）比较；未知参考时不比较。/ Convert into the front buffer first, then compare with the back buffer (shown reference); skip comparison without a known reference.
    pn_status_t converted=pn_device_frame_copy(frame,hardware.framebuffer,684u*1216u/2u);if(converted!=PN_OK)return converted;
    uint64_t converted_at=now_ms();(void)converted_at;
    pn_device_box_t dirty={0};bool known=panel_known && pn_device_frame_dirty(hardware.framebuffer,hardware.hl.back_fb,684u*1216u/2u,8,&dirty)==PN_OK;
    EpdRect changed={.x=dirty.x,.y=dirty.y,.width=dirty.width,.height=dirty.height};
    uint64_t dirty_at=now_ms();(void)dirty_at;
    if(profile==PN_REFRESH_GL16 && known && !changed.width && !atomic_load(&deep_clean_pending))return PN_OK;
    read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FULL);epd_poweron();
    enum EpdDrawMode mode=profile==PN_REFRESH_GC16?MODE_GC16:MODE_GL16;
    // 软刷新累计满额时改用GC16，目标帧与区域不变。/ When the soft-refresh budget is spent, switch to GC16 with the same target frame and area.
    // 阅读页数设置不能让管理页每次点击都清屏；管理页独立使用较长保洁间隔。/ Reading-page preferences must not clear management pages on every tap; use a separate maintenance interval.
    static unsigned management_refreshes;
    bool body_refresh=atomic_load(&reading_presentation);
    const char *reason=mode==MODE_GC16?"requested":body_refresh?"reading":"management";
    (void)reason;
    if(!body_refresh && mode!=MODE_GC16 && management_refreshes+1>=24u){mode=MODE_GC16;reason="maintenance";}
    enum EpdDrawError result;
    uint64_t panel_at=now_ms();
    (void)panel_at;
    bool deep_clean=atomic_exchange(&deep_clean_pending,false);
    if(!panel_known || deep_clean){reason=deep_clean?"manual":"unknown-reference";mode=MODE_GC16;epd_clear();result=epd_hl_update_screen_from_white(&hardware.hl,MODE_GC16,25);}
    // 已知参考帧的周期GC16直接全屏转换；无需先另做一次白场清除。/ Periodic GC16 with a known reference directly updates every pixel without an extra white-field clear.
    else if(mode==MODE_GC16)result=epd_hl_update_screen_full(&hardware.hl,MODE_GC16,25);
    else if(body_refresh){
        // 页脚未变时只全像素清理正文带，不驱动灰色进度轨；页脚变化时合并为一次整屏更新。
        // 面板旋转90°，逻辑页脚是横跨全部物理行的窄列，单独再画一次也要扫完整帧（真机约+400ms，扫描877ms）。
        // Without footer changes clean only the body band, leaving the gray rail undriven; with footer changes merge into one full-screen update.
        // The panel is rotated 90 degrees, so the logical footer is a narrow column across every physical row and a separate draw costs a whole extra scan (about +400 ms on device, 877 ms scans).
        const int footer_band=PN_READER_FOOTER_BAND_Y;
        bool footer=changed.width && changed.y+changed.height>footer_band;
        result=epd_hl_update_area_full(&hardware.hl,MODE_GL16,25,(EpdRect){0,0,684,footer?1216:footer_band});
    }
    else{
        EpdRect box=changed;int limit=actions_mode && !actions.confirming?60:PARTIAL_MAX_PERCENT;
        bool partial=!body_refresh && mode==MODE_GL16 && known && (long)box.width*box.height*100<=684L*1216L*limit;
        // 不变帧已在上电及转换前返回，不消耗软刷新额度。/ Unchanged frames returned before power-up/conversion without consuming the soft-refresh allowance.
        if(partial && !box.width)result=EPD_DRAW_SUCCESS;
        else if(partial)result=epd_hl_update_area_full(&hardware.hl,mode,25,box);
        else result=epd_hl_update_screen_full(&hardware.hl,mode,25);
    }
    if(result&EPD_DRAW_EMPTY_LINE_QUEUE){
        // 供数不足：退回安全时钟，白场重建参考帧，目标帧保留在framebuffer。/ Underrun: fall back to the safe clock and rebuild the reference frame from white; the target stays in the framebuffer.
        ESP_LOGW(TAG,"Line queue underrun, pclk back to %d MHz",PCLK_SAFE_MHZ);
        read_pico_epd_set_pclk(PCLK_SAFE_MHZ);read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FULL);
        epd_clear();result=epd_hl_update_screen_from_white(&hardware.hl,MODE_GC16,25);mode=MODE_GC16;reason="underrun";atomic_store(&overlap_disabled,true);
    }
    uint64_t scanned_at=now_ms();(void)scanned_at;
    panel_known=result==EPD_DRAW_SUCCESS;
    if(panel_known){
        if(mode==MODE_GC16){management_refreshes=0;atomic_store(&full_clean_completed,true);}
        else if(!body_refresh)management_refreshes++;
    }else if(deep_clean)atomic_store(&deep_clean_pending,true);
    // epdiy已把前缓冲写入后缓冲；失败时panel_known=false，下次从白场整屏重建。/ epdiy already copied the front buffer into the back buffer; on failure panel_known=false forces a full rebuild from white next time.
    // 成功则保活到空闲期满；失败立即断电。/ Keep the rails up until idle expiry on success; drop them at once on failure.
    rails_deadline_ms=now_ms()+RAILS_IDLE_MS;pn_device_power_busy(PN_POWER_RAILS,true);if(!panel_known)rails_release();
    if(!panel_known)ESP_LOGE(TAG,"Panel present failed: %u",(unsigned)result);
    ESP_LOGI(TAG,"Present mode=%u reason=%s body=%d prepare=%llu ms panel=%llu ms pool=%u/%u",(unsigned)mode,reason,body_refresh,(unsigned long long)(panel_at-prepared_at),(unsigned long long)(now_ms()-panel_at),(unsigned)pool.used,(unsigned)pool.limit);
    ESP_LOGI(TAG,"Display stages dirty=%llu convert=%llu power=%llu scan=%llu remember=%llu ms",(unsigned long long)(dirty_at-converted_at),(unsigned long long)(converted_at-prepared_at),(unsigned long long)(panel_at-dirty_at),(unsigned long long)(scanned_at-panel_at),(unsigned long long)(now_ms()-scanned_at));
    return panel_known?PN_OK:PN_IO;
}
static pn_status_t present_impl(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)ctx;
    // 池只由产品worker分配；显示任务只写epdiy自有缓冲。/ Only the product worker allocates; display writes only epdiy-owned buffers.
    if(!reader_active()){pn_free(page_copy);page_copy=NULL;}
    bool backup=reader_active() && !locked && !toolbar_open && frame->width==684 && frame->height==1216 && frame->stride>=342;
    if(backup && !page_copy)page_copy=pn_alloc_tagged(&pool,PN_MEM_FRAME,342u*1216u);
    pn_status_t status=async_port.panel?pn_device_async_present(&async_port,frame,profile):present_panel(NULL,frame,profile);
    if(status==PN_OK && backup && page_copy && frame->pixels!=page_copy)for(int y=0;y<1216;y++)memcpy(page_copy+(size_t)y*342,frame->pixels+(size_t)y*frame->stride,342);
    return status;
}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    pn_profile_span_t span;pn_profile_begin(&span,&pool,"display.receipt");
    pn_status_t status=present_impl(ctx,frame,profile);pn_profile_end(&span,status);return status;
}
static bool reader_active(void){return reader.impl || epub.impl;}
/// 仅阅读翻页的扫描期间为待确认页准备下一页。/ Prepare the page after the pending one only during reading-turn scans.
static pn_status_t reader_overlap(void *ctx){(void)ctx;
    if(atomic_load(&overlap_disabled) || !atomic_load(&reading_presentation) || !reader_active())return PN_EMPTY;
    return epub.impl?pn_epub_app_overlap(&epub):pn_reader_app_overlap(&reader);}
static pn_status_t active_step(pn_reader_action_t action,uint64_t now){
    uint64_t started=now_ms();(void)started;
    pn_reader_app_refresh_period(&reader,pn_settings_refresh_pages(input_flags));
    pn_epub_app_refresh_period(&epub,pn_settings_refresh_pages(input_flags));
    if(atomic_exchange(&full_clean_completed,false)){pn_reader_app_refresh_reset(&reader);pn_epub_app_refresh_reset(&epub);}
    pn_frame_t before;pn_turn_t effect={.pool=&pool,.style=(pn_turn_style_t)((input_flags&PN_INPUT_TURN_MASK)>>6),.reverse=action==PN_APP_PREVIOUS,.present=present};
    if((action==PN_APP_NEXT || action==PN_APP_PREVIOUS) && panel_known && page_copy && pn_frame_bind(&before,page_copy,342u*1216u,684,1216))effect.before=&before;
    else effect.style=PN_TURN_NONE;
    atomic_store(&reading_presentation,true);
    pn_status_t status=epub.impl?pn_epub_app_step(&epub,action,now,pn_turn_present,&effect):pn_reader_app_step(&reader,action,now,pn_turn_present,&effect);
    atomic_store(&reading_presentation,false);
    ESP_LOGI(TAG,"Reader action=%u status=%d elapsed=%llu ms",(unsigned)action,(int)status,(unsigned long long)(now_ms()-started));
    // 翻页诊断区分实际EOF与失败，不把百分比或未确认候选页当作文件结束。/ Turn diagnostics distinguish EOF from failure without treating a percentage or unconfirmed candidate as EOF.
    if(reader.impl && (action==PN_APP_NEXT || action==PN_APP_PREVIOUS)){
        pn_txt_progress_t at;unsigned basis=0;
        if(pn_reader_app_progress(&reader,&at)==PN_OK && pn_reader_app_percent(&reader,&basis)==PN_OK)
            ESP_LOGI(TAG,"TXT turn action=%u status=%d offset=%llu/%llu progress=%u",(unsigned)action,(int)status,(unsigned long long)at.source_offset,(unsigned long long)at.source_size,basis);
    }
    return status;
}
static pn_status_t active_close(uint64_t now){recent_loaded=false;pn_status_t status=epub.impl?pn_epub_app_close(&epub,now):pn_reader_app_close(&reader,now);if(status==PN_OK){reader_notice=NULL;progress_save_failed=false;}return status;}
static void message(const char *title,const char *detail){
    reading_menu=false;back_to_settings=false;
    pn_toc_ui_close(&toc);
    pn_bookmark_ui_cancel(&bookmarks);
    pn_style_ui_close(&styles);pn_font_ui_close(&fonts);pn_wallpaper_ui_close(&wallpaper_ui);pn_font_manage_close(&font_manage);pn_settings_ui_close(&settings_ui);
    status_page=true;shelf_mode=false;index_mode=false;search_mode=false;hub_mode=false;actions_mode=false;
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,28);
    // 状态页：产品名、标题、按行说明，底部圆角“重试”按钮（点击区y>=1070）。/ Status page: product name, title, line-by-line explanation and a rounded Retry button at the bottom (touch area y>=1070).
    if(status==PN_OK){pn_frame_clear(&frame,15);status=pn_w_text(&font,&frame,PN_BRAND_NAME,32,44,620,PN_ALIGN_LEFT);}
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
    status_page=true;shelf_mode=false;index_mode=false;search_mode=false;hub_mode=false;actions_mode=false;
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,PN_WALLPAPER_BYTES);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_lock_selection_t selection={.mode=PN_LOCK_DEFAULT,.hint=true};
    pn_status_t status=pn_frame_bind(&frame,pixels,PN_WALLPAPER_BYTES,PN_WALLPAPER_WIDTH,PN_WALLPAPER_HEIGHT)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK && mount_wallpaper()){pn_wallpaper_store_t store;pn_media_lease_t lease;
        if(pn_wallpaper_store_init(&store,&wallpaper_media,"/wallpaper/lock.a","/wallpaper/lock.b")==PN_OK && pn_media_acquire(&wallpaper_media,PN_MEDIA_READ,&lease)==PN_OK){
            pn_status_t loaded=pn_wallpaper_load(&store,&lease,&selection,&frame);(void)pn_media_release(&wallpaper_media,&lease);
            if(loaded!=PN_OK){if(loaded!=PN_EMPTY)ESP_LOGW(TAG,"Wallpaper record unusable: %d; default lock screen",(int)loaded);selection=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=true};}}}
    if(status==PN_OK && selection.mode==PN_LOCK_MASK){
        // 只在锁屏解码/合成，底图取成功正文副本，绝不修改阅读器帧。 / Decode/composite only while locking, using a confirmed text copy without modifying reader pixels.
        pn_status_t mask=PN_EMPTY;
        if(reader_suspended && page_copy && pn_sd_resource_path(selection.source,PN_SD_WALLPAPERS)){
            mask=pn_wallpaper_mask_open_cached(&lock_mask,&pool,&sd_media,&selection,PN_SD_MOUNT "/" PN_SD_MASK_CACHE);
            if(mask==PN_OK){memcpy(frame.pixels,page_copy,PN_WALLPAPER_BYTES);mask=pn_wallpaper_mask_paint(&lock_mask,&frame);}
        }
        pn_wallpaper_mask_close(&lock_mask);
        if(mask!=PN_OK)selection=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=true};
    }
    if(selection.mode==PN_LOCK_SIMPLE || selection.mode==PN_LOCK_BOOK)selection=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=true};
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,32);
    if(status==PN_OK)status=pn_lock_render(&selection,&font,hint,&frame);
    if(status==PN_OK)status=present(NULL,&frame,PN_REFRESH_GC16);
    pn_font_close(&font);pn_free(pixels);
    if(status!=PN_OK){ESP_LOGE(TAG,"Lock screen unavailable: %d",(int)status);message("已锁屏",hint);}
}
static bool stop_reader(void){
    if(!reader_active()){pn_toc_ui_close(&toc);pn_bookmark_ui_cancel(&bookmarks);pn_style_ui_close(&styles);pn_jump_ui_close(&jump_ui);pn_font_ui_close(&fonts);return true;}
    pn_book_id_t id;bool identified=(epub.impl?pn_epub_app_identity(&epub,&id):pn_reader_app_identity(&reader,&id))==PN_OK;
    pn_status_t status=active_close(now_ms());
    if(status!=PN_OK && reader_active()){ESP_LOGE(TAG,"Save barrier failed: %d; session retained",(int)status);message("保存失败","仍保留当前位置\n修复内部存储后重试");return false;}
    if(status==PN_OK && identified){memset(&last_read_book,0,sizeof last_read_book);last_read_book.format=selected_format;last_read_book.expected=id;last_read_book.identified=true;
        snprintf(last_read_book.path,sizeof last_read_book.path,"%s",selected_path);const char *name=strrchr(selected_path,'/');name=name?name+1:selected_path;
        size_t length=strlen(name);if(length<sizeof last_read_book.name)memcpy(last_read_book.name,name,length+1); // 超长名保留完整路径/身份，不切断UTF8。/ Keep full path/identity for overlong names without splitting UTF-8.
        struct stat info;if(stat(selected_path,&info)==0 && info.st_size>=0)last_read_book.size=(uint64_t)info.st_size;last_read_known=true;}
    reader_suspended=false;pn_toc_ui_close(&toc);
    pn_bookmark_ui_cancel(&bookmarks);pn_style_ui_close(&styles);pn_jump_ui_close(&jump_ui);pn_font_ui_close(&fonts);return true;
}
/* 状态带时间：开机或手动联网对时后读取已确认的PMU UTC，之后按开机计时推算；锁屏不显示时间。时区固定为UTC+8。
 * Status-band clock: read confirmed PMU UTC at boot or after explicit network synchronization, then advance by uptime; the lock screen shows no time. The time zone is fixed at UTC+8. */
#define CLOCK_UTC_OFFSET (8*3600)
static bool clock_known;static uint32_t clock_base_sec;static uint64_t clock_base_ms;
static void clock_read_boot(uint64_t now){
    if(read_pico_pmu_cmd(PMU_CMD_TIME_GET,NULL,0)!=ESP_OK)return;
    const pmu_snapshot_t *snapshot=read_pico_pmu_get();
    if(!snapshot || !snapshot->time_ok || !snapshot->time_synced || snapshot->unix_sec<1700000000u)return;
    clock_base_sec=snapshot->unix_sec;clock_base_ms=now;clock_known=true;
}
static void clock_tick(uint64_t now){
    if(!clock_known){pn_w_set_clock(NULL);return;}
    uint64_t t=(uint64_t)clock_base_sec+(now-clock_base_ms)/1000u+CLOCK_UTC_OFFSET;
    char text[8];unsigned hh=(unsigned)((t/3600u)%24u),mm=(unsigned)((t/60u)%60u);
    text[0]=(char)('0'+hh/10);text[1]=(char)('0'+hh%10);text[2]=':';text[3]=(char)('0'+mm/10);text[4]=(char)('0'+mm%10);text[5]=0;
    pn_w_set_clock(text);
}
/// 日期时间显示只用已确认UTC与单调计时，不拿编译时间猜日期。 / Display dates only from confirmed UTC and monotonic elapsed time, never compilation time.
static void update_time_ui(bool redraw){
    if(!settings_ui.impl)return;
    pn_settings_time_t value={.running=time_sync.impl!=NULL,.available=*saved_network!=0};
    if(clock_known){uint64_t ms=now_ms(),utc=(uint64_t)clock_base_sec+(ms>=clock_base_ms?(ms-clock_base_ms)/1000u:0);if(utc<=4102444799u)(void)pn_settings_time_format(&value,(uint32_t)utc);}
    snprintf(value.message,sizeof value.message,"%s",time_notice);pn_settings_ui_set_time(&settings_ui,&value);
    if(redraw && pn_settings_ui_screen(&settings_ui)==5)(void)pn_settings_ui_present(&settings_ui,present,NULL);
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
/// 书架选项（绘制与命中共用）。/ Shelf options shared by drawing and hit testing.
static pn_shelf_options_t shelf_options(void){
    bool query=!recent_mode && !fav_mode && *search_query;
    return (pn_shelf_options_t){.home=home_mode,.battery_percent=-1,.columns=shelf_prefs.columns,.style=shelf_prefs.style,.actions=actions_mode?&actions:NULL,.search=true,.import_tile=true,.layout_toggle=true,.favorites_tab=true,.favorites=fav_mode,.list_mode=list_mode || query,.query=query};
}
/// 读收藏夹到favorites（没有记录为空表）。/ Load favorites (an empty list without a record).
static pn_status_t favorites_load(void){
    if(!data_ready)return PN_UNSUPPORTED;
    if(!favorites)favorites=pn_alloc_tagged(&pool,PN_MEM_UI,sizeof *favorites);
    if(!favorites)return PN_NO_MEMORY;
    pn_media_lease_t lease={0};pn_status_t status=pn_media_acquire(&data_media,PN_MEDIA_READ,&lease);
    pn_journal_files_t files;pn_journal_io_t io;
    if(status==PN_OK)status=pn_favorites_files(&files,&data_media,&lease,"/data/progress",&io);
    if(status==PN_OK)status=pn_favorites_load(&io,&pool,favorites);
    if(lease.ticket)(void)pn_media_release(&data_media,&lease);
    if(status==PN_EMPTY){memset(favorites,0,sizeof *favorites);status=PN_OK;}
    return status;
}
static bool favorites_valid; ///< 收藏快照有效，修改后失效 / Valid favorite snapshot, invalidated after writes
/// 页面内复用收藏快照，失败允许下次重试。/ Reuse favorites within a page; failures can be retried.
static pn_status_t favorites_ensure(void){
    if(favorites_valid)return PN_OK;
    pn_status_t status=favorites_load();favorites_valid=status==PN_OK;return status;
}
static pn_status_t draw_shelf(pn_refresh_t profile){
    ESP_LOGI(TAG,"Shelf draw profile=%u home=%u covers_dirty=%u",(unsigned)profile,(unsigned)home_mode,(unsigned)covers_dirty);
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,24);
    if(status==PN_OK){pn_w_set_battery(battery_percent());pn_shelf_options_t options=shelf_options();if(favorites_ensure()==PN_OK)options.favorite_items=favorites;status=pn_shelf_render_with_font_file_ex(shelf_page,&font,&frame,shelf_focus,recent_mode,true,shelf_covers,&pool,&sd_media,NULL,&options);}
    // 书籍操作面板叠在书架上（删除确认为整页）。/ The book actions sheet overlays the shelf (the delete confirmation is a full page).
    if(status==PN_OK)status=present(NULL,&frame,profile);
    pn_font_close(&font);pn_free(pixels);return status;
}
/// 空闲时解码一本封面；整页完成后一次GL16重绘，避免逐本闪屏。/ Decode one cover per idle tick; redraw once with GL16 after the page completes to avoid per-book flashing.
/// 返回本轮是否做了封面/重画工作。/ Return whether this round did cover or redraw work.
static bool tick_covers(void){
    // 邻页/字形/章节缓存可按需回收，计入封面解码预算；封面缓存本身不计。/ Neighbor/glyph/chapter caches are reclaimable on demand and count toward the cover budget; the cover cache itself does not.
    size_t free_bytes=(pool.limit>pool.used?pool.limit-pool.used:0)+pn_cache_reclaimable(&pool,(1u<<PN_CACHE_NEIGHBOR)|(1u<<PN_CACHE_GLYPH)|(1u<<PN_CACHE_CHAPTER)),reserve=128u*1024u;bool changed=false;
    pn_status_t status=pn_shelf_covers_step(shelf_covers,shelf_page,&pool,&sd_media,cover_cache,cover_salt,free_bytes>reserve?free_bytes-reserve:1,&changed);
    if(changed)covers_dirty=true;
    if(status==PN_EMPTY && covers_dirty){pn_status_t shown=draw_shelf(PN_REFRESH_GL16);if(shown==PN_OK)covers_dirty=false;else if(shown!=PN_CANCELLED)ESP_LOGW(TAG,"Cover redraw failed");}
    else if(status!=PN_OK && status!=PN_EMPTY && status!=PN_BUSY && status!=PN_CANCELLED){ESP_LOGW(TAG,"Cover queue stopped: %d",(int)status);pn_shelf_covers_reset(shelf_covers,NULL);covers_dirty=false;}
    return status==PN_OK || changed;
}
/// 最近记录在本owner内复用，关闭阅读或换卡后重载。/ Reuse recents within this owner; reload after closing a reader or changing media.
static pn_status_t load_recent(void){
    if(recent_loaded && recent_epoch==sd_media.epoch)return PN_OK;
    if(!recent_snapshot)recent_snapshot=pn_alloc_tagged(&pool,PN_MEM_UI,sizeof *recent_snapshot);
    pn_media_lease_t lease={0};pn_status_t status=recent_snapshot?pn_media_acquire(&data_media,PN_MEDIA_READ,&lease):PN_NO_MEMORY;
    pn_journal_files_t files;pn_journal_io_t io;
    if(status==PN_OK)status=pn_recent_files(&files,&data_media,&lease,"/data/progress",&io);
    if(status==PN_OK)status=pn_recent_load(&io,&pool,recent_snapshot);
    if(lease.ticket)(void)pn_media_release(&data_media,&lease);
    if(status==PN_EMPTY){memset(recent_snapshot,0,sizeof *recent_snapshot);status=PN_OK;}
    if(status==PN_OK){filter_recent_available(recent_snapshot);recent_loaded=true;recent_epoch=sd_media.epoch;}
    return status;
}
/// 取最近阅读第一本作为“继续阅读”卡；读取失败或没有记录时不显示卡。/ Use the first recent entry as the continue-reading card; show none when unreadable or empty.
static void refresh_last(void){
    if(!shelf_covers)return;
    pn_catalog_page_t *tmp=pn_alloc_tagged(&pool,PN_MEM_UI,sizeof *tmp);bool found=false;
    if(tmp && data_ready){
        pn_status_t status=load_recent();
        // 书架条目的“进度 · 格式”行用同一份记录。/ The shelf entries' progress line uses the same snapshot.
        if(status==PN_OK && shelf_page)pn_catalog_apply_recent(shelf_page,recent_snapshot);
        if(status==PN_OK && recent_snapshot->count && pn_catalog_recent_page(recent_snapshot,0,tmp)==PN_OK && tmp->count){pn_shelf_covers_set_last(shelf_covers,&tmp->items[0]);found=true;}
    }
    if(!found)pn_shelf_covers_set_last(shelf_covers,NULL);
    pn_free(tmp);
}
static void show_shelf(const char *cursor,bool previous){
    pn_wallpaper_mask_close(&lock_mask);
    if(!shelf_page){shelf_page=pn_alloc_tagged(&pool,PN_MEM_UI,sizeof *shelf_page);if(!shelf_page){message("内存不足","稍后重试");return;}}
    pn_catalog_page_t *next=pn_alloc_tagged(&pool,PN_MEM_UI,sizeof *next);if(!next){message("内存不足","稍后重试");return;}
    pn_media_lease_t lease={0};pn_status_t status;
    if(!home_mode && fav_mode){
        status=favorites_load();
        size_t start=!*cursor?0:previous?(fav_start>=shelf_per_page()?fav_start-shelf_per_page():0):fav_start+shelf_per_page();
        if(status==PN_OK && start>=favorites->count && start){pn_free(next);return;}
        if(status==PN_OK){status=pn_catalog_favorites_page(favorites,start,shelf_per_page(),next);if(status==PN_OK)fav_start=start;}
        if(status==PN_OK && recent_snapshot)pn_catalog_apply_recent(next,recent_snapshot);
        if(status!=PN_OK){fav_mode=false;pn_free(next);message("收藏读取失败","记录保留，点下方回书架");return;}
    }else if(home_mode || recent_mode){
        status=load_recent();
        size_t start=(home_mode || !*cursor)?0:previous?(recent_start>=shelf_per_page()?recent_start-shelf_per_page():0):recent_start+shelf_per_page();
        if(status==PN_OK && start>=recent_snapshot->count && start){pn_free(next);return;}
        if(status==PN_OK){status=pn_catalog_recent_page_n(recent_snapshot,start,shelf_per_page(),next);if(status==PN_OK)recent_start=start;}
        if(status!=PN_OK){recent_mode=false;pn_free(next);message("历史读取失败","记录保留，点下方回书架");return;}
    }else{
        status=pn_media_acquire(&sd_media,PN_MEDIA_READ,&lease);
        if(status==PN_OK)status=pn_catalog_index_page(&catalog_index,&lease,cursor,previous,jump_letter,*search_query?search_query:NULL,shelf_per_page(),next);
        if(status==PN_EMPTY)status=*search_query?(previous?pn_catalog_search_page_before_n(&sd_media,&lease,book_directory,search_query,cursor,shelf_per_page(),next):pn_catalog_search_page_n(&sd_media,&lease,book_directory,search_query,cursor,shelf_per_page(),next)):jump_letter?pn_catalog_page_from_n(&sd_media,&lease,book_directory,jump_letter,shelf_per_page(),next):previous?pn_catalog_page_before_n(&sd_media,&lease,book_directory,cursor,shelf_per_page(),next):pn_catalog_page_n(&sd_media,&lease,book_directory,cursor,shelf_per_page(),next);
        jump_letter=0;
        if(lease.ticket)(void)pn_media_release(&sd_media,&lease);
    }
    if(status==PN_OK && (next->count || !*cursor)){*shelf_page=*next;shelf_focus=-1;}
    pn_free(next);
    // 换页先用缓存封面，首帧即完整；未命中的留给空闲逐条解码。/ Use cached covers on page change so the first paint is complete; misses decode later per idle tick.
    covers_inactive=false;
    if(status==PN_OK && !shelf_covers)shelf_covers=pn_cache_alloc(&pool,PN_CACHE_COVER,PN_MEM_UI,sizeof *shelf_covers);
    if(status==PN_OK && shelf_covers){bool changed;refresh_last();pn_shelf_covers_reset(shelf_covers,shelf_page);covers_dirty=false;
        if(pn_shelf_covers_cached(shelf_covers,shelf_page,&pool,&sd_media,cover_cache,&changed)!=PN_OK)pn_shelf_covers_reset(shelf_covers,NULL);}
    if(status==PN_OK)status=draw_shelf(panel_known?PN_REFRESH_GL16:PN_REFRESH_GC16);
    if(status!=PN_OK){message("读取书架失败","当前位置仍保留，重试");return;}
    status_page=false;shelf_mode=true;locked=false;
}
/// 缓存预算查询由设备port提供，不用TF大小推断内部状态容量。/ The device port reports cache capacity without inferring internal-state space from TF size.
static pn_status_t index_space(void *ctx,uint64_t *available){(void)ctx;size_t total=0,used=0;if(esp_littlefs_info("data",&total,&used)!=ESP_OK || used>total)return PN_IO;*available=total-used;return PN_OK;}
static pn_status_t show_loading(const char *path){
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,342u*1216u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,342u*1216u,684,1216)?pn_font_open(&font,&pool,&source,28):PN_NO_MEMORY;
    const char *title=strrchr(path,'/');title=title?title+1:path;
    if(status==PN_OK)status=pn_reader_loading_render(&font,&frame,title);
    if(status==PN_OK)status=present(NULL,&frame,panel_known?PN_REFRESH_GL16:PN_REFRESH_GC16);
    pn_font_close(&font);pn_free(pixels);return status;
}
static void start_reader_impl(void){
    if(transfer.impl)return;
    if(!stop_reader())return;
    pn_wallpaper_mask_close(&lock_mask);
    if(!data_ready){data_ready=mount_data();if(!data_ready){message("内部存储不可用","保留数据，未格式化\n请先初始化数据分区");return;}}
    if(!shelf_prefs_loaded && pn_settings_load_shelf(&data_media,"/data/progress",&shelf_prefs)==PN_OK)shelf_prefs_loaded=true;
    if(!input_flags_loaded && pn_settings_load_flags(&data_media,"/data/progress",&input_flags)==PN_OK)input_flags_loaded=true;
    bool waiting=sd_recovery.pending;pn_status_t recovered=pn_device_sd_prepare(&sd_recovery,&sd_media,now_ms());
    if(recovered!=PN_OK){
        if(recovered==PN_BUSY){if(!waiting)message(sd_recovery.pending?"正在读取TF卡":"卡仍被占用",sd_recovery.pending?"请稍后\n未格式化":"等待旧文件关闭后重试");}
        else if(recovered==PN_STALE_MEDIA)message("请插入TF卡","书籍放 piconano/books\n字体放 piconano/fonts");
        else{ESP_LOGW(TAG,"TF prepare failed: %d",(int)recovered);message("TF卡不可用","未格式化\n检查卡后重试");}
        return;
    }
    if(!sd_media.available){pn_status_t s=pn_media_attach(&sd_media,card_instance++);if(s!=PN_OK){message("卡仍被占用","等待旧文件关闭后重试");return;}}
    if(!*selected_path){show_shelf("",false);return;}
    // 缺失或旧目录的书只退回可操作书库，不反复进入重试错误页；持久记录保留。/ Missing books or legacy paths return to a usable library without an error loop; durable records stay intact.
    const char *prefix=PN_SD_MOUNT "/" PN_SD_BOOKS "/";struct stat selected_info;
    if(strncmp(selected_path,prefix,strlen(prefix)) || stat(selected_path,&selected_info) || !S_ISREG(selected_info.st_mode)){
        ESP_LOGW(TAG,"Selected book unavailable; returning to canonical library");selected_path[0]=0;selected_identified=false;home_mode=false;show_shelf("",false);return;
    }
    const char *font=NULL;
    covers_inactive=true;
    pn_status_t loading=pn_reader_loading_needed(selected_format==PN_BOOK_TXT,(uint64_t)selected_info.st_size)?show_loading(selected_path):PN_OK;
    if(loading==PN_CANCELLED)return;
    if(loading!=PN_OK)ESP_LOGW(TAG,"Opening feedback unavailable: %d",(int)loading);
    uint64_t opened_at=now_ms();(void)opened_at;
    pn_status_t status=selected_format==PN_BOOK_EPUB?pn_epub_app_open_on_media(&epub,&pool,&sd_media,selected_path,font,"/data/progress",44,now_ms()):pn_reader_app_open_on_media(&reader,&pool,&sd_media,selected_path,font,"/data/progress",44,now_ms());
    ESP_LOGI(TAG,"Book prepare format=%u status=%d elapsed=%llu ms",(unsigned)selected_format,(int)status,(unsigned long long)(now_ms()-opened_at));
    if(status==PN_OK && selected_identified){pn_book_id_t actual;status=epub.impl?pn_epub_app_identity(&epub,&actual):pn_reader_app_identity(&reader,&actual);if(status==PN_OK && memcmp(actual.sha256,selected_expected.sha256,32))status=PN_STALE_JOB;}
    if(status==PN_OK)status=active_step(PN_APP_OPEN,now_ms());
    if(status==PN_CANCELLED){pn_cancel_scope_t shield;pn_cancel_enter(&shield,NULL,NULL);if(stop_reader())show_shelf("",false);pn_cancel_leave(&shield);return;}
    if(status==PN_STALE_JOB){if(stop_reader()){recent_mode=false;selected_identified=false;selected_path[0]=0;message("书籍内容已改变","从全部书架重新选择\n历史记录仍保留");}return;}
    if(status!=PN_OK){ESP_LOGE(TAG,"Reader open/present failed: %d",(int)status);if(stop_reader())message("打开书籍失败","检查书籍或字体后重试");return;}
    locked=false;status_page=false;shelf_mode=false;
}
static void start_reader(void){
    if(opening)return;
    bool gate=*selected_path!=0 && !transfer.impl;
    if(gate){opening=true;pn_async_block_controls(&async_port.input,true);pn_device_async_view(&async_port,18,false,input_flags);}
    start_reader_impl();
    if(gate){pn_async_block_controls(&async_port.input,false);opening=false;}
}
/// 挂载完成后继续原请求，失败保留重试入口；不在轮询中重新发起挂载。/ Continue the original request after mounting, keep Retry on failure, and never restart a mount from polling.
static void tick_sd_recovery(uint64_t now){
    pn_status_t status=pn_device_sd_poll(&sd_recovery,now);
    if(status==PN_BUSY || status==PN_EMPTY)return;
    if(status==PN_OK){ESP_LOGI(TAG,"TF remount complete; reopening library");start_reader();}
    else if(status==PN_STALE_MEDIA)message("请插入TF卡","插卡后点下方重试");
    else{ESP_LOGW(TAG,"TF recovery failed: %d",(int)status);message("TF卡不可用","未格式化\n检查卡后重试");}
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
/// 对时只借用无线；打开时复制凭据，所有长网络等待由poll推进。 / Time sync borrows only radio; opening copies credentials and polls advance all long network waits.
static void begin_time_sync(void){
    if(time_sync.impl || transfer.impl)return;
    pn_network_credentials_t credentials={0};pn_status_t status=load_network(&credentials);
    if(status==PN_OK && !credentials.ssid[0])status=PN_EMPTY;
    pn_device_wifi_config_t config={.mode=PN_WIFI_STA};
    if(status==PN_OK){memcpy(config.ssid,credentials.ssid,sizeof config.ssid);memcpy(config.password,credentials.password,sizeof config.password);status=pn_device_time_open(&time_sync,&config);}
    pn_network_wipe(&credentials);volatile unsigned char *bytes=(volatile unsigned char *)&config;for(size_t i=0;i<sizeof config;i++)bytes[i]=0;
    snprintf(time_notice,sizeof time_notice,"%s",status==PN_OK?"正在连接家庭WiFi…":status==PN_EMPTY?"请先保存家庭WiFi":"对时未开始，稍后重试");time_poll_at=0;update_time_ui(true);
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
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
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
    pn_wallpaper_mask_close(&lock_mask);reading_menu=false;pn_wallpaper_store_t store;
    bool store_ok=mount_wallpaper() && pn_wallpaper_store_init(&store,&wallpaper_media,"/wallpaper/lock.a","/wallpaper/lock.b")==PN_OK;
    pn_status_t status=pn_wallpaper_ui_open(&wallpaper_ui,&pool,&sd_media,PN_SD_MOUNT "/" PN_SD_WALLPAPERS,store_ok?&store:NULL,present,NULL);
    if(status!=PN_OK){message("壁纸设置未打开","保留原锁屏，稍后重试");return;}
    status_page=true;shelf_mode=false;index_mode=false;search_mode=false;hub_mode=false;actions_mode=false;
}
/// 进入字体管理：先关闭正文和其字体源，删除/设默认在无消费者时进行。/ Enter font management after closing the book and its font sources, so deletion and defaults happen without consumers.
static void begin_fonts(void){
    if(transfer.impl || font_manage.impl || wallpaper_ui.impl)return;
    if(!stop_reader())return;
    pn_wallpaper_mask_close(&lock_mask);
    reading_menu=false;
    if(!data_ready)data_ready=mount_data();
    pn_status_t status=pn_font_manage_open(&font_manage,&pool,&sd_media,PN_SD_MOUNT "/" PN_SD_FONTS,data_ready?&data_media:NULL,data_ready?"/data/progress":NULL,present,NULL);
    if(status!=PN_OK){message("字体管理未打开","字体与选择均保留，稍后重试");return;}
    status_page=true;shelf_mode=false;index_mode=false;search_mode=false;hub_mode=false;actions_mode=false;
}
/// 设置页：壁纸/字体入口与翻页开关；进入前同样保存并关闭正文。/ Settings: wallpaper/font entries and page-turn switches; the book is saved and closed first as well.
static void storage_note(char *out,size_t cap);
static void show_hub(void);
static void begin_settings(void){
    if(transfer.impl || settings_ui.impl || font_manage.impl || wallpaper_ui.impl)return;
    if(!stop_reader())return;
    pn_wallpaper_mask_close(&lock_mask);
    reading_menu=false;
    if(!data_ready)data_ready=mount_data();
    pn_status_t status=pn_settings_ui_open(&settings_ui,&pool,data_ready?&data_media:NULL,data_ready?"/data/progress":NULL,present,NULL);
    if(status!=PN_OK){message("设置未打开","原设置保留，稍后重试");return;}
    {pn_settings_about_t about={0};const esp_app_desc_t *app=esp_app_get_description();if(app){char version[24];memcpy(version,app->version,sizeof version-1);version[sizeof version-1]=0;snprintf(about.version,sizeof about.version,"版本 %.20s",version);}
     storage_note(about.storage,sizeof about.storage);snprintf(about.internal,sizeof about.internal,"%s",data_ready?"可用（阅读记录、书签与设置）":"不可用");
     snprintf(about.screen,sizeof about.screen,"4.7 英寸 · 684×1216 · 16 级灰阶");pn_settings_ui_set_about(&settings_ui,&about);}
    refresh_network_name();update_time_ui(false);if(*saved_network)(void)pn_settings_ui_set_lan(&settings_ui,saved_network,present,NULL);
    status_page=true;shelf_mode=false;index_mode=false;search_mode=false;hub_mode=false;actions_mode=false;
}
static void end_settings(void){input_flags=settings_ui.flags;shelf_prefs=settings_ui.shelf;pn_settings_ui_close(&settings_ui);if(*selected_path)start_reader();else show_shelf("",false);}
static void end_fonts(void){pn_font_manage_close(&font_manage);if(back_to_settings){back_to_settings=false;begin_settings();return;}if(*selected_path)start_reader();else show_shelf("",false);}
static void end_wallpaper(void){pn_wallpaper_ui_close(&wallpaper_ui);if(back_to_settings){back_to_settings=false;begin_settings();return;}if(*selected_path)start_reader();else show_shelf("",false);}
static void begin_transfer(bool station){
    if(time_sync.impl){message("正在对时","请结束对时后再传书");return;}
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
    pn_status_t status=pn_device_transfer_open(&transfer,&sd_media,PN_SD_MOUNT,&config);
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
    transfer_view.localsend=true;transfer_view.phase=state.phase==PN_DTRANSFER_READY?PN_TVIEW_READY:state.phase==PN_DTRANSFER_STOPPING?PN_TVIEW_STOPPING:state.phase==PN_DTRANSFER_STARTING?PN_TVIEW_STARTING:PN_TVIEW_FAILED;
    memcpy(transfer_view.ssid,state.network.ssid,sizeof transfer_view.ssid);memcpy(transfer_view.password,state.network.ap_password,sizeof transfer_view.password);
    if(state.network.address[0])snprintf(transfer_view.address,sizeof transfer_view.address,"http://%s",state.network.address);
    if(state.released && transfer_return && pn_device_transfer_close(&transfer)==PN_OK){
        // 关闭完成后前台返回；抬手等普通事件不能取消这次唯一重绘，紧急导航仍可取消。/ Return in the foreground after shutdown; ordinary release events cannot cancel the only redraw, while urgent navigation can.
        pn_async_scope_t returned;pn_async_begin(&async_port.input,&returned,false);
        reading_menu=false;favorites_valid=false;recent_loaded=false;pn_catalog_index_invalidate(&catalog_index);
        if(management_home_pending){management_home_pending=false;home_mode=true;selected_path[0]=0;show_shelf("",false);}
        else if(transfer_lock)lock_and_sleep("传输已停止，按电源键继续阅读");else start_reader();
        pn_async_end(&returned);
        return;
    }
    if(now-last_transfer_draw>=1000 && memcmp(&transfer_view,&last_transfer_view,sizeof transfer_view)){paint_transfer(false);last_transfer_draw=now;}
}
/// 锁屏后浅睡，电源键唤醒即解锁回原书；唤醒源不可用时保持锁屏等待轮询。/ Light-sleep after locking and unlock back to the book on a power-key wake; if the wake source is unavailable stay locked and keep polling.
static bool prepare_lock(void){
    reader_suspended=false;
    if(!reader_active() || toolbar_open || styles.active || fonts.active || toc.active || jump_ui.active || bookmarks.mode!=PN_BUI_CLOSED){toolbar_open=false;return stop_reader();}
    pn_status_t status=epub.impl?pn_epub_app_suspend(&epub,now_ms()):pn_reader_app_suspend(&reader,now_ms());
    if(status==PN_OK){reader_suspended=true;return true;}
    if(status==PN_BUSY || status==PN_EMPTY)return stop_reader();
    ESP_LOGE(TAG,"Warm save barrier failed: %d",(int)status);message("保存失败","当前位置仍保留，稍后重试");return false;
}
static void resume_reader(void){
    if(reader_suspended){
        read_pico_sd_info_t card={0};esp_err_t checked=read_pico_sd_get_info(&card);
        if(checked==ESP_OK && card.mounted && pn_device_sleep_media_unchanged()){
            pn_status_t status=epub.impl?pn_epub_app_resume(&epub,now_ms(),present,NULL):pn_reader_app_resume(&reader,now_ms(),present,NULL);
            if(status==PN_OK){reader_suspended=false;locked=false;status_page=false;shelf_mode=false;ESP_LOGI(TAG,"Warm reader resumed without reopening book");return;}
        }
        if(checked!=ESP_OK || !card.mounted || !pn_device_sleep_media_unchanged()){
            if(epub.impl)(void)pn_epub_app_media_lost(&epub);else (void)pn_reader_app_media_lost(&reader);
        }
    }
    start_reader();
}
static void lock_and_sleep_impl(const char *hint){
    ESP_LOGI(TAG,"Locking; idle at %llu ms",(unsigned long long)last_input);
    locked=true;show_lock(hint);if(!reader_suspended)pn_wallpaper_mask_close(&lock_mask);
    if(!pn_device_async_off(&async_port)){ESP_LOGW(TAG,"Display off barrier failed; staying awake");return;} // 显示owner断轨后再交出PMU/触摸。/ Display owner drops rails before handing off PMU/touch.
    if(!pn_device_async_pause(&async_port,true)){ESP_LOGW(TAG,"Input pause timed out; staying awake");(void)pn_device_async_pause(&async_port,false);return;}
    if(pn_device_sleep_until_key()){locked=false;last_input=now_ms();ESP_LOGI(TAG,"Unlocked at %llu ms",(unsigned long long)last_input);resume_reader();}
    (void)pn_device_async_pause(&async_port,false);
}
static void lock_and_sleep(const char *hint){
    // 锁屏/断轨/交接是串行屏障，后到输入在唤醒后处理，不半途取消硬件步骤。/ Lock/off/handoff form a serial barrier; handle later input after wake without cancelling hardware steps midway.
    pn_cancel_scope_t shield;pn_cancel_enter(&shield,NULL,NULL);lock_and_sleep_impl(hint);pn_cancel_leave(&shield);
}
/// 字母跳转页。/ Letter-index page.
/// 打开搜索输入页；沿用字母页的状态约定（不在书架模式）。/ Open the search input page, following the letter-index page's state conventions (not in shelf mode).
static void show_search(void){
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,40);
    if(status==PN_OK)status=pn_search_ui_render(&search_ui,&font,&frame);
    if(status==PN_OK)status=present(NULL,&frame,PN_REFRESH_GL16);
    pn_font_close(&font);pn_free(pixels);
    if(status==PN_OK){search_mode=true;shelf_mode=false;status_page=true;}
}
/// 存储卡容量说明（实际读取，读不到为空串）。/ Card capacity note from an actual read; empty when unreadable.
static void storage_note(char *out,size_t cap){
    out[0]=0;uint64_t total=0,free_bytes=0;
    if(sd_media.available && esp_vfs_fat_info(PN_SD_MOUNT,&total,&free_bytes)==ESP_OK && total)
        snprintf(out,cap,"共 %llu.%llu GB · 可用 %llu.%llu GB",(unsigned long long)(total>>30),(unsigned long long)((total%(1ull<<30))*10>>30),(unsigned long long)(free_bytes>>30),(unsigned long long)((free_bytes%(1ull<<30))*10>>30));
}
/// 无等待状态推进；PMU提交在本控制owner且无线已释放后进行。 / Advance without waiting; this controller commits PMU only after radio release.
static void tick_time_sync(uint64_t now){
    if(!time_sync.impl || (time_poll_at && now<time_poll_at))return;
    time_poll_at=now+100;pn_device_time_state_t before={0},state={0};(void)pn_device_time_state(&time_sync,&before);(void)pn_device_time_poll(&time_sync,now);(void)pn_device_time_state(&time_sync,&state);
    if(!state.released){if(state.phase!=before.phase){snprintf(time_notice,sizeof time_notice,"%s",state.phase==PN_TIME_WAITING?"正在获取网络时间…":state.phase==PN_TIME_STOPPING?"正在结束网络服务…":"正在连接家庭WiFi…");update_time_ui(true);}return;}
    if(state.phase==PN_TIME_SUCCEEDED){uint32_t confirmed=0;pn_status_t status=PN_IO;uint64_t observed=now_ms();
        // 清理重试的耗时计入UTC，单调倒退或超出日期范围时不提交。 / Advance UTC across cleanup retries; never commit on monotonic rollback or date overflow.
        if(observed>=state.acquired_ms){uint64_t utc=(uint64_t)state.utc+(observed-state.acquired_ms)/1000u;if(utc<=4102444799u){state.utc=(uint32_t)utc;status=pn_device_time_commit(&state,&confirmed);}}
        if(status==PN_OK){clock_base_sec=confirmed;clock_base_ms=now_ms();clock_known=true;clock_tick(clock_base_ms);snprintf(time_notice,sizeof time_notice,"已校准，设备时钟继续走时");}
        else snprintf(time_notice,sizeof time_notice,"网络时间已获取，设备时钟未确认");}
    else snprintf(time_notice,sizeof time_notice,"%s",state.phase==PN_TIME_CANCELLED?"已结束对时":state.sdk_error==ESP_ERR_TIMEOUT?"网络对时超时，原显示时间保留":"对时失败，原显示时间保留");
    ESP_LOGI(TAG,"Time sync phase=%d error=%d sdk=%d",(int)state.phase,(int)state.error,state.sdk_error);(void)pn_device_time_close(&time_sync);update_time_ui(true);
    if(time_lock_pending){time_lock_pending=false;if(prepare_lock())lock_and_sleep("再按电源键继续阅读");}
}
/// 传书一级页：按实际能力列出通道。/ Transfer root tab listing channels by real capability.
static void show_hub(void){
    if(transfer.impl)return;
    refresh_network_name();storage_note(hub_storage,sizeof hub_storage);
    hub_info=(pn_transfer_hub_info_t){.hotspot=true,.lan=*saved_network!=0,.localsend=true,.lan_name=saved_network,.storage=hub_storage};
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,28);
    if(status==PN_OK)status=pn_transfer_hub_render(&font,&frame,&hub_info);
    if(status==PN_OK)status=present(NULL,&frame,PN_REFRESH_GL16);
    pn_font_close(&font);pn_free(pixels);
    if(status==PN_OK){shelf_mode=false;index_mode=false;search_mode=false;hub_mode=true;status_page=true;}
    else message("传书页未打开","稍后重试");
}
static void show_index(void){
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,684u*1216u/2u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK)status=pn_font_open(&font,&pool,&builtin,40);
    if(status==PN_OK)status=pn_shelf_index_render(&font,&frame);
    if(status==PN_OK)status=present(NULL,&frame,PN_REFRESH_GL16);
    pn_font_close(&font);pn_free(pixels);
    if(status==PN_OK){index_mode=true;shelf_mode=false;status_page=true;}
}
/// 屏下三键：阅读KEY1/3翻页、KEY2菜单、长按KEY2回书架；书架KEY1/3翻书目页；菜单KEY2返回。/ Touch keys: reading KEY1/3 turn pages, KEY2 opens the menu and a long KEY2 returns to the shelf; shelf KEY1/3 page the catalog; menu KEY2 goes back.
/// 当前书不可用的工具栏入口：TXT没有目录，搜索尚未实现。/ Toolbar entries unavailable for the current book: TXT has no TOC and search is not implemented.
static unsigned tool_unavailable(void){return epub.impl?0u:1u;}
/// 在最近一页上叠加工具栏并呈现；失败时保持原页面。/ Overlay the toolbar on the latest page and present it; the page stays on failure.
static void toolbar_show(void){
    if(toolbar_open || !reader_active() || status_page || !page_copy)return;
    pn_frame_t frame;pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();
    pn_status_t status=pn_frame_bind(&frame,page_copy,342u*1216u,684,1216)?pn_font_open(&font,&pool,&builtin,34):PN_INVALID;
    if(status==PN_OK){toolbar_open=true;status=pn_reader_toolbar_present(&pool,&font,&frame,tool_unavailable(),progress_save_failed?"保存失败，当前页保留，稍后重试":reader_notice,present,NULL);if(status!=PN_OK)toolbar_open=false;}
    pn_font_close(&font);if(status!=PN_OK)ESP_LOGW(TAG,"Toolbar: %d",(int)status);
}
/// 关闭工具栏并恢复原页面；强刷传PN_REFRESH_GC16。/ Close the toolbar and restore the page; pass PN_REFRESH_GC16 for a full refresh.
static void toolbar_close(pn_refresh_t profile){
    toolbar_open=false;if(!page_copy)return;
    pn_frame_t frame;if(pn_frame_bind(&frame,page_copy,342u*1216u,684,1216) && present(NULL,&frame,profile)!=PN_OK)ESP_LOGW(TAG,"Toolbar close failed");
}
/// 翻页错误也在异步输入路径可见；已显示页与保存故障分别反馈。/ Surface errors for asynchronous input, distinguishing shown pages from save failures.
static void report_reader_result(pn_status_t status){
    if(status==PN_OK){reader_notice=NULL;return;}
    if(status==PN_EMPTY || status==PN_CANCELLED)return;
    bool confirmed=epub.impl?pn_epub_app_last_confirmed(&epub):pn_reader_app_last_confirmed(&reader);
    if(confirmed)progress_save_failed=true;
    else reader_notice="翻页未完成，原页保留，请重试";
    ESP_LOGW(TAG,"Reader result=%d confirmed=%d",(int)status,confirmed);
    if(page_copy)toolbar_show();else message("阅读未完成","重试或按电源键返回");
}
static int active_hit(int x,int y);
static void apply_selection(int selection);
/// 三键焦点导航适用的页面：触摸页与工具栏等（书架和阅读正文各有自己的按键规则）。/ Pages three-key focus navigation applies to: touch pages and the toolbar (the shelf and the reading text have their own key rules).
static bool focus_page(void){
    return actions_mode || hub_mode || transfer.impl || wallpaper_ui.impl || font_manage.impl || settings_ui.impl || search_mode || index_mode || reading_menu || fonts.active || toc.active || jump_ui.active || styles.active || bookmarks.mode!=PN_BUI_CLOSED || toolbar_open || status_page;
}
static int focus_hit(void *ctx,int x,int y){(void)ctx;return active_hit(x,y);}
/// 重新扫描当前页的焦点表，尽量保持原焦点。/ Rescan the page's focus table, keeping the focus where possible.
static void focus_rescan(void){
    int keep=-1;const pn_focus_item_t *current=pn_focus_current(&focus_nav);if(current)keep=current->code;
    pn_focus_scan(&focus_nav,focus_hit,NULL,NULL,0);
    for(size_t i=0;keep>=0 && i<focus_nav.count;i++)if(focus_nav.items[i].code==keep)focus_nav.index=(int)i;
}
/// 当前是否是阻断确认弹窗（删除字体/删除书签），是则给出确认与取消的命中码。/ Whether a blocking confirmation dialog (delete font / delete bookmark) is showing, giving its confirm and cancel hit codes.
static bool dialog_codes(int *confirm,int *cancel){
    if(font_manage.impl && font_manage.screen==PN_FMU_CONFIRMING){*confirm=PN_FMU_CONFIRM;*cancel=PN_FMU_CANCEL;return true;}
    if(bookmarks.mode==PN_BUI_DELETE){*confirm=PN_BUI_CONFIRM;*cancel=PN_BUI_CANCEL;return true;}
    if(actions_mode && actions.confirming){*confirm=PN_BA_CONFIRM;*cancel=PN_BA_CANCEL;return true;}
    return false;
}
/// 弹窗左键取消、右键明确确认；中键由首页保存路径处理。
/// Dialog left cancels, right explicitly confirms; middle uses the saved Home route.
static void dialog_key(int key,pn_key_event_t event,int confirm,int cancel){
    if(event!=PN_KEY_SHORT)return;
    if(key==PN_KEY_1){focus_nav.index=-1;apply_selection(cancel);return;}
    if(key==PN_KEY_3){focus_nav.index=-1;apply_selection(confirm);return;}
}
/// 打开第index本书的操作面板。/ Open the actions sheet for entry index.
static void open_actions(int index){
    if(!shelf_page || index<0 || (size_t)index>=shelf_page->count)return;
    actions=(pn_book_actions_t){.item=shelf_page->items[index]};
    if(shelf_covers){snprintf(actions.title,sizeof actions.title,"%s",shelf_covers->display[index].title);snprintf(actions.author,sizeof actions.author,"%s",shelf_covers->display[index].author);}
    uint64_t started=now_ms();(void)started;
    actions.favorites_known=favorites_ensure()==PN_OK;actions.favorite=actions.favorites_known && pn_favorites_contains(favorites,actions.item.path);
    uint64_t favorites_ms=now_ms()-started,paint_ms=0;(void)favorites_ms;(void)paint_ms;
    actions_mode=true;
    // 成功底图直接叠加面板，按需准备面板文字；不扫描目录或重绘封面。/ Overlay the confirmed shelf with the sheet's glyphs, without directory scans or cover painting.
    pn_status_t status=PN_UNSUPPORTED;
    if(panel_known){
        uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,342u*1216u);pn_frame_t frame;pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();
        status=pn_frame_bind(&frame,pixels,342u*1216u,684,1216)?PN_OK:PN_NO_MEMORY;
        if(status==PN_OK)status=pn_device_frame_restore(hardware.hl.back_fb,684u*1216u/2u,&frame);
        if(status==PN_OK)status=pn_font_open(&font,&pool,&source,24);
        pn_w_font_scope_t scope={0};
        if(status==PN_OK)status=pn_w_font_scope_begin(&scope,&pool,NULL);
        if(status==PN_OK)status=pn_book_actions_render(&actions,&font,&frame);
        pn_w_font_scope_end(&scope);
        paint_ms=now_ms()-started-favorites_ms;
        if(status==PN_OK)status=present(NULL,&frame,PN_REFRESH_GL16);
        pn_font_close(&font);pn_free(pixels);
    }else status=draw_shelf(PN_REFRESH_GL16);
    ESP_LOGI(TAG,"Actions favorites=%llu ms paint=%llu ms total=%llu ms status=%d",(unsigned long long)favorites_ms,(unsigned long long)paint_ms,(unsigned long long)(now_ms()-started),(int)status);
    if(status!=PN_OK){actions_mode=false;ESP_LOGW(TAG,"Book actions present failed");}
}
/// 打开书架第index项（触摸与KEY2共用）。/ Open shelf entry index (shared by touch and KEY2).
static void open_shelf_item(int index){
    if(!shelf_page || index<0 || (size_t)index>=shelf_page->count)return;
    const pn_catalog_item_t *item=&shelf_page->items[index];
    if(item->format==PN_BOOK_TXT || item->format==PN_BOOK_EPUB){selected_format=item->format;strcpy(selected_path,item->path);selected_identified=item->identified;selected_expected=item->expected;start_reader();}
    else message("格式尚未接入","书籍仍保留，选择TXT或EPUB");
}
static void handle_key(int key,pn_key_event_t event){
    if(focus_page() && event==PN_KEY_SHORT && key==PN_KEY_2){
        if(transfer.impl){management_home_pending=true;transfer_return=true;transfer_lock=false;(void)pn_device_transfer_request_stop(&transfer);return;}
        if(!stop_reader())return;
        if(settings_ui.impl){input_flags=settings_ui.flags;shelf_prefs=settings_ui.shelf;}
        pn_wallpaper_ui_close(&wallpaper_ui);pn_font_manage_close(&font_manage);pn_settings_ui_close(&settings_ui);
        reading_menu=false;toolbar_open=false;actions_mode=false;hub_mode=false;index_mode=false;search_mode=false;back_to_settings=false;
        focus_nav.index=-1;home_mode=true;selected_path[0]=0;show_shelf("",false);return;
    }
    {int confirm=0,cancel=0;if(dialog_codes(&confirm,&cancel)){dialog_key(key,event,confirm,cancel);return;}}
    if(focus_page()){
        if(event!=PN_KEY_SHORT)return;
        if(key==PN_KEY_1 && fonts.active && fonts.preview){apply_selection(PN_FUI_FORM);return;}
        if(key==PN_KEY_1 && styles.active && styles.preview){apply_selection(PN_SUI_FORM);return;}
        int back=transfer.impl?PN_TRANSFER_VIEW_STOP:wallpaper_ui.impl?(wallpaper_ui.screen==PN_WUI_LIST?PN_WUI_BACK:PN_WUI_CANCEL):font_manage.impl?PN_FMU_BACK:settings_ui.impl?PN_SETUI_BACK:actions_mode?PN_BA_CANCEL:hub_mode?PN_HUB_SHELF:search_mode?PN_SEARCH_BACK:index_mode?'<':reading_menu?PN_READING_MENU_RESUME:fonts.active?PN_FUI_BACK:toc.active?PN_TOC_UI_BACK:jump_ui.active?PN_JUI_CANCEL:styles.active?(styles.more?PN_SUI_BACK_MAIN:PN_SUI_CANCEL):bookmarks.mode!=PN_BUI_CLOSED?PN_BUI_BACK:toolbar_open?PN_TOOL_CLOSE:-1;
        if(key==PN_KEY_1){focus_nav.index=-1;int command=pn_key_management_command(key,event,back,-1,-1);if(command>=0)apply_selection(command);return;}
        if(key==PN_KEY_3){
            if(fonts.active){apply_selection(fonts.preview?PN_FUI_FORM:fonts.mode?PN_FUI_APPLY:PN_FUI_SELECT);return;}
            if(styles.active){apply_selection(styles.preview?PN_SUI_FORM:PN_SUI_APPLY);return;}
            if(jump_ui.active){apply_selection(PN_JUI_CONFIRM);return;}
            if(toc.active){if(toc.selected<toc.count)apply_selection(PN_TOC_UI_ROW+(int)toc.selected);return;}
            if(bookmarks.mode!=PN_BUI_CLOSED){apply_selection(bookmarks.mode==PN_BUI_RENAME?PN_BUI_SAVE:bookmarks.mode==PN_BUI_ACTIONS?PN_BUI_JUMP:PN_BUI_SELECT);return;}
            focus_rescan();const pn_focus_item_t *item=pn_focus_current(&focus_nav);
            if(!item){for(size_t i=0;i<focus_nav.count;i++)if(focus_nav.items[i].code!=back){item=&focus_nav.items[i];break;}}
            if(item){int code=item->code;focus_nav.index=-1;apply_selection(code);}return;
        }
        return;
    }
    if(transfer.impl || wallpaper_ui.impl || font_manage.impl || settings_ui.impl)return;
    if(bookmarks.mode!=PN_BUI_CLOSED || styles.active || fonts.active || toc.active || jump_ui.active)return;
    if(toolbar_open){if(key==PN_KEY_2 && event==PN_KEY_SHORT)toolbar_close(PN_REFRESH_GL16);return;}
    if(reading_menu){
        if(event==PN_KEY_SHORT && key==PN_KEY_2){reading_menu=false;if(reader_active()){if(active_step(PN_APP_OPEN,now_ms())==PN_OK){status_page=false;shelf_mode=false;}}else start_reader();}
        return;
    }
    if(shelf_mode && shelf_page){
        if(event!=PN_KEY_SHORT)return;
        if(home_mode){
            if(key==PN_KEY_2){if(shelf_page->count)open_shelf_item(shelf_focus<0?0:shelf_focus);else{home_mode=false;show_shelf("",false);}}
            else if(shelf_page->count){int next=shelf_focus<0?0:shelf_focus+(key==PN_KEY_3?1:-1);if(next>=0 && next<(int)shelf_page->count){shelf_focus=next;(void)draw_shelf(PN_REFRESH_GL16);}}
            return;
        }
        // KEY2：先把焦点放到当前页第一项，再按一次打开焦点项（docs/ARCHITECTURE.md“三键与步进输入”）；设置从底栏进入。/ KEY2: first focus the page's first entry, press again to open it (docs/ARCHITECTURE.md, key and stepper input); Settings is reached from the bottom bar.
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
    report_reader_result(status);
}
/// 按当前活动页面扫描命中码（触摸与三键焦点共用）。/ Hit code of the active page at a point (shared by touch and three-key focus).
static int active_hit(int x,int y){
    int hit=-1;
    if(transfer.impl)hit=pn_transfer_view_hit(&transfer_view,x,y);
    else if(wallpaper_ui.impl)hit=pn_wallpaper_ui_hit(&wallpaper_ui,x,y);
    else if(font_manage.impl)hit=pn_font_manage_hit(&font_manage,x,y);
    else if(settings_ui.impl)hit=pn_settings_ui_hit(&settings_ui,x,y);
    else if(hub_mode)hit=pn_transfer_hub_hit(&hub_info,x,y);
    else if(search_mode)hit=pn_search_ui_hit(x,y);
    else if(index_mode){char letter=pn_shelf_index_hit(x,y);hit=letter?(int)(unsigned char)letter:-1;}
    else if(reading_menu)hit=pn_reading_menu_hit_lan(x,y,*saved_network!=0);
    else if(fonts.active)hit=pn_font_ui_hit(&fonts,x,y);
    else if(toc.active)hit=pn_toc_ui_hit(&toc,x,y);
    else if(jump_ui.active)hit=pn_jump_ui_hit(&jump_ui,x,y);
    else if(styles.active)hit=pn_style_ui_hit(&styles,x,y);
    else if(bookmarks.mode!=PN_BUI_CLOSED)hit=pn_bookmark_ui_hit(&bookmarks,x,y);
    else if(actions_mode)hit=pn_book_actions_hit(&actions,x,y);
    else if(shelf_mode && shelf_page){pn_shelf_options_t options=shelf_options();hit=pn_shelf_hit_ex(shelf_page,x,y,&options);}
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
    else if(actions_mode){
        if(selection==PN_BA_CANCEL){if(actions.confirming){actions.confirming=false;actions.notice=NULL;(void)draw_shelf(PN_REFRESH_GL16);}else{actions_mode=false;(void)draw_shelf(PN_REFRESH_GL16);}}
        else if(selection==PN_BA_OPEN){actions_mode=false;
            if(actions.item.format==PN_BOOK_TXT || actions.item.format==PN_BOOK_EPUB){selected_format=actions.item.format;strcpy(selected_path,actions.item.path);selected_identified=false;start_reader();}
            else message("格式尚未接入","书籍仍保留，选择TXT或EPUB");}
        else if(selection==PN_BA_FAVORITE){
            pn_media_lease_t lease={0};pn_status_t status=data_ready?pn_media_acquire(&data_media,PN_MEDIA_WRITE,&lease):PN_UNSUPPORTED;pn_journal_files_t files;pn_journal_io_t io;bool now=actions.favorite;
            if(status==PN_OK)status=pn_favorites_files(&files,&data_media,&lease,"/data/progress",&io);
            if(status==PN_OK)status=pn_favorites_toggle(&io,&pool,actions.item.path,&now);
            if(lease.ticket)(void)pn_media_release(&data_media,&lease);
            actions.notice=status==PN_OK?(now?"已加入收藏":"已取消收藏"):status==PN_LIMIT?"收藏最多 50 本":"保存失败，请重试";
            if(status==PN_OK)actions.favorite=now;
            favorites_valid=false;
            (void)draw_shelf(PN_REFRESH_GL16);}
        else if(selection==PN_BA_DELETE){actions.confirming=true;actions.notice=NULL;(void)draw_shelf(PN_REFRESH_GL16);}
        else if(selection==PN_BA_CONFIRM){
            // 删除前确认没有书正打开；只删用户选中的这一个文件。/ Make sure no book is open first, and delete only the one file the user chose.
            pn_status_t status=stop_reader()?PN_OK:PN_BUSY;pn_media_lease_t lease={0};
            if(status==PN_OK)status=pn_media_acquire(&sd_media,PN_MEDIA_WRITE,&lease);
            if(status==PN_OK){if(remove(actions.item.path)!=0)status=PN_IO;else{pn_catalog_index_invalidate(&catalog_index);recent_loaded=false;}}
            if(lease.ticket)(void)pn_media_release(&sd_media,&lease);
            if(status==PN_OK){actions_mode=false;if(!strcmp(selected_path,actions.item.path))selected_path[0]=0;show_shelf("",false);}
            else{actions.notice="删除失败，文件仍保留";(void)draw_shelf(PN_REFRESH_GL16);}}
    }
    else if(hub_mode){
        if(selection==PN_HUB_HOTSPOT){hub_mode=false;begin_transfer(false);}
        else if(selection==PN_HUB_LAN){hub_mode=false;begin_transfer(true);}
        else if(selection==PN_HUB_LOCALSEND){hub_mode=false;begin_transfer(*saved_network!=0);}
        else if(selection==PN_HUB_SHELF || selection==PN_HUB_HOME){hub_mode=false;home_mode=selection==PN_HUB_HOME;show_shelf("",false);}
        else if(selection==PN_HUB_SETTINGS){hub_mode=false;begin_settings();}
    }
    else if(search_mode){
        if(selection==PN_SEARCH_BACK){search_mode=false;show_shelf("",false);}
        else if(selection==PN_SEARCH_DONE){snprintf(search_query,sizeof search_query,"%s",search_ui.query);search_mode=false;recent_mode=false;show_shelf("",false);}
        else{
            bool changed=selection==PN_SEARCH_DELETE?pn_search_ui_delete(&search_ui):selection==PN_SEARCH_CLEAR?(*search_ui.query?(search_ui.query[0]=0,true):false):pn_search_ui_append(&search_ui,(char)selection);
            if(changed)show_search();
        }
    }
    else if(index_mode){index_mode=false;if(selection!='<'){jump_letter=(char)selection;recent_mode=false;}show_shelf("",false);}
    else if(settings_ui.impl){update_time_ui(false);if(selection==PN_SETUI_FULL_REFRESH)atomic_store(&deep_clean_pending,true);pn_status_t status=pn_settings_ui_event(&settings_ui,selection,present,NULL);if(status!=PN_OK && status!=PN_EMPTY && status!=PN_LIMIT)ESP_LOGW(TAG,"Settings: %d",(int)status);
        if(settings_ui.request==PN_SETUI_TIME_SYNC || settings_ui.request==PN_SETUI_TIME_CANCEL){int request=settings_ui.request;settings_ui.request=0;if(request==PN_SETUI_TIME_SYNC)begin_time_sync();else{(void)pn_device_time_request_stop(&time_sync);snprintf(time_notice,sizeof time_notice,"正在结束对时…");update_time_ui(true);}}
        else if(settings_ui.request){int request=settings_ui.request;input_flags=settings_ui.flags;shelf_prefs=settings_ui.shelf;pn_settings_ui_close(&settings_ui);back_to_settings=request==PN_SETUI_WALLPAPER || request==PN_SETUI_FONTS;if(request==PN_SETUI_WALLPAPER)begin_wallpaper();else if(request==PN_SETUI_LAN)begin_transfer(true);else if(request==PN_SETUI_SHELF || request==PN_SETUI_HOME){home_mode=request==PN_SETUI_HOME;selected_path[0]=0;show_shelf("",false);}else if(request==PN_SETUI_TRANSFER)show_hub();else begin_fonts();}
        else if(!settings_ui.active)end_settings();}
    else if(toolbar_open){
        if(selection==PN_TOOL_CLOSE)toolbar_close(PN_REFRESH_GL16);
        else if(selection==PN_TOOL_REFRESH){atomic_store(&deep_clean_pending,true);toolbar_close(PN_REFRESH_GC16);}
        else if(selection==PN_TOOL_TOC){toolbar_open=false;if(epub.impl)(void)pn_toc_ui_open(&toc,&epub,present,NULL);}
        else if(selection==PN_TOOL_BOOKMARKS){toolbar_open=false;if(epub.impl)(void)pn_bookmark_ui_open_epub(&bookmarks,&epub,present,NULL);else (void)pn_bookmark_ui_open(&bookmarks,&reader,present,NULL);}
        else if(selection==PN_TOOL_JUMP){toolbar_open=false;pn_status_t status=epub.impl?pn_jump_ui_open_epub(&jump_ui,&epub,present,NULL):pn_jump_ui_open(&jump_ui,&reader,present,NULL);if(status!=PN_OK)ESP_LOGW(TAG,"Jump UI open: %d",(int)status);}
        else if(selection==PN_TOOL_TYPESET){toolbar_open=false;if(epub.impl)(void)pn_style_ui_open_epub(&styles,&epub,present,NULL);else (void)pn_style_ui_open(&styles,&reader,present,NULL);}
        else if(selection==PN_TOOL_SHELF){toolbar_open=false;if(stop_reader()){selected_path[0]=0;show_shelf("",false);}}
    }
    else if(reading_menu){if(selection==PN_READING_MENU_LAN)begin_transfer(true);else if(selection==PN_READING_MENU_SETTINGS)begin_settings();else if(selection==PN_READING_MENU_TRANSFER)begin_transfer(false);else if(selection==PN_READING_MENU_SHELF){home_mode=false;recent_mode=false;fav_mode=false;search_query[0]=0;reading_menu=false;if(stop_reader()){selected_path[0]=0;show_shelf("",false);}}else if(selection==PN_READING_MENU_RESUME){reading_menu=false;if(reader_active()){if(active_step(PN_APP_OPEN,now_ms())==PN_OK){status_page=false;shelf_mode=false;}}else start_reader();}}
    else if(shelf_mode && selection==PN_SHELF_READING_HOME){if(!home_mode){home_mode=true;show_shelf("",false);}}
    else if(shelf_mode && selection==PN_SHELF_LAYOUT){list_mode=!list_mode;shelf_focus=-1;show_shelf("",false);}
                else if(shelf_mode && selection==PN_SHELF_IMPORT && !recent_mode)show_hub();
    else if(shelf_mode && selection>=PN_SHELF_MORE && selection<PN_SHELF_MORE+PN_CATALOG_PAGE_MAX)open_actions(selection-PN_SHELF_MORE);
    else if(shelf_mode && selection==PN_SHELF_TRANSFER)show_hub();
    else if(shelf_mode && selection==PN_SHELF_SEARCH){if(*search_query && !recent_mode){search_query[0]=0;show_shelf("",false);}else{pn_search_ui_open(&search_ui,NULL);show_search();}}
    else if(shelf_mode && selection==PN_SHELF_INDEX && !recent_mode && !*search_query)show_index();
    else if(shelf_mode && selection==PN_SHELF_MENU)begin_settings();
    else if(jump_ui.active){pn_status_t status=pn_jump_ui_event(&jump_ui,selection,now_ms(),present,NULL);if(status!=PN_OK && status!=PN_EMPTY && status!=PN_BUSY)ESP_LOGW(TAG,"Jump UI: %d",(int)status);}
    else if(fonts.active){(void)pn_font_ui_event(&fonts,selection,now_ms(),present,NULL);if(!fonts.active){bool returned=fonts.return_presented;pn_font_ui_close(&fonts);if(styles.active){if(returned){styles.presented=true;styles.preview=false;}else (void)pn_style_ui_present(&styles,present,NULL);}}}
    else if(toc.active){(void)pn_toc_ui_event(&toc,selection,now_ms(),present,NULL);}
    else if(selection==14 && reader_active() && !shelf_mode && !status_page && bookmarks.mode==PN_BUI_CLOSED && !styles.active && !toc.active && !fonts.active){pn_status_t status=epub.impl?pn_jump_ui_open_epub(&jump_ui,&epub,present,NULL):pn_jump_ui_open(&jump_ui,&reader,present,NULL);if(status!=PN_OK)ESP_LOGW(TAG,"Jump UI open: %d",(int)status);}
    else if(selection==13 && epub.impl && bookmarks.mode==PN_BUI_CLOSED && !styles.active){(void)pn_toc_ui_open(&toc,&epub,present,NULL);}
    else if(styles.active){pn_status_t status=pn_style_ui_event(&styles,selection,now_ms(),present,NULL);if(status!=PN_OK && status!=PN_EMPTY && status!=PN_BUSY)ESP_LOGW(TAG,"Style UI: %d",(int)status);}
    else if(selection==12 && reader_active() && bookmarks.mode==PN_BUI_CLOSED && !shelf_mode && !status_page){if(epub.impl)(void)pn_style_ui_open_epub(&styles,&epub,present,NULL);else (void)pn_style_ui_open(&styles,&reader,present,NULL);}
    else if(bookmarks.mode!=PN_BUI_CLOSED){pn_status_t status=pn_bookmark_ui_event(&bookmarks,selection,NULL,now_ms(),present,NULL);if(status!=PN_OK && status!=PN_EMPTY && status!=PN_BUSY)ESP_LOGW(TAG,"Bookmark UI: %d",(int)status);}
    else if(selection==10){if(epub.impl)(void)pn_bookmark_ui_open_epub(&bookmarks,&epub,present,NULL);else (void)pn_bookmark_ui_open(&bookmarks,&reader,present,NULL);}
    else if(selection==11){if(epub.impl)(void)pn_epub_app_bookmark_return(&epub,now_ms(),present,NULL);else (void)pn_reader_app_bookmark_return(&reader,now_ms(),present,NULL);}
    else if(shelf_mode && (selection==PN_SHELF_TAB_ALL || selection==PN_SHELF_TAB_RECENT || selection==PN_SHELF_HOME || selection==PN_SHELF_TAB_FAVORITES)){bool want_recent=selection==PN_SHELF_TAB_RECENT,want_fav=selection==PN_SHELF_TAB_FAVORITES;if(home_mode || want_recent!=recent_mode || want_fav!=fav_mode){home_mode=false;recent_mode=want_recent;fav_mode=want_fav;fav_start=0;search_query[0]=0;show_shelf("",false);}}
    else if(shelf_mode && selection==PN_SHELF_CONTINUE){
        // 继续阅读卡直接打开最近一本，不改变当前书架模式。/ The continue card opens the latest book directly without changing the shelf mode.
        if(shelf_covers && shelf_covers->has_last){const pn_catalog_item_t *last=&shelf_covers->last;
            if(last->format!=PN_BOOK_TXT && last->format!=PN_BOOK_EPUB)message("格式尚未接入","历史记录仍保留");
            else{selected_format=last->format;strcpy(selected_path,last->path);selected_identified=last->identified;selected_expected=last->expected;start_reader();}}
    }
    else if(shelf_mode && selection>=0 && selection<PN_CATALOG_PAGE_MAX && shelf_page)open_shelf_item(selection);else if(shelf_mode && shelf_page && shelf_page->count && (selection==PN_SHELF_NEXT || selection==PN_SHELF_PREVIOUS)){
        char cursor[PN_CATALOG_NAME_MAX];strcpy(cursor,shelf_page->items[selection==PN_SHELF_NEXT?shelf_page->count-1:0].name);show_shelf(cursor,selection==PN_SHELF_PREVIOUS);
    }else if(selection==8){reading_menu=true;refresh_network_name();paint_transfer(true);}
    else if(selection==9){selected_path[0]=0;start_reader();}
}
/// 共用开机图只借一帧内存，推屏后立即归还；不读TF。/ Shared boot art borrows one frame, releases it after presentation, and never reads the SD card.
static void show_boot_brand(void){
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,684u*1216u/2u);pn_frame_t frame;
    pn_status_t status=pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216)?pn_brand_image_render(&frame):PN_NO_MEMORY;
    // 只有开机画面带版本号；默认锁屏仍用纯品牌图。/ Only the boot screen carries the version; the default lock screen keeps the plain brand art.
    if(status==PN_OK)status=pn_brand_version_render(&frame,PN_PRODUCT_VERSION);
    if(status==PN_OK)status=present(NULL,&frame,PN_REFRESH_GC16);
    pn_free(pixels);if(status!=PN_OK)ESP_LOGW(TAG,"Brand screen unavailable: %d",(int)status);
}
static void device_task(void *arg){
#if defined(ESP_PLATFORM)
    pn_profile_port(profile_clock,profile_emit,NULL);
#endif
    #ifdef ESP_PLATFORM
    pn_status_t sha_status=pn_device_identity_init();ESP_LOGI(TAG,"Hardware SHA self-test status=%d",(int)sha_status);
    pn_device_power_init();
#endif
    (void)arg;esp_err_t error=nvs_flash_init();if(error!=ESP_OK){ESP_LOGE(TAG,"NVS unavailable: %s; preserving data",esp_err_to_name(error));vTaskDelete(NULL);return;}
    pn_profile_span_t board_span;pn_profile_begin(&board_span,NULL,"device.board-init");
    error=read_pico_init(&hardware);pn_profile_end(&board_span,error==ESP_OK?PN_OK:PN_IO);if(error!=ESP_OK){ESP_LOGE(TAG,"Board init failed: %s",esp_err_to_name(error));read_pico_deinit(&hardware);vTaskDelete(NULL);return;}
    int vcom=0;if(!hardware.pmu_ready || read_pico_pmu_vcom_get(&vcom)!=ESP_OK){ESP_LOGE(TAG,"Factory VCOM unavailable; display withheld");read_pico_deinit(&hardware);vTaskDelete(NULL);return;}
    epd_set_vcom((uint16_t)vcom);
    size_t available=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    size_t budget=available>PN_HW_PSRAM_RESERVE?available-PN_HW_PSRAM_RESERVE:0;if(budget>PN_HW_POOL_BYTES)budget=PN_HW_POOL_BYTES;
    if(pn_pool_init(&pool,budget,psram_alloc,psram_free,NULL)!=0){read_pico_deinit(&hardware);vTaskDelete(NULL);return;}
    pn_cache_attach(&pool,&cover_cache_owner,PN_CACHE_COVER,NULL,cover_bytes,cover_drop);
    pn_media_init(&sd_media);pn_media_init(&data_media);pn_media_init(&wallpaper_media);read_pico_pmu_drain_events();esp_fill_random(cover_salt,sizeof cover_salt);
    if(read_pico_pmu_report_ready()!=ESP_OK){ESP_LOGE(TAG,"PMU running handshake failed; display withheld");read_pico_deinit(&hardware);vTaskDelete(NULL);return;}
    pn_device_async_start(&async_port,&hardware,present_panel,rails_idle_check,rails_release);pn_device_async_bind_product(&async_port);async_port.overlap=reader_overlap;pn_device_power_sleep_pins();
    if(!async_port.input_active || !async_port.display_active)ESP_LOGW(TAG,"Async task startup degraded: input=%d display=%d",(int)async_port.input_active,(int)async_port.display_active);
    clock_read_boot(now_ms());
    show_boot_brand();
    pn_page_index_space_port(index_space,NULL);
    (void)pn_catalog_index_init(&catalog_index,&pool,&sd_media,book_directory,PN_SD_MOUNT "/" PN_SD_INDEX_CACHE);
    start_reader();pn_reader_input_t input={0};pn_tap_t tap={0};int hold_hit=-1;uint64_t hold_start=0,hold_last=0;bool hold_fired=false;uint64_t last_card=0,last_key=0,last_ui_retry=0,boot=now_ms();
    pn_async_scope_t scope;bool scope_active=false;
#define POWER_IDLE_ROUNDS 100u ///< 约1秒，覆盖输入后的邻页预排与字形预热 / About one second, covering neighbor prefetch and glyph warming after input
    unsigned power_idle=0; ///< 连续无输入轮数（每轮约10ms）/ Consecutive rounds without input (about 10 ms each)
    unsigned background_rounds=0; ///< 连续1ms让出的预热轮数 / Consecutive warming rounds using 1 ms yields
    for(;;){uint64_t now=now_ms();
        if(scope_active)pn_async_end(&scope);
        bool body=reader_active() && !locked && !focus_page() && !status_page && !reading_menu && !shelf_mode && !toolbar_open;
        uint32_t view=locked?1:fonts.active?2:toc.active?3:styles.active?4:jump_ui.active?5:bookmarks.mode!=PN_BUI_CLOSED?6:actions_mode?7:settings_ui.impl?8:wallpaper_ui.impl?9:font_manage.impl?10:hub_mode?11:index_mode?12:search_mode?13:toolbar_open?14:body?15:home_mode?16:17;
        if(shelf_mode && !home_mode && shelf_page)view+=(uint32_t)shelf_page->index*32u;
        pn_device_async_view(&async_port,view,body,input_flags);
        pn_device_input_event_t queued={0};pn_status_t available=async_port.input_active?pn_device_async_take(&async_port,&queued):PN_EMPTY;
        // 有输入立即回到最高频；连续空闲约1秒才放锁，交互中不反复切频。/ Input returns to maximum at once; release only after about one second idle so interaction does not toggle frequency.
        if(available==PN_OK){power_idle=0;pn_device_power_busy(PN_POWER_PRODUCT,true);}
        // 输入健康：首个事件立即记录，之后计数变化时最多每分钟一行。/ Input health: log the first event at once, then at most one line a minute when counters change.
        {static bool first_logged;static uint64_t health_at;static unsigned last_reads,last_errors,last_sent;unsigned reads=atomic_load(&async_port.touch_reads),errors=atomic_load(&async_port.touch_errors),sent=atomic_load(&async_port.events_sent);
         if((available==PN_OK && !first_logged) || (now-health_at>=60000u && (reads!=last_reads || errors!=last_errors || sent!=last_sent))){
            if(available==PN_OK && !first_logged){first_logged=true;ESP_LOGI(TAG,"Input first event kind=%d after boot=%llu ms",(int)queued.kind,(unsigned long long)(now-boot));}
            else ESP_LOGI(TAG,"Input health reads=%u errors=%u events=%u touch_wake=%d",reads,errors,sent,(int)async_port.touch_wake);
            health_at=now;last_reads=reads;last_errors=errors;last_sent=sent;}}
        pn_device_power_busy(PN_POWER_SESSION,transfer.impl!=NULL || time_sync.impl!=NULL);
        pn_async_begin(&async_port.input,&scope,available==PN_EMPTY);scope_active=true;
        if(available==PN_CANCELLED){pn_reader_input_cancel(&input);pn_key_cancel(&keys);int ignored;(void)pn_tap_feed(&tap,1,-1,false,&ignored);hold_hit=-1;touch_held=false;}
        if(available==PN_OK){last_input=queued.at;if(queued.kind==PN_DASYNC_KEY && !locked)handle_key(queued.key,queued.key_event);
            if(queued.kind==PN_DASYNC_OPEN_CANCEL && !locked){if(queued.key==PN_KEY_2){home_mode=true;selected_path[0]=0;}if(stop_reader())show_shelf("",false);}
            if(queued.kind==PN_DASYNC_ACTION && body && !locked){if(queued.action==PN_APP_TOOLS)toolbar_show();else{pn_status_t status=active_step(queued.action,now);report_reader_result(status);}}}
        if(!async_port.display_active)rails_idle_check(now);
        if(transfer.impl)tick_transfer(now);
        if(time_sync.impl)tick_time_sync(now);
        if(!transfer.impl && !locked && sd_recovery.pending)tick_sd_recovery(now);
        {static uint64_t last_battery,last_clock,date_minute;if(now-last_battery>=60000 || !last_battery){last_battery=now;pn_w_set_battery(battery_percent());}if(now-last_clock>=10000 || !last_clock){last_clock=now;clock_tick(now);if(settings_ui.impl && pn_settings_ui_screen(&settings_ui)==5 && now/60000u!=date_minute){date_minute=now/60000u;update_time_ui(true);}}} // 电量与日期页每分钟推进，正文不因对时刷新。 / Advance battery and the date page once a minute; time sync never redraws reading content.
        {static uint64_t prefs_retry;
         if(data_ready && (!shelf_prefs_loaded || !input_flags_loaded) && now>=prefs_retry){
            prefs_retry=now+3000;
            if(!shelf_prefs_loaded && pn_settings_load_shelf(&data_media,"/data/progress",&shelf_prefs)==PN_OK){shelf_prefs_loaded=true;if(shelf_mode)show_shelf("",false);}
            if(!input_flags_loaded && pn_settings_load_flags(&data_media,"/data/progress",&input_flags)==PN_OK)input_flags_loaded=true;
         }} // 读取失败限频重试，不能每10ms重复访问存储。/ Rate-limit failed loads instead of repeating storage reads every 10 ms.
        input.config=(pn_reader_input_config_t){.left_hand=(input_flags&PN_INPUT_LEFT_HAND)!=0,.no_swipe=(input_flags&PN_INPUT_NO_SWIPE)!=0,.no_edge_tap=(input_flags&PN_INPUT_NO_EDGE_TAP)!=0,.no_keys=(input_flags&PN_INPUT_NO_KEYS)!=0};
        if(!transfer.impl && now-last_card>=250){last_card=now;
            if(sd_media.available && pn_device_card_presence()==PN_STALE_MEDIA){if(reader_active()){if(epub.impl)(void)pn_epub_app_media_lost(&epub);else (void)pn_reader_app_media_lost(&reader);(void)stop_reader();}else (void)pn_media_detach(&sd_media);selected_path[0]=0;pn_reader_input_cancel(&input);message("卡已移除","保留上次阅读位置\n插卡后点下方重试");}}
        if((async_port.input_active && available==PN_OK && queued.kind==PN_DASYNC_POWER) || (!async_port.input_active && now-last_key>=100)){last_key=now;if((async_port.input_active || read_pico_pmu_take_key_short()) && now-boot>=1000){pn_reader_input_cancel(&input);
                if(transfer.impl){transfer_return=true;transfer_lock=true;(void)pn_device_transfer_request_stop(&transfer);}
                else if(time_sync.impl){time_lock_pending=true;(void)pn_device_time_request_stop(&time_sync);}
                else if(locked){locked=false;last_input=now;resume_reader();}else if(prepare_lock())lock_and_sleep("再按电源键继续阅读");}}
        // 按键路径可能睡眠后更新last_input；重取当前时间，避免睡前时间减去唤醒时间下溢后立即锁屏。
        // The key path can sleep and update last_input; refresh time to avoid subtracting wake time from pre-sleep time.
        now=now_ms();
        // 无操作满5分钟自动锁屏；传输中由其停止流程负责。/ Auto-lock after five idle minutes; transfers handle their own stop flow.
        if(!locked && !transfer.impl && !time_sync.impl && now>=last_input && now-last_input>=AUTO_LOCK_MS && now-boot>=AUTO_LOCK_MS){pn_reader_input_cancel(&input);if(prepare_lock())lock_and_sleep("再按电源键继续阅读");else last_input=now;}
        if(reader_active() && !locked){pn_status_t saved=epub.impl?pn_epub_app_tick(&epub,now_ms()):pn_reader_app_tick(&reader,now_ms());if(saved==PN_OK)progress_save_failed=false;else if(saved!=PN_BUSY){progress_save_failed=true;ESP_LOGW(TAG,"Pending progress save: %d",(int)saved);}}
        if(bookmarks.mode!=PN_BUI_CLOSED && !bookmarks.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_bookmark_ui_present(&bookmarks,present,NULL);}
        if(toc.active && !toc.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_toc_ui_present(&toc,present,NULL);}
        if(styles.active && !styles.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_style_ui_present(&styles,present,NULL);}
        if(jump_ui.active && !jump_ui.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_jump_ui_present(&jump_ui,present,NULL);}
        if(styles.request_fonts && !locked){styles.request_fonts=false;(void)pn_font_ui_open(&fonts,&pool,epub.impl?NULL:&reader,epub.impl?&epub:NULL,PN_SD_MOUNT "/" PN_SD_FONTS,present,NULL);pn_font_ui_return_to(&fonts,pn_style_ui_render,&styles);}
        if(fonts.active && !fonts.presented && !locked && now-last_ui_retry>=1000){last_ui_retry=now;(void)pn_font_ui_present(&fonts,present,NULL);}
        if(hardware.touch_ready && (!async_port.input_active || (available==PN_OK && queued.kind==PN_DASYNC_TOUCH))){cst836u_touch_t touch={0};esp_err_t read;uint64_t touch_at=now_ms();if(async_port.input_active){touch.count=queued.count;touch.x=queued.x;touch.y=queued.y;read=queued.valid?ESP_OK:ESP_FAIL;touch_at=queued.at;}else read=cst836u_read(hardware.touch,&touch);pn_reader_action_t action;touch_held=read==ESP_OK && touch.count>0;if(touch_held)last_input=now_ms();
            int hit=active_hit(touch.x,touch.y),selection=-1;
            // 步进键长按：按住满600 ms后每180 ms再走一档；松手时不再多走一档（docs/ARCHITECTURE.md“三键与步进输入”）。
            // Stepper hold: after 600 ms held, step again every 180 ms; releasing does not add another step (docs/ARCHITECTURE.md, key and stepper input).
            {bool repeatable=(styles.active && hit>=PN_SUI_FIELD && hit<PN_SUI_FIELD+PN_SUI_FIELDS*2) || (jump_ui.active && hit>=PN_JUI_STEP && hit<PN_JUI_STEP+4);
             // 书架封面长按600 ms：打开书籍操作面板（只触发一次，松手不再打开书）。/ A 600 ms long press on a shelf cover opens the book actions sheet (once; releasing does not open the book).
             bool long_press=shelf_mode && shelf_page && !actions_mode && hit>=0 && hit<(int)shelf_page->count && hit<PN_CATALOG_PAGE_MAX;
             uint64_t tick=touch_at;
             if(touch_held && (repeatable || long_press) && hit==hold_hit){
                 if(long_press){if(!hold_fired && tick-hold_start>=600 && !locked){hold_fired=true;open_actions(hit);}}
                 else if(tick-hold_start>=600 && tick-hold_last>=180 && !locked){hold_last=tick;hold_fired=true;apply_selection(hit);}}
             else{hold_hit=touch_held && (repeatable || long_press)?hit:-1;hold_start=tick;hold_last=0;if(touch_held)hold_fired=false;}}
            if(pn_tap_feed(&tap,touch.count,hit,read==ESP_OK,&selection) && !locked){
                if(hold_fired)hold_fired=false;else apply_selection(selection);
                pn_reader_input_cancel(&input);
            }
            {if(!async_port.input_active){int key=-1;pn_key_event_t event=pn_key_feed(&keys,touch.count,touch.x,touch.y,read==ESP_OK,now_ms(),&key);if(event!=PN_KEY_NONE && !locked){pn_reader_input_cancel(&input);handle_key(key,event);}}}
            if(!transfer.impl && !wallpaper_ui.impl && !font_manage.impl && !settings_ui.impl && !reading_menu && !shelf_mode && !status_page && bookmarks.mode==PN_BUI_CLOSED && !styles.active && !jump_ui.active && !fonts.active && !toc.active && !toolbar_open && !async_port.input_active && pn_reader_input_feed(&input,touch.count,touch.x,touch.y,read==ESP_OK,&action) && !locked){
                if(action==PN_APP_TOOLS)toolbar_show();
                else if(reader_active() && !status_page){pn_status_t status=active_step(action,now_ms());report_reader_result(status);}
                else start_reader();}}
        if(shelf_mode && shelf_covers && shelf_page && !actions_mode && !transfer.impl && !wallpaper_ui.impl && !font_manage.impl && !settings_ui.impl && !locked && !status_page && !reading_menu && !touch_held && now_ms()>=last_input && now_ms()-last_input>=800){
            // 后台确有工作时回到最高频，封面/索引不在降频下变慢。/ Real background work returns to maximum so covers and indexing are not slowed by DFS.
            bool worked=available==PN_EMPTY && pn_catalog_index_build(&catalog_index)==PN_OK;worked|=tick_covers();if(worked){power_idle=0;pn_device_power_busy(PN_POWER_PRODUCT,true);}}
        bool background=false;
        if(body && available==PN_EMPTY && !touch_held){pn_status_t prefetched=epub.impl?pn_epub_app_prefetch(&epub):pn_reader_app_prefetch(&reader);
            // 真机连翻约1秒一页时，10ms间隔会让预热来不及；仍有字形待预热就以1ms让出继续，最多连续50轮。
            // On device, turning about once per second leaves 10 ms gaps too slow for warming; keep going with 1 ms yields while glyphs remain, at most 50 rounds in a row.
            background=prefetched==PN_OK || (epub.impl?pn_epub_app_warm_pending(&epub):pn_reader_app_warm_pending(&reader));
            if(background){power_idle=0;pn_device_power_busy(PN_POWER_PRODUCT,true);}}
        if(available!=PN_OK){if(power_idle<POWER_IDLE_ROUNDS)power_idle++;if(power_idle>=POWER_IDLE_ROUNDS)pn_device_power_busy(PN_POWER_PRODUCT,false);
            // 活动/会话/降级输入时10ms；长空闲时等输入通知，最多100ms处理定时职责。/ 10 ms while active, in sessions or with degraded input; when long idle wait for input notification, at most 100 ms for timed duties.
            bool slow=async_port.input_active && power_idle>=POWER_IDLE_ROUNDS && !transfer.impl && !time_sync.impl;
            if(background && background_rounds<50u){background_rounds++;vTaskDelay(pdMS_TO_TICKS(1));}
            else{background_rounds=0;if(async_port.product_task)pn_device_async_wait(&async_port,slow?100u:10u);else vTaskDelay(pdMS_TO_TICKS(10));}}
    }
}
void app_main(void){
    // 字体/解析独占一个较大栈的worker，不在小型启动栈上运行。/ Run font/parsing on one larger-stack worker, not the small boot stack.
    if(xTaskCreatePinnedToCore(device_task,"pn_device",PN_HW_PRODUCT_STACK_BYTES,NULL,3,NULL,1)!=pdPASS)ESP_LOGE(TAG,"Device worker creation failed");
}
