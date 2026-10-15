/* SPDX-License-Identifier: Apache-2.0
 * 中文：编译真实设备资格路径及原波形，以SDK扫描替身验证半程参考、计数、定稿与退出恢复。
 * English: compile actual device qualification wiring and original waveforms; SDK scan doubles verify partial references, counts, settling and exit recovery. */
#define _POSIX_C_SOURCE 200809L
#include "e0470_epaper_waveform.h"
#define main boot_fixture_main
#include "test_device_boot.c"
#undef main
#include "../../main/app_main.c"
#include <unistd.h>
static uint8_t differences[684u*1216u];static bool lines[684];static uint8_t columns[608];
static unsigned phase_calls;static bool phase_fault,phase_underrun;
EpdRect epd_full_screen(void){return (EpdRect){0,0,1216,684};}
enum EpdDrawError epd_draw_base(EpdRect area,const uint8_t *data,EpdRect crop,enum EpdDrawMode mode,int temperature,const bool *dirty_lines,const uint8_t *dirty_columns,const EpdWaveform *wave){
    assert(area.width==1216 && area.height==684 && crop.width==1216 && crop.height==684 && mode==(MODE_PACKING_1PPB_DIFFERENCE|MODE_DU) && temperature==25);
    assert(data==differences && dirty_lines==lines && dirty_columns==columns && last_scan==READ_PICO_EPD_SCAN_FAST && !panel_known && atomic_load(&list_unknown));
    const EpdWaveformPhases *original=e0470_waveform_phases(&E0470_FOLLOW_WAVEFORM,1),*phase=wave->mode_data[0]->range_data[0];assert(phase->phases==1 && phase->luts==original->luts && phase->phase_times==original->phase_times);
    phase_calls++;if(phase_underrun){phase_underrun=false;return EPD_DRAW_EMPTY_LINE_QUEUE;}return phase_fault?EPD_DRAW_FAILURE:EPD_DRAW_SUCCESS;
}
static bool cancelled(void *ctx){(void)ctx;return true;}
static void pointer(unsigned count,int y,uint64_t now){pn_status_t s;assert(pn_font_manage_pointer(&font_manage,count,400,y,true,now,present,NULL,&s) && (s==PN_OK || s==PN_EMPTY));}
int main(int argc,char **argv){
    if(argc>1){int result=boot_fixture_main(argc,argv);assert(!list_vcom_valid && !presents && !phase_calls);return result;}
    assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);assert(read_pico_init(&hardware)==ESP_OK);e0470_waveform_init();hardware.hl.waveform=&E0470_WAVEFORM;hardware.hl.difference_fb=differences;hardware.hl.dirty_lines=lines;hardware.hl.dirty_columns=columns;
    int vcom;assert(read_pico_pmu_vcom_get(&vcom)==ESP_OK);epd_set_vcom((uint16_t)vcom);pn_async_init(&async_port.input);async_port.panel=present_panel;atomic_store(&async_port.body,false);
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,415872);pn_frame_t frame;assert(pixels && pn_frame_bind(&frame,pixels,415872,684,1216));pn_frame_clear(&frame,7);pn_follow_box_t area={32,334,620,600};pn_frame_rect(&frame,area.x,area.y,area.width,area.height,15);
    assert(list_present(NULL,PN_LIST_NORMALIZE,&frame,area,NULL)==PN_UNSUPPORTED && !presents);list_vcom_valid=true;
    assert(present(NULL,&frame,PN_REFRESH_GC16)==PN_OK && panel_known);unsigned revision=async_port.revision,quota=maintenance.management,clean=clear_calls;uint8_t *saved=malloc(415872);assert(saved);memcpy(saved,stub_back,415872);
    pn_frame_rect(&frame,32,334,8,9,0);assert(list_present(NULL,PN_LIST_NORMALIZE,&frame,area,NULL)==PN_OK && panel_known && last_scan==READ_PICO_EPD_SCAN_FULL && hardware.hl.waveform==&E0470_WAVEFORM && async_port.revision==revision && maintenance.management==quota);
    // 归一只覆盖列表：框外灰阶必须完全保留。/ Normalization touches only the list; exterior grays remain byte-for-byte unchanged.
    for(int x=0;x<684;x++)for(int y=0;y<1216;y++)if(x<32 || x>=652 || y<334 || y>=934){size_t at=(size_t)(683-x)*608+(size_t)y/2;unsigned shift=((unsigned)y&1u)*4u;assert(((stub_back[at]>>shift)&15)==((saved[at]>>shift)&15));}
    pn_follow_t follow={0};assert(pn_follow_open_area(&follow,&pool,&frame,area,7,8)==PN_OK);memcpy(saved,stub_back,415872);pn_frame_rect(&frame,32,334,8,9,15);pn_frame_rect(&frame,40,345,8,9,0);assert(pn_follow_mark(&follow,&frame,area)==PN_OK);
    pn_follow_job_t job;for(unsigned i=0;i<8;i++){assert(pn_follow_prepare(&follow,&job)==PN_OK);assert(list_present(NULL,PN_LIST_PHASE,&frame,area,&job)==PN_OK && !memcmp(stub_back,saved,415872) && pn_follow_stats(&follow).phases==i+1 && !pn_follow_stats(&follow).borrowed && async_port.revision==revision && maintenance.management==quota && last_scan==READ_PICO_EPD_SCAN_FULL);}
    assert(!pn_follow_stats(&follow).pending && phase_calls==8 && atomic_load(&list_unknown));assert(list_present(NULL,PN_LIST_REFERENCE,&frame,area,NULL)==PN_OK && panel_known && !atomic_load(&list_unknown));
    assert(memcmp(saved,stub_back,415872));pn_frame_rect(&frame,32,334,620,600,13);assert(list_present(NULL,PN_LIST_SETTLE,&frame,area,NULL)==PN_OK && panel_known && maintenance.management==quota && clear_calls==clean && async_port.revision==revision);
    assert(pn_follow_abandon(&follow)==PN_OK && pn_follow_close(&follow)==PN_OK);
    // 交接前取消保留计数；欠载不补写目标back，下一恢复用现有GC并还原时序。/ Prehandoff cancellation preserves counts; underruns never publish target back, and existing GC restores with timing returned.
    pn_frame_rect(&frame,32,334,620,600,15);assert(list_present(NULL,PN_LIST_NORMALIZE,&frame,area,NULL)==PN_OK && pn_follow_open_area(&follow,&pool,&frame,area,7,8)==PN_OK);pn_frame_pixel(&frame,32,334,0);assert(pn_follow_mark(&follow,&frame,area)==PN_OK && pn_follow_prepare(&follow,&job)==PN_OK);
    unsigned scans=phase_calls;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,NULL);assert(list_present(NULL,PN_LIST_PHASE,&frame,area,&job)==PN_CANCELLED && phase_calls==scans && pn_follow_stats(&follow).known && !pn_follow_stats(&follow).borrowed);pn_cancel_leave(&scope);
    memcpy(saved,stub_back,415872);phase_underrun=true;assert(pn_follow_prepare(&follow,&job)==PN_OK && list_present(NULL,PN_LIST_PHASE,&frame,area,&job)==PN_IO && !pn_follow_stats(&follow).known && !memcmp(saved,stub_back,415872) && atomic_load(&overlap_disabled) && last_pclk==PCLK_SAFE_MHZ && last_scan==READ_PICO_EPD_SCAN_FULL);
    assert(pn_follow_abandon(&follow)==PN_OK && pn_follow_close(&follow)==PN_OK);pn_frame_rect(&frame,32,334,620,600,13);clean=clear_calls;assert(list_present(NULL,PN_LIST_RECOVER,&frame,area,NULL)==PN_OK && clear_calls==clean+1 && panel_known && !atomic_load(&list_unknown) && async_port.revision==revision);
    assert(list_present(NULL,PN_LIST_ABANDON,&frame,area,NULL)==PN_OK && atomic_load(&list_unknown));clean=clear_calls;assert(present(NULL,&frame,PN_REFRESH_GL16)==PN_OK && clear_calls==clean+1 && panel_known && !atomic_load(&list_unknown));
    panel_fault=true;settle_recoverable=true;assert(list_present(NULL,PN_LIST_SETTLE,&frame,area,NULL)==PN_IO && !panel_known && atomic_load(&list_unknown) && !settle_recoverable && async_port.revision==revision+1);
    assert(list_present(NULL,PN_LIST_RECOVER,&frame,area,NULL)==PN_IO && !panel_known && atomic_load(&list_unknown));panel_fault=false;assert(list_present(NULL,PN_LIST_RECOVER,&frame,area,NULL)==PN_OK && panel_known && !atomic_load(&list_unknown));
    pn_free(pixels);free(saved);pn_free(page_copy);page_copy=NULL;
    // 用真实字体UI和设备端口保留半程驱动，再走实际普通重绘/首页/换卡/USB/锁屏出口。
    // Retain partial drives through the real font UI/device port, then exercise actual redraw/home/card/USB/lock exits.
    char directory[]="/tmp/pn-device-list-XXXXXX";assert(mkdtemp(directory));pn_text_source_t source=pn_font_builtin_source();uint8_t block[4096];
    for(unsigned i=0;i<24;i++){char path[256];snprintf(path,sizeof path,"%s/Font%02u.ttf",directory,i);FILE *f=fopen(path,"wb");assert(f);for(uint64_t at=0;at<source.size;){size_t n;assert(source.read_at(source.ctx,at,block,sizeof block,&n)==PN_OK && n && fwrite(block,1,n,f)==n);at+=n;}assert(!fclose(f));}
    pn_media_init(&sd_media);pn_media_init(&data_media);pn_media_init(&wallpaper_media);
    for(unsigned route=0;route<6;route++){
        if(!sd_media.available)assert(pn_media_attach(&sd_media,route+10)==PN_OK);
        assert(pn_font_manage_open(&font_manage,&pool,&sd_media,directory,NULL,NULL,present,NULL)==PN_OK && pn_font_manage_motion(&font_manage,list_present,NULL)==PN_OK);status_page=true;shelf_mode=false;
        pointer(1,850,100);pointer(1,550,1100);pointer(1,213,2100);assert(pn_font_manage_tick(&font_manage,2110)==PN_OK && pn_font_manage_motion_stats(&font_manage).pending && atomic_load(&list_unknown) && !panel_known);unsigned before=phase_calls;clean=clear_calls;
        if(route==0){assert(pn_font_manage_present(&font_manage,present,NULL)==PN_OK && !pn_font_manage_motion_stats(&font_manage).active);pn_font_manage_close(&font_manage);}
        else if(route==1){handle_key(PN_KEY_2,PN_KEY_SHORT);assert(!font_manage.impl && home_mode);}
        else if(route==2){assert(pn_media_detach(&sd_media)==PN_OK);message("卡已移除","保留记录");assert(!font_manage.impl);}
        else if(route==3){begin_usb();assert(!font_manage.impl);if(usb.open){pn_device_usb_request_stop(&usb);(void)pn_device_usb_poll(&usb,0);}}
        else if(route==4){assert(prepare_lock() && !pn_font_manage_motion_stats(&font_manage).active);wake_idle=true;show_lock("退出列表");wake_idle=false;assert(!font_manage.impl);}
        else{uint8_t *reference=malloc(415872);assert(reference);memcpy(reference,stub_back,415872);unsigned view=async_port.revision,debt=maintenance.management;pn_status_t s;assert(pn_font_manage_pointer(&font_manage,2,0,0,true,2200,present,NULL,&s));pointer(0,0,2210);
            for(unsigned i=0;i<7;i++){assert(pn_font_manage_tick(&font_manage,2300+i)==PN_OK && !memcmp(reference,stub_back,415872));}
            assert(!pn_font_manage_motion_stats(&font_manage).pending && pn_font_manage_tick(&font_manage,2520)==PN_OK && font_manage.presented && !pn_font_manage_motion_stats(&font_manage).active && async_port.revision==view && maintenance.management==debt);free(reference);before=phase_calls;pn_font_manage_close(&font_manage);}
        assert(phase_calls==before && (route<5?clear_calls>=clean+1:clear_calls==clean) && panel_known && !atomic_load(&list_unknown) && pn_font_manage_tick(&font_manage,9000)==PN_EMPTY && !pn_media_active(&sd_media));
    }
    for(unsigned i=0;i<24;i++){char path[256];snprintf(path,sizeof path,"%s/Font%02u.ttf",directory,i);assert(!remove(path));}assert(!rmdir(directory));
    // 同一设备出口实际接入目录：半程停止、完整确认和失败恢复都不能移动正文。
    // Bind the actual TOC through the same device port: partial exit, confirmation and recovery must preserve body position.
    const char *book=getenv("PN_TEST_TOC_BOOK"),*face=getenv("PN_TEST_TOC_FONT");
    if(book && face){
        pn_epub_progress_t before,after;status_page=false;home_mode=false;
        assert(pn_epub_app_open(&epub,&pool,book,face,NULL,44,0)==PN_OK && pn_epub_app_step(&epub,PN_APP_OPEN,1,present,NULL)==PN_OK && pn_epub_app_progress(&epub,&before)==PN_OK);
        for(unsigned route=0;route<3;route++){
            assert(begin_toc()==PN_OK && toc.motion_ctx && toc.presented);pn_status_t s;
            assert(pn_toc_ui_pointer(&toc,1,400,900,true,100,present,NULL,&s) && s==PN_OK);
            assert(pn_toc_ui_pointer(&toc,1,400,500,true,1100,present,NULL,&s) && s==PN_OK);
            assert(pn_toc_ui_pointer(&toc,1,400,250,true,2100,present,NULL,&s) && s==PN_OK && pn_toc_ui_motion_stats(&toc).offset==650);
            unsigned view=async_port.revision,debt=maintenance.management;
            if(route==2)phase_fault=true;
            assert(pn_toc_ui_tick(&toc,2110)==(route==2?PN_IO:PN_OK) && !panel_known && atomic_load(&list_unknown));phase_fault=false;
            assert(pn_epub_app_progress(&epub,&after)==PN_OK && !memcmp(&before.location.position,&after.location.position,sizeof before.location.position) && !strcmp(before.location.path,after.location.path));
            unsigned scans=phase_calls;
            if(route==0){clean=clear_calls;assert(pn_toc_ui_event(&toc,PN_TOC_UI_BACK,2120,present,NULL)==PN_OK && !toc.active && !toc.motion_ctx && clear_calls==clean+1 && phase_calls==scans);}
            else{
                assert(pn_toc_ui_pointer(&toc,2,0,0,true,2200,present,NULL,&s) && pn_toc_ui_pointer(&toc,0,0,0,true,2210,present,NULL,&s));
                if(route==1){uint8_t *reference=malloc(415872);assert(reference);memcpy(reference,stub_back,415872);for(unsigned i=0;i<7;i++)assert(pn_toc_ui_tick(&toc,2300+i)==PN_OK && !memcmp(reference,stub_back,415872));free(reference);assert(pn_toc_ui_tick(&toc,2520)==PN_OK);}
                else assert(pn_toc_ui_tick(&toc,2520)==PN_EMPTY && pn_toc_ui_tick(&toc,3110)==PN_OK);
                assert(toc.presented && !pn_toc_ui_motion_stats(&toc).active && !pn_epub_app_last_confirmed(&epub));
                assert(async_port.revision==view && maintenance.management==(route==2?0u:debt));
                assert(pn_toc_ui_event(&toc,PN_TOC_UI_BACK,3200,present,NULL)==PN_OK);
            }
            assert(panel_known && !atomic_load(&list_unknown) && pn_epub_app_progress(&epub,&after)==PN_OK && !memcmp(&before.location.position,&after.location.position,sizeof before.location.position) && !strcmp(before.location.path,after.location.path));
        }
        // 注入目录事件之后推进同一设备时钟，避免关闭测试制造倒退时间。
        // Advance the device clock beyond injected TOC events so closing never manufactures a backwards timestamp.
        clock_us+=4000000;assert(stop_reader() && !epub.impl && !toc.motion_ctx);pn_free(page_copy);page_copy=NULL;
    }
    management_fonts_close();pn_free(shelf_page);pn_free(shelf_covers);pn_free(recent_snapshot);pn_free(favorites);pn_catalog_index_close(&catalog_index);assert(!pool.used && !pool.live);
    puts("device list: real qualification/ports, VCOM, original phases, immutable back until drain, 7/8 receipts, revision/quota, canceled/underrun recovery and actual UI redraw/home/card/USB/lock barriers passed");return 0;
}
