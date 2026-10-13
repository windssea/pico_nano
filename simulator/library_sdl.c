/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享书目与阅读控制器的PC窗口，保存成功才返回书架。
 * English: PC window sharing catalog and reader controllers, returning only after saving.
 * 冻结：TXT/EPUB按内容验证后阅读；窗口呈现成功才确认；ARGB不计入设备预算。
 * Frozen: validate TXT/EPUB content before reading; confirm after window presentation; ARGB is outside the device budget.
 */
#define _XOPEN_SOURCE 700 // realpath
#include "pn_brand.h"
#include "pn_profile.h"
#include "pn_brand_image.h"
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include "pn_reader_input.h"
#include "pn_shelf_view.h"
#include "pn_reader_chrome.h"
#include "pn_reader_toolbar.h"
#include "pn_wallpaper_ui.h"
#include "pn_font_manage.h"
#include "pn_settings_ui.h"
#include "pn_turn_transition.h"
#include "pn_tap.h"
#include "pn_key.h"
#include "pn_focus.h"
#include "async_sdl.h"
#include <SDL.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "pn_search_ui.h"
#include "pn_jump_ui.h"
#include "pn_transfer_hub.h"
#include "pn_book_actions.h"
#include "pn_catalog_index.h"
#include <sys/statvfs.h>
#include "bookmark_sdl.h"
#include "input_script.h"
#include "style_sdl.h"
#include "font_sdl.h"
#include "toc_sdl.h"
typedef struct {
    pn_catalog_index_t catalog_index;bool recent_loaded;
    pn_sdl_async_t async; ///< SDL主线程端口 / SDL main-thread port
    const char *cover_argument,*wallpaper_store_argument; ///< 启动参数 / Startup arguments
    pn_pool_t *pool;
    const char *directory,*font_path,*fallback_path,*state_dir;
    char font_dir_real[1024],wallpaper_dir_real[1024]; ///< 目录的绝对路径：字体引用和壁纸源路径要求绝对路径 / Absolute directories: font references and wallpaper sources require absolute paths
    char font_real[1024]; ///< font_path的绝对路径，书名回退字体要求绝对路径 / Absolute form of font_path; the title fallback font requires absolute paths
    pn_media_t media;
    pn_media_t state_media;
    pn_recent_snapshot_t *recent;
    bool recent_mode;
    size_t recent_start;
    pn_shelf_prefs_t shelf_prefs; ///< 已保存书架外观 / Saved shelf appearance
    bool list_mode; ///< 列表模式（每页5本）/ List mode (five per page)
    bool home; ///< 阅读首页与书库共用快照，不改阅读来源 / Home and library share snapshots, preserving reading origin
    bool shelf_focus_visible; ///< 仅主动键盘导航显示轻量焦点 / Show subtle focus only during deliberate keyboard navigation
    pn_catalog_page_t *page;
    pn_font_t font;
    pn_frame_t frame;
    pn_reader_app_t reader;
    pn_epub_app_t epub;
    pn_toc_ui_t toc;
    bool toc_pointer,pointer_down;
    pn_bookmark_ui_t bookmarks;
    pn_style_ui_t styles;
    pn_font_ui_t fonts;bool font_pointer;
    uint64_t ui_retry;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *argb;
    int selected;
    pn_cache_owner_t cover_cache; ///< 封面预算owner / Cover-budget owner
    bool covers_inactive; ///< 借用完毕才允许回收 / Reclaim only after all cover borrowing ends
    pn_shelf_covers_t *covers; ///< 当前页封面槽 / Current-page cover slots
    char cover_dir[512]; ///< 显式--cover-cache目录，空则只解码不缓存 / Explicit --cover-cache directory; empty decodes without caching
    uint8_t salt[16]; ///< EPUB XML哈希盐 / EPUB XML hash salt
    bool covers_dirty; ///< 本页有新封面待重绘 / New covers await a redraw for this page
    pn_wallpaper_ui_t wallpaper; ///< 壁纸设置页 / Wallpaper settings page
    pn_wallpaper_mask_t lock_mask; ///< 锁屏时临时遮罩 / Temporary mask used only while locked
    const char *wallpaper_dir; ///< 原图目录，NULL禁用 / Source directory, NULL disables the page
    pn_media_t wallpaper_media; ///< 模拟内部壁纸分区 / Simulated internal wallpaper partition
    pn_wallpaper_store_t wallpaper_store; ///< 显式记录目录的两槽 / Two slots in the explicit record directory
    bool wallpaper_store_ok; ///< 已给出记录目录 / Record directory supplied
    pn_font_manage_t font_manage; ///< 字体管理页 / Font management page
    const char *font_dir; ///< --font-dir，NULL禁用 / --font-dir, NULL disables the page
    pn_settings_ui_t settings; ///< 设置页 / Settings page
    uint8_t input_flags; ///< 已保存翻页标志 / Saved page-turn flags
    bool index; ///< 字母跳转页 / Letter-index page
    bool toolbar; ///< 阅读工具栏正显示 / The reading toolbar is showing
    uint8_t *page_px; ///< 最近一次呈现帧的副本，关闭工具栏时恢复 / Copy of the latest presented frame, restored when the toolbar closes
    char jump; ///< 下次首页查询的字母 / Letter for the next first-page query
    bool fav_mode; ///< 书架“收藏”分类 / Shelf Favorites category
    size_t fav_start; ///< 收藏页起点 / Favorites page start
    pn_favorites_t *favorites; ///< 收藏夹（按需分配）/ Favorites (allocated on demand)
    bool favorites_valid; ///< 当前收藏快照可复用 / Reusable favorite snapshot
    pn_focus_t management_focus; ///< 管理页确认目标 / Management confirmation target
    bool actions_mode; ///< 书籍操作面板 / Book actions sheet
    pn_book_actions_t actions; ///< 面板状态 / Sheet state
    bool hub; ///< 传书一级页正显示 / The Transfer root tab is showing
    char storage[64]; ///< 书库所在磁盘容量说明 / Capacity note of the library disk
    bool back_to_settings; ///< 子页从设置页进入，返回时回设置页 / The sub-page was opened from Settings, so Back returns there
    pn_jump_ui_t jumpui; ///< 进度跳转面板 / Progress jump panel
    pn_search_ui_t search; ///< 搜索页输入状态 / Search page input state
    bool search_open; ///< 搜索页正显示 / The search page is showing
    char query[PN_CATALOG_QUERY_MAX+1]; ///< 当前生效的搜索词，空表示不过滤 / Active search query, empty means unfiltered
    bool open_failed; ///< 打开失败时在书架显示提示 / Show a shelf notice after an open failure
    pn_catalog_item_t last_read;bool last_read_known; ///< 最近成功打开书的快照 / Snapshot of the latest successfully opened book
    bool locked; ///< PC锁屏预览，不模拟PMU睡眠 / PC lock preview without pretending to simulate PMU sleep
} library_t;
/* 书架选项：模拟器没有电量计，故不显示电量。/ Shelf options: the simulator has no fuel gauge so no battery is shown. */
/* 模拟器没有电量计：电量取环境变量PN_SIM_BATTERY（0–100，默认85，仅为演示值，不是实测）。
 * The simulator has no fuel gauge: the battery comes from PN_SIM_BATTERY (0–100, default 85), a demo value, not a measurement. */
