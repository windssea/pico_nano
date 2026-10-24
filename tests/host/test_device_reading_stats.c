/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实产品路由的计时、翻页、扫描失败和菜单排除，硬件为替身。/ English: timing, turns, scan failures and menu exclusion through real product routes with hardware substitutes.
 */
#define main stats_boot_fixture_main
#include "test_device_boot.c"
#undef main
#include "../../main/app_main.c"
#include "device_reader_step.h"
int main(int argc,char **argv){
    assert(argc==4);bool publication=strstr(argv[1],".epub")!=NULL;
    assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    assert(read_pico_init(&hardware)==ESP_OK);pn_media_init(&sd_media);pn_media_init(&data_media);
    assert(pn_media_attach(&sd_media,1)==PN_OK && pn_media_attach(&data_media,2)==PN_OK);
    int vcom;assert(read_pico_pmu_vcom_get(&vcom)==ESP_OK);epd_set_vcom((uint16_t)vcom);
    assert(mkdir(argv[2],0700)==0);data_ready=true;assert(pn_reading_stats_init(&reading_stats,&data_media,argv[2])==PN_OK);
    clock_known=true;clock_base_sec=1792339200;clock_base_ms=now_ms();
    pn_status_t status=publication?pn_epub_app_open_on_media(&epub,&pool,&sd_media,argv[1],argv[3],argv[2],32,now_ms()):pn_reader_app_open_on_media(&reader,&pool,&sd_media,argv[1],argv[3],argv[2],32,now_ms());
    assert(status==PN_OK && !reading_stats.loaded);
    assert(fixture_reader_step(PN_APP_OPEN,now_ms())==PN_OK && !reading_stats.loaded);
    if(publication){
        // 实际空闲路由必须能完成已有落盘再准备邻页，且不呈现/提交当前位置。/ The real idle route must finish an existing write then prepare the neighbor without presenting or committing a new position.
        pn_epub_progress_t before,after;assert(pn_epub_app_progress(&epub,&before)==PN_OK);unsigned shown=presents;
        clock_us+=2000000;assert(pn_epub_app_cache_idle(&epub,now_ms())==PN_BUSY && pn_epub_app_cache_pending(&epub) && pn_epub_app_prefetch_pending(&epub));
        unsigned ticks=0;while(pn_epub_app_cache_pending(&epub) || pn_epub_app_prefetch_pending(&epub)){assert(++ticks<4096);(void)tick_reader_background(now_ms());}
        assert(pn_epub_app_progress(&epub,&after)==PN_OK && !memcmp(&before.location,&after.location,sizeof before.location) && presents==shown && !reader_prefetch_batch);
    }
    reading_stats_poll(true);clock_us+=61000000;reading_stats_pause();assert(reading_stats.book.ms==61000 && !reading_stats.book.turns);
    // 正文可见时的预排不挡计时；菜单/锁屏/加载通过同一门控排除。/ Visible-body preparation keeps timing; menus, locking and loading use the same exclusion gate.
    for(unsigned i=0;i<3;i++){clock_us+=100000000;reading_stats_poll(false);assert(reading_stats.book.ms==61000);}
    reading_stats_poll(true);clock_us+=1000000;assert(fixture_reader_step(PN_APP_NEXT,now_ms())==PN_OK);assert(reading_stats.book.turns==1 && reading_stats.book.ms==62000 && !reading_stats.active);
    uint64_t turns=reading_stats.book.turns;panel_known=false;panel_fault=true;clock_us+=1000000;assert(fixture_reader_step(PN_APP_NEXT,now_ms())!=PN_OK);assert(reading_stats.book.turns==turns);panel_fault=false;
    reading_stats_poll(true);assert(!reading_stats.active);clock_us+=1000000;reading_stats_pause();assert(reading_stats.book.ms==62000);
    clock_us+=1000000;assert(fixture_reader_step(PN_APP_NEXT,now_ms())==PN_OK && reading_stats.book.turns==turns+1);
    clock_us+=1000000;assert(fixture_reader_step(PN_APP_PREVIOUS,now_ms())==PN_OK && reading_stats.book.turns==turns+2);
    clock_us+=1000000;assert(fixture_reader_step(PN_APP_OPEN,now_ms())==PN_OK && reading_stats.book.turns==turns+2);
    uint64_t retained=reading_stats.book.ms;reading_stats_flush();pn_reading_stats_t loaded;assert(pn_reading_stats_init(&loaded,&data_media,argv[2])==PN_OK);
    pn_book_id_t id;assert((publication?pn_epub_app_identity(&epub,&id):pn_reader_app_identity(&reader,&id))==PN_OK && pn_reading_stats_select(&loaded,&id)==PN_OK);
    assert(loaded.book.ms==retained && loaded.book.turns==3);pn_reading_summary_t summary=pn_reading_stats_summary(&loaded,true,UINT64_C(1792339200000));assert(summary.today_ms==retained && summary.today_turns==3 && summary.week_ms[0]==retained);
    // 真正关闭TXT/EPUB后借用保存快照，首页不再读同一日志；换卡仍拒绝缓存。/ Reuse saved TXT/EPUB snapshots after real close without rereading the journal; card changes still invalidate them.
    recent_snapshot=pn_alloc_tagged(&pool,PN_MEM_UI,sizeof *recent_snapshot);assert(recent_snapshot);recent_loaded=false;
    assert(stop_reader() && recent_loaded && recent_epoch==sd_media.epoch);
    bool available=data_media.available;data_media.available=false;assert(load_recent()==PN_OK);
    sd_media.epoch++;assert(load_recent()!=PN_OK);data_media.available=available;
    pn_free(recent_snapshot);recent_snapshot=NULL;pn_free(page_copy);page_copy=NULL;assert(!pool.used && !pool.live && !pn_media_active(&data_media) && !pn_media_active(&sd_media));
    printf("device reading stats: %s, first screen no stats I/O, body dwell, exclusions, confirmed turns, failed scan, redraw and reload passed\n",publication?"EPUB":"TXT");return 0;
}
