/* 翻页记录与设置页：默认、开关保存、不能全关、子页请求、占用失败、无存储。/ Page-turn record and settings page: defaults, saved toggles, not all off, sub-page requests, busy failures, no storage. */
#define _POSIX_C_SOURCE 200809L
#include "pn_settings_ui.h"
#include "pn_wallpaper.h"
#include "pn_widgets.h"
#include "pn_reader_input.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *capture;
static unsigned presents;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)ctx;(void)profile;presents++;if(capture){FILE *f=fopen(capture,"wb");assert(f);fprintf(f,"P5 684 1216 15 ");for(int y=0;y<1216;y++)for(int x=0;x<684;x++)fputc(pn_frame_get(frame,x,y),f);fclose(f);}return PN_OK;}
static bool release(pn_reader_input_t *i,pn_reader_action_t *a){assert(!pn_reader_input_feed(i,0,0,0,true,a));return pn_reader_input_feed(i,0,0,0,true,a);}
static bool tap(pn_reader_input_t *i,int x,int y,pn_reader_action_t *a){assert(!pn_reader_input_feed(i,1,x,y,true,a));return release(i,a);}
static bool swipe(pn_reader_input_t *i,int x0,int y0,int x1,int y1,pn_reader_action_t *a){assert(!pn_reader_input_feed(i,1,x0,y0,true,a));assert(!pn_reader_input_feed(i,1,x1,y1,true,a));return release(i,a);}
int main(int argc,char **argv){
    capture=argc>1?argv[1]:NULL;
    /* 识别器阈值与偏好。/ Recognizer thresholds and preferences. */
    pn_reader_input_t in={0};pn_reader_action_t a;
    assert(tap(&in,171,500,&a) && a==PN_APP_PREVIOUS && tap(&in,513,500,&a) && a==PN_APP_NEXT && tap(&in,341,500,&a) && a==PN_APP_PREVIOUS && tap(&in,342,500,&a) && a==PN_APP_NEXT);
    assert(swipe(&in,400,500,336,500,&a) && a==PN_APP_NEXT && !swipe(&in,400,500,337,500,&a) && swipe(&in,300,500,400,560,&a) && a==PN_APP_PREVIOUS && !swipe(&in,300,500,400,580,&a));
    in.config.left_hand=true;assert(tap(&in,100,500,&a) && a==PN_APP_NEXT && tap(&in,600,500,&a) && a==PN_APP_PREVIOUS && swipe(&in,400,500,300,500,&a) && a==PN_APP_NEXT);
    in.config=(pn_reader_input_config_t){.no_swipe=true};assert(!swipe(&in,400,500,300,500,&a) && tap(&in,600,500,&a) && a==PN_APP_NEXT);
    in.config=(pn_reader_input_config_t){.no_edge_tap=true};assert(!tap(&in,600,500,&a) && swipe(&in,400,500,300,500,&a) && !tap(&in,342,500,&a));
    /* 记录：无记录EMPTY、往返、未知标志拒绝。/ Record: EMPTY without one, round trip, unknown flags rejected. */
    char root[]="/tmp/pn-settings-XXXXXX";assert(mkdtemp(root));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    uint8_t flags=0xee;assert(pn_settings_load_flags(&media,root,&flags)==PN_OK && flags==PN_INPUT_FLAGS_DEFAULT);
    pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);pn_journal_files_t files;pn_journal_io_t io;assert(pn_input_prefs_files(&files,&media,&lease,root,&io)==PN_OK);
    assert(pn_input_prefs_save(&io,0xc0)==PN_OK && pn_input_prefs_load(&io,&flags)==PN_OK && flags==0xc0 && pn_input_prefs_save(&io,0x20)==PN_OK && pn_input_prefs_save(&io,PN_INPUT_LEFT_HAND)==PN_OK && pn_input_prefs_load(&io,&flags)==PN_OK && flags==PN_INPUT_LEFT_HAND);
    uint8_t future[6]={'P','N','I','P',4,0};assert(pn_journal_save(&io,future,6)==PN_OK && pn_input_prefs_load(&io,&flags)==PN_UNSUPPORTED);
    // v3只承载水波纹；v2带样式3或v3不带都是损坏的混合。/ v3 carries only ripple; v2 with style 3 or v3 without it are inconsistent mixes.
    uint8_t mixed[6]={'P','N','I','P',3,0};assert(pn_journal_save(&io,mixed,6)==PN_OK && pn_input_prefs_load(&io,&flags)==PN_UNSUPPORTED);mixed[4]=2;mixed[5]=0xc0;assert(pn_journal_save(&io,mixed,6)==PN_OK && pn_input_prefs_load(&io,&flags)==PN_UNSUPPORTED);
    /* 外观记录：默认、非法密度、CRC回退、未来版本和错误不改输出。/ Appearance records: defaults, invalid density, CRC fallback, future versions and output preservation on errors. */
    {pn_journal_files_t sf;pn_journal_io_t sio;pn_shelf_prefs_t prefs=pn_shelf_prefs_default(),out={4,PN_SHELF_PAPER};
     assert(pn_shelf_prefs_files(&sf,&media,&lease,root,&sio)==PN_OK && pn_shelf_prefs_load(&sio,&out)==PN_EMPTY && out.columns==4);
     assert(pn_shelf_prefs_save(&sio,&prefs)==PN_OK);prefs.columns=2;assert(pn_shelf_prefs_save(&sio,&prefs)==PN_INVALID);
     prefs=(pn_shelf_prefs_t){4,PN_SHELF_WOOD};assert(pn_shelf_prefs_save(&sio,&prefs)==PN_OK);
     FILE *bad=fopen(sf.paths[1],"r+b");assert(bad && !fseek(bad,-1,SEEK_END));int byte=fgetc(bad);assert(byte>=0 && !fseek(bad,-1,SEEK_END) && fputc(byte^1,bad)>=0 && !fclose(bad));
     assert(pn_shelf_prefs_load(&sio,&out)==PN_OK && out.columns==3 && out.style==PN_SHELF_DEFAULT);
     uint8_t unknown[8]={'P','N','S','H',2,4,0,0};assert(pn_journal_save(&sio,unknown,8)==PN_OK && pn_shelf_prefs_load(&sio,&out)==PN_UNSUPPORTED && out.columns==3);
     unknown[4]=1;unknown[5]=7;assert(pn_journal_save(&sio,unknown,8)==PN_OK && pn_shelf_prefs_load(&sio,&out)==PN_CORRUPT && out.columns==3);
     prefs=pn_shelf_prefs_default();assert(pn_shelf_prefs_save(&sio,&prefs)==PN_OK);
    }
    assert(pn_input_prefs_save(&io,0)==PN_OK);assert(pn_media_release(&media,&lease)==PN_OK);
    /* 样式与旧版本：新记录持久化，旧版无样式。/ Styles and legacy versions: persist new records, legacy has no effect. */
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
    assert(pn_input_prefs_files(&files,&media,&lease,root,&io)==PN_OK);
    assert(pn_input_prefs_save(&io,0x80)==PN_OK && pn_input_prefs_load(&io,&flags)==PN_OK && flags==0x80);
    uint8_t legacy[6]={'P','N','I','P',1,PN_INPUT_LEFT_HAND};assert(pn_journal_save(&io,legacy,6)==PN_OK && pn_input_prefs_load(&io,&flags)==PN_OK && flags==PN_INPUT_LEFT_HAND);
    assert(pn_input_prefs_save(&io,0xc0)==PN_OK && pn_input_prefs_load(&io,&flags)==PN_OK && flags==0xc0);
    assert(pn_input_prefs_save(&io,0)==PN_OK && pn_media_release(&media,&lease)==PN_OK);
    /* 设置页。/ Settings page. */
    pn_pool_t pool;assert(!pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL));pn_settings_ui_t ui={0};
    assert(pn_settings_ui_open(&ui,&pool,&media,root,present,NULL)==PN_OK && ui.active && ui.presented && ui.flags==0);
    assert(!ui.index_wait && pn_settings_ui_event(&ui,PN_SETUI_ABOUT,present,NULL)==PN_OK);
    assert(pn_settings_ui_hit(&ui,400,750)==PN_SETUI_TIME && pn_settings_ui_hit(&ui,400,870)==PN_SETUI_UPDATE);
    for(int y=0;y<1216;y++)assert(pn_settings_ui_hit(&ui,400,y)!=PN_SETUI_INDEX_WAIT);
    /* 隐藏入口后保留旧存档及内部命令兼容，不静默重写偏好。/ Preserve old preferences and internal commands after hiding the entry, without silently rewriting settings. */
    assert(pn_settings_ui_event(&ui,PN_SETUI_INDEX_WAIT,present,NULL)==PN_OK && ui.index_wait);
    bool wait=false;assert(pn_settings_load_index(&media,root,&wait)==PN_OK && wait);
    assert(pn_settings_ui_event(&ui,PN_SETUI_INDEX_WAIT,present,NULL)==PN_OK && !ui.index_wait);
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_INDEX_WAIT,present,NULL)==PN_BUSY && !ui.index_wait);
    assert(pn_media_release(&media,&lease)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK);

    /* 一级页：显示与字体（字体、壁纸、书架与主页、状态栏设置）、阅读（全局排版、按键与手势、刷新与屏幕）、设备（WiFi、存储与关于）；标题区无按钮。/ Root page groups: display (with Status Bar), reading (with Global Typesetting), and device; single page without scrolling. */
    assert(pn_settings_ui_hit(&ui,100,180)==PN_SETUI_FONTS && pn_settings_ui_hit(&ui,100,270)==PN_SETUI_WALLPAPER && pn_settings_ui_hit(&ui,100,360)==PN_SETUI_APPEARANCE && pn_settings_ui_hit(&ui,100,440)==PN_SETUI_STATUS_BAR && pn_settings_ui_hit(&ui,100,570)==PN_SETUI_GLOBAL_STYLE && pn_settings_ui_hit(&ui,100,660)==PN_SETUI_KEYS && pn_settings_ui_hit(&ui,100,740)==PN_SETUI_REFRESH && pn_settings_ui_hit(&ui,100,870)==PN_SETUI_WIFI && pn_settings_ui_hit(&ui,100,950)==PN_SETUI_ABOUT && pn_settings_ui_hit(&ui,100,1050)==-1 && pn_settings_ui_hit(&ui,100,60)==-1 && pn_settings_ui_hit(&ui,560,100)==-1);
    /* 全局排版与单书排版对齐（含全部12项、边距详调、阅读背景、书内图片/上下栏与重置），以及主页个性化与顶部横幅快速缓存。/ Global typesetting parity with book style (all 12 fields, margins, backgrounds, images/bars & reset) plus Home personalization & fast banner cache. */
    assert(pn_settings_ui_event(&ui,PN_SETUI_GLOBAL_STYLE,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_ADJ+1,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_BARS,present,NULL)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_IMAGES,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_ADJ+15,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_EXT+1,present,NULL)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_MARGINS,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_MADJ+1,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_BG,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_BG_ITEM+2,present,NULL)==PN_OK);
    // 负字间距与“全局阅读线与章节”子页立即持久化。/ Negative tracking and global reading-guide & chapter sub-page save immediately.
    assert(pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_ADJ+10,present,NULL)==PN_OK);
    assert(pn_settings_ui_hit(&ui,200,1010)==PN_SETUI_GSTYLE_CHAPTER);
    assert(pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_CHAPTER,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_CADJ+1,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_CADJ+3,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_CADJ+5,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_CADJ+7,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_CADJ+13,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK);
    {pn_style_t gs={0};assert(pn_style_global_load(&media,root,&gs)==PN_OK && gs.pixels==46 && gs.tracking_percent==-5 && gs.reading_line_follow==1 && gs.reading_line_thickness_extra==1 && gs.chapter_scale_percent==130 && gs.chapter_align==1 && gs.footer_title_mode==1 && gs.hide_bars==1 && gs.hide_images==1 && gs.clarity==1 && gs.reading_line==1 && gs.margin_top==4 && gs.background==7);}
    assert(pn_settings_ui_event(&ui,PN_SETUI_GSTYLE_RESET,present,NULL)==PN_OK);
    {pn_style_t gs={0};assert(pn_style_global_load(&media,root,&gs)==PN_OK && gs.pixels==44 && !gs.hide_bars && !gs.hide_images && !gs.margin_top && !gs.background);}
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==0);
    {uint8_t *bpx=pn_alloc(&pool,PN_HOME_BANNER_BYTES),*bout=pn_alloc(&pool,PN_HOME_BANNER_BYTES);assert(bpx && bout);memset(bpx,0x5a,PN_HOME_BANNER_BYTES);
     assert(pn_home_banner_cache_save(&media,root,"demo.png",bpx,PN_HOME_BANNER_BYTES)==PN_OK);
     assert(pn_home_banner_cache_load(&media,root,"other.png",bout,PN_HOME_BANNER_BYTES)==PN_EMPTY);
     assert(pn_home_banner_cache_load(&media,root,"demo.png",bout,PN_HOME_BANNER_BYTES)==PN_OK && !memcmp(bpx,bout,PN_HOME_BANNER_BYTES));
     pn_home_banner_cache_clear(&media,root);
     assert(pn_home_banner_cache_load(&media,root,"demo.png",bout,PN_HOME_BANNER_BYTES)==PN_EMPTY);
     pn_free(bpx);pn_free(bout);}
    assert(pn_settings_ui_event(&ui,PN_SETUI_APPEARANCE,present,NULL)==PN_OK && pn_settings_ui_hit(&ui,200,980)==PN_SETUI_HOME_MOTTO && pn_settings_ui_hit(&ui,200,1080)==PN_SETUI_HOME_BANNER);
    assert(pn_settings_ui_event(&ui,PN_SETUI_HOME_MOTTO,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_MOTTO_PRESET+1,present,NULL)==PN_OK && !strcmp(ui.home.header,"开卷有益") && !strcmp(pn_w_brand(),"开卷有益"));
    {pn_home_prefs_t hp={0};assert(pn_home_prefs_load_dir(&media,root,&hp)==PN_OK && !strcmp(hp.header,"开卷有益"));}
    assert(pn_settings_ui_event(&ui,PN_SETUI_MOTTO_RESET,present,NULL)==PN_OK && !*ui.home.header && !*pn_w_brand());
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_HOME_BANNER,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_BANNER_DEFAULT,present,NULL)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==0);
    assert(pn_settings_ui_hit(&ui,100,1150)==PN_SETUI_HOME && pn_settings_ui_hit(&ui,260,1150)==PN_SETUI_SHELF && pn_settings_ui_hit(&ui,430,1150)==PN_SETUI_TRANSFER && pn_settings_ui_hit(&ui,600,1150)==-1);
    assert(pn_settings_ui_event(&ui,PN_SETUI_TRANSFER,present,NULL)==PN_OK && ui.request==PN_SETUI_TRANSFER && ui.active);ui.request=0;
    /* 刷新与屏幕：三档策略立即保存，整屏刷新只重画本页。/ Refresh page: the three policies save at once and a full refresh only repaints this page. */
    assert(pn_settings_ui_event(&ui,PN_SETUI_REFRESH,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==2 && pn_settings_ui_hit(&ui,100,60)==PN_SETUI_BACK);
    assert(pn_settings_ui_hit(&ui,100,240)==PN_SETUI_LEVEL+1 && pn_settings_ui_hit(&ui,340,240)==PN_SETUI_LEVEL && pn_settings_ui_hit(&ui,560,240)==PN_SETUI_LEVEL+2 && pn_settings_ui_hit(&ui,300,560)==PN_SETUI_FULL_REFRESH);
    assert(pn_settings_ui_event(&ui,PN_SETUI_LEVEL+1,present,NULL)==PN_OK && (ui.flags&PN_INPUT_REFRESH_MASK)==0x10 && pn_settings_refresh_pages(ui.flags)==6);
    assert(pn_settings_ui_event(&ui,PN_SETUI_FULL_REFRESH,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_LEVEL,present,NULL)==PN_OK && pn_settings_refresh_pages(ui.flags)==14);
    assert(pn_settings_ui_event(&ui,PN_SETUI_TURN+2,present,NULL)==PN_EMPTY && !(ui.flags&PN_INPUT_TURN_MASK));
    /* 翻页效果：直接/高清/水波纹/极速四段，分别保存为样式 0/1/3/2。/ Turn effect: direct/crisp/ripple/fast 4 segments saving as style 0/1/3/2. */
    assert(pn_settings_ui_hit(&ui,100,940)==PN_SETUI_TURN && pn_settings_ui_hit(&ui,260,940)==PN_SETUI_TURN_CRISP && pn_settings_ui_hit(&ui,420,940)==PN_SETUI_TURN+1 && pn_settings_ui_hit(&ui,580,940)==PN_SETUI_TURN_FAST);
    /* 自动锁屏时间：默认5分钟，5段位（1/5/10/30分钟、从不）立即保存，占用失败保留旧值，非法与未来记录拒绝覆盖。/ Auto-lock timeout: default 5 min, 5 presets persist immediately, busy preserves old value, invalid/future records rejected. */
    assert(ui.auto_lock_minutes==PN_AUTO_LOCK_DEFAULT_MIN);
    assert(pn_settings_auto_lock_ms(0u)==0u && pn_settings_auto_lock_ms(1u)==60000u && pn_settings_auto_lock_ms(5u)==300000u && pn_settings_auto_lock_ms(30u)==1800000u && pn_settings_auto_lock_ms(999u)==300000u);
    assert(pn_settings_ui_hit(&ui,85,718)==PN_SETUI_AUTO_LOCK && pn_settings_ui_hit(&ui,210,718)==PN_SETUI_AUTO_LOCK+1 && pn_settings_ui_hit(&ui,340,718)==PN_SETUI_AUTO_LOCK+2 && pn_settings_ui_hit(&ui,468,718)==PN_SETUI_AUTO_LOCK+3 && pn_settings_ui_hit(&ui,595,718)==PN_SETUI_AUTO_LOCK+4);
    {uint16_t al=99u;assert(pn_settings_load_auto_lock(&media,root,&al)==PN_OK && al==5u);
     assert(pn_settings_ui_event(&ui,PN_SETUI_AUTO_LOCK+0,present,NULL)==PN_OK && ui.auto_lock_minutes==1u && pn_settings_load_auto_lock(&media,root,&al)==PN_OK && al==1u);
     assert(pn_settings_ui_event(&ui,PN_SETUI_AUTO_LOCK+4,present,NULL)==PN_OK && ui.auto_lock_minutes==0u && pn_settings_load_auto_lock(&media,root,&al)==PN_OK && al==0u);
     assert(pn_settings_ui_event(&ui,PN_SETUI_AUTO_LOCK+3,present,NULL)==PN_OK && ui.auto_lock_minutes==30u && pn_settings_load_auto_lock(&media,root,&al)==PN_OK && al==30u);
     pn_media_lease_t al_busy;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&al_busy)==PN_OK);
     assert(pn_settings_ui_event(&ui,PN_SETUI_AUTO_LOCK+2,present,NULL)==PN_BUSY && ui.auto_lock_minutes==30u);
     assert(pn_media_release(&media,&al_busy)==PN_OK);
     assert(pn_settings_ui_event(&ui,PN_SETUI_AUTO_LOCK+2,present,NULL)==PN_OK && ui.auto_lock_minutes==10u && pn_settings_load_auto_lock(&media,root,&al)==PN_OK && al==10u);
     // 验证底层 A/B 记录边界校验（超范围、未来版本与不覆盖未知记录）。/ Verify A/B record validation (bounds, future versions, no overwrite).
     pn_media_lease_t al_l;pn_journal_files_t al_f;pn_journal_io_t al_io;
     assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&al_l)==PN_OK && pn_auto_lock_prefs_files(&al_f,&media,&al_l,root,&al_io)==PN_OK);
     assert(pn_auto_lock_prefs_save(&al_io,PN_AUTO_LOCK_MAX_MIN+1u)==PN_INVALID);
     uint8_t bad_ver[8]={'P','N','A','L',2,0,15,0};assert(pn_journal_save(&al_io,bad_ver,8)==PN_OK && pn_auto_lock_prefs_load(&al_io,&al)==PN_UNSUPPORTED && al==10u && pn_auto_lock_prefs_save(&al_io,5u)==PN_UNSUPPORTED);
     uint8_t bad_val[8]={'P','N','A','L',1,0,250,0};assert(pn_journal_save(&al_io,bad_val,8)==PN_OK && pn_auto_lock_prefs_load(&al_io,&al)==PN_CORRUPT && al==10u);
     uint8_t restore[8]={'P','N','A','L',1,0,10,0};assert(pn_journal_save(&al_io,restore,8)==PN_OK && pn_auto_lock_prefs_load(&al_io,&al)==PN_OK && al==10u);
     assert(pn_media_release(&media,&al_l)==PN_OK);
    }
    assert(pn_settings_ui_event(&ui,PN_SETUI_TURN_CRISP,present,NULL)==PN_OK && ((ui.flags&PN_INPUT_TURN_MASK)>>6)==1u && pn_settings_load_flags(&media,root,&flags)==PN_OK && flags==ui.flags);
    assert(pn_settings_ui_event(&ui,PN_SETUI_TURN_FAST,present,NULL)==PN_OK && ((ui.flags&PN_INPUT_TURN_MASK)>>6)==2u && pn_settings_load_flags(&media,root,&flags)==PN_OK && flags==ui.flags);
    assert(pn_settings_ui_event(&ui,PN_SETUI_TURN+1,present,NULL)==PN_OK && (ui.flags&PN_INPUT_TURN_MASK)==PN_INPUT_TURN_MASK && pn_settings_load_flags(&media,root,&flags)==PN_OK && flags==ui.flags);
    assert(pn_settings_ui_event(&ui,PN_SETUI_TURN+1,present,NULL)==PN_EMPTY && pn_settings_ui_event(&ui,PN_SETUI_TURN,present,NULL)==PN_OK && !(ui.flags&PN_INPUT_TURN_MASK));
    assert(pn_settings_load_flags(&media,root,&flags)==PN_OK && flags==ui.flags);
    assert(pn_settings_ui_event(&ui,PN_SETUI_TURN,present,NULL)==PN_EMPTY && !(ui.flags&PN_INPUT_TURN_MASK));
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && ui.active && pn_settings_ui_screen(&ui)==0);
    /* 存储与关于：只显示调用方填写的实际信息。/ Storage and about shows only what the caller actually read. */
    {pn_settings_about_t about={0};strcpy(about.version,"0.0.57");strcpy(about.storage,"共 29.7 GB · 可用 12.1 GB");pn_settings_ui_set_about(&ui,&about);
     assert(pn_settings_ui_event(&ui,PN_SETUI_ABOUT,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==3 && pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK);}
    /* 日期显示和联网请求由控制层推进，UI不等待网络。 / Date display and network requests are advanced by the controller without UI network waits. */
    {pn_settings_time_t clock={0};strcpy(clock.datetime,"unchanged");assert(pn_settings_time_format(&clock,123)==PN_INVALID && !strcmp(clock.datetime,"unchanged"));
     assert(pn_settings_time_format(&clock,1709222400u)==PN_OK && !strcmp(clock.datetime,"2024-03-01 00:00"));
     assert(pn_settings_time_format(&clock,4102444799u)==PN_OK && !strcmp(clock.datetime,"2100-01-01 07:59"));
     assert(pn_settings_ui_event(&ui,PN_SETUI_ABOUT,present,NULL)==PN_OK && pn_settings_ui_hit(&ui,100,750)==PN_SETUI_TIME);
     pn_settings_ui_set_time(&ui,&clock);assert(pn_settings_ui_event(&ui,PN_SETUI_TIME,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==5);
     assert(pn_settings_ui_hit(&ui,100,740)==-1 && pn_settings_ui_event(&ui,PN_SETUI_TIME_SYNC,present,NULL)==PN_EMPTY);
     clock.available=true;pn_settings_ui_set_time(&ui,&clock);assert(pn_settings_ui_hit(&ui,100,740)==PN_SETUI_TIME_SYNC);
     ui.presented=false;assert(pn_settings_ui_event(&ui,PN_SETUI_TIME_SYNC,present,NULL)==PN_BUSY);ui.presented=true;
     assert(pn_settings_ui_event(&ui,PN_SETUI_TIME_SYNC,present,NULL)==PN_OK && ui.request==PN_SETUI_TIME_SYNC);ui.request=0;
     clock.running=true;pn_settings_ui_set_time(&ui,&clock);assert(pn_settings_ui_hit(&ui,100,740)==PN_SETUI_TIME_CANCEL && pn_settings_ui_event(&ui,PN_SETUI_TIME_SYNC,present,NULL)==PN_EMPTY);
     assert(pn_settings_ui_event(&ui,PN_SETUI_TIME_CANCEL,present,NULL)==PN_OK && ui.request==PN_SETUI_TIME_CANCEL);ui.request=0;
     assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==3);
     assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==0);}
    /* 按键与手势：四个翻页开关与上下翻页模式。/ Keys and gestures: the four page-turn switches and vertical page-turn mode. */
    assert(pn_settings_ui_event(&ui,PN_SETUI_KEYS,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==1 && pn_settings_ui_hit(&ui,100,680)==PN_SETUI_TOGGLE && pn_settings_ui_hit(&ui,100,1000)==PN_SETUI_TOGGLE+3 && pn_settings_ui_hit(&ui,100,1100)==PN_SETUI_VERTICAL_TURN && pn_settings_ui_hit(&ui,100,1160)==-1);
    assert(!ui.vertical_turn && pn_settings_ui_event(&ui,PN_SETUI_VERTICAL_TURN,present,NULL)==PN_OK && ui.vertical_turn);
    {bool v=false;assert(pn_settings_load_vertical_turn(&media,root,&v)==PN_OK && v);}
    assert(pn_settings_ui_event(&ui,PN_SETUI_TOGGLE,present,NULL)==PN_OK && ui.flags==PN_INPUT_LEFT_HAND && pn_settings_load_flags(&media,root,&flags)==PN_OK && flags==PN_INPUT_LEFT_HAND);
    assert(pn_settings_ui_event(&ui,PN_SETUI_TOGGLE+1,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_TOGGLE+2,present,NULL)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_TOGGLE+3,present,NULL)==PN_LIMIT && !(ui.flags&PN_INPUT_NO_KEYS));
    pn_media_lease_t writer;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&writer)==PN_OK);uint8_t before=ui.flags;
    assert(pn_settings_ui_event(&ui,PN_SETUI_TOGGLE+2,present,NULL)==PN_BUSY && ui.flags==before);
    assert(pn_settings_ui_event(&ui,PN_SETUI_VERTICAL_TURN,present,NULL)==PN_BUSY && ui.vertical_turn);
    assert(pn_media_release(&media,&writer)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_VERTICAL_TURN,present,NULL)==PN_OK && !ui.vertical_turn);
    {bool v=true;assert(pn_settings_load_vertical_turn(&media,root,&v)==PN_OK && !v);}
    assert(pn_settings_ui_event(&ui,PN_SETUI_VERTICAL_TURN,present,NULL)==PN_OK && ui.vertical_turn);
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && ui.active && pn_settings_ui_screen(&ui)==0);
    assert(pn_settings_ui_event(&ui,PN_SETUI_FONTS,present,NULL)==PN_OK && ui.request==PN_SETUI_FONTS && ui.active);ui.request=0;
    /* 设置页不再有局域网传书行：保存网络后仍只有“存储与关于”，传书只从底栏进入。/ No LAN transfer row: even with a saved network only "Storage & about" remains; transfer starts from the bottom bar. */
    assert(pn_settings_ui_hit(&ui,100,950)==PN_SETUI_ABOUT && pn_settings_ui_event(&ui,PN_SETUI_LAN,present,NULL)==PN_EMPTY && !ui.request);
    // SSID行已移出设置首页，更新后台快照不能再提交相同画面。
    // The SSID row moved out of Settings; updating its compatibility snapshot must not submit an identical frame.
    unsigned shown=presents;assert(pn_settings_ui_set_lan(&ui,"家里的WiFi",present,NULL)==PN_OK && presents==shown && ui.presented && pn_settings_ui_hit(&ui,100,850)==PN_SETUI_WIFI && pn_settings_ui_hit(&ui,100,950)==PN_SETUI_ABOUT && pn_settings_ui_hit(&ui,100,1050)==-1);
    assert(pn_settings_ui_event(&ui,PN_SETUI_LAN,present,NULL)==PN_EMPTY && !ui.request);
    /* 外观独立保存：密度、四种预览、占用失败与重开；不改翻页标志。/ Appearance saves separately: density, four previews, busy failure and reopen without changing page-turn flags. */
    assert(ui.shelf.columns==3 && ui.shelf.style==PN_SHELF_DEFAULT);
    assert(pn_settings_ui_event(&ui,PN_SETUI_APPEARANCE,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==4);
    assert(pn_settings_ui_hit(&ui,100,240)==PN_SETUI_COLUMNS && pn_settings_ui_hit(&ui,550,240)==PN_SETUI_COLUMNS+1);
    assert(pn_settings_ui_hit(&ui,100,560)==PN_SETUI_STYLE && pn_settings_ui_hit(&ui,550,560)==PN_SETUI_STYLE+1 && pn_settings_ui_hit(&ui,100,780)==PN_SETUI_STYLE+2 && pn_settings_ui_hit(&ui,550,780)==PN_SETUI_STYLE+3);
    assert(pn_settings_ui_hit(&ui,340,680)==-1 && pn_settings_ui_event(&ui,PN_SETUI_COLUMNS+1,present,NULL)==PN_OK && ui.shelf.columns==4);
    for(int i=0;i<PN_SHELF_STYLE_COUNT;i++)assert(pn_settings_ui_event(&ui,PN_SETUI_STYLE+i,present,NULL)==PN_OK && ui.shelf.style==i);
    pn_shelf_prefs_t prefs;assert(pn_settings_load_shelf(&media,root,&prefs)==PN_OK && prefs.columns==4 && prefs.style==PN_SHELF_PAPER);
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&writer)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_COLUMNS,present,NULL)==PN_BUSY && ui.shelf.columns==4);
    assert(pn_media_release(&media,&writer)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && ui.active);
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && !ui.active);pn_settings_ui_close(&ui);pn_settings_ui_close(&ui);assert(!pool.used && !pool.live);
    assert(pn_settings_ui_open(&ui,&pool,&media,root,present,NULL)==PN_OK && ui.shelf.columns==4 && ui.shelf.style==PN_SHELF_PAPER && ui.vertical_turn && ui.auto_lock_minutes==10u);pn_settings_ui_close(&ui);
    assert(pn_settings_load_flags(&media,root,&flags)==PN_OK && flags==(PN_INPUT_LEFT_HAND|PN_INPUT_NO_SWIPE|PN_INPUT_NO_EDGE_TAP));
    /* 无存储：显示默认、不能保存。/ No storage: defaults shown, nothing saved. */
    assert(pn_settings_ui_open(&ui,&pool,NULL,NULL,present,NULL)==PN_OK && ui.flags==0 && ui.auto_lock_minutes==PN_AUTO_LOCK_DEFAULT_MIN && pn_settings_ui_event(&ui,PN_SETUI_TOGGLE,present,NULL)==PN_UNSUPPORTED && ui.flags==0);
    assert(pn_settings_ui_event(&ui,PN_SETUI_REFRESH,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_AUTO_LOCK+0,present,NULL)==PN_UNSUPPORTED && ui.auto_lock_minutes==PN_AUTO_LOCK_DEFAULT_MIN);pn_settings_ui_close(&ui);
    assert(pn_settings_ui_open(&ui,&pool,NULL,NULL,present,NULL)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_COLUMNS+1,present,NULL)==PN_UNSUPPORTED && ui.shelf.columns==3);pn_settings_ui_close(&ui);
    /* 打开时逐分配失败回收。/ Every allocation failure during open recovers. */
    pn_pool_t probe;assert(!pn_pool_init(&probe,2u*1024u*1024u,NULL,NULL,NULL));assert(pn_settings_ui_open(&ui,&probe,&media,root,present,NULL)==PN_OK);size_t attempts=probe.attempts;pn_settings_ui_close(&ui);
    for(size_t fail=1;fail<=attempts;fail++){assert(!pn_pool_init(&probe,2u*1024u*1024u,NULL,NULL,NULL));probe.fail_at=fail;pn_status_t s=pn_settings_ui_open(&ui,&probe,&media,root,present,NULL);if(s==PN_OK)pn_settings_ui_close(&ui);else assert(!ui.impl);assert(!probe.used && !probe.live);}
    puts("settings: thresholds, left hand, swipe/tap switches, record, toggles, not-all-off, busy, requests, no storage, fault-injected open passed");return 0;
}