static int sim_battery(void){const char *v=getenv("PN_SIM_BATTERY");if(!v || !*v)return 85;int n=atoi(v);return n<0?-1:n>100?100:n;}
static pn_shelf_options_t shelf_options(const library_t *s){pn_w_set_battery(sim_battery());bool query=!s->recent_mode && !s->fav_mode && *s->query;return (pn_shelf_options_t){.home=s->home,.battery_percent=-1,.columns=s->shelf_prefs.columns,.style=s->shelf_prefs.style,.actions=s->actions_mode?&s->actions:NULL,.search=true,.layout_toggle=true,.favorites_tab=true,.favorites=s->fav_mode,.list_mode=s->list_mode || query,.query=query};}
static size_t shelf_per_page(const library_t *s){pn_shelf_options_t o=shelf_options(s);return s->home?3u:pn_shelf_capacity(&o);}
static bool reading(library_t *s){return s->reader.impl || s->epub.impl;}
static pn_status_t active_step(library_t *s,pn_reader_action_t action,uint64_t now,pn_reader_present_fn present,void *ctx){
    pn_reader_app_refresh_period(&s->reader,pn_settings_refresh_pages(s->input_flags));
    pn_epub_app_refresh_period(&s->epub,pn_settings_refresh_pages(s->input_flags));
    pn_frame_t before;pn_turn_t effect={.pool=s->pool,.style=(pn_turn_style_t)((s->input_flags&PN_INPUT_TURN_MASK)>>6),.reverse=action==PN_APP_PREVIOUS,.present=present,.ctx=ctx};
    if((action==PN_APP_NEXT || action==PN_APP_PREVIOUS) && s->page_px && pn_frame_bind(&before,s->page_px,342u*1216u,684,1216))effect.before=&before;else effect.style=PN_TURN_NONE;
    return s->epub.impl?pn_epub_app_step(&s->epub,action,now,pn_turn_present,&effect):pn_reader_app_step(&s->reader,action,now,pn_turn_present,&effect);
}
static pn_status_t active_close(library_t *s,uint64_t now){s->recent_loaded=false;return s->epub.impl?pn_epub_app_close(&s->epub,now):pn_reader_app_close(&s->reader,now);}
static bool timing_on(void){static int on=-1;if(on<0){const char *v=getenv("PN_SIM_TIMING");on=v && *v=='1';}return on;}
static uint64_t profile_clock(void *ctx){(void)ctx;uint64_t c=SDL_GetPerformanceCounter(),f=SDL_GetPerformanceFrequency();return (c/f)*1000000u+(c%f)*1000000u/f;}
static void profile_emit(void *ctx,const pn_profile_event_t *e){
    (void)ctx;fprintf(stderr,"profile phase=%s elapsed_us=%llu status=%d begin=%zu end=%zu peak=%zu attempts=%zu backend=host\n",e->phase,(unsigned long long)e->elapsed_us,(int)e->status,e->begin_bytes,e->end_bytes,e->peak_bytes,e->attempts);
    if(e->pool){fprintf(stderr,"memory phase=%s",e->phase);for(unsigned j=0;j<PN_MEM_COUNT;j++){const pn_memory_stats_t *m=&e->pool->memory[j];fprintf(stderr," %s=%zu/%zu/%zu/%zu",pn_memory_tag_name((pn_memory_tag_t)j),m->used,m->peak,m->allocations,m->failures);}fprintf(stderr," heap=unavailable stack=unavailable\n");}
    if(e->pool)for(unsigned k=0;k<PN_CACHE_COUNT;k++){const pn_cache_stats_t *c=&e->pool->cache_stats[k];fprintf(stderr,"cache phase=%s kind=%u bytes=%zu peak=%zu hits=%zu misses=%zu denied=%zu evictions=%zu reclaimed=%zu backend=host\n",e->phase,k,c->bytes,c->peak,c->hits,c->misses,c->denied,c->evictions,c->reclaimed);}
}
/* 呈现并记住最近一帧（工具栏打开时不记，以便关闭后恢复原页面）。/ Present and remember the latest frame (not while the toolbar is open, so closing restores the original page). */
static pn_status_t present_raw_main(void *ctx,const pn_frame_t *frame);
static pn_status_t present_raw(library_t *s,const pn_frame_t *frame){
    pn_status_t status=pn_sdl_async_present(&s->async,frame,PN_REFRESH_GC16);
    if(status!=PN_OK)return status;
    if(pn_cancel_checkpoint()!=PN_OK)return status; // 已成功呈现只跳过开发截图，仍返回成功回执。/ After presentation succeeds, skip only the dev capture and retain its successful receipt.
    // 开发辅助：PN_SIM_FRAME_DIR指向目录时，每次呈现另存一张PGM，便于审查界面；不影响显示。/ Dev aid: when PN_SIM_FRAME_DIR names a directory, every present also saves a PGM for UI review; display is unaffected.
    const char *dump=getenv("PN_SIM_FRAME_DIR");
    if(dump && *dump && strlen(dump)<400){static unsigned index;char path[512];snprintf(path,sizeof path,"%s/frame-%04u.pgm",dump,++index);
        FILE *file=fopen(path,"wb");
        if(file){
            fprintf(file,"P5\n%d %d\n255\n",frame->width,frame->height);
            uint8_t row[684]; // 行批量写，避免截图自身放大输入延迟。/ Write rows in batches so capture does not inflate input latency.
            for(int y=0;frame->width<=(int)sizeof row && y<frame->height;y++){for(int x=0;x<frame->width;x++)row[x]=(uint8_t)(pn_frame_get(frame,x,y)*17u);if(fwrite(row,1,(size_t)frame->width,file)!=(size_t)frame->width)break;}
            fclose(file);
        }
    }
    return status;
}
static pn_status_t present_impl(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    library_t *s=ctx;(void)profile;
    if(!reading(s)){pn_free(s->page_px);s->page_px=NULL;}
    // 备份只供阅读工具栏关闭时恢复；书架/设置不需要，避免挤占封面解码预算。/ Only reading needs the backup for toolbar restoration; shelf/settings copies waste the cover decode budget.
    pn_status_t status=present_raw(s,frame);
    if(status==PN_OK && reading(s) && !s->toolbar && frame->width==684 && frame->height==1216 && frame->stride>=342){
        if(!s->page_px)s->page_px=pn_alloc_tagged(s->pool,PN_MEM_FRAME,342u*1216u);
        if(s->page_px)for(int y=0;y<1216;y++)memcpy(s->page_px+(size_t)y*342,frame->pixels+(size_t)y*frame->stride,342);
    }
    return status;
}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    pn_profile_span_t span;pn_profile_begin(&span,((library_t *)ctx)->pool,"display.receipt");
    pn_status_t status=present_impl(ctx,frame,profile);pn_profile_end(&span,status);return status;
}
static pn_status_t present_raw_main(void *ctx,const pn_frame_t *frame){
    library_t *s=ctx;
    uint64_t t0=SDL_GetTicks64();
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++){
        uint32_t value=pn_frame_get(frame,x,y)*17u;s->argb[(size_t)y*frame->width+x]=0xff000000u|value*0x010101u;
    }
    if(SDL_UpdateTexture(s->texture,NULL,s->argb,frame->width*(int)sizeof(uint32_t))!=0 || SDL_RenderClear(s->renderer)!=0 || SDL_RenderCopy(s->renderer,s->texture,NULL,NULL)!=0)return PN_IO;
    uint64_t t1=SDL_GetTicks64();SDL_RenderPresent(s->renderer);
    if(timing_on())fprintf(stderr,"timing present convert+upload=%llums swap=%llums\n",(unsigned long long)(t1-t0),(unsigned long long)(SDL_GetTicks64()-t1));
    return PN_OK;
}
static pn_status_t favorites_load(library_t *s);
/// 收藏修改后才重新加载，失败不缓存。/ Reload after favorite writes; do not cache failures.
static pn_status_t favorites_ensure(library_t *s){
    if(s->favorites_valid)return PN_OK;
    pn_status_t status=favorites_load(s);s->favorites_valid=status==PN_OK;return status;
}
static size_t cover_bytes(void *ctx){library_t *s=ctx;return pn_alloc_size(s->covers);}
static void cover_drop(void *ctx){library_t *s=ctx;if(s->covers_inactive){pn_free(s->covers);s->covers=NULL;s->covers_dirty=false;}}
static void covers_ensure(library_t *s){
    s->covers_inactive=false;
    if(!s->covers){s->covers=pn_cache_alloc(s->pool,PN_CACHE_COVER,PN_MEM_UI,sizeof *s->covers);if(s->covers)pn_shelf_covers_reset(s->covers,NULL);}
}
static pn_status_t draw_impl(library_t *s){
    covers_ensure(s);
    pn_wallpaper_mask_close(&s->lock_mask);
    if(!s->frame.pixels){uint8_t *pixels=pn_alloc_tagged(s->pool,PN_MEM_FRAME,342u*1216u);if(!pixels || !pn_frame_bind(&s->frame,pixels,342u*1216u,684,1216)){pn_free(pixels);return PN_NO_MEMORY;}}
    if(!s->font.impl){pn_text_source_t builtin=pn_font_builtin_source();pn_status_t opened=pn_font_open(&s->font,s->pool,&builtin,24);if(opened!=PN_OK)return opened;}
    uint64_t t0=SDL_GetTicks64();
    pn_shelf_options_t options=shelf_options(s);
    if(favorites_ensure(s)==PN_OK)options.favorite_items=s->favorites;
    pn_status_t status=pn_shelf_render_with_font_file_ex(s->page,&s->font,&s->frame,s->shelf_focus_visible?s->selected:-1,s->recent_mode,false,s->covers,s->pool,&s->media,*s->font_real?s->font_real:NULL,&options);
    if(status==PN_OK && s->open_failed){pn_frame_rect(&s->frame,32,970,620,64,15);status=pn_font_size(&s->font,28);if(status==PN_OK)status=pn_w_text(&s->font,&s->frame,"打开失败，请重试",48,1012,580,PN_ALIGN_LEFT);}
    if(timing_on())fprintf(stderr,"timing shelf render=%llums\n",(unsigned long long)(SDL_GetTicks64()-t0));
    if(status==PN_OK)printf("navigation root=%s count=%zu\n",s->home?"home":"library",s->page->count);
    return status==PN_OK?present(s,&s->frame,PN_REFRESH_GC16):status;
}
static pn_status_t draw(library_t *s){
    pn_profile_span_t span;pn_profile_begin(&span,s->pool,"ui.shelf");
    pn_status_t status=draw_impl(s);pn_profile_end(&span,status);return status;
}
static pn_status_t recent_load(library_t *s);
/* 最近阅读第一本作为“继续阅读”卡；没有记录时清除。/ The first recent entry becomes the continue-reading card; cleared when there is none. */
static void covers_last(library_t *s){
    if(!s->covers)return;
    pn_catalog_page_t *tmp=pn_alloc_tagged(s->pool,PN_MEM_UI,sizeof *tmp);bool found=false;
    bool loaded=tmp && recent_load(s)==PN_OK;
    if(loaded && s->page)pn_catalog_apply_recent(s->page,s->recent);
    if(loaded && s->recent->count && pn_catalog_recent_page(s->recent,0,tmp)==PN_OK && tmp->count){pn_shelf_covers_set_last(s->covers,&tmp->items[0]);found=true;}
    if(!found)pn_shelf_covers_set_last(s->covers,NULL);
    pn_free(tmp);
}
/* 换页：先复用缓存封面再绘制，其余空闲逐条解码。/ Page change: reuse cached covers before drawing; the rest decode one per idle tick. */
static void covers_reset(library_t *s){
    s->shelf_focus_visible=false;
    covers_ensure(s);if(!s->covers)return;
    bool changed;covers_last(s);pn_shelf_covers_reset(s->covers,s->page);s->covers_dirty=false;
    if(*s->cover_dir && pn_shelf_covers_cached(s->covers,s->page,s->pool,&s->media,s->cover_dir,&changed)!=PN_OK)pn_shelf_covers_reset(s->covers,NULL);
}
static void covers_tick(library_t *s){
    if(!s->covers)return;
    bool changed=false;
    // 仅在余量不足时让字形缓存让出，缓存命中不逐屏清空。/ Yield glyph caches only under pressure; cache hits no longer clear them on every screen.
    pn_cache_reclaim(s->pool,128u*1024u+s->pool->cache_reserve);
    pn_status_t status=pn_shelf_covers_step(s->covers,s->page,s->pool,&s->media,*s->cover_dir?s->cover_dir:NULL,s->salt,s->pool->limit>s->pool->used?s->pool->limit-s->pool->used:1,&changed);
    if(changed)s->covers_dirty=true;
    if(status==PN_EMPTY && s->covers_dirty){size_t ready=0;for(size_t i=0;i<s->page->count;i++)ready+=s->covers->state[i]==PN_COVER_READY;
        pn_status_t shown=draw(s);if(shown==PN_OK)s->covers_dirty=false;printf("covers ready=%zu status=%d\n",ready,(int)shown);}
    else if(status!=PN_OK && status!=PN_EMPTY && status!=PN_BUSY && status!=PN_CANCELLED)pn_shelf_covers_reset(s->covers,NULL);
}
static pn_status_t recent_load(library_t *s){
    if(s->recent_loaded)return PN_OK;
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(&s->state_media,PN_MEDIA_READ,&lease);if(status!=PN_OK)return status;
    pn_journal_files_t files;pn_journal_io_t io;status=pn_recent_files(&files,&s->state_media,&lease,s->state_dir,&io);
    if(status==PN_OK)status=pn_recent_load(&io,s->pool,s->recent);
    (void)pn_media_release(&s->state_media,&lease);if(status==PN_EMPTY){memset(s->recent,0,sizeof *s->recent);status=PN_OK;}if(status==PN_OK)s->recent_loaded=true;return status;
}
/* 书库所在磁盘的容量（模拟器的“存储卡”）。/ Capacity of the disk holding the library (the simulator's "card"). */
static void storage_note(library_t *s){
    struct statvfs st;s->storage[0]=0;
    if(statvfs(s->directory,&st)==0 && st.f_blocks){unsigned long long total=(unsigned long long)st.f_blocks*st.f_frsize,avail=(unsigned long long)st.f_bavail*st.f_frsize;
        snprintf(s->storage,sizeof s->storage,"共 %llu.%llu GB · 可用 %llu.%llu GB",total>>30,((total%(1ull<<30))*10)>>30,avail>>30,((avail%(1ull<<30))*10)>>30);}
}
static void open_actions(library_t *s,int index){
    if(index<0 || (size_t)index>=s->page->count)return;
    s->actions=(pn_book_actions_t){.item=s->page->items[index]};
    if(s->covers){snprintf(s->actions.title,sizeof s->actions.title,"%s",s->covers->display[index].title);snprintf(s->actions.author,sizeof s->actions.author,"%s",s->covers->display[index].author);}
    uint64_t started=SDL_GetTicks64();
    s->actions.favorites_known=favorites_ensure(s)==PN_OK;s->actions.favorite=s->actions.favorites_known && pn_favorites_contains(s->favorites,s->actions.item.path);
    pn_w_font_scope_t scope={0};pn_status_t status=pn_w_font_scope_begin(&scope,s->pool,NULL);
    if(status==PN_OK)status=pn_book_actions_render(&s->actions,&s->font,&s->frame);
    pn_w_font_scope_end(&scope);
    if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GL16);
    s->actions_mode=status==PN_OK;s->management_focus.index=-1;
    printf("actions open index=%d status=%d favorite=%d prepare+present=%llums\n",index,(int)status,(int)s->actions.favorite,(unsigned long long)(SDL_GetTicks64()-started));
}
static int shelf_hit(library_t *s,int x,int y){pn_shelf_options_t o=shelf_options(s);return pn_shelf_hit_ex(s->page,x,y,&o);}
static pn_status_t open_settings(library_t *s){
    pn_status_t status=pn_settings_ui_open(&s->settings,s->pool,&s->state_media,s->state_dir,present,s);
    if(status==PN_OK){pn_settings_about_t about={0};storage_note(s);snprintf(about.version,sizeof about.version,"PC 模拟器");snprintf(about.storage,sizeof about.storage,"%s",s->storage);
        snprintf(about.internal,sizeof about.internal,"%s",s->state_dir?"可用（--state-dir）":"未指定 --state-dir");snprintf(about.screen,sizeof about.screen,"684×1216 · 16 级灰阶（模拟）");pn_settings_ui_set_about(&s->settings,&about);}
    return status;
}
/* 传书一级页：模拟器没有无线传书，只显示原因。/ Transfer root tab: the simulator has no wireless transfer and only shows the reason. */
static pn_transfer_hub_info_t hub_info(library_t *s){return (pn_transfer_hub_info_t){.storage=s->storage,.unavailable="PC 模拟器不提供无线传书"};}
static pn_status_t open_hub(library_t *s){
    storage_note(s);pn_transfer_hub_info_t info=hub_info(s);
    pn_status_t status=pn_transfer_hub_render(&s->font,&s->frame,&info);if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GL16);
    s->hub=status==PN_OK;printf("hub open status=%d\n",(int)status);return status;
}
static pn_status_t favorites_load(library_t *s){
    if(!s->state_dir)return PN_UNSUPPORTED;
    if(!s->favorites)s->favorites=pn_alloc_tagged(s->pool,PN_MEM_UI,sizeof *s->favorites);
    if(!s->favorites)return PN_NO_MEMORY;
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(&s->state_media,PN_MEDIA_READ,&lease);if(status!=PN_OK)return status;
    pn_journal_files_t files;pn_journal_io_t io;status=pn_favorites_files(&files,&s->state_media,&lease,s->state_dir,&io);
    if(status==PN_OK)status=pn_favorites_load(&io,s->pool,s->favorites);
    (void)pn_media_release(&s->state_media,&lease);if(status==PN_EMPTY){memset(s->favorites,0,sizeof *s->favorites);status=PN_OK;}return status;
}
static pn_status_t page(library_t *s,bool previous,bool first){
    s->open_failed=false;
    if(!s->home && s->fav_mode){pn_status_t status=favorites_load(s);if(status!=PN_OK)return status;
        size_t per=shelf_per_page(s),start=first?0:previous?(s->fav_start>=per?s->fav_start-per:0):s->fav_start+per;
        if(start>=s->favorites->count && start)return PN_EMPTY;
        status=pn_catalog_favorites_page(s->favorites,start,per,s->page);if(status==PN_OK){s->fav_start=start;s->selected=s->page->count?0:-1;covers_reset(s);status=draw(s);printf("favorites_page start=%zu count=%zu\n",start,s->page->count);}return status;
    }
    if(s->home || s->recent_mode){pn_status_t status=recent_load(s);if(status!=PN_OK)return status;
        size_t per=shelf_per_page(s);size_t start=(s->home || first)?0:previous?(s->recent_start>=per?s->recent_start-per:0):s->recent_start+per;
        if(start>=s->recent->count && start)return PN_EMPTY;
        status=pn_catalog_recent_page_n(s->recent,start,per,s->page);if(status==PN_OK){s->recent_start=start;s->selected=s->page->count?0:-1;covers_reset(s);status=draw(s);printf("recent_page start=%zu count=%zu\n",start,s->page->count);}return status;
    }
    if(!first && (!s->page->count || (!previous && !s->page->more)))return PN_EMPTY;
    pn_catalog_page_t *next=pn_alloc_tagged(s->pool,PN_MEM_UI,sizeof *next);if(!next)return PN_NO_MEMORY;
    pn_media_lease_t lease={0};pn_status_t status=pn_media_acquire(&s->media,PN_MEDIA_READ,&lease);
    const char *cursor=first?"":s->page->items[previous?0:s->page->count-1].name;
    if(status==PN_OK){
        pn_catalog_index_stats_t before,after;pn_catalog_index_stats(&s->catalog_index,&before);
        status=pn_catalog_index_page(&s->catalog_index,&lease,cursor,previous,s->jump,*s->query?s->query:NULL,shelf_per_page(s),next);
        pn_catalog_index_stats(&s->catalog_index,&after);if(timing_on() && status==PN_OK)fprintf(stderr,"timing catalog index hit records=%llu total=%zu\n",(unsigned long long)(after.records_read-before.records_read),next->total);
        if(status==PN_EMPTY)status=*s->query?(previous?pn_catalog_search_page_before_n(&s->media,&lease,s->directory,s->query,cursor,shelf_per_page(s),next):pn_catalog_search_page_n(&s->media,&lease,s->directory,s->query,cursor,shelf_per_page(s),next)):s->jump?pn_catalog_page_from_n(&s->media,&lease,s->directory,s->jump,shelf_per_page(s),next):previous?pn_catalog_page_before_n(&s->media,&lease,s->directory,cursor,shelf_per_page(s),next):pn_catalog_page_n(&s->media,&lease,s->directory,cursor,shelf_per_page(s),next);
        s->jump=0;
        (void)pn_media_release(&s->media,&lease);
    }
    if(status==PN_OK && (first || next->count)){*s->page=*next;s->selected=next->count?0:-1;covers_reset(s);status=draw(s);
        if(status==PN_OK)printf("library_page first=%s count=%zu\n",next->count?next->items[0].name:"",next->count);
    }else if(status==PN_OK)status=PN_EMPTY;
    pn_free(next);return status;
}
static pn_status_t open_item_impl(library_t *s,const pn_catalog_item_t *book,uint64_t now){
    if(book->format!=PN_BOOK_TXT && book->format!=PN_BOOK_EPUB){pn_sdl_async_title(&s->async,PN_BRAND_NAME " - 此格式尚未接入");return PN_EMPTY;}
    pn_status_t status;
    // 进入阅读后不再使用书架备份，先释放，给首章图片留出解码预算。/ The shelf backup is unused after entering reading; release it before decoding first-section images.
    s->covers_inactive=true;
    s->open_failed=false;pn_free(s->page_px);s->page_px=NULL;
    pn_wallpaper_mask_close(&s->lock_mask);
    if(pn_reader_loading_needed(book->format==PN_BOOK_TXT,book->size) && s->font.impl && s->frame.pixels){status=pn_reader_loading_render(&s->font,&s->frame,book->name);if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GL16);if(status==PN_CANCELLED)return status;}
    if(book->format==PN_BOOK_EPUB){pn_font_close(&s->font);pn_free(s->frame.pixels);s->frame.pixels=NULL;status=pn_epub_app_open_on_media(&s->epub,s->pool,&s->media,book->path,s->font_path,s->state_dir,32,now);}
    else status=pn_reader_app_open_on_media(&s->reader,s->pool,&s->media,book->path,s->font_path,s->state_dir,32,now);
    fprintf(stderr,"library_open prepare name=%s status=%d pool=%zu/%zu\n",book->name,(int)status,s->pool->used,s->pool->limit);
    if(status==PN_OK && s->fallback_path)status=s->epub.impl?pn_epub_app_fallback_font(&s->epub,s->fallback_path):pn_reader_app_fallback_font(&s->reader,s->fallback_path);
    if(status==PN_OK && book->identified){pn_book_id_t actual;status=s->epub.impl?pn_epub_app_identity(&s->epub,&actual):pn_reader_app_identity(&s->reader,&actual);if(status==PN_OK && memcmp(actual.sha256,book->expected.sha256,32))status=PN_STALE_JOB;printf("recent_identity status=%d\n",(int)status);}
    if(status==PN_OK)status=active_step(s,PN_APP_OPEN,now,present,s);
    fprintf(stderr,"library_open first-page name=%s status=%d\n",book->name,(int)status);
    if(status==PN_OK){pn_txt_progress_t progress;s->last_read=*book;
        s->last_read.identified=(s->epub.impl?pn_epub_app_identity(&s->epub,&s->last_read.expected):pn_reader_app_identity(&s->reader,&s->last_read.expected))==PN_OK;s->last_read_known=s->last_read.identified;
        printf("library_open name=%s\n",book->name);
        if(s->epub.impl){pn_epub_progress_t location;if(pn_epub_app_progress(&s->epub,&location)==PN_OK)printf("epub_library_position path=%s element=%llu run=%u offset=%llu\n",location.location.path,(unsigned long long)location.location.position.element,location.location.position.run,(unsigned long long)location.location.position.offset);}
        else if(pn_reader_app_progress(&s->reader,&progress)==PN_OK)printf("library_position offset=%llu\n",(unsigned long long)progress.source_offset);
    }
    else if(reading(s)){s->open_failed=status!=PN_CANCELLED;pn_status_t closed=active_close(s,now);if(closed==PN_OK && status!=PN_CANCELLED){(void)draw(s);printf("shelf_open_failed status=%d notice=1\n",(int)status);}}
    else{s->open_failed=status!=PN_CANCELLED;if(status!=PN_CANCELLED){(void)draw(s);printf("shelf_open_failed status=%d notice=1\n",(int)status);}}
    return status;
}
static pn_status_t open_item(library_t *s,const pn_catalog_item_t *book,uint64_t now){
    pn_async_block_controls(&s->async.input,true);pn_sdl_async_title(&s->async,PN_BRAND_NAME " - 正在打开书籍");
    printf("opening begin name=%s\n",book->name);pn_status_t status=open_item_impl(s,book,now);
    pn_async_block_controls(&s->async.input,false);pn_sdl_async_title(&s->async,PN_BRAND_NAME);
    printf("opening end status=%d\n",(int)status);return status;
}
static pn_status_t open_book(library_t *s,int selected,uint64_t now){
    if(selected<0 || (size_t)selected>=s->page->count)return PN_EMPTY;
    return open_item(s,&s->page->items[selected],now);
}
/* 继续阅读：直接打开最近一本，不改变书架的全部/最近模式。/ Continue reading: open the latest book directly without changing the shelf's All/Recent mode. */
static pn_status_t continue_book(library_t *s,uint64_t now){
    if(recent_load(s)!=PN_OK || !s->recent->count)return PN_EMPTY;
    pn_catalog_page_t *tmp=pn_alloc_tagged(s->pool,PN_MEM_UI,sizeof *tmp);if(!tmp)return PN_NO_MEMORY;
    pn_status_t status=pn_catalog_recent_page(s->recent,0,tmp);
    if(status==PN_OK && tmp->count)status=open_item(s,&tmp->items[0],now);else if(status==PN_OK)status=PN_EMPTY;
    pn_free(tmp);return status;
}
/* 工具栏中当前书不能用的入口：TXT没有目录，搜索尚未实现。/ Entries unavailable for the current book: TXT has no TOC and search is not implemented. */
static unsigned toolbar_unavailable(library_t *s){return s->epub.impl?0u:1u;}
/* 在上一帧页面上叠加工具栏并呈现。/ Overlay the toolbar on the last page frame and present it. */
static pn_status_t toolbar_show(library_t *s){
    if(!reading(s) || !s->page_px)return PN_EMPTY;
    pn_frame_t frame;if(!pn_frame_bind(&frame,s->page_px,342u*1216u,684,1216))return PN_INVALID;
    pn_font_t ui={0};pn_text_source_t builtin=pn_font_builtin_source();pn_status_t status=pn_font_open(&ui,s->pool,&builtin,34);
    if(status==PN_OK){s->toolbar=true;status=pn_reader_toolbar_present(s->pool,&ui,&frame,toolbar_unavailable(s),NULL,present,s);if(status!=PN_OK)s->toolbar=false;}
    pn_font_close(&ui);
    printf("toolbar open status=%d\n",(int)status);return status;
}
/* 关闭工具栏：恢复原页面；refresh为真时按强刷重画。/ Close the toolbar and restore the page; a full refresh repaint when refresh is true. */
static pn_status_t toolbar_close(library_t *s,bool refresh){
    s->toolbar=false;
    if(!s->page_px)return PN_EMPTY;
    pn_frame_t frame;if(!pn_frame_bind(&frame,s->page_px,342u*1216u,684,1216))return PN_INVALID;
    pn_status_t status=present_raw(s,&frame);if(status==PN_OK && refresh){pn_reader_app_refresh_reset(&s->reader);pn_epub_app_refresh_reset(&s->epub);}
    printf("toolbar close refresh=%d status=%d\n",(int)refresh,(int)status);return status;
}
static pn_status_t return_to_shelf(library_t *s,uint64_t now){
    pn_status_t status=active_close(s,now);
    if(status==PN_OK){pn_toc_ui_close(&s->toc);pn_bookmark_ui_cancel(&s->bookmarks);pn_style_ui_close(&s->styles);pn_jump_ui_close(&s->jumpui);pn_font_ui_close(&s->fonts);pn_sdl_async_text(&s->async,false);if(s->home)status=page(s,false,true);else{covers_reset(s);status=draw(s);}printf("library_return status=%d\n",(int)status);}return status;
}
/// 管理页命中源，与点击共用。/ Management hit source shared with pointer input.
static int management_hit(void *ctx,int x,int y){
    library_t *s=ctx;
    if(s->fonts.active)return pn_font_ui_hit(&s->fonts,x,y);
    if(s->toc.active)return pn_toc_ui_hit(&s->toc,x,y);
    if(s->jumpui.active)return pn_jump_ui_hit(&s->jumpui,x,y);
    if(s->styles.active)return pn_style_ui_hit(&s->styles,x,y);
    if(s->bookmarks.mode!=PN_BUI_CLOSED)return pn_bookmark_ui_hit(&s->bookmarks,x,y);
    if(s->actions_mode)return pn_book_actions_hit(&s->actions,x,y);
    if(s->settings.impl)return pn_settings_ui_hit(&s->settings,x,y);
    if(s->font_manage.impl)return pn_font_manage_hit(&s->font_manage,x,y);
    if(s->wallpaper.impl)return pn_wallpaper_ui_hit(&s->wallpaper,x,y);
    if(s->hub){pn_transfer_hub_info_t info=hub_info(s);return pn_transfer_hub_hit(&info,x,y);}
    if(s->search_open)return pn_search_ui_hit(x,y);
    if(s->index){char letter=pn_shelf_index_hit(x,y);return letter?(unsigned char)letter:-1;}
    if(s->toolbar)return pn_reader_toolbar_hit(x,y,toolbar_unavailable(s));
    return -1;
}
/// 数字1/2/3模拟左/中/右键；方向键仍可选择阅读菜单项。/ Digits 1/2/3 simulate left/middle/right keys; arrows still select reader-menu items.
static bool management_event(library_t *s,SDL_Event *event,uint64_t now){
    bool active=s->fonts.active || s->toc.active || s->jumpui.active || s->styles.active || s->bookmarks.mode!=PN_BUI_CLOSED || s->actions_mode || s->settings.impl || s->font_manage.impl || s->wallpaper.impl || s->hub || s->search_open || s->index || s->toolbar;
    if(!active || event->type!=SDL_KEYDOWN || event->key.keysym.sym<SDLK_1 || event->key.keysym.sym>SDLK_3)return false;
    if(event->key.repeat)return true;
    int key=(int)(event->key.keysym.sym-SDLK_1);
    if(key==PN_KEY_2){
        pn_status_t status=reading(s)?active_close(s,now):PN_OK;
        if(status!=PN_OK){printf("management home status=%d retained=1\n",(int)status);return true;}
        pn_toc_ui_close(&s->toc);pn_bookmark_ui_cancel(&s->bookmarks);pn_style_ui_close(&s->styles);pn_jump_ui_close(&s->jumpui);pn_font_ui_close(&s->fonts);
        pn_wallpaper_ui_close(&s->wallpaper);pn_font_manage_close(&s->font_manage);pn_settings_ui_close(&s->settings);
        s->toolbar=false;s->hub=false;s->index=false;s->search_open=false;s->actions_mode=false;s->back_to_settings=false;s->home=true;
        s->management_focus.index=-1;pn_sdl_async_text(&s->async,false);status=page(s,false,true);printf("management home status=%d\n",(int)status);return true;
    }
    if(key==PN_KEY_3 && (s->fonts.active || s->toc.active || s->styles.active || s->jumpui.active || s->bookmarks.mode!=PN_BUI_CLOSED)){
        event->key.keysym.sym=SDLK_RETURN;return false;
    }
    if(key==PN_KEY_1 && ((s->fonts.active && s->fonts.preview) || (s->styles.active && s->styles.preview))){event->key.keysym.sym=SDLK_ESCAPE;return false;}
    int back=s->fonts.active?PN_FUI_BACK:s->toc.active?PN_TOC_UI_BACK:s->jumpui.active?PN_JUI_CANCEL:s->styles.active?(s->styles.more?PN_SUI_BACK_MAIN:PN_SUI_CANCEL):s->bookmarks.mode!=PN_BUI_CLOSED?PN_BUI_BACK:s->actions_mode?PN_BA_CANCEL:s->settings.impl?PN_SETUI_BACK:s->font_manage.impl?PN_FMU_BACK:s->wallpaper.impl?(s->wallpaper.screen==PN_WUI_LIST?PN_WUI_BACK:PN_WUI_CANCEL):s->hub?PN_HUB_SHELF:s->search_open?PN_SEARCH_BACK:s->index?'<':PN_TOOL_CLOSE;
    pn_focus_scan(&s->management_focus,management_hit,s,NULL,0);
    int confirm=-1;
    if(s->actions_mode && s->actions.confirming)confirm=PN_BA_CONFIRM;
    else if(s->font_manage.impl && s->font_manage.screen==PN_FMU_CONFIRMING)confirm=PN_FMU_CONFIRM;
    else for(size_t i=0;i<s->management_focus.count;i++)if(s->management_focus.items[i].code!=back){confirm=s->management_focus.items[i].code;break;}
    int command=pn_key_management_command(key,PN_KEY_SHORT,back,confirm,-1);
    for(size_t i=0;i<s->management_focus.count;i++)if(s->management_focus.items[i].code==command){
        // 同命中码可能有分离区域，外接矩形中心未必可点。/ A code can span disjoint regions; its bounding-box center may not be clickable.
        for(int y=4;y<1216;y+=8)for(int x=4;x<684;x+=8)if(management_hit(s,x,y)==command){
            *event=(SDL_Event){.type=SDL_MOUSEBUTTONUP};event->button.button=SDL_BUTTON_LEFT;event->button.x=x;event->button.y=y;
            s->toc_pointer=true;s->font_pointer=true;return false;
        }
    }
    event->key.keysym.sym=SDLK_ESCAPE;return key==PN_KEY_3;
}
/// 设置页借用在读书快照，正文关闭后仍能预览封面。/ Settings use an in-progress book snapshot, allowing cover preview after the reader closes.
static pn_status_t wallpaper_open(library_t *s){
    pn_wallpaper_mask_close(&s->lock_mask);
    pn_status_t status=pn_wallpaper_ui_open(&s->wallpaper,s->pool,&s->media,s->wallpaper_dir,s->wallpaper_store_ok?&s->wallpaper_store:NULL,present,s);
    return status;
}
/// PC电源边界：保存关闭正文后，用共用锁屏记录/绘制预览；不模拟硬件睡眠或功耗。/ PC power boundary: save/close the reader and preview shared lock records/rendering; no hardware sleep or power simulation.
static pn_status_t lock_preview(library_t *s,uint64_t now){
    pn_status_t status=PN_OK;
    if(reading(s)){status=s->epub.impl?pn_epub_app_suspend(&s->epub,now):pn_reader_app_suspend(&s->reader,now);if(status==PN_BUSY)status=active_close(s,now);}
    if(status!=PN_OK)return status;
    s->toolbar=false;
    if(!s->frame.pixels){uint8_t *pixels=pn_alloc_tagged(s->pool,PN_MEM_FRAME,PN_WALLPAPER_BYTES);if(!pixels)return PN_NO_MEMORY;pn_frame_bind(&s->frame,pixels,PN_WALLPAPER_BYTES,684,1216);}
    if(!s->font.impl){pn_text_source_t font=pn_font_builtin_source();status=pn_font_open(&s->font,s->pool,&font,32);}
    pn_lock_selection_t selection={.mode=PN_LOCK_DEFAULT,.hint=true};
    if(status==PN_OK && s->wallpaper_store_ok){pn_media_lease_t lease;pn_status_t loaded=pn_media_acquire(&s->wallpaper_media,PN_MEDIA_READ,&lease);
        if(loaded==PN_OK){loaded=pn_wallpaper_load(&s->wallpaper_store,&lease,&selection,&s->frame);(void)pn_media_release(&s->wallpaper_media,&lease);}
        if(loaded!=PN_OK)selection=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=true};}
    if(selection.mode==PN_LOCK_SIMPLE || selection.mode==PN_LOCK_BOOK)selection=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=true};
    if(selection.mode==PN_LOCK_MASK){
        pn_status_t mask=PN_EMPTY;
        if(reading(s) && s->page_px){
            mask=pn_wallpaper_mask_open(&s->lock_mask,s->pool,&s->media,&selection);
            if(mask==PN_OK){memcpy(s->frame.pixels,s->page_px,PN_WALLPAPER_BYTES);mask=pn_wallpaper_mask_paint(&s->lock_mask,&s->frame);}
        }
        printf("lock_mask status=%d\n",(int)mask);pn_wallpaper_mask_close(&s->lock_mask);
        if(mask!=PN_OK)selection=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=true};
    }
    if(!reading(s))pn_wallpaper_mask_close(&s->lock_mask);
    if(status==PN_OK)status=pn_lock_render(&selection,&s->font,"按电源键继续阅读",&s->frame);
    if(status==PN_OK)status=present_raw(s,&s->frame);
    s->locked=status==PN_OK;printf("lock_preview status=%d mode=%d cached=%d\n",(int)status,(int)selection.mode,selection.cached_book);return status;
}
static int library_worker(void *ctx){
    library_t *s=ctx;pn_pool_t *pool=s->pool;
    if(timing_on())pn_profile_port(profile_clock,profile_emit,NULL);
    const char *font_path=s->font_path,*cover_cache=s->cover_argument,*wallpaper_dir=s->wallpaper_dir,*wallpaper_store=s->wallpaper_store_argument,*font_dir=s->font_dir;
    if(font_path){char *resolved=realpath(font_path,NULL);if(resolved){if(strlen(resolved)<sizeof s->font_real)strcpy(s->font_real,resolved);free(resolved);}}
    s->page=pn_alloc_tagged(pool,PN_MEM_UI,sizeof *s->page);s->frame.pixels=pn_alloc_tagged(pool,PN_MEM_FRAME,342u*1216u);
    s->recent=pn_alloc_tagged(pool,PN_MEM_UI,sizeof *s->recent);pn_cache_attach(pool,&s->cover_cache,PN_CACHE_COVER,s,cover_bytes,cover_drop);covers_ensure(s);
    if(cover_cache && (size_t)snprintf(s->cover_dir,sizeof s->cover_dir,"%s",cover_cache)>=sizeof s->cover_dir)s->cover_dir[0]=0;
    s->wallpaper_dir=wallpaper_dir;s->font_dir=font_dir;
    if(font_dir){char *resolved=realpath(font_dir,NULL);if(resolved){if(strlen(resolved)<sizeof s->font_dir_real){strcpy(s->font_dir_real,resolved);s->font_dir=s->font_dir_real;}free(resolved);}}
    if(wallpaper_dir){char *resolved=realpath(wallpaper_dir,NULL);if(resolved){if(strlen(resolved)<sizeof s->wallpaper_dir_real){strcpy(s->wallpaper_dir_real,resolved);s->wallpaper_dir=s->wallpaper_dir_real;}free(resolved);}}
    if(wallpaper_store){char a[PN_WALLPAPER_PATH_MAX],b[PN_WALLPAPER_PATH_MAX];pn_media_init(&s->wallpaper_media);
        s->wallpaper_store_ok=(size_t)snprintf(a,sizeof a,"%s/lock.a",wallpaper_store)<sizeof a && (size_t)snprintf(b,sizeof b,"%s/lock.b",wallpaper_store)<sizeof b &&
            pn_media_attach(&s->wallpaper_media,3)==PN_OK && pn_wallpaper_store_init(&s->wallpaper_store,&s->wallpaper_media,a,b)==PN_OK;}
    {FILE *random=fopen("/dev/urandom","rb");bool seeded=random && fread(s->salt,1,sizeof s->salt,random)==sizeof s->salt;if(random)fclose(random);if(!seeded){pn_free(s->covers);s->covers=NULL;}}
    s->argb=malloc(684u*1216u*sizeof(uint32_t));
    int result=1;pn_status_t status=PN_NO_MEMORY;
    if(!s->page || !s->recent || !s->frame.pixels || !s->argb)goto cleanup;
    memset(s->page,0,sizeof *s->page);
    if(!pn_frame_bind(&s->frame,s->frame.pixels,342u*1216u,684,1216))goto cleanup;
    if(getenv("PN_SIM_BOOT_PREVIEW")){status=pn_brand_image_render(&s->frame);if(status==PN_OK)status=present_raw(s,&s->frame);if(status!=PN_OK)goto cleanup;SDL_Delay(900);}
    pn_text_source_t source=pn_font_builtin_source();status=pn_font_open(&s->font,pool,&source,24);if(status!=PN_OK)goto cleanup;
    pn_media_init(&s->media);status=pn_media_attach(&s->media,1);if(status!=PN_OK)goto cleanup;
    if(cover_cache){char cache[PN_CATALOG_PATH_MAX];int n=snprintf(cache,sizeof cache,"%s/library",cover_cache);if(n>0 && (size_t)n<sizeof cache)(void)pn_catalog_index_init(&s->catalog_index,pool,&s->media,s->directory,cache);}
    pn_media_init(&s->state_media);status=pn_media_attach(&s->state_media,1);if(status!=PN_OK)goto cleanup;
    s->shelf_prefs=pn_shelf_prefs_default();
    (void)pn_settings_load_shelf(&s->state_media,s->state_dir,&s->shelf_prefs);
    s->home=true;status=page(s,false,true);if(status!=PN_OK)goto cleanup;
    if(pn_settings_load_flags(&s->state_media,s->state_dir,&s->input_flags)!=PN_OK)s->input_flags=0;
    result=0;bool running=true;pn_tap_t tap={0};pn_reader_input_t input={0};
    while(running){
        {static time_t last;time_t t=time(NULL);if(t!=last){last=t;struct tm local;char text[8];if(localtime_r(&t,&local) && strftime(text,sizeof text,"%H:%M",&local))pn_w_set_clock(text);}} // 模拟器用本机时间 / The simulator uses the host clock
        uint32_t view=s->locked?1:s->fonts.active?2:s->toc.active?3:s->styles.active?4:s->jumpui.active?5:s->bookmarks.mode!=PN_BUI_CLOSED?6:s->actions_mode?7:s->settings.impl?8:s->wallpaper.impl?9:s->font_manage.impl?10:s->hub?11:s->index?12:s->search_open?13:s->toolbar?14:reading(s)?15:s->home?16:17;
        if(!reading(s) && !s->home && s->page)view+=(uint32_t)s->page->index*32u;
        SDL_Event event;bool got=pn_sdl_async_wait(&s->async,&event,100,view);uint64_t now=SDL_GetTicks64();
        if(got && event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){s->pointer_down=false;s->toc_pointer=false;s->font_pointer=false;cancel_pointer(&input,&tap,true);}

        if(reading(s)){status=s->epub.impl?pn_epub_app_tick(&s->epub,now):pn_reader_app_tick(&s->reader,now);if(status!=PN_OK && status!=PN_BUSY)pn_sdl_async_title(&s->async,PN_BRAND_NAME " - 保存失败，关闭前请重试");}
        if(s->bookmarks.mode!=PN_BUI_CLOSED && !s->bookmarks.presented && now-s->ui_retry>=1000){s->ui_retry=now;status=pn_bookmark_ui_present(&s->bookmarks,present,s);}
        // 页面请求不等待下一次输入，空闲轮询也完成导航。/ Navigation requests complete during idle polling without waiting for another input.
        if(!s->locked && s->styles.request_fonts){s->styles.request_fonts=false;status=pn_font_ui_open(&s->fonts,s->pool,s->epub.impl?NULL:&s->reader,s->epub.impl?&s->epub:NULL,s->font_dir,present,s);pn_font_ui_return_to(&s->fonts,pn_style_ui_render,&s->styles);printf("font_ui open status=%d active=%d\n",status,s->fonts.active);}
        if(!got){if(!s->locked && reading(s) && !s->toolbar && !s->styles.active && !s->fonts.active && !s->toc.active && !s->jumpui.active && s->bookmarks.mode==PN_BUI_CLOSED){status=s->epub.impl?pn_epub_app_prefetch(&s->epub):pn_reader_app_prefetch(&s->reader);if(status==PN_OK)printf("neighbor prefetch ready\n");}
            if(!s->locked && !reading(s) && !s->pointer_down && !s->hub && !s->index && !s->search_open && !s->actions_mode && !s->wallpaper.impl && !s->font_manage.impl && !s->settings.impl){pn_status_t indexed=pn_catalog_index_build(&s->catalog_index);if(indexed==PN_OK)printf("catalog index built\n");covers_tick(s);}
            continue;}

        if(!s->locked && management_event(s,&event,now)){cancel_pointer(&input,&tap,true);continue;}
        if(s->locked){
            if(event.type==SDL_QUIT){running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat && (event.key.keysym.sym==SDLK_l || event.key.keysym.sym==SDLK_RETURN || event.key.keysym.sym==SDLK_ESCAPE)){
                s->locked=false;
                if(reading(s))status=s->epub.impl?pn_epub_app_resume(&s->epub,now,present,s):pn_reader_app_resume(&s->reader,now,present,s);
                else status=s->last_read_known?open_item(s,&s->last_read,now):draw(s);
                printf("lock_preview wake status=%d\n",(int)status);}
            continue;
        }
        if(font_modal(&s->fonts,&event,now,present,s,&tap,&s->font_pointer,atomic_load(&s->async.mouse))){if(!s->fonts.active){bool returned=s->fonts.return_presented;pn_font_ui_close(&s->fonts);if(s->styles.active){if(returned){s->styles.presented=true;s->styles.preview=false;}else (void)pn_style_ui_present(&s->styles,present,s);}}continue;}

        if(event.type==SDL_KEYDOWN || event.type==SDL_TEXTINPUT)s->pointer_down=false;
        if(s->toc.active && event.type!=SDL_QUIT){
            if(!s->toc.presented && now-s->ui_retry>=1000){s->ui_retry=now;(void)pn_toc_ui_present(&s->toc,present,s);}
            int command=event.type==SDL_KEYDOWN && !event.key.repeat?toc_key(&s->toc,event.key.keysym.sym):-1;
            if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION){
                if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT)s->toc_pointer=true;
                if((event.type!=SDL_MOUSEMOTION && event.button.button!=SDL_BUTTON_LEFT) || !s->toc_pointer)continue;
                bool motion=event.type==SDL_MOUSEMOTION;int target=pn_toc_ui_hit(&s->toc,motion?event.motion.x:event.button.x,motion?event.motion.y:event.button.y);
                (void)pn_tap_feed(&tap,1,target,true,&command);
                if(event.type==SDL_MOUSEBUTTONUP){s->toc_pointer=false;(void)pn_tap_feed(&tap,0,-1,true,&command);(void)pn_tap_feed(&tap,0,-1,true,&command);}
            }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){s->toc_pointer=false;int ignored;(void)pn_tap_feed(&tap,1,-1,false,&ignored);}
            if(command>=0){if(event.type==SDL_KEYDOWN)s->toc_pointer=false;status=pn_toc_ui_event(&s->toc,command,now,present,s);toc_report(&s->toc,command,status);cancel_pointer(&input,&tap,(atomic_load(&s->async.mouse)&SDL_BUTTON_LMASK)==0);}continue;
        }
        if(s->jumpui.active){
            // 跳转面板：Esc取消，Enter跳转，左右键±1，上下翻页键±大步；鼠标松开命中。/ Jump panel: Esc cancels, Enter jumps, left/right step by 1, page keys by the large step; mouse release hits.
            int command=-1;
            if(event.type==SDL_KEYDOWN && !event.key.repeat){SDL_Keycode key=event.key.keysym.sym;
                command=key==SDLK_ESCAPE?PN_JUI_CANCEL:key==SDLK_RETURN?PN_JUI_CONFIRM:key==SDLK_LEFT?PN_JUI_STEP+1:key==SDLK_RIGHT?PN_JUI_STEP+2:key==SDLK_PAGEUP?PN_JUI_STEP:key==SDLK_PAGEDOWN?PN_JUI_STEP+3:-1;}
            else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)command=pn_jump_ui_hit(&s->jumpui,event.button.x,event.button.y);
            if(command>=0){status=pn_jump_ui_event(&s->jumpui,command,now,present,s);printf("jump_ui command=%d status=%d active=%d draft=%u\n",command,(int)status,(int)s->jumpui.active,s->jumpui.draft);cancel_pointer(&input,&tap,(atomic_load(&s->async.mouse)&SDL_BUTTON_LMASK)==0);}
            continue;
        }
        if(s->styles.active){
            if(!s->styles.presented && now-s->ui_retry>=1000){s->ui_retry=now;(void)pn_style_ui_present(&s->styles,present,s);}
            int command=event.type==SDL_KEYDOWN && !event.key.repeat?style_key(&s->styles,event.key.keysym.sym):-1;
            if(command>=0){status=pn_style_ui_event(&s->styles,command,now,present,s);style_report(&s->styles,command,status);cancel_pointer(&input,&tap,(atomic_load(&s->async.mouse)&SDL_BUTTON_LMASK)==0);continue;}
        }
        if(s->bookmarks.mode!=PN_BUI_CLOSED){
            int command=event.type==SDL_KEYDOWN && !event.key.repeat?bookmark_key(&s->bookmarks,event.key.keysym.sym):event.type==SDL_TEXTINPUT && s->bookmarks.mode==PN_BUI_RENAME?PN_BUI_TEXT:-1;
            if(command>=0){status=pn_bookmark_ui_event(&s->bookmarks,command,event.type==SDL_TEXTINPUT?event.text.text:NULL,now,present,s);bookmark_report(&s->bookmarks,command,status);cancel_pointer(&input,&tap,(atomic_load(&s->async.mouse)&SDL_BUTTON_LMASK)==0);continue;}
        }
        input.config=(pn_reader_input_config_t){.left_hand=(s->input_flags&PN_INPUT_LEFT_HAND)!=0,.no_swipe=(s->input_flags&PN_INPUT_NO_SWIPE)!=0,.no_edge_tap=(s->input_flags&PN_INPUT_NO_EDGE_TAP)!=0,.no_keys=(s->input_flags&PN_INPUT_NO_KEYS)!=0};
        if(s->actions_mode){
            // 书籍操作面板：鼠标松开命中，Esc取消。/ Book actions sheet: mouse release hits, Esc cancels.
            int hit=-1;
            if(event.type==SDL_QUIT){running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_ESCAPE)hit=PN_BA_CANCEL;
            else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)hit=pn_book_actions_hit(&s->actions,event.button.x,event.button.y);
            if(hit<0)continue;
            if(hit==PN_BA_CANCEL){if(s->actions.confirming)s->actions.confirming=false;else s->actions_mode=false;status=draw(s);}
            else if(hit==PN_BA_DELETE){s->actions.confirming=true;s->actions.notice=NULL;status=draw(s);}
            else if(hit==PN_BA_FAVORITE){
                pn_media_lease_t lease;bool now=s->actions.favorite;status=pn_media_acquire(&s->state_media,PN_MEDIA_WRITE,&lease);
                if(status==PN_OK){pn_journal_files_t files;pn_journal_io_t io;status=pn_favorites_files(&files,&s->state_media,&lease,s->state_dir,&io);if(status==PN_OK)status=pn_favorites_toggle(&io,s->pool,s->actions.item.path,&now);(void)pn_media_release(&s->state_media,&lease);}
                if(status==PN_OK)s->actions.favorite=now;
                s->favorites_valid=false;
                s->actions.notice=status==PN_OK?(now?"已加入收藏":"已取消收藏"):status==PN_LIMIT?"收藏最多 50 本":"保存失败，请重试";(void)draw(s);
            }
            else if(hit==PN_BA_CONFIRM){
                pn_media_lease_t lease;status=pn_media_acquire(&s->media,PN_MEDIA_WRITE,&lease);
                if(status==PN_OK){if(remove(s->actions.item.path)!=0)status=PN_IO;else{pn_catalog_index_invalidate(&s->catalog_index);s->recent_loaded=false;}(void)pn_media_release(&s->media,&lease);}
                if(status==PN_OK){s->actions_mode=false;status=page(s,false,true);}else{s->actions.notice="删除失败，文件仍保留";(void)draw(s);}
            }
            else if(hit==PN_BA_OPEN){pn_catalog_item_t *book=pn_alloc_tagged(s->pool,PN_MEM_UI,sizeof *book);s->actions_mode=false;if(book){*book=s->actions.item;status=open_item(s,book,now);pn_free(book);}}
            printf("actions command=%d status=%d active=%d favorite=%d\n",hit,(int)status,(int)s->actions_mode,(int)s->actions.favorite);
            continue;
        }
        if(s->hub){
            // 传书一级页：底栏切换书架/设置，Esc回书架。/ Transfer root tab: the bottom bar switches to the shelf or Settings; Esc returns to the shelf.
            int hit=-1;
            if(event.type==SDL_QUIT){running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_ESCAPE)hit=PN_HUB_SHELF;
            else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT){pn_transfer_hub_info_t info=hub_info(s);hit=pn_transfer_hub_hit(&info,event.button.x,event.button.y);}
            if(hit==PN_HUB_SHELF || hit==PN_HUB_HOME){s->hub=false;s->home=hit==PN_HUB_HOME;status=page(s,false,true);}
            else if(hit==PN_HUB_SETTINGS){s->hub=false;status=open_settings(s);}
            if(hit>=0)printf("hub command=%d status=%d\n",hit,(int)status);
            continue;
        }
        if(s->index){
            // 字母页：鼠标松开选字母，Esc返回。/ Index page: mouse release picks a letter, Esc returns.
            char letter=0;
            if(event.type==SDL_QUIT){running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_ESCAPE)letter='<';
            else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)letter=pn_shelf_index_hit(event.button.x,event.button.y);
            if(letter){s->index=false;if(letter!='<'){s->jump=letter;s->recent_mode=false;}status=page(s,false,true);printf("index letter=%c status=%d first=%s\n",letter,(int)status,s->page->count?s->page->items[0].name:"");}
            continue;
        }
        if(s->search_open){
            // 搜索页：字母/数字键输入，Backspace删除，Enter搜索，Esc返回；鼠标松开命中屏幕键盘。/ Search page: letter/digit keys type, Backspace deletes, Enter searches, Esc returns; mouse release hits the on-screen keyboard.
            int key=PN_SEARCH_NONE;
            if(event.type==SDL_QUIT){running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat){SDL_Keycode sym=event.key.keysym.sym;
                key=sym==SDLK_ESCAPE?PN_SEARCH_BACK:sym==SDLK_BACKSPACE?PN_SEARCH_DELETE:sym==SDLK_RETURN?PN_SEARCH_DONE:(sym>='a' && sym<='z') || (sym>='0' && sym<='9')?(int)sym:PN_SEARCH_NONE;}
            else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)key=pn_search_ui_hit(event.button.x,event.button.y);
            if(key==PN_SEARCH_NONE)continue;
            if(key==PN_SEARCH_BACK){s->search_open=false;status=draw(s);}
            else if(key==PN_SEARCH_DONE){snprintf(s->query,sizeof s->query,"%s",s->search.query);s->search_open=false;s->recent_mode=false;status=page(s,false,true);printf("search query=%s status=%d count=%zu\n",s->query,(int)status,s->page->count);}
            else{
                bool changed=key==PN_SEARCH_DELETE?pn_search_ui_delete(&s->search):key==PN_SEARCH_CLEAR?(*s->search.query?(s->search.query[0]=0,true):false):pn_search_ui_append(&s->search,(char)key);
                if(changed){status=pn_search_ui_render(&s->search,&s->font,&s->frame);if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GL16);}
            }
            continue;
        }
        if(s->settings.impl){
            // 设置页：Esc返回，鼠标松开命中；子页按已启用选项打开。/ Settings: Esc returns, mouse release hits; sub-pages open when their options are enabled.
            int command=-1;
            if(event.type==SDL_QUIT){pn_settings_ui_close(&s->settings);running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_ESCAPE)command=PN_SETUI_BACK;
            else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)command=pn_settings_ui_hit(&s->settings,event.button.x,event.button.y);
            if(command>=0){status=pn_settings_ui_event(&s->settings,command,present,s);if(command==PN_SETUI_FULL_REFRESH && status==PN_OK){pn_reader_app_refresh_reset(&s->reader);pn_epub_app_refresh_reset(&s->epub);}s->input_flags=s->settings.flags;s->shelf_prefs=s->settings.shelf;printf("settings command=%d status=%d flags=%u active=%d\n",command,(int)status,(unsigned)s->settings.flags,(int)s->settings.active);
                int request=s->settings.request;
                if(request || !s->settings.active){pn_settings_ui_close(&s->settings);
                    s->back_to_settings=(request==PN_SETUI_FONTS && s->font_dir) || (request==PN_SETUI_WALLPAPER && s->wallpaper_dir);
                    if(request==PN_SETUI_TRANSFER)status=open_hub(s);
                    else if(request==PN_SETUI_FONTS && s->font_dir)status=pn_font_manage_open(&s->font_manage,s->pool,&s->media,s->font_dir,&s->state_media,s->state_dir,present,s);
                    else if(request==PN_SETUI_WALLPAPER && s->wallpaper_dir)status=wallpaper_open(s);
                    else{if(request==PN_SETUI_HOME)s->home=true;else if(request==PN_SETUI_SHELF)s->home=false;status=page(s,false,true);}
                    printf("settings closed request=%d columns=%u style=%u\n",request,(unsigned)s->shelf_prefs.columns,(unsigned)s->shelf_prefs.style);}}
            continue;
        }
        if(s->font_manage.impl){
            // 字体管理：Esc返回/取消，鼠标松开命中。/ Font management: Esc back/cancel, mouse release hits.
            int command=-1;
            if(event.type==SDL_QUIT){pn_font_manage_close(&s->font_manage);running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_ESCAPE)command=s->font_manage.screen==PN_FMU_CONFIRMING?PN_FMU_CANCEL:PN_FMU_BACK;
            else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)command=pn_font_manage_hit(&s->font_manage,event.button.x,event.button.y);
            if(command>=0){status=pn_font_manage_event(&s->font_manage,command,present,s);printf("font_manage command=%d status=%d screen=%d active=%d\n",command,(int)status,(int)s->font_manage.screen,(int)s->font_manage.active);
                if(!s->font_manage.active){pn_font_manage_close(&s->font_manage);if(s->back_to_settings){s->back_to_settings=false;status=open_settings(s);}else (void)draw(s);}}
            continue;
        }
        if(s->wallpaper.impl){
            // 壁纸页：Esc返回/取消，Enter应用，鼠标松开命中。/ Wallpaper page: Esc back/cancel, Enter apply, mouse release hits.
            int command=-1;
            if(event.type==SDL_QUIT){pn_wallpaper_ui_close(&s->wallpaper);running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat){SDL_Keycode key=event.key.keysym.sym;if(key==SDLK_ESCAPE)command=s->wallpaper.screen==PN_WUI_LIST?PN_WUI_BACK:PN_WUI_CANCEL;else if(key==SDLK_RETURN)command=PN_WUI_APPLY;}
            else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)command=pn_wallpaper_ui_hit(&s->wallpaper,event.button.x,event.button.y);
            if(command>=0){status=pn_wallpaper_ui_event(&s->wallpaper,command,present,s);printf("wallpaper_ui command=%d status=%d screen=%d active=%d\n",command,(int)status,(int)s->wallpaper.screen,(int)s->wallpaper.active);
                if(!s->wallpaper.active){pn_wallpaper_ui_close(&s->wallpaper);if(s->back_to_settings){s->back_to_settings=false;status=open_settings(s);}else (void)draw(s);}}
            continue;
        }
        if(s->toolbar){
            // 工具栏：Esc或点上方正文关闭，点按钮执行；鼠标松开命中。/ Toolbar: Esc or tapping the text above closes, buttons act; hits on mouse release.
            int tool=-1;
            if(event.type==SDL_QUIT){s->toolbar=false;if(reading(s)){status=return_to_shelf(s,now);if(status!=PN_OK){pn_sdl_async_title(&s->async,PN_BRAND_NAME " - 保存失败，按退出键重试");}}if(!reading(s))running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_ESCAPE)tool=PN_TOOL_CLOSE;
            else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)tool=pn_reader_toolbar_hit(event.button.x,event.button.y,toolbar_unavailable(s));
            if(tool==PN_TOOL_CLOSE)status=toolbar_close(s,false);
            else if(tool==PN_TOOL_REFRESH)status=toolbar_close(s,true);
            else if(tool==PN_TOOL_TOC){s->toolbar=false;status=pn_toc_ui_open(&s->toc,&s->epub,present,s);toc_report(&s->toc,0,status);}
            else if(tool==PN_TOOL_BOOKMARKS){s->toolbar=false;status=s->epub.impl?pn_bookmark_ui_open_epub(&s->bookmarks,&s->epub,present,s):pn_bookmark_ui_open(&s->bookmarks,&s->reader,present,s);printf("bookmark_ui open status=%d mode=%d count=%zu\n",(int)status,(int)s->bookmarks.mode,s->bookmarks.count);pn_sdl_async_text(&s->async,false);}
            else if(tool==PN_TOOL_JUMP){s->toolbar=false;status=s->epub.impl?pn_jump_ui_open_epub(&s->jumpui,&s->epub,present,s):pn_jump_ui_open(&s->jumpui,&s->reader,present,s);}
            else if(tool==PN_TOOL_TYPESET){s->toolbar=false;status=s->epub.impl?pn_style_ui_open_epub(&s->styles,&s->epub,present,s):pn_style_ui_open(&s->styles,&s->reader,present,s);printf("style_ui open status=%d pixels=%u\n",(int)status,s->styles.draft.pixels);}
            else if(tool==PN_TOOL_SHELF){s->toolbar=false;status=return_to_shelf(s,now);}
            cancel_pointer(&input,&tap,(atomic_load(&s->async.mouse)&SDL_BUTTON_LMASK)==0);
            continue;
        }
        if(event.type==SDL_QUIT || (event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_ESCAPE)){
            if(reading(s)){status=return_to_shelf(s,now);if(status!=PN_OK){pn_sdl_async_title(&s->async,PN_BRAND_NAME " - 保存失败，按退出键重试");if(!reading(s)){result=1;running=false;}}}
            else running=false;
            if(event.type==SDL_QUIT && !reading(s))running=false;
            cancel_pointer(&input,&tap,(atomic_load(&s->async.mouse)&SDL_BUTTON_LMASK)==0);continue;
        }
        int hit=-1;bool commit=false;pn_reader_action_t action=PN_APP_OPEN;bool turn=false;
        if(event.type==SDL_KEYDOWN && !event.key.repeat){
            SDL_Keycode key=event.key.keysym.sym;cancel_pointer(&input,&tap,(atomic_load(&s->async.mouse)&SDL_BUTTON_LMASK)==0);
            if(key==SDLK_l){status=lock_preview(s,now);continue;}
            if(key==SDLK_h && reading(s)){s->toolbar=false;s->home=true;status=return_to_shelf(s,now);continue;}
            if(reading(s)){
                if(s->styles.active)continue;
                if(key==SDLK_g){status=s->epub.impl?pn_jump_ui_open_epub(&s->jumpui,&s->epub,present,s):pn_jump_ui_open(&s->jumpui,&s->reader,present,s);printf("jump_ui open status=%d\n",(int)status);continue;}
                if(s->bookmarks.mode!=PN_BUI_CLOSED)continue;
                if(key==SDLK_t && s->epub.impl){status=pn_toc_ui_open(&s->toc,&s->epub,present,s);toc_report(&s->toc,0,status);continue;}
                if(key==SDLK_m){status=s->epub.impl?pn_bookmark_ui_open_epub(&s->bookmarks,&s->epub,present,s):pn_bookmark_ui_open(&s->bookmarks,&s->reader,present,s);printf("bookmark_ui open status=%d mode=%d count=%zu\n",(int)status,(int)s->bookmarks.mode,s->bookmarks.count);pn_sdl_async_text(&s->async,false);continue;}
                if(key==SDLK_s){status=s->epub.impl?pn_style_ui_open_epub(&s->styles,&s->epub,present,s):pn_style_ui_open(&s->styles,&s->reader,present,s);printf("style_ui open status=%d pixels=%u\n",(int)status,s->styles.draft.pixels);continue;}
                if(key==SDLK_k){status=toolbar_show(s);continue;}
                if(key==SDLK_u){status=s->epub.impl?pn_epub_app_bookmark_return(&s->epub,now,present,s):pn_reader_app_bookmark_return(&s->reader,now,present,s);printf("bookmark_return status=%d\n",(int)status);continue;}
                if(key==SDLK_RIGHT || key==SDLK_PAGEDOWN || key==SDLK_SPACE){action=PN_APP_NEXT;turn=true;}
                else if(key==SDLK_LEFT || key==SDLK_PAGEUP){action=PN_APP_PREVIOUS;turn=true;}
                else if(key==SDLK_PLUS || key==SDLK_EQUALS){action=PN_APP_LARGER;turn=true;}
                else if(key==SDLK_MINUS){action=PN_APP_SMALLER;turn=true;}
            }else{
                if(key==SDLK_h || key==SDLK_b){s->home=key==SDLK_h;status=page(s,false,true);printf("navigation root=%s status=%d\n",s->home?"home":"library",(int)status);continue;}
                if(key==SDLK_j && !s->recent_mode){status=pn_shelf_index_render(&s->font,&s->frame);if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GL16);s->index=status==PN_OK;printf("index open status=%d\n",(int)status);continue;}
                if(key==SDLK_p){status=open_settings(s);printf("settings open status=%d flags=%u\n",(int)status,(unsigned)s->settings.flags);continue;}
                if(key==SDLK_f && s->font_dir){status=pn_font_manage_open(&s->font_manage,s->pool,&s->media,s->font_dir,&s->state_media,s->state_dir,present,s);printf("font_manage open status=%d\n",(int)status);continue;}
                if(key==SDLK_w && s->wallpaper_dir){status=wallpaper_open(s);printf("wallpaper_ui open status=%d\n",(int)status);continue;}
                if(key==SDLK_SLASH){s->home=false;pn_search_ui_open(&s->search,NULL);s->search_open=true;status=pn_search_ui_render(&s->search,&s->font,&s->frame);if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GL16);continue;}
                if(key==SDLK_r){s->home=false;s->recent_mode=!s->recent_mode;*s->query=0;status=page(s,false,true);continue;}
                if(key==SDLK_c){status=continue_book(s,now);continue;}
                if(key==SDLK_RETURN){hit=s->home?(s->page->count?(s->shelf_focus_visible?s->selected:PN_SHELF_CONTINUE):PN_SHELF_HOME):s->selected;commit=true;}
                else if(key==SDLK_PAGEDOWN){hit=PN_SHELF_NEXT;commit=true;}
                else if(key==SDLK_PAGEUP){hit=PN_SHELF_PREVIOUS;commit=true;}
                else if(key==SDLK_DOWN || key==SDLK_UP){int next=s->selected+(s->shelf_focus_visible?(key==SDLK_DOWN?1:-1):0);if(next>=0 && (size_t)next<s->page->count){s->selected=next;s->shelf_focus_visible=true;status=draw(s);}}
            }
        }else if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION){
            if(!reading(s) && event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_RIGHT){int h=shelf_hit(s,event.button.x,event.button.y);if(h>=0 && h<PN_CATALOG_PAGE_MAX)open_actions(s,h);continue;} // 右键=长按 / Right click = long press
            bool motion=event.type==SDL_MOUSEMOTION;
            if((motion && !(event.motion.state&SDL_BUTTON_LMASK)) || (!motion && event.button.button!=SDL_BUTTON_LEFT))continue;
            if(event.type==SDL_MOUSEBUTTONDOWN)s->pointer_down=true;
            if(!s->pointer_down)continue;
            int x=motion?event.motion.x:event.button.x,y=motion?event.motion.y:event.button.y;

            int target=s->styles.active?pn_style_ui_hit(&s->styles,x,y):s->bookmarks.mode!=PN_BUI_CLOSED?pn_bookmark_ui_hit(&s->bookmarks,x,y):reading(s)?(y>=1144 && x<420 && (s->epub.impl?pn_epub_app_bookmark_can_return(&s->epub):pn_reader_app_bookmark_can_return(&s->reader))?11:y>=1144 && x>=420?14:-1):shelf_hit(s,x,y);
            (void)pn_tap_feed(&tap,1,target,true,&hit);
            if(reading(s) && s->bookmarks.mode==PN_BUI_CLOSED && !s->styles.active)(void)pn_reader_input_feed(&input,1,x,y,true,&action);
            if(event.type==SDL_MOUSEBUTTONUP){
                s->pointer_down=false;
                (void)pn_tap_feed(&tap,0,-1,true,&hit);commit=pn_tap_feed(&tap,0,-1,true,&hit);
                if(reading(s) && s->bookmarks.mode==PN_BUI_CLOSED && !s->styles.active){(void)pn_reader_input_feed(&input,0,0,0,true,&action);turn=pn_reader_input_feed(&input,0,0,0,true,&action);}
            }
        }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
            s->pointer_down=false;pn_reader_input_cancel(&input);(void)pn_tap_feed(&tap,1,-1,false,&hit);
        }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED){
            status=present_raw(s,&s->frame);
        }
        if(commit){
            if(s->styles.active){status=pn_style_ui_event(&s->styles,hit,now,present,s);style_report(&s->styles,hit,status);turn=false;}
            else if(reading(s) && hit==12 && s->bookmarks.mode==PN_BUI_CLOSED){status=s->epub.impl?pn_style_ui_open_epub(&s->styles,&s->epub,present,s):pn_style_ui_open(&s->styles,&s->reader,present,s);turn=false;}
            else if(s->bookmarks.mode!=PN_BUI_CLOSED){status=pn_bookmark_ui_event(&s->bookmarks,hit,NULL,now,present,s);bookmark_report(&s->bookmarks,hit,status);turn=false;}
            else if(reading(s) && hit==10){status=s->epub.impl?pn_bookmark_ui_open_epub(&s->bookmarks,&s->epub,present,s):pn_bookmark_ui_open(&s->bookmarks,&s->reader,present,s);turn=false;pn_reader_input_cancel(&input);}
            else if(reading(s) && hit==11){status=s->epub.impl?pn_epub_app_bookmark_return(&s->epub,now,present,s):pn_reader_app_bookmark_return(&s->reader,now,present,s);printf("bookmark_return status=%d\n",(int)status);turn=false;}
            else if(reading(s) && hit==14){status=s->epub.impl?pn_jump_ui_open_epub(&s->jumpui,&s->epub,present,s):pn_jump_ui_open(&s->jumpui,&s->reader,present,s);printf("jump_ui open status=%d\n",(int)status);turn=false;pn_reader_input_cancel(&input);}
            else if(reading(s) && hit==13 && s->epub.impl){status=pn_toc_ui_open(&s->toc,&s->epub,present,s);toc_report(&s->toc,0,status);turn=false;}
            else if(reading(s) && hit==8){status=return_to_shelf(s,now);turn=false;}
            else if(!reading(s)){
                if(hit==PN_SHELF_READING_HOME){s->home=true;status=page(s,false,true);}
                else if(hit==PN_SHELF_LAYOUT){s->list_mode=!s->list_mode;status=page(s,false,true);}
                else if(hit==PN_SHELF_TOGGLE){s->recent_mode=!s->recent_mode;*s->query=0;status=page(s,false,true);}
                else if(hit==PN_SHELF_SEARCH && *s->query && !s->recent_mode){*s->query=0;status=page(s,false,true);}
                else if(hit==PN_SHELF_SEARCH){pn_search_ui_open(&s->search,NULL);s->search_open=true;status=pn_search_ui_render(&s->search,&s->font,&s->frame);if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GL16);}
                else if(hit==PN_SHELF_TAB_ALL || hit==PN_SHELF_TAB_RECENT || hit==PN_SHELF_HOME || hit==PN_SHELF_TAB_FAVORITES){bool want=hit==PN_SHELF_TAB_RECENT,fav=hit==PN_SHELF_TAB_FAVORITES;if(hit==PN_SHELF_HOME || want!=s->recent_mode || fav!=s->fav_mode){s->home=false;s->recent_mode=want;s->fav_mode=fav;s->fav_start=0;*s->query=0;status=page(s,false,true);}}
                else if(hit==PN_SHELF_INDEX && !s->recent_mode && !*s->query){status=pn_shelf_index_render(&s->font,&s->frame);if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GL16);s->index=status==PN_OK;printf("index open status=%d\n",(int)status);}
                else if(hit>=PN_SHELF_MORE && hit<PN_SHELF_MORE+PN_CATALOG_PAGE_MAX)open_actions(s,hit-PN_SHELF_MORE);
                else if(hit==PN_SHELF_TRANSFER || hit==PN_SHELF_IMPORT)status=open_hub(s);
                else if(hit==PN_SHELF_MENU){status=open_settings(s);printf("settings open status=%d flags=%u\n",(int)status,(unsigned)s->settings.flags);}
                else if(hit==PN_SHELF_CONTINUE)status=continue_book(s,now);
                else if(hit==PN_SHELF_NEXT || hit==PN_SHELF_PREVIOUS)status=page(s,hit==PN_SHELF_PREVIOUS,false);
                else status=open_book(s,hit,now);
            }
            cancel_pointer(&input,&tap,event.type==SDL_MOUSEBUTTONUP || (atomic_load(&s->async.mouse)&SDL_BUTTON_LMASK)==0);
        }
        if(turn && reading(s) && action==PN_APP_TOOLS){turn=false;status=toolbar_show(s);}
        if(turn && reading(s)){pn_sdl_async_title(&s->async,PN_BRAND_NAME " - 正在翻页");uint64_t turn_start=SDL_GetTicks64();status=active_step(s,action,now,present,s);if(timing_on())fprintf(stderr,"timing page-turn total=%llums\n",(unsigned long long)(SDL_GetTicks64()-turn_start));printf("library_turn pointer=%d status=%d\n",event.type==SDL_MOUSEBUTTONUP,(int)status);if(status==PN_OK && s->reader.impl){pn_txt_progress_t position;if(pn_reader_app_progress(&s->reader,&position)==PN_OK)printf("library_confirmed offset=%llu\n",(unsigned long long)position.source_offset);}}
        if(turn && reading(s) && status==PN_OK)pn_sdl_async_title(&s->async,PN_BRAND_NAME);
        if(status!=PN_OK && status!=PN_EMPTY && status!=PN_CANCELLED){
            bool confirmed=turn && reading(s) && (s->epub.impl?pn_epub_app_last_confirmed(&s->epub):pn_reader_app_last_confirmed(&s->reader));
            pn_sdl_async_title(&s->async,confirmed?PN_BRAND_NAME " - 当前页已显示，保存失败，请重试":PN_BRAND_NAME " - 操作未完成，请重试");
        }
    }
