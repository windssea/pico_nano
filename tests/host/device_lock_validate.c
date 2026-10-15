/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实产品锁屏/唤醒路由，书籍/字体由用户样本副本传入，硬件只替换port。
 * English: real product lock/wake routing with supplied book/font copies and hardware ports replaced. */
#define main boot_fixture_main
#define pn_device_sleep_until_key boot_fixture_sleep
#define read_pico_sd_get_info boot_fixture_sd_info
#define epd_hl_update_screen_full boot_fixture_full
#include "test_device_boot.c"
#undef main
#undef pn_device_sleep_until_key
#undef read_pico_sd_get_info
#undef epd_hl_update_screen_full
#include "device_async.h"
static pn_device_async_t *lock_port;
static bool inject_power;
enum EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState *h,enum EpdDrawMode m,int t){
    enum EpdDrawError result=boot_fixture_full(h,m,t);
    if(inject_power){
        inject_power=false;pn_device_input_event_t input={.kind=PN_DASYNC_POWER,.at=(uint64_t)clock_us/1000};
        pn_async_event_t event={.bytes=sizeof input,.navigation=true,.urgent=true};memcpy(event.payload,&input,sizeof input);
        assert(pn_async_send(&lock_port->input,&event)==PN_OK);
    }
    return result;
}
bool pn_device_sleep_until_key(void){
    assert(lock_port && atomic_load(&lock_port->paused));pn_device_input_event_t input;
    assert(pn_device_async_take(lock_port,&input)==PN_EMPTY);sleep_calls++;clock_us+=2000000;return true;
}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info){*info=(read_pico_sd_info_t){.mounted=true,.present=true};return ESP_OK;}
#include "../../main/app_main.c"
static void check_position(const pn_txt_progress_t *text,const pn_epub_progress_t *publication){
    if(epub.impl){pn_epub_progress_t p;assert(pn_epub_app_progress(&epub,&p)==PN_OK);
        assert(!strcmp(p.location.path,publication->location.path) && !memcmp(&p.location.position,&publication->location.position,sizeof p.location.position));}
    else{pn_txt_progress_t p;assert(pn_reader_app_progress(&reader,&p)==PN_OK && p.source_offset==text->source_offset);}
}
int main(int argc,char **argv){
    assert(argc==5);bool is_epub=!strcmp(argv[1],"epub");
    assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    assert(read_pico_init(&hardware)==ESP_OK);pn_media_init(&sd_media);pn_media_init(&data_media);pn_media_init(&wallpaper_media);
    assert(pn_media_attach(&sd_media,1)==PN_OK);int vcom;assert(read_pico_pmu_vcom_get(&vcom)==ESP_OK);epd_set_vcom((uint16_t)vcom);
    // 内部锁屏存储不可用时走真实默认锁屏，不访问设备目录。/ Use the real default lock fallback when internal wallpaper storage is unavailable, without device paths.
    wake_idle=true;pn_async_init(&async_port.input);lock_port=&async_port;
    pn_status_t status=is_epub?pn_epub_app_open_on_media(&epub,&pool,&sd_media,argv[2],argv[3],argv[4],32,now_ms()):pn_reader_app_open_on_media(&reader,&pool,&sd_media,argv[2],argv[3],argv[4],32,now_ms());
    assert(status==PN_OK);assert(active_step(PN_APP_OPEN,now_ms())==PN_OK);
    clock_us+=100000;status=active_step(PN_APP_NEXT,now_ms());assert(status==PN_OK || status==PN_EMPTY);
    pn_txt_progress_t text={0};pn_epub_progress_t publication={0};
    assert((is_epub?pn_epub_app_progress(&epub,&publication):pn_reader_app_progress(&reader,&text))==PN_OK);
    void *original=is_epub?epub.impl:reader.impl;uint8_t confirmed[sizeof stub_back];memcpy(confirmed,stub_back,sizeof confirmed);
    for(unsigned round=0;round<3;round++){
        clock_us+=100000;assert(prepare_lock() && reader_suspended);inject_power=true;
        lock_and_sleep("按电源键继续阅读");
        assert(!inject_power && !locked && !reader_suspended && !status_page && !shelf_mode && !atomic_load(&async_port.paused));
        assert((is_epub?epub.impl:reader.impl)==original && !memcmp(confirmed,stub_back,sizeof confirmed));check_position(&text,&publication);
    }
    // 恢复扫描失败保留挂起帧，下一次电源键可原位重试，不关闭/重开或提前解锁。/ A failed restore retains the suspended frame for the next power-key retry without reopening or early unlocking.
    clock_us+=100000;assert(prepare_lock());locked=true;show_lock("按电源键继续阅读");panel_fault=true;
    resume_reader();assert(locked && reader_suspended && (is_epub?epub.impl:reader.impl)==original);check_position(&text,&publication);
    panel_fault=false;clock_us+=100000;resume_reader();assert(!locked && !reader_suspended && !memcmp(confirmed,stub_back,sizeof confirmed));check_position(&text,&publication);
    clock_us+=100000;assert(stop_reader());management_fonts_close();pn_free(page_copy);page_copy=NULL;
    assert(!pn_media_active(&sd_media) && pool.used==0 && pool.live==0 && sleep_calls==3);
    printf("device_lock kind=%s cycles=3 pixels_exact=1 position_exact=1 restore_failure_retry=1 stale_power_drained=1 used=%zu live=%zu peak=%zu\n",argv[1],pool.used,pool.live,pool.peak);
    return 0;
}
