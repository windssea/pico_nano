/* 翻页记录与设置页：默认、开关保存、不能全关、子页请求、占用失败、无存储。/ Page-turn record and settings page: defaults, saved toggles, not all off, sub-page requests, busy failures, no storage. */
#define _POSIX_C_SOURCE 200809L
#include "pn_settings_ui.h"
#include "pn_reader_input.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *capture;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)ctx;(void)profile;if(capture){FILE *f=fopen(capture,"wb");assert(f);fprintf(f,"P5 684 1216 15 ");for(int y=0;y<1216;y++)for(int x=0;x<684;x++)fputc(pn_frame_get(frame,x,y),f);fclose(f);}return PN_OK;}
static bool release(pn_reader_input_t *i,pn_reader_action_t *a){assert(!pn_reader_input_feed(i,0,0,0,true,a));return pn_reader_input_feed(i,0,0,0,true,a);}
static bool tap(pn_reader_input_t *i,int x,int y,pn_reader_action_t *a){assert(!pn_reader_input_feed(i,1,x,y,true,a));return release(i,a);}
static bool swipe(pn_reader_input_t *i,int x0,int y0,int x1,int y1,pn_reader_action_t *a){assert(!pn_reader_input_feed(i,1,x0,y0,true,a));assert(!pn_reader_input_feed(i,1,x1,y1,true,a));return release(i,a);}
int main(int argc,char **argv){
    capture=argc>1?argv[1]:NULL;
    /* 识别器阈值与偏好。/ Recognizer thresholds and preferences. */
    pn_reader_input_t in={0};pn_reader_action_t a;
    assert(tap(&in,171,500,&a) && a==PN_APP_PREVIOUS && tap(&in,513,500,&a) && a==PN_APP_NEXT && tap(&in,172,500,&a) && a==PN_APP_TOOLS && tap(&in,512,500,&a) && a==PN_APP_TOOLS);
    assert(swipe(&in,400,500,336,500,&a) && a==PN_APP_NEXT && !swipe(&in,400,500,337,500,&a) && swipe(&in,300,500,400,560,&a) && a==PN_APP_PREVIOUS && !swipe(&in,300,500,400,580,&a));
    in.config.left_hand=true;assert(tap(&in,100,500,&a) && a==PN_APP_NEXT && tap(&in,600,500,&a) && a==PN_APP_PREVIOUS && swipe(&in,400,500,300,500,&a) && a==PN_APP_NEXT);
    in.config=(pn_reader_input_config_t){.no_swipe=true};assert(!swipe(&in,400,500,300,500,&a) && tap(&in,600,500,&a) && a==PN_APP_NEXT);
    in.config=(pn_reader_input_config_t){.no_edge_tap=true};assert(!tap(&in,600,500,&a) && swipe(&in,400,500,300,500,&a) && tap(&in,342,500,&a) && a==PN_APP_TOOLS);
    /* 记录：无记录EMPTY、往返、未知标志拒绝。/ Record: EMPTY without one, round trip, unknown flags rejected. */
    char root[]="/tmp/pn-settings-XXXXXX";assert(mkdtemp(root));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    uint8_t flags=0xee;assert(pn_settings_load_flags(&media,root,&flags)==PN_OK && flags==0);
    pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);pn_journal_files_t files;pn_journal_io_t io;assert(pn_input_prefs_files(&files,&media,&lease,root,&io)==PN_OK);
    assert(pn_input_prefs_save(&io,0x40)==PN_INVALID && pn_input_prefs_save(&io,0x20)==PN_OK && pn_input_prefs_save(&io,PN_INPUT_LEFT_HAND)==PN_OK && pn_input_prefs_load(&io,&flags)==PN_OK && flags==PN_INPUT_LEFT_HAND);
    uint8_t future[6]={'P','N','I','P',2,0};assert(pn_journal_save(&io,future,6)==PN_OK && pn_input_prefs_load(&io,&flags)==PN_UNSUPPORTED);
    assert(pn_input_prefs_save(&io,0)==PN_OK);assert(pn_media_release(&media,&lease)==PN_OK);
    /* 设置页。/ Settings page. */
    pn_pool_t pool;assert(!pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL));pn_settings_ui_t ui={0};
    assert(pn_settings_ui_open(&ui,&pool,&media,root,present,NULL)==PN_OK && ui.active && ui.presented && ui.flags==0);
    /* 一级页：字体、壁纸、按键与手势、刷新与屏幕、存储与关于；底栏书架/传书交给调用方，标题区无返回。/ Root page: fonts, wallpaper, keys, refresh, about; the bottom-bar shelf/transfer go to the caller and the title has no back. */
    assert(pn_settings_ui_hit(&ui,100,220)==PN_SETUI_FONTS && pn_settings_ui_hit(&ui,100,380)==PN_SETUI_WALLPAPER && pn_settings_ui_hit(&ui,100,500)==PN_SETUI_KEYS && pn_settings_ui_hit(&ui,100,600)==PN_SETUI_REFRESH && pn_settings_ui_hit(&ui,100,750)==PN_SETUI_ABOUT && pn_settings_ui_hit(&ui,100,900)==-1 && pn_settings_ui_hit(&ui,100,60)==-1);
    assert(pn_settings_ui_hit(&ui,100,1150)==PN_SETUI_SHELF && pn_settings_ui_hit(&ui,340,1150)==PN_SETUI_TRANSFER && pn_settings_ui_hit(&ui,600,1150)==-1);
    assert(pn_settings_ui_event(&ui,PN_SETUI_TRANSFER,present,NULL)==PN_OK && ui.request==PN_SETUI_TRANSFER && ui.active);ui.request=0;
    /* 刷新与屏幕：三档策略立即保存，整屏刷新只重画本页。/ Refresh page: the three policies save at once and a full refresh only repaints this page. */
    assert(pn_settings_ui_event(&ui,PN_SETUI_REFRESH,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==2 && pn_settings_ui_hit(&ui,100,60)==PN_SETUI_BACK);
    assert(pn_settings_ui_hit(&ui,100,240)==PN_SETUI_LEVEL+1 && pn_settings_ui_hit(&ui,340,240)==PN_SETUI_LEVEL && pn_settings_ui_hit(&ui,560,240)==PN_SETUI_LEVEL+2 && pn_settings_ui_hit(&ui,300,560)==PN_SETUI_FULL_REFRESH);
    assert(pn_settings_ui_event(&ui,PN_SETUI_LEVEL+1,present,NULL)==PN_OK && (ui.flags&PN_INPUT_REFRESH_MASK)==0x10 && pn_settings_refresh_pages(ui.flags)==6);
    assert(pn_settings_ui_event(&ui,PN_SETUI_FULL_REFRESH,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_LEVEL,present,NULL)==PN_OK && pn_settings_refresh_pages(ui.flags)==14);
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && ui.active && pn_settings_ui_screen(&ui)==0);
    /* 存储与关于：只显示调用方填写的实际信息。/ Storage and about shows only what the caller actually read. */
    {pn_settings_about_t about={0};strcpy(about.version,"0.0.57");strcpy(about.storage,"共 29.7 GB · 可用 12.1 GB");pn_settings_ui_set_about(&ui,&about);
     assert(pn_settings_ui_event(&ui,PN_SETUI_ABOUT,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==3 && pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK);}
    /* 按键与手势：四个翻页开关。/ Keys and gestures: the four page-turn switches. */
    assert(pn_settings_ui_event(&ui,PN_SETUI_KEYS,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==1 && pn_settings_ui_hit(&ui,100,680)==PN_SETUI_TOGGLE && pn_settings_ui_hit(&ui,100,1000)==PN_SETUI_TOGGLE+3 && pn_settings_ui_hit(&ui,100,1100)==-1);
    assert(pn_settings_ui_event(&ui,PN_SETUI_TOGGLE,present,NULL)==PN_OK && ui.flags==PN_INPUT_LEFT_HAND && pn_settings_load_flags(&media,root,&flags)==PN_OK && flags==PN_INPUT_LEFT_HAND);
    assert(pn_settings_ui_event(&ui,PN_SETUI_TOGGLE+1,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_TOGGLE+2,present,NULL)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_TOGGLE+3,present,NULL)==PN_LIMIT && !(ui.flags&PN_INPUT_NO_KEYS));
    pn_media_lease_t writer;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&writer)==PN_OK);uint8_t before=ui.flags;
    assert(pn_settings_ui_event(&ui,PN_SETUI_TOGGLE+2,present,NULL)==PN_BUSY && ui.flags==before);assert(pn_media_release(&media,&writer)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && ui.active && pn_settings_ui_screen(&ui)==0);
    assert(pn_settings_ui_event(&ui,PN_SETUI_FONTS,present,NULL)==PN_OK && ui.request==PN_SETUI_FONTS && ui.active);ui.request=0;
    /* 局域网传书行：设置网络名后才出现，选中交给调用方。/ The LAN transfer row appears only after a network name is set and selecting it is handed to the caller. */
    assert(pn_settings_ui_hit(&ui,100,750)==PN_SETUI_ABOUT && pn_settings_ui_event(&ui,PN_SETUI_LAN,present,NULL)==PN_EMPTY && !ui.request);
    assert(pn_settings_ui_set_lan(&ui,"家里的WiFi",present,NULL)==PN_OK && pn_settings_ui_hit(&ui,100,750)==PN_SETUI_LAN && pn_settings_ui_hit(&ui,100,850)==PN_SETUI_ABOUT);
    assert(pn_settings_ui_event(&ui,PN_SETUI_LAN,present,NULL)==PN_OK && ui.request==PN_SETUI_LAN);ui.request=0;
    assert(pn_settings_ui_set_lan(&ui,"",present,NULL)==PN_OK && pn_settings_ui_hit(&ui,100,850)==-1);
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && !ui.active);pn_settings_ui_close(&ui);pn_settings_ui_close(&ui);assert(!pool.used && !pool.live);
    assert(pn_settings_load_flags(&media,root,&flags)==PN_OK && flags==(PN_INPUT_LEFT_HAND|PN_INPUT_NO_SWIPE|PN_INPUT_NO_EDGE_TAP));
    /* 无存储：显示默认、不能保存。/ No storage: defaults shown, nothing saved. */
    assert(pn_settings_ui_open(&ui,&pool,NULL,NULL,present,NULL)==PN_OK && ui.flags==0 && pn_settings_ui_event(&ui,PN_SETUI_TOGGLE,present,NULL)==PN_UNSUPPORTED && ui.flags==0);pn_settings_ui_close(&ui);
    /* 打开时逐分配失败回收。/ Every allocation failure during open recovers. */
    pn_pool_t probe;assert(!pn_pool_init(&probe,2u*1024u*1024u,NULL,NULL,NULL));assert(pn_settings_ui_open(&ui,&probe,&media,root,present,NULL)==PN_OK);size_t attempts=probe.attempts;pn_settings_ui_close(&ui);
    for(size_t fail=1;fail<=attempts;fail++){assert(!pn_pool_init(&probe,2u*1024u*1024u,NULL,NULL,NULL));probe.fail_at=fail;pn_status_t s=pn_settings_ui_open(&ui,&probe,&media,root,present,NULL);if(s==PN_OK)pn_settings_ui_close(&ui);else assert(!ui.impl);assert(!probe.used && !probe.live);}
    puts("settings: thresholds, left hand, swipe/tap switches, record, toggles, not-all-off, busy, requests, no storage, fault-injected open passed");return 0;
}
