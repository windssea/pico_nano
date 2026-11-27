/* 排版交互的呈现屏障和草稿。/ Typesetting interaction presentation barriers and drafts. */
#define _POSIX_C_SOURCE 200809L
#include "test_remove_indexes.h"
#include "pn_style_ui.h"
#include "pn_status_bar_ui.h"
#include "pn_settings_ui.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {bool fail;bool capture;} screen_t;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)profile;screen_t *s=ctx;const char *path=getenv("PN_STYLE_CAPTURE");
    if(s->capture && !s->fail && path){FILE *f=fopen(path,"wb");assert(f && fprintf(f,"P5\n684 1216\n255\n")>0);for(int y=0;y<1216;y++){uint8_t row[684];for(int x=0;x<684;x++)row[x]=(uint8_t)(pn_frame_get(frame,x,y)*17);assert(fwrite(row,1,sizeof row,f)==sizeof row);}assert(fclose(f)==0);}
    return s->fail?PN_IO:PN_OK;
}
int main(void){char root[]="/tmp/pn-style-ui-XXXXXX";assert(mkdtemp(root));char book[384],state[384];snprintf(book,sizeof book,"%s/book.txt",root);snprintf(state,sizeof state,"%s/state",root);FILE *f=fopen(book,"wb");assert(f);for(unsigned i=0;i<200;i++)assert(fputs("文字沿着清晰的行列展开。调整字距和段落间距，找到舒服的阅读节奏。\n",f)>=0);assert(fclose(f)==0);
    pn_pool_t pool;assert(pn_pool_init(&pool,2*1024*1024,NULL,NULL,NULL)==0);pn_reader_app_t app={0};screen_t screen={0};pn_style_ui_t ui={0};
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,0)==PN_OK && pn_reader_app_step(&app,PN_APP_OPEN,1,present,&screen)==PN_OK);
    screen.fail=true;assert(pn_style_ui_open(&ui,&app,present,&screen)==PN_IO && !ui.presented);
    assert(pn_style_ui_event(&ui,PN_SUI_APPLY,2,present,&screen)==PN_BUSY && ui.active);
    screen.fail=false;screen.capture=true;assert(pn_style_ui_event(&ui,PN_SUI_RETRY,2,present,&screen)==PN_OK && ui.presented);
    /* 去重后十项五行双列在一页可达（前四行八项步进，第五行左背景、右章节标题），标签与列间隙不误触。
     * Deduplicated 10 cells in 5 two-column rows (first 4 rows steppers, row 4 left Background / right Chapter Title) without label/gutter hits. */
    {static const unsigned rows[8]={0,1,2,5,3,4,9,10};
     for(unsigned i=0;i<8;i++){if(rows[i]==3)continue;int hit=pn_style_ui_hit(&ui,i%2?620:300,564+(int)(i/2)*96);assert(hit==PN_SUI_FIELD+(int)rows[i]*2+1);assert(pn_style_ui_event(&ui,hit,2,present,&screen)==PN_OK && !ui.more);}}
    for(unsigned i=0;i<2;i++){int hit=pn_style_ui_hit(&ui,60,756);assert(hit==PN_SUI_FIELD+6);assert(pn_style_ui_event(&ui,hit,2,present,&screen)==PN_OK);}
    assert(pn_style_ui_hit(&ui,340,610)==-1 && pn_style_ui_hit(&ui,100,540)==-1);
    assert(pn_style_ui_hit(&ui,300,200)==PN_SUI_PREVIEW && pn_style_ui_hit(&ui,180,950)==PN_SUI_BACKGROUND && pn_style_ui_hit(&ui,500,950)==PN_SUI_CHAPTER && pn_style_ui_hit(&ui,100,480)==PN_SUI_FONTS && pn_style_ui_hit(&ui,100,60)==PN_SUI_CANCEL && pn_style_ui_hit(&ui,600,90)==PN_SUI_RESET && pn_style_ui_hit(&ui,150,1150)==PN_SUI_USE_GLOBAL && pn_style_ui_hit(&ui,300,1150)==PN_SUI_APPLY);
    assert(pn_style_ui_event(&ui,PN_SUI_MORE,2,present,&screen)==PN_OK && !ui.more && pn_style_ui_hit(&ui,100,60)==PN_SUI_CANCEL);
    assert(pn_style_ui_event(&ui,PN_SUI_BACK_MAIN,2,present,&screen)==PN_OK && !ui.more);
    assert(ui.draft.pixels==46 && ui.draft.line_percent==150 && ui.draft.gap_percent==45 && ui.draft.indent_em==0 && ui.draft.margin==48 && ui.draft.gl_before_clear==12 && ui.draft.tracking_percent==5 && ui.draft.weight==1 && ui.draft.clarity==1);
    /* 档位项首尾循环：清晰度从黑白再右切回到标准，再左切回黑白；字重从细左切到特粗；数值项字号到上限仍停住。
     * Option fields wrap: sharpness steps right from black-and-white back to standard and left again to black-and-white; weight steps left from light to heaviest; numeric size still stops at its limit. */
    {pn_style_t keep=ui.draft;ui.draft.clarity=3;assert(pn_style_ui_event(&ui,PN_SUI_FIELD+10*2+1,2,present,&screen)==PN_OK && ui.draft.clarity==0);
     assert(pn_style_ui_event(&ui,PN_SUI_FIELD+10*2,2,present,&screen)==PN_OK && ui.draft.clarity==3);
     ui.draft.weight=-1;assert(pn_style_ui_event(&ui,PN_SUI_FIELD+9*2,2,present,&screen)==PN_OK && ui.draft.weight==3);
     ui.draft.reading_line=2;assert(pn_style_ui_event(&ui,PN_SUI_FIELD+6*2+1,2,present,&screen)==PN_OK && ui.draft.reading_line==3);
     assert(pn_style_ui_event(&ui,PN_SUI_FIELD+6*2+1,2,present,&screen)==PN_OK && ui.draft.reading_line==0);
     ui.draft.margin=20;assert(pn_style_ui_event(&ui,PN_SUI_FIELD+5*2,2,present,&screen)==PN_OK && ui.draft.margin==48);
     ui.draft.pixels=72;assert(pn_style_ui_event(&ui,PN_SUI_FIELD+1,2,present,&screen)==PN_EMPTY && ui.draft.pixels==72);ui.draft=keep;}
    // 图库顺序与持久ID分离，只展示无、4、7；返回/选图均不保存。/ Gallery order is separate from IDs and lists only None, 4 and 7; Back/select never save.
    assert(!ui.draft.background && pn_style_ui_event(&ui,PN_SUI_BACKGROUND,2,present,&screen)==PN_OK && ui.backgrounds);
    {const int ids[]={0,4,7};for(unsigned i=0;i<3;i++)assert(pn_style_ui_hit(&ui,100+(int)i*212,220)==PN_SUI_BACKGROUND_ITEM+ids[i]);}
    assert(pn_style_ui_hit(&ui,100,506)==-1 && pn_style_ui_hit(&ui,524,792)==-1);
    assert(pn_style_ui_event(&ui,PN_SUI_BACKGROUND_ITEM+1,2,present,&screen)==PN_EMPTY && ui.backgrounds && !ui.draft.background);
    assert(pn_style_ui_hit(&ui,235,300)==-1 && pn_style_ui_hit(&ui,100,60)==PN_SUI_BACKGROUND_BACK);
    assert(pn_style_ui_event(&ui,PN_SUI_BACKGROUND_ITEM+7,2,present,&screen)==PN_OK && !ui.backgrounds && ui.draft.background==7);
    assert(pn_style_ui_event(&ui,PN_SUI_BACKGROUND,2,present,&screen)==PN_OK);
    assert(pn_style_ui_event(&ui,PN_SUI_CANCEL,2,present,&screen)==PN_OK && ui.active && !ui.backgrounds && ui.draft.background==7);
    {const unsigned ids[]={0,4,7,0};ui.draft.background=0;for(unsigned i=1;i<4;i++){assert(pn_style_ui_event(&ui,PN_SUI_FIELD+11*2+1,2,present,&screen)==PN_OK && ui.draft.background==ids[i]);}assert(pn_style_ui_event(&ui,PN_SUI_FIELD+11*2,2,present,&screen)==PN_OK && ui.draft.background==7);}
    /* 预设与恢复默认只改草稿。/ Presets and restore-defaults change only the draft. */
    assert(pn_style_ui_hit(&ui,100,372)==PN_SUI_PRESET && pn_style_ui_hit(&ui,340,372)==PN_SUI_PRESET+1 && pn_style_ui_hit(&ui,560,372)==PN_SUI_PRESET+2);
    assert(pn_style_ui_event(&ui,PN_SUI_PRESET+1,2,present,&screen)==PN_OK && ui.draft.pixels==36 && ui.draft.line_percent==125 && ui.draft.margin==24 && ui.draft.tracking_percent==0 && ui.draft.background==7);
    assert(pn_style_ui_event(&ui,PN_SUI_PRESET+2,2,present,&screen)==PN_OK && ui.draft.pixels==56);
    assert(pn_style_ui_event(&ui,PN_SUI_RESET,2,present,&screen)==PN_OK && ui.draft.pixels==44 && ui.draft.line_percent==145 && ui.draft.gap_percent==40 && ui.draft.indent_em==2 && ui.draft.margin==32 && !ui.draft.background);
    {pn_style_t reset=ui.draft;assert(pn_style_ui_event(&ui,PN_SUI_PRESET,2,present,&screen)==PN_OK && !memcmp(&reset,&ui.draft,sizeof reset));}
    assert(pn_style_ui_hit(&ui,300,250)==PN_SUI_PREVIEW && pn_style_ui_hit(&ui,300,1100)==-1);
    assert(pn_style_ui_hit(&ui,300,1050)==PN_SUI_FIELD+24 && !ui.draft.hide_images);
    assert(pn_style_ui_event(&ui,PN_SUI_FIELD+24,2,present,&screen)==PN_OK && ui.draft.hide_images);
    {pn_style_t current;assert(pn_reader_app_style_get(&app,&current)==PN_OK && !current.hide_images);}
    assert(pn_style_ui_event(&ui,PN_SUI_FIELD+25,2,present,&screen)==PN_OK && !ui.draft.hide_images);
    /* 右下角直接接入共享可视化「状态栏设置」子页（对本书独立，支持恢复全局），并在「边距与栏位」保留单书去上下栏开关。
     * Bottom-right cell opens shared visual Status Bar Settings (per-book independent with restore-global); bar-less toggle stays on Margins and via PN_SUI_TOGGLE_BARS. */
    pn_status_bar_prefs_t global_sb=pn_status_bar_prefs_default();
    global_sb.enabled=1;global_sb.position=PN_SBAR_POS_BOTTOM;
    {pn_media_t sm;pn_media_init(&sm);assert(pn_media_attach(&sm,1)==PN_OK);assert(pn_settings_save_status_bar(&sm,state,&global_sb)==PN_OK);}
    pn_status_bar_set_active(&global_sb);
    assert(pn_reader_app_style_is_inherited(&app) && pn_reader_app_status_bar_is_inherited(&app) && !pn_status_bar_has_book_override());
    assert(pn_style_ui_hit(&ui,500,1050)==PN_SUI_STATUS_BAR && !ui.draft.hide_bars);
    assert(pn_style_ui_event(&ui,PN_SUI_TOGGLE_BARS,2,present,&screen)==PN_OK && ui.draft.hide_bars==1);
    assert(pn_style_ui_event(&ui,PN_SUI_STATUS_BAR,2,present,&screen)==PN_OK && ui.status_bar_page);
    /* 跟随全局态底部为提示条（不触发恢复全局按钮），修改任意项后转为本书独立并显示「恢复全局状态栏 / 本书恢复默认」。
     * While following global, footer is informational; editing any field creates a per-book override and shows Restore Global / Reset Book Default buttons. */
    assert(pn_style_ui_hit(&ui,180,920)==-1);
    assert(pn_style_ui_hit(&ui,360,500)==PN_SUI_SBAR_ACTION+PN_SBAR_UI_POS_TOP);
    assert(pn_style_ui_event(&ui,PN_SUI_SBAR_ACTION+PN_SBAR_UI_POS_TOP,2,present,&screen)==PN_OK && ui.status_bar_changed && !ui.draft.hide_bars);
    assert(!pn_reader_app_status_bar_is_inherited(&app) && pn_status_bar_has_book_override());
    assert(pn_status_bar_active().enabled==1 && pn_status_bar_active().position==PN_SBAR_POS_TOP);
    assert(pn_status_bar_global().enabled==1 && pn_status_bar_global().position==PN_SBAR_POS_BOTTOM);
    assert(pn_style_ui_event(&ui,PN_SUI_SBAR_ACTION+PN_SBAR_UI_SLOT_BASE+1,2,present,&screen)==PN_OK && ui.sbar_pick_slot==1);
    assert(pn_style_ui_event(&ui,PN_SUI_SBAR_ACTION+PN_SBAR_UI_PICK_BASE+PN_SBAR_SLOT_BOOK,2,present,&screen)==PN_OK && ui.sbar_pick_slot==-1 && pn_status_bar_active().slots[1]==PN_SBAR_SLOT_BOOK);
    /* 验证全局 status_bar.{a,b} 未被本书独立设置覆盖。/ Verify global status_bar.{a,b} was untouched by the per-book override. */
    {pn_media_t sm;pn_media_init(&sm);assert(pn_media_attach(&sm,1)==PN_OK);pn_status_bar_prefs_t loaded={0};assert(pn_settings_load_status_bar(&sm,state,&loaded)==PN_OK && loaded.enabled==1 && loaded.position==PN_SBAR_POS_BOTTOM);}
    assert(pn_style_ui_hit(&ui,180,920)==PN_SUI_SBAR_ACTION+PN_SBAR_UI_USE_GLOBAL && pn_style_ui_hit(&ui,520,920)==PN_SUI_SBAR_ACTION+PN_SBAR_UI_RESET);
    assert(pn_style_ui_event(&ui,PN_SUI_SBAR_ACTION+PN_SBAR_UI_RESET,2,present,&screen)==PN_OK && !pn_status_bar_active().enabled && !pn_reader_app_status_bar_is_inherited(&app));
    assert(pn_style_ui_event(&ui,PN_SUI_SBAR_ACTION+PN_SBAR_UI_USE_GLOBAL,2,present,&screen)==PN_OK && pn_reader_app_status_bar_is_inherited(&app) && !pn_status_bar_has_book_override() && pn_status_bar_active().position==PN_SBAR_POS_BOTTOM);
    assert(pn_style_ui_hit(&ui,180,920)==-1);
    assert(pn_style_ui_event(&ui,PN_SUI_SBAR_ACTION+PN_SBAR_UI_BACK,2,present,&screen)==PN_OK && !ui.status_bar_page);
    assert(pn_style_ui_event(&ui,PN_SUI_TOGGLE_BARS,2,present,&screen)==PN_OK && ui.draft.hide_bars==1);
    assert(pn_style_ui_hit(&ui,490,640)==PN_SUI_MARGINS);
    /* 第 4 行右格直达「章节标题」子页，子页不再重复阅读线两行。/ Row 4 right opens the Chapter Title sub-page directly, without duplicate reading-line rows. */
    assert(pn_style_ui_hit(&ui,500,950)==PN_SUI_CHAPTER && pn_style_ui_event(&ui,PN_SUI_CHAPTER,2,present,&screen)==PN_OK && ui.chapter_page);
    assert(pn_style_ui_hit(&ui,280,410)==PN_SUI_CHAPTER_ADJ+5 && pn_style_ui_event(&ui,PN_SUI_CHAPTER_ADJ+5,2,present,&screen)==PN_OK && ui.draft.chapter_scale_percent==130);
    assert(pn_style_ui_event(&ui,PN_SUI_CHAPTER_RESET,2,present,&screen)==PN_OK && ui.draft.chapter_scale_percent==0);
    assert(pn_style_ui_event(&ui,PN_SUI_CHAPTER_BACK,2,present,&screen)==PN_OK && !ui.chapter_page);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGINS,2,present,&screen)==PN_OK && ui.margins && !ui.margins_from_bg);
    assert(pn_style_ui_hit(&ui,100,60)==PN_SUI_MARGINS_BACK && pn_style_ui_hit(&ui,600,90)==PN_SUI_MARGINS_RESET && pn_style_ui_hit(&ui,300,440)==PN_SUI_TOGGLE_BARS);
    /* 放大后的 108x108 边距详调命中区覆盖卡片全高，便于手指点击。/ Enlarged 108x108 margin stepper hit targets cover the full stepper row height. */
    assert(pn_style_ui_hit(&ui,130,500)==PN_SUI_MARGIN_ADJ && pn_style_ui_hit(&ui,230,590)==PN_SUI_MARGIN_ADJ+1 && pn_style_ui_hit(&ui,450,890)==PN_SUI_MARGIN_ADJ+14 && pn_style_ui_hit(&ui,550,980)==PN_SUI_MARGIN_ADJ+15);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGIN_ADJ+1,2,present,&screen)==PN_OK && ui.draft.margin_top==4);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGIN_ADJ+3,2,present,&screen)==PN_OK && ui.draft.margin_bottom==4);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGIN_ADJ+5,2,present,&screen)==PN_OK && pn_style_margin_left(&ui.draft)==36 && pn_style_margin_right(&ui.draft)==32);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGIN_ADJ+7,2,present,&screen)==PN_OK && pn_style_margin_right(&ui.draft)==36);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGIN_ADJ+9,2,present,&screen)==PN_OK && ui.draft.bg_margin_top==4);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGIN_ADJ+11,2,present,&screen)==PN_OK && ui.draft.bg_margin_bottom==4);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGIN_ADJ+13,2,present,&screen)==PN_OK && ui.draft.bg_margin_left==4);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGIN_ADJ+15,2,present,&screen)==PN_OK && ui.draft.bg_margin_right==4);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGINS_RESET,2,present,&screen)==PN_OK && !ui.draft.hide_bars && !ui.draft.margin_top && !ui.draft.bg_margin_left && pn_style_margin_left(&ui.draft)==32 && pn_style_margin_right(&ui.draft)==32);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGINS_BACK,2,present,&screen)==PN_OK && !ui.margins && !ui.backgrounds);
    assert(pn_style_ui_event(&ui,PN_SUI_BACKGROUND,2,present,&screen)==PN_OK && ui.backgrounds && pn_style_ui_hit(&ui,340,1140)==PN_SUI_MARGINS);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGINS,2,present,&screen)==PN_OK && ui.margins && ui.margins_from_bg);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGINS_BACK,2,present,&screen)==PN_OK && !ui.margins && ui.backgrounds);
    assert(pn_style_ui_event(&ui,PN_SUI_BACKGROUND_BACK,2,present,&screen)==PN_OK && !ui.backgrounds);
    screen.capture=false;const char *preview_path=getenv("PN_STYLE_PREVIEW_CAPTURE");if(preview_path){assert(setenv("PN_STYLE_CAPTURE",preview_path,1)==0);screen.capture=true;}
    assert(pn_style_ui_event(&ui,PN_SUI_PREVIEW,3,present,&screen)==PN_OK && ui.preview);assert(pn_style_ui_hit(&ui,300,400)==PN_SUI_FORM);screen.capture=false;
    assert(pn_style_ui_event(&ui,PN_SUI_FORM,3,present,&screen)==PN_OK && !ui.preview);assert(pn_style_ui_event(&ui,PN_SUI_CANCEL,4,present,&screen)==PN_OK && !ui.active);
    pn_style_t style;assert(pn_reader_app_style_get(&app,&style)==PN_OK && style.pixels==44);
    /* 先应用单书特殊排版，再点击“使用全局”清除单书覆盖并立即采用全局排版；同时验证单书状态栏挂起/重开隔离与阅读线/边距/章节/背景子页的恢复全局动作。
     * Apply per-book override first, then tap "Use Global" to clear override and adopt global style; also verify per-book status-bar suspend/reopen isolation and sub-page restore-global actions. */
    {pn_media_t sm;pn_media_init(&sm);assert(pn_media_attach(&sm,1)==PN_OK);pn_style_t gs=pn_style_default(52);gs.reading_line=1;gs.reading_line_dash=16;gs.margin_top=12;gs.chapter_scale_percent=140;gs.background=4;assert(pn_style_global_save(&sm,state,&gs)==PN_OK);}
    assert(pn_style_ui_open(&ui,&app,present,&screen)==PN_OK && pn_style_ui_event(&ui,PN_SUI_FIELD+1,5,present,&screen)==PN_OK && pn_style_ui_event(&ui,PN_SUI_APPLY,5,present,&screen)==PN_OK && !ui.active);
    assert(pn_reader_app_style_get(&app,&style)==PN_OK && style.pixels==46 && !pn_reader_app_style_is_inherited(&app));
    /* 验证边距/章节/背景子页的「恢复全局」命中与草稿同步。/ Verify restore-global hit and draft sync in margins, chapter, and background sub-pages. */
    assert(pn_style_ui_open(&ui,&app,present,&screen)==PN_OK);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGINS,6,present,&screen)==PN_OK && pn_style_ui_hit(&ui,560,356)==PN_SUI_MARGINS_GLOBAL);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGINS_GLOBAL,6,present,&screen)==PN_OK && ui.draft.margin_top==12);
    assert(pn_style_ui_event(&ui,PN_SUI_MARGINS_BACK,6,present,&screen)==PN_OK);
    assert(pn_style_ui_event(&ui,PN_SUI_CHAPTER,6,present,&screen)==PN_OK && pn_style_ui_hit(&ui,540,900)==PN_SUI_CHAPTER_GLOBAL);
    assert(pn_style_ui_event(&ui,PN_SUI_CHAPTER_GLOBAL,6,present,&screen)==PN_OK && ui.draft.chapter_scale_percent==140);
    assert(pn_style_ui_event(&ui,PN_SUI_CHAPTER_BACK,6,present,&screen)==PN_OK);
    assert(pn_style_ui_event(&ui,PN_SUI_BACKGROUND,6,present,&screen)==PN_OK && pn_style_ui_hit(&ui,560,154)==PN_SUI_BG_GLOBAL);
    assert(pn_style_ui_event(&ui,PN_SUI_BG_GLOBAL,6,present,&screen)==PN_OK && !ui.backgrounds && ui.draft.background==4);
    assert(pn_style_ui_event(&ui,PN_SUI_USE_GLOBAL,6,present,&screen)==PN_OK && !ui.active);
    assert(pn_reader_app_style_get(&app,&style)==PN_OK && style.pixels==52 && pn_reader_app_style_is_inherited(&app));
    /* 验证半屏阅读线微调卡：修改保存转为本书独立 -> 再点「用全局」并保存，恢复为跟随全局。
     * Verify live reading-line half-sheet: edit+save creates per-book override, then Restore Global + Save returns to following global. */
    assert(pn_style_ui_open_guide(&ui,&app,7,present,&screen)==PN_OK && ui.guide_sheet);
    assert(pn_style_ui_hit(&ui,480,640)==PN_SUI_GUIDE_GLOBAL);
    assert(pn_style_ui_event(&ui,PN_SUI_GUIDE_STYLE+3,7,present,&screen)==PN_OK && ui.draft.reading_line==3);
    assert(pn_style_ui_event(&ui,PN_SUI_APPLY,7,present,&screen)==PN_OK && !ui.active && !pn_reader_app_style_is_inherited(&app));
    assert(pn_style_ui_open_guide(&ui,&app,8,present,&screen)==PN_OK && ui.guide_sheet);
    assert(pn_style_ui_event(&ui,PN_SUI_GUIDE_GLOBAL,8,present,&screen)==PN_OK && ui.draft.reading_line==1 && ui.draft.reading_line_dash==16);
    assert(pn_style_ui_event(&ui,PN_SUI_APPLY,8,present,&screen)==PN_OK && !ui.active && pn_reader_app_style_is_inherited(&app));
    /* 验证单书独立状态栏在挂起/恢复/重开时的严格隔离。/ Verify strict per-book status-bar isolation across suspend, resume, and reopen. */
    pn_status_bar_prefs_t book_top=pn_status_bar_prefs_default();book_top.enabled=1;book_top.position=PN_SBAR_POS_TOP;
    assert(pn_reader_app_status_bar_save(&app,&book_top)==PN_OK && !pn_reader_app_status_bar_is_inherited(&app) && pn_status_bar_active().position==PN_SBAR_POS_TOP);
    assert(pn_reader_app_suspend(&app,9)==PN_OK && !pn_status_bar_has_book_override() && pn_status_bar_active().position==PN_SBAR_POS_BOTTOM);
    assert(pn_reader_app_resume(&app,10,present,&screen)==PN_OK && pn_status_bar_has_book_override() && pn_status_bar_active().position==PN_SBAR_POS_TOP);
    assert(pn_reader_app_close(&app,11)==PN_OK && !pn_status_bar_has_book_override() && pn_status_bar_active().position==PN_SBAR_POS_BOTTOM);
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,12)==PN_OK && !pn_reader_app_status_bar_is_inherited(&app) && pn_status_bar_active().position==PN_SBAR_POS_TOP);
    assert(pn_reader_app_status_bar_use_global(&app)==PN_OK && pn_reader_app_status_bar_is_inherited(&app) && pn_status_bar_active().position==PN_SBAR_POS_BOTTOM);
    assert(pn_reader_app_close(&app,13)==PN_OK);
    pn_status_bar_set_active(NULL);pn_status_bar_set_book_override(NULL);
    pn_test_remove_indexes(state);DIR *dir=opendir(state);assert(dir);struct dirent *entry;while((entry=readdir(dir))){if(entry->d_name[0]=='.')continue;char p[1024];int n=snprintf(p,sizeof p,"%s/%s",state,entry->d_name);assert(n>0 && (size_t)n<sizeof p && unlink(p)==0);}assert(closedir(dir)==0 && rmdir(state)==0 && unlink(book)==0 && rmdir(root)==0 && !pool.used && !pool.live);puts("style UI: confirmed controls, typesetting/guide fields, preview return and discarded draft passed");return 0;
}
