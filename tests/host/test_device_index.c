/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实设备开书门先完成整书表再显示，关闭选项先显示后后台。/ English: real device opening gate waits for a full table when enabled, or displays first and builds in the background. */
#define _POSIX_C_SOURCE 200809L
#define main index_boot_fixture_main
#include "test_device_boot.c"
#undef main
#include "pn_epub_app.h"
static unsigned index_calls;
static pn_status_t counted_index_step(pn_epub_app_t *app,pn_book_index_state_t *state){index_calls++;return pn_epub_app_index_step(app,state);}
#define pn_epub_app_index_step counted_index_step
#include "../../main/app_main.c"
#undef pn_epub_app_index_step
int main(int argc,char **argv){
 assert(argc==4);assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);assert(read_pico_init(&hardware)==ESP_OK);
 pn_media_init(&sd_media);pn_media_init(&data_media);assert(pn_media_attach(&sd_media,1)==PN_OK && pn_media_attach(&data_media,2)==PN_OK);
 int vcom;assert(read_pico_pmu_vcom_get(&vcom)==ESP_OK);epd_set_vcom((uint16_t)vcom);assert(!mkdir(argv[2],0700));char tf[PN_JOURNAL_PATH_MAX];assert(snprintf(tf,sizeof tf,"%s/tf",argv[2])<(int)sizeof tf);assert(!setenv("PN_READING_CACHE",tf,1));
 strcpy(selected_path,argv[1]);selected_format=PN_BOOK_EPUB;data_ready=true;index_wait=getenv("PN_TEST_WAIT_INDEX")!=NULL;index_wait_loaded=true;
 assert(pn_epub_app_open_on_media(&epub,&pool,&sd_media,argv[1],argv[3],argv[2],44,0)==PN_OK);pn_async_init(&async_port.input);
 unsigned before=presents;pn_status_t s=first_page_or_index();
 if(index_wait){assert(s==PN_BUSY && index_gate && opening);pn_epub_progress_t position;assert(pn_epub_app_progress(&epub,&position)==PN_EMPTY);unsigned ticks=0;while(index_gate){assert(++ticks<100000);clock_us+=10000;unsigned calls=index_calls;assert(tick_opening());if(ticks==1 && index_gate)assert(index_calls-calls>32);}assert(pn_epub_app_progress(&epub,&position)==PN_OK && presents>before);}
 else{assert(s==PN_OK && !index_gate && presents>before);unsigned ticks=0;while(pn_epub_app_index_pending(&epub)){assert(++ticks<100000);clock_us+=10000;(void)tick_reader_background(now_ms());}}
 pn_book_index_state_t info;assert(pn_epub_app_index_step(&epub,&info)==PN_OK && info.complete && info.pages);assert(stop_reader());
 pn_free(page_copy);pn_free(recent_snapshot);pn_free(shelf_page);pn_free(shelf_covers);pn_free(favorites);pn_menu_cache_close(&menu_cache);pn_font_close(&management_font);pn_w_font_scope_end(&management_scope);
 assert(!pool.used && !pool.live);puts("device index: first-display gate, background ordering, complete table and zero leaks passed");return 0;
}
