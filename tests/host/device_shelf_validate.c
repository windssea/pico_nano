/* SPDX-License-Identifier: Apache-2.0
 * 中文：用户书库驱动真实设备书架/手势/翻页路由，硬件只替换port。
 * English: supplied library drives real device shelf, gesture and paging routes with hardware ports replaced. */
#define main boot_fixture_main
#define read_pico_sd_get_info boot_fixture_sd_info
#include "test_device_boot.c"
#undef main
#undef read_pico_sd_get_info
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info){*info=(read_pico_sd_info_t){.mounted=true,.present=true};return ESP_OK;}
#include "../../main/app_main.c"
static pn_shelf_gesture_t gesture;
static pn_tap_press_t navigation;
static uint64_t tick=1000;
static void sample(unsigned count,unsigned event,int x,int y){
    cst836u_touch_t touch={.count=count,.x=x,.y=y};bool ending=false;
    for(unsigned i=0;i<count && i<CST836U_MAX_POINTS;i++)touch.points[i]=(cst836u_point_t){.active=event!=1,.event=event,.id=i,.x=x,.y=y};
    assert(pn_device_touch_normalize(&touch,&ending)==PN_OK);
    if(!root_navigation_touch(&navigation,touch.count,touch.x,touch.y,true,NULL))
        (void)shelf_content_touch(&gesture,touch.count,touch.x,touch.y,true,ending,tick+=30);
}
static void quick(bool previous,int drift){
    sample(1,0,previous?150:550,650);
    sample(1,1,previous?550:150,650+drift);sample(1,1,previous?550:150,650+drift);
}
int main(int argc,char **argv){
    assert(argc==2);unsigned columns=(unsigned)atoi(argv[1]);assert(columns==3 || columns==4 || columns==5);
    assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    assert(read_pico_init(&hardware)==ESP_OK);pn_media_init(&sd_media);pn_media_init(&data_media);pn_media_init(&wallpaper_media);
    assert(pn_media_attach(&sd_media,1)==PN_OK);int vcom;assert(read_pico_pmu_vcom_get(&vcom)==ESP_OK);epd_set_vcom((uint16_t)vcom);
    pn_async_init(&async_port.input);shelf_prefs.columns=columns==4?4:3;list_mode=columns==5;home_mode=false;
    show_shelf("",false);size_t capacity=columns==5?5:columns*3;assert(shelf_mode && !status_page && shelf_page->count==capacity && shelf_page->more && !shelf_page->index);
    char first[PN_CATALOG_NAME_MAX];strcpy(first,shelf_page->items[0].name);
    // 只有按下和UP终点也应翻页；再次UP不再计作按住。/ DOWN and an UP endpoint alone must page; another UP is never a held contact.
    quick(false,0);assert(shelf_page->index==capacity && !actions_mode && !reader_active());
    quick(true,0);assert(!shelf_page->index && !strcmp(first,shelf_page->items[0].name));
    quick(false,110);assert(shelf_page->index==capacity);quick(true,110);assert(!shelf_page->index);
    // 原普通移动+空触点释放仍有效；垂直/多点取消。/ Ordinary movement plus empty release still works; vertical and multitouch cancel.
    sample(1,0,550,650);sample(1,2,150,650);sample(0,0,0,0);sample(0,0,0,0);assert(shelf_page->index==capacity);
    quick(true,0);assert(!shelf_page->index);
    sample(1,0,550,500);sample(1,2,520,850);sample(1,1,150,500);sample(1,1,150,500);assert(!shelf_page->index);
    sample(1,0,550,650);sample(2,2,400,650);sample(1,1,150,650);sample(1,1,150,650);assert(!shelf_page->index);
    quick(true,0);assert(!shelf_page->index);
    unsigned flips=0;while(shelf_page->more){quick(false,0);assert(++flips<10);}
    size_t end=shelf_page->index;quick(false,0);assert(shelf_page->index==end);
    while(shelf_page->index){quick(true,0);assert(++flips<20);}assert(!strcmp(first,shelf_page->items[0].name));
    assert(!reader_active() && !actions_mode);
    // 主页面连续锁解锁，真实产品路由不得积累暂停或丢失主页。/ Repeated Home locks exercise real product routing without accumulating pauses or losing Home.
    assert(pn_media_attach(&data_media,1)==PN_OK);data_ready=true;wake_idle=true;home_mode=true;selected_path[0]=0;show_shelf("",false);assert(shelf_mode && !locked && home_mode);
    for(unsigned cycle=0;cycle<64;cycle++){clock_us+=100000;assert(prepare_lock());lock_and_sleep("按电源键继续");assert(!locked && shelf_mode && home_mode && !reader_active() && !atomic_load(&async_port.paused));}
    assert(sleep_calls==64);puts("home_lock cycles=64 resumed=1");
    pn_cover_work_abort(&cover_work);management_fonts_close();pn_free(shelf_page);pn_free(shelf_covers);pn_free(recent_snapshot);pn_free(favorites);pn_free(page_copy);pn_menu_cache_close(&menu_cache);
    assert(!pn_media_active(&sd_media) && pool.used==0 && pool.live==0);
    printf("device_shelf columns=%u capacity=%zu raw_up_swipe=1 diagonal_swipe=1 ordinary_swipe=1 edges_cancel=1 position_restored=1 used=0 live=0 peak=%zu\n",columns,capacity,pool.peak);return 0;
}