cleanup:;
    pn_cancel_scope_t shield;pn_cancel_enter(&shield,NULL,NULL);
    if(reading(s))(void)active_close(s,SDL_GetTicks64());
    pn_catalog_index_close(&s->catalog_index);
    pn_wallpaper_mask_close(&s->lock_mask);
    pn_wallpaper_ui_close(&s->wallpaper);pn_font_manage_close(&s->font_manage);pn_settings_ui_close(&s->settings);
    pn_font_ui_close(&s->fonts);
    pn_cache_detach(&s->cover_cache);pn_font_close(&s->font);pn_free(s->frame.pixels);pn_free(s->page_px);pn_free(s->favorites);pn_free(s->page);pn_free(s->recent);pn_free(s->covers);
    free(s->argb);s->argb=NULL;pn_cancel_leave(&shield);return result;
}

int pn_sim_library_window(pn_pool_t *pool,const char *directory,const char *font_path,const char *fallback_path,const char *state_dir,const char *cover_cache,const char *wallpaper_dir,const char *wallpaper_store,const char *font_dir){
    library_t *s=pn_alloc_tagged(pool,PN_MEM_UI,sizeof *s);if(!s)return 1;memset(s,0,sizeof *s);
    s->pool=pool;s->directory=directory;s->font_path=font_path;s->fallback_path=fallback_path;s->state_dir=state_dir;s->cover_argument=cover_cache;s->wallpaper_dir=wallpaper_dir;s->wallpaper_store_argument=wallpaper_store;s->font_dir=font_dir;
    if(!pn_sdl_async_init(&s->async,present_raw_main,s)){pn_sdl_async_close(&s->async);pn_free(s);return 1;}
    s->window=s->async.window;s->renderer=s->async.renderer;s->texture=s->async.texture;
    int result=pn_sdl_async_run(&s->async,library_worker,s);
    printf("async mailbox coalesced=%llu dropped=%llu\n",(unsigned long long)s->async.input.coalesced,(unsigned long long)s->async.input.dropped);
    pn_sdl_async_close(&s->async);pn_free(s);return result;
}
