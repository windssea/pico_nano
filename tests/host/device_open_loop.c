/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行真实产品主循环，硬件和等待port为替身；覆盖接管、退出与锁屏恢复。
 * English: run the real product loop with hardware/wait substitutes, covering takeover, exit and lock recovery. */
static void loop_delay(unsigned);
#define main boot_fixture_main
#define vTaskDelay boot_fixture_delay
#define read_pico_sd_get_info boot_fixture_sd_info
#include "test_device_boot.c"
#undef read_pico_sd_get_info
#undef vTaskDelay
#undef main
void vTaskDelay(unsigned n){loop_delay(n);}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info){*info=(read_pico_sd_info_t){.mounted=true,.present=true};return ESP_OK;}
#include "../../main/app_main.c"

static const char *book_path,*scenario;
static unsigned rounds,phase,opening_frames;
static void *retained_reader;
static unsigned idle_warms,idle_checks,warms_since_check,max_warms_between_checks;
static pn_epub_progress_t turn_before;
static unsigned turn_phase;
/// 通过真实异步邮箱投递，依次覆盖点击动作和右键，不直接调用翻页函数。/ Deliver through the real async mailbox, covering tap actions and Right without directly calling navigation.
static void queue_turn(bool key){
    pn_device_input_event_t input={.kind=key?PN_DASYNC_KEY:PN_DASYNC_ACTION,.at=now_ms(),.action=PN_APP_NEXT,.key=PN_KEY_3,.key_event=PN_KEY_SHORT};
    pn_async_event_t event={.navigation=true,.bytes=sizeof input,.view=atomic_load(&async_port.input.view)};
    memcpy(event.payload,&input,sizeof input);assert(pn_async_send(&async_port.input,&event)==PN_OK);
}
static uint64_t idle_clock(void *ctx){(void)ctx;return (uint64_t)clock_us;}
static void idle_observe(void *ctx,const pn_profile_event_t *e){
    (void)ctx;
    if(!strcmp(e->phase,"idle.verify")){idle_checks++;warms_since_check=0;}
    if(!strcmp(e->phase,"idle.prefetch") && e->status==PN_OK && now_ms()>=last_input && now_ms()-last_input>=300 && pn_epub_app_verify_pending(&epub)){
        idle_warms++;if(++warms_since_check>max_warms_between_checks)max_warms_between_checks=warms_since_check;
    }
}
void __wrap_pn_device_async_wait(pn_device_async_t *port,unsigned n){(void)port;loop_delay(n);}
static void loop_delay(unsigned n){
    clock_us+=(int64_t)n*1000;assert(++rounds<2000);
    if(!phase){
        assert(stop_reader());strcpy(selected_path,book_path);selected_format=PN_BOOK_EPUB;selected_identified=false;selected_size=0;
        if(!strcmp(scenario,"font")){
            pn_media_lease_t lease;pn_font_asset_t asset={0};pn_font_preferences_t prefs={0};
            assert(pn_media_acquire(&sd_media,PN_MEDIA_READ,&lease)==PN_OK);
            assert(pn_font_asset_open(&asset,&pool,&sd_media,&lease,"/sdcard/piconano/fonts/recovery.ttf",0,44)==PN_OK && pn_font_asset_details(&asset,&prefs.primary,NULL)==PN_OK);
            assert(pn_font_asset_close(&asset)==PN_OK && pn_media_release(&sd_media,&lease)==PN_OK);
            prefs.primary.identity.sha256[0]^=1;prefs.fallback.kind=PN_FONT_RESIDENT;
            assert(pn_media_acquire(&data_media,PN_MEDIA_WRITE,&lease)==PN_OK);pn_journal_files_t files;pn_journal_io_t io;
            assert(pn_font_preferences_files(&files,&data_media,&lease,"/data/progress",NULL,&io)==PN_OK && pn_font_preferences_save(&io,&pool,NULL,&prefs)==PN_OK);
            assert(pn_media_release(&data_media,&lease)==PN_OK);
        }
        if(!menu_cache.pool)assert(pn_menu_cache_init(&menu_cache,&pool)==PN_OK);
        bool hit,aux;assert(pn_menu_cache_begin(&menu_cache,123,1,&hit,&aux));pn_menu_cache_commit(&menu_cache,123,1);
        assert(pn_menu_cache_bytes(&menu_cache)>0);
        // 更早的空闲调度可能已经启动任务；明确重置本用例，并沿产品预计算入口先载入缓存。
        // Earlier idle scheduling may already have started a job; reset this scenario and load the cache as the product precompute entry does.
        identity_job_stop();identity_cache_ready();assert(pn_identity_cache_job_begin(&identity_job,&pool,&sd_media,book_path)==PN_OK);
        opening_frames=presents;
        start_reader();assert(opening && identity_takeover_active && !foreground_open.impl && !pn_menu_cache_bytes(&menu_cache));
        phase=1;return;
    }
    if(phase==1 && !strcmp(scenario,"cancel")){
        opening_cancel(PN_KEY_2);assert(!opening && !identity_takeover_active && !foreground_open.impl && !reader_active() && !async_port.input.controls_blocked && !*selected_path);
        assert(prepare_lock());wake_idle=true;unsigned before=presents;lock_and_sleep("loop cancel wake");
        assert(!locked && !opening && !reader_suspended && shelf_mode && presents>=before+2 && !atomic_load(&async_port.paused));
        phase=3;longjmp(end,1);
    }
    if(phase==1 && !strcmp(scenario,"lock")){
        assert(prepare_lock());assert(!opening && !identity_takeover_active && !foreground_open.impl);
        wake_idle=true;lock_and_sleep("loop opening wake");assert(!locked && !reader_suspended);phase=2;return;
    }
    if(phase==1 && !strcmp(scenario,"retry")){identity_job_stop();phase=2;return;}
    if(!opening){
        assert(reader_active() && !status_page && !identity_takeover_active && !foreground_open.impl && !async_port.input.controls_blocked);
        assert(!pn_menu_cache_bytes(&menu_cache));
        if(!strcmp(scenario,"turn")){
            pn_epub_progress_t at;assert(pn_epub_app_progress(&epub,&at)==PN_OK);
            if(!turn_phase){async_port.input_active=true;turn_before=at;turn_phase=1;queue_turn(false);return;}
            if(!memcmp(&at.location,&turn_before.location,sizeof at.location))return;
            if(turn_phase==1){turn_before=at;turn_phase=2;queue_turn(true);return;}
            assert(turn_phase==2 && !reader_turn_pending);printf("product NEXT tap and key both confirmed\n");
        }
        if(!strcmp(scenario,"success"))assert(presents==opening_frames+2u);
        if(!strcmp(scenario,"font")){
            // 推进模拟安静时钟，让复核与未完预热同时具备资格。/ Advance the fake quiet clock so verification competes with unfinished warming.
            if(!retained_reader){retained_reader=epub.impl;clock_us+=300000;}
            assert(epub.impl==retained_reader && !*trust_failed_path);
            if(!pn_epub_app_font_unavailable(&epub) || pn_epub_app_font_recovery_pending(&epub) || pn_epub_app_verify_pending(&epub))return;
        }
        for(unsigned i=0;i<3;i++){assert(prepare_lock() && reader_suspended);wake_idle=true;lock_and_sleep("loop reading wake");assert(!locked && !opening && !reader_suspended && reader_active());}
        phase=3;longjmp(end,1);
    }
}
int main(int argc,char **argv){
    assert(argc==3);book_path=argv[1];scenario=argv[2];
    if(!strcmp(scenario,"font"))pn_profile_port(idle_clock,idle_observe,NULL);
    if(!setjmp(end))device_task(NULL);
    if(!strcmp(scenario,"font")){assert(idle_warms && idle_checks && max_warms_between_checks<=1);pn_profile_port(NULL,NULL,NULL);printf("idle fairness warms=%u checks=%u max_warms_between=%u\n",idle_warms,idle_checks,max_warms_between_checks);}
    assert(phase==3);printf("product_loop scenario=%s rounds=%u opening_clear=1 wake_restored=1\n",scenario,rounds);
    // longjmp结束模拟任务；清理不能再借用该任务栈上的取消scope。/ longjmp ended the simulated task; cleanup must not borrow its stack cancellation scope.
    pn_cancel_scope_t shield;pn_cancel_enter(&shield,NULL,NULL);
    assert(stop_reader());system_font_release();pn_catalog_index_close(&catalog_index);
    pn_free(shelf_page);pn_free(shelf_covers);pn_free(recent_snapshot);pn_free(favorites);pn_free(page_copy);
    assert(!pn_media_active(&sd_media) && !pn_media_active(&data_media) && !pool.used && !pool.live);return 0;
}
