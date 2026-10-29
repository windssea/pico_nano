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
#include "pn_builtin_art.h"
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include "pn_hot_session.h"
#include "pn_opening_view.h"
#include "pn_reader_input.h"
#include "pn_shelf_view.h"
#include "pn_sudoku.h"
#include "pn_sokoban.h"
#include "pn_minesweeper.h"
#include "pn_reader_chrome.h"
#include "pn_reader_toolbar.h"
#include "pn_wallpaper_ui.h"
#include "pn_bookmark_lock.h"
#include "pn_font_manage.h"
#include "pn_settings_ui.h"
#include "pn_turn_transition.h"
#include "pn_ripple.h"
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
    pn_minesweeper_t minesweeper; ///< 按需扫雷会话 / On-demand Minesweeper session
    pn_sokoban_t sokoban; ///< 同源推箱子 / Shared Sokoban
    pn_sudoku_t sudoku; ///< 同源数独状态 / Shared Sudoku state
    uint8_t entertainment; ///< 0书籍、1娱乐；棋局由独立控制器拥有 / 0 books, 1 entertainment; a separate controller owns the game
    bool game_exit_clean_pending; ///< 一次游戏退出清屏意图，成功目标回执后消费；PC不仿真面板相位 / One game-exit clear intent, consumed by a successful destination receipt; PC does not simulate panel phases
    bool list_mode; ///< 列表模式（每页5本）/ List mode (five per page)
    bool home; ///< 阅读首页与书库共用快照，不改阅读来源 / Home and library share snapshots, preserving reading origin
    bool shelf_focus_visible; ///< 仅主动键盘导航显示轻量焦点 / Show subtle focus only during deliberate keyboard navigation
    bool shelf_draw_valid;uint32_t shelf_draw_revision;pn_w_status_snapshot_t shelf_draw_status; ///< 已确认底图/代次/状态带 / Confirmed base, revision and status band
    pn_catalog_page_t *page;
    pn_font_t font;
    pn_w_font_scope_t ui_scope; ///< owner寿命的常驻补字缓存 / Resident fallback cache with owner lifetime
    pn_frame_t frame;
    pn_reader_app_t reader;
    pn_epub_app_t epub;
    bool index_wait,index_gate,index_finishing;uint64_t index_progress_at;pn_book_index_state_t opening_index; ///< 开书等待/后台共用编译游标 / Shared compile progress for waiting/background opening
    bool reader_turn_pending; ///< NEXT准备跨事件轮继续 / Continue NEXT preparation across event ticks
    pn_hot_session_t hot; ///< 单个已保存会话，写入前释放 / One saved session, released before writes
    pn_opening_view_t opening_view; ///< 开书重试共用固定展示，不参与原书校验 / Fixed presentation across opening retries, never source validation
    pn_epub_open_job_t foreground_open;pn_catalog_item_t *opening_book; ///< 私有开书任务及唯一请求快照，不提前覆盖last_read / Private opening job and sole request snapshot, never overwriting last_read early
    bool opening_feedback_shown; ///< 本请求最多一次普通加载页，快速恢复可省略 / At most one regular loader for this request; fast recovery may omit it
    uint64_t open_measure_us,turn_measure_us; ///< 完整操作含让出等待，仅主机观察 / Whole operation including yield gaps, host observation only
    pn_toc_ui_t toc;
    bool toc_pointer,pointer_down;
    pn_feedback_t feedback; ///< 独立原图/按下局部像素 / Independent original/pressed local pixels
    pn_tap_t feedback_tap; ///< 管理按压取消与释放，不代替阅读手势 / Management press cancellation/release, independent of reader gestures
    bool feedback_enabled,feedback_known,feedback_release; ///< 可见开发开关、底图已知、抬手恢复待处理 / Visible dev switch, known base, pending release restoration
    pn_feedback_idle_t feedback_idle; ///< 产品owner空闲定稿状态 / Product-owner idle-settle state
    bool follow_enabled,follow_trace,follow_fail; ///< 连续列表软件演练及一次故障注入 / Continuous-list software exercise and one-shot failure injection
    pn_feedback_box_t content_area; ///< 最近普通显示的实际变化范围 / Actual change bounds of the last normal presentation
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
    pn_cover_work_t cover_work; ///< 自有分步提取/缓存上下文，不借用封面槽 / Owned staged extraction/cache contexts without borrowed cover slots
    char cover_dir[512]; ///< 显式--cover-cache目录，空则只解码不缓存 / Explicit --cover-cache directory; empty decodes without caching
    uint8_t salt[16]; ///< EPUB XML哈希盐 / EPUB XML hash salt
    uint64_t covers_shown_at; ///< 最近一次封面补绘 / Last incremental cover presentation
    bool covers_dirty; ///< 本页有新封面待重绘 / New covers await a redraw for this page
    bool covers_cache_done; ///< 当前绑定的逐槽磁盘探测完成 / Per-slot disk probes finished for this binding
    pn_wallpaper_ui_t wallpaper; ///< 壁纸设置页 / Wallpaper settings page
    pn_bookmark_lock_t bookmark_lock; ///< 最后确认的本地阅读快照 / Last confirmed local reading snapshot
    pn_reading_stats_t reading_stats; ///< 与设备同源的阅读统计 / Reading statistics shared with the device
    uint64_t home_stats_retry; ///< 首页空闲读档重试 / Home idle load retry
    uint64_t stats_retry; ///< 读档失败限频 / Throttled load retries
    bool stats_display_failed; ///< 失败推屏后成功恢复才计时 / Resume timing only after successful display recovery
    pn_catalog_item_t bookmark_lock_item; ///< 快照所对应的书，不能跟随未确认选择 / Snapshot source, never an unconfirmed selection
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
static bool stats_clock(uint64_t *utc);
static pn_shelf_options_t shelf_options(const library_t *s){
    pn_w_set_battery(sim_battery());bool query=!s->recent_mode && !s->fav_mode && *s->query;
    pn_shelf_options_t o={.home=s->home,.entertainment_tab=true,.entertainment=s->home?0:s->entertainment,.battery_percent=-1,.columns=s->shelf_prefs.columns,.style=s->shelf_prefs.style,.actions=s->actions_mode?&s->actions:NULL,.search=false,.layout_toggle=true,.favorites_tab=true,.favorites=s->fav_mode,.list_mode=s->list_mode || query,.query=query};
    if(o.home){uint64_t utc=0;bool dated=stats_clock(&utc);o.reading=pn_reading_stats_summary(&s->reading_stats,dated,utc);
        o.reading.known=o.reading.known && s->covers && s->covers->has_last && s->covers->last.identified && !memcmp(s->covers->last.expected.sha256,s->reading_stats.book.id.sha256,32);
        unsigned year,month,day,weekday;if(dated && pn_bookmark_lock_date((uint32_t)(utc/1000u),&year,&month,&day,&weekday)){o.reading.dated=true;o.reading.weekday=weekday;snprintf(o.home_date,sizeof o.home_date,"%u月%u日 · 周%s",month,day,((const char *const[]){"一","二","三","四","五","六","日"})[weekday]);}
    }
    return o;
}
static size_t shelf_per_page(const library_t *s){pn_shelf_options_t o=shelf_options(s);return s->home?3u:pn_shelf_capacity(&o);}
static bool reading(library_t *s){return s->reader.impl || s->epub.impl;}
static bool stats_clock(uint64_t *utc){struct timespec t;if(clock_gettime(CLOCK_REALTIME,&t) || t.tv_sec<0)return false;*utc=(uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;return *utc<=UINT64_C(4294967295999);}
static void stats_pause(library_t *s){uint64_t utc=0;bool dated=stats_clock(&utc);pn_reading_stats_gate(&s->reading_stats,SDL_GetTicks64(),false,dated,utc);}
static void stats_poll(library_t *s,bool body){
    body=body && !s->stats_display_failed;
    if(!s->reading_stats.media && s->state_media.available)(void)pn_reading_stats_init(&s->reading_stats,&s->state_media,s->state_dir);
    bool matched=false;uint64_t now=SDL_GetTicks64(),utc=0;
    if(body && s->reading_stats.media){pn_book_id_t id;unsigned basis=0;size_t section=0,count=0;
        if((s->epub.impl?pn_epub_app_identity(&s->epub,&id):pn_reader_app_identity(&s->reader,&id))==PN_OK){
            matched=s->reading_stats.loaded && s->reading_stats.has_book && !memcmp(id.sha256,s->reading_stats.book.id.sha256,32);
            pn_status_t visible=matched?PN_OK:s->epub.impl?pn_epub_app_percent(&s->epub,&basis,&section,&count):pn_reader_app_percent(&s->reader,&basis);
            if(!matched && visible==PN_OK && now>=s->stats_retry){stats_pause(s);pn_status_t status=pn_reading_stats_select(&s->reading_stats,&id);matched=status==PN_OK;if(!matched){s->stats_retry=SDL_GetTicks64()+3000u;fprintf(stderr,"reading_stats load status=%d\n",(int)status);}}
        }}
    bool dated=stats_clock(&utc);pn_reading_stats_gate(&s->reading_stats,SDL_GetTicks64(),body && matched,dated,utc);
}
static void stats_flush(library_t *s){stats_pause(s);if(s->reading_stats.media){pn_status_t status=pn_reading_stats_save(&s->reading_stats,SDL_GetTicks64(),true);if(status!=PN_OK)fprintf(stderr,"reading_stats save status=%d\n",(int)status);}}
static void stats_snapshot(library_t *s,pn_bookmark_lock_t *snapshot){
    if(!s->reading_stats.media && s->state_media.available)(void)pn_reading_stats_init(&s->reading_stats,&s->state_media,s->state_dir);
    if(s->reading_stats.media && pn_reading_stats_load(&s->reading_stats)==PN_OK){uint64_t utc=0;bool dated=stats_clock(&utc);snapshot->reading=pn_reading_stats_summary(&s->reading_stats,dated,utc);
        snapshot->reading.known=s->bookmark_lock_item.identified && s->reading_stats.has_book && !memcmp(s->bookmark_lock_item.expected.sha256,s->reading_stats.book.id.sha256,32);}
}
static pn_status_t active_step(library_t *s,pn_reader_action_t action,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(action==PN_APP_NEXT && !s->reader_turn_pending)s->turn_measure_us=pn_profile_time();
    stats_pause(s);
    pn_reader_app_refresh_period(&s->reader,pn_settings_refresh_pages(s->input_flags));
    pn_epub_app_refresh_period(&s->epub,pn_settings_refresh_pages(s->input_flags));
    pn_frame_t before;pn_turn_t effect={.pool=s->pool,.style=(pn_turn_style_t)((s->input_flags&PN_INPUT_TURN_MASK)>>6),.reverse=action==PN_APP_PREVIOUS,.present=present,.ctx=ctx};
    if((action==PN_APP_NEXT || action==PN_APP_PREVIOUS) && s->page_px && pn_frame_bind(&before,s->page_px,342u*1216u,684,1216))effect.before=&before;else effect.style=PN_TURN_NONE;
    // 与设备保持相同步数/软时间预算，不把主机耗时当硬件指标。/ Match the device step/soft-time budget without treating host timings as hardware measurements.
    pn_status_t status;uint64_t batch_started=SDL_GetTicks64();unsigned batch_steps=0;
    do{status=s->epub.impl?pn_epub_app_step_yield(&s->epub,action,now,pn_turn_present,&effect):pn_reader_app_step(&s->reader,action,now,pn_turn_present,&effect);}
    while(s->epub.impl && action==PN_APP_NEXT && status==PN_BUSY && ++batch_steps<32u && SDL_GetTicks64()-batch_started<8u);
    if(action==PN_APP_NEXT && status!=PN_BUSY && s->turn_measure_us){uint64_t end=pn_profile_time();pn_profile_observe(s->pool,"sim.next.request",status,end-s->turn_measure_us,NULL);s->turn_measure_us=0;}
    s->reader_turn_pending=s->epub.impl && action==PN_APP_NEXT && status==PN_BUSY;
    s->async.defer_ack=s->reader_turn_pending;
    if((action==PN_APP_NEXT || action==PN_APP_PREVIOUS) && (s->epub.impl?pn_epub_app_last_confirmed(&s->epub):pn_reader_app_last_confirmed(&s->reader))){stats_poll(s,true);stats_pause(s);uint64_t utc=0;bool dated=stats_clock(&utc);pn_book_id_t id;
        if((s->epub.impl?pn_epub_app_identity(&s->epub,&id):pn_reader_app_identity(&s->reader,&id))==PN_OK && s->reading_stats.has_book && !memcmp(id.sha256,s->reading_stats.book.id.sha256,32))pn_reading_stats_turn(&s->reading_stats,dated,utc);}
    return status;
}
static pn_status_t opening_abort(library_t *s){
    pn_opening_view_close(&s->opening_view);
    if(s->index_gate){s->index_gate=false;if(s->epub.impl)(void)pn_epub_app_discard_unshown(&s->epub);if(s->reader.impl)(void)pn_reader_app_discard_unshown(&s->reader);}
    pn_status_t status=pn_epub_app_open_job_close(&s->foreground_open);if(s->opening_book){pn_free(s->opening_book);s->opening_book=NULL;s->async.defer_ack=false;pn_async_block_controls(&s->async.input,false);pn_sdl_async_title(&s->async,PN_BRAND_NAME);}return status;}
/// 在关闭或挂起前捕获已确认进度，不解析书籍。/ Capture confirmed progress before closing or suspending, without parsing books.
static void bookmark_lock_capture(library_t *s){
    pn_bookmark_lock_t next={.battery=-1};unsigned basis=0;size_t section=0,count=0;
    pn_status_t status=s->epub.impl?pn_epub_app_percent(&s->epub,&basis,&section,&count):pn_reader_app_percent(&s->reader,&basis);
    if(status!=PN_OK || !s->last_read_known)return;
    if(s->epub.impl && pn_epub_app_book_display(&s->epub,&next.book)!=PN_OK)return;
    if(!*next.book.title){const char *name=s->last_read.name;if(strlen(name)<sizeof next.book.title){strcpy(next.book.title,name);char *dot=strrchr(next.book.title,'.');if(dot)*dot=0;}else strcpy(next.book.title,"当前书籍");}
    next.basis=basis;next.section=section;next.count=count;next.progress_known=true;s->bookmark_lock=next;s->bookmark_lock_item=s->last_read;
    s->bookmark_lock_item.identified=(s->epub.impl?pn_epub_app_identity(&s->epub,&s->bookmark_lock_item.expected):pn_reader_app_identity(&s->reader,&s->bookmark_lock_item.expected))==PN_OK;
}
static pn_status_t active_close(library_t *s,uint64_t now){stats_flush(s);bookmark_lock_capture(s);s->recent_loaded=false;pn_status_t status=opening_abort(s);if(status==PN_OK)status=pn_hot_session_drop_for_switch(&s->hot);if(status!=PN_OK)return status;return s->epub.impl?pn_epub_app_close(&s->epub,now):pn_reader_app_close(&s->reader,now);}
/// 与设备同源的缓存写租约，不因索引构建关闭只读字体/热会话。/ Match device cache-write leases without closing read-only fonts/warm sessions for indexing.
static pn_status_t catalog_build_idle(library_t *s){return pn_catalog_index_build(&s->catalog_index);}
static bool timing_on(void){static int on=-1;if(on<0){const char *v=getenv("PN_SIM_TIMING");on=v && *v=='1';}return on;}
static uint64_t profile_clock(void *ctx){(void)ctx;uint64_t c=SDL_GetPerformanceCounter(),f=SDL_GetPerformanceFrequency();return (c/f)*1000000u+(c%f)*1000000u/f;}
/// 索引诊断只聚合，避免每个32事件小步打印多行；嵌套阶段不可相加。/ Aggregate index diagnostics instead of printing each 32-event step; nested stages are not additive.
static struct {const char *phase;uint64_t calls,total,maximum;} index_profile[12];
static void profile_index_flush(void){
    for(unsigned i=0;i<12;i++)if(index_profile[i].calls){
        fprintf(stderr,"index_phase phase=%s calls=%llu total_us=%llu max_us=%llu backend=host\n",index_profile[i].phase,(unsigned long long)index_profile[i].calls,(unsigned long long)index_profile[i].total,(unsigned long long)index_profile[i].maximum);
        index_profile[i].calls=index_profile[i].total=index_profile[i].maximum=0;
    }
}
static void profile_emit(void *ctx,const pn_profile_event_t *e){
    if(!strncmp(e->phase,"index.",6)){
        for(unsigned i=0;i<12;i++)if(!index_profile[i].phase || !strcmp(index_profile[i].phase,e->phase)){
            index_profile[i].phase=e->phase;index_profile[i].calls++;index_profile[i].total+=e->elapsed_us;
            if(e->elapsed_us>index_profile[i].maximum)index_profile[i].maximum=e->elapsed_us;
            break;
        }
        return;
    }
    (void)ctx;fprintf(stderr,"profile phase=%s elapsed_us=%llu status=%d begin=%zu end=%zu peak=%zu attempts=%zu backend=host\n",e->phase,(unsigned long long)e->elapsed_us,(int)e->status,e->begin_bytes,e->end_bytes,e->peak_bytes,e->attempts);
    if(e->pool){fprintf(stderr,"memory phase=%s",e->phase);for(unsigned j=0;j<PN_MEM_COUNT;j++){const pn_memory_stats_t *m=&e->pool->memory[j];fprintf(stderr," %s=%zu/%zu/%zu/%zu",pn_memory_tag_name((pn_memory_tag_t)j),m->used,m->peak,m->allocations,m->failures);}fprintf(stderr," heap=unavailable stack=unavailable\n");}
    if(e->pool)for(unsigned k=0;k<PN_CACHE_COUNT;k++){const pn_cache_stats_t *c=&e->pool->cache_stats[k];fprintf(stderr,"cache phase=%s kind=%u bytes=%zu peak=%zu hits=%zu misses=%zu denied=%zu evictions=%zu reclaimed=%zu backend=host\n",e->phase,k,c->bytes,c->peak,c->hits,c->misses,c->denied,c->evictions,c->reclaimed);}
}
/* 呈现并记住最近一帧（工具栏打开时不记，以便关闭后恢复原页面）。/ Present and remember the latest frame (not while the toolbar is open, so closing restores the original page). */
static pn_status_t present_raw_main(void *ctx,const pn_frame_t *frame);
static int root_navigation_hit(library_t *s,int x,int y);
static pn_status_t present_raw(library_t *s,const pn_frame_t *frame){
    // 非一级页呈现立即切断底栏连续上下文，不能等下一轮事件循环。
    // Non-root presentations immediately break continuous navigation context, without waiting for the next event loop.
    if(root_navigation_hit(s,80,1150)<0)pn_async_set_root(&s->async.input,false);
    pn_status_t status=pn_sdl_async_present(&s->async,frame,PN_REFRESH_GC16);
    if(status!=PN_OK)return status;
    if(pn_cancel_checkpoint()!=PN_OK && !getenv("PN_SIM_CAPTURE_CANCELLED"))return status; // 已成功呈现可跳过开发截图；竞争验证可显式捕获已呈现但随后取消的帧。/ A successful presentation may skip its dev capture; race checks may explicitly capture a displayed frame even when cancellation follows.
    // 开发辅助：PN_SIM_FRAME_DIR指向目录时，每次呈现另存一张PGM，便于审查界面；不影响显示。/ Dev aid: when PN_SIM_FRAME_DIR names a directory, every present also saves a PGM for UI review; display is unaffected.
    const char *dump=getenv("PN_SIM_FRAME_DIR");
    if(dump && *dump && strlen(dump)<400){static unsigned index;char path[512];snprintf(path,sizeof path,"%s/frame-%04u.pgm",dump,++index);
        FILE *file=fopen(path,"wb");
        if(file){
            fprintf(file,"P5\n%d %d\n255\n",frame->width,frame->height);
            uint8_t row[684]; // 行批量写，避免截图自身放大输入延迟。/ Write rows in batches so capture does not inflate input latency.
            for(int y=0;frame->width<=(int)sizeof row && y<frame->height;y++){for(int x=0;x<frame->width;x++)row[x]=(uint8_t)(pn_frame_get(frame,x,y)*17u);if(fwrite(row,1,(size_t)frame->width,file)!=(size_t)frame->width)break;}
            fclose(file);
            if(getenv("PN_SIM_CAPTURE_READING") && reading(s) && !s->locked && !s->toolbar && !s->styles.active && !s->fonts.active && !(s->foreground_open.impl || s->index_gate))printf("reading_frame=%u\n",index);
        }
    }
    return status;
}
/// 模拟器演示水波纹调度：与设备同源的弧形等级逐级推进；柔和波纹只替换变化像素，清理波纹前沿显示深色波带；只示意顺序，不模拟粒子光学。
/// Simulator demo of the ripple schedule: the same arc levels as the device advance one by one; gentle ripples replace changed pixels only and cleaning ripples show a dark band at the front; order only, not particle optics.
static pn_status_t ripple_demo(library_t *s,const pn_frame_t *next,bool reverse,bool clean){
    static pn_ripple_row_t rows[2][PN_RIPPLE_ROWS];static bool ready[2];int d=reverse?1:0;
    // 只在可见窗口演示；无头自动测试不插入示意帧与延时。/ Demonstrate only in a visible window; headless automated tests get no illustrative frames or delays.
    const char *driver=SDL_GetCurrentVideoDriver();if(driver && !strcmp(driver,"dummy"))return PN_OK;
    if(!s->page_px || next->width!=684 || next->height!=1216)return PN_OK;
    if(!ready[d]){if(pn_ripple_rows(reverse,rows[d],PN_RIPPLE_ROWS)!=PN_OK)return PN_OK;ready[d]=true;}
    uint8_t *pixels=pn_alloc_tagged(s->pool,PN_MEM_FRAME,342u*1216u);if(!pixels)return PN_OK;
    pn_frame_t old,step;pn_status_t status=pn_frame_bind(&old,s->page_px,342u*1216u,684,1216) && pn_frame_bind(&step,pixels,342u*1216u,684,1216)?PN_OK:PN_INVALID;
    for(unsigned front=0;front<PN_RIPPLE_BANDS && status==PN_OK;front++){
        for(int x=0;x<684;x++){const pn_ripple_row_t *row=&rows[d][683-x];unsigned segment=0;
            for(int y=0;y<1216;y++){while(row->end[segment]<=y)segment++;unsigned band=row->band[segment];
                uint8_t to=pn_frame_get(next,x,y),from=pn_frame_get(&old,x,y);
                pn_frame_pixel(&step,x,y,band<front?to:band==front?(clean?(uint8_t)(to/3u):(uint8_t)((to+from)/2u)):from);}}
        status=present_raw(s,&step);SDL_Delay(30);
    }
    pn_free(pixels);return status;
}
static pn_status_t present_impl(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    library_t *s=ctx;
    bool game_exit_clean=s->game_exit_clean_pending;
    if(game_exit_clean)profile=PN_REFRESH_EXIT_GC16;
    if(profile>=PN_REFRESH_RIPPLE && profile<=PN_REFRESH_RIPPLE_CLEAN_REVERSE){bool clean=profile>=PN_REFRESH_RIPPLE_CLEAN;pn_status_t shown=ripple_demo(s,frame,profile==PN_REFRESH_RIPPLE_REVERSE || profile==PN_REFRESH_RIPPLE_CLEAN_REVERSE,clean);if(shown!=PN_OK)return shown;profile=clean?PN_REFRESH_GC16:PN_REFRESH_GL16;}
    pn_status_t stopped=pn_font_manage_motion_abort(&s->font_manage);if(stopped!=PN_OK)return stopped;stopped=pn_toc_ui_motion_abort(&s->toc);if(stopped!=PN_OK)return stopped;
    if(!reading(s)){pn_free(s->page_px);s->page_px=NULL;}
    // 备份只供阅读工具栏关闭时恢复；书架/设置不需要，避免挤占封面解码预算。/ Only reading needs the backup for toolbar restoration; shelf/settings copies waste the cover decode budget.
    pn_status_t status=present_raw(s,frame);
    if(status==PN_OK){s->feedback_known=true;(void)pn_feedback_discard(&s->feedback);pn_feedback_idle_cover(&s->feedback_idle,(profile==PN_REFRESH_GC16 || profile==PN_REFRESH_EXIT_GC16)?(pn_feedback_box_t){0,0,684,1216}:s->content_area);
        if(game_exit_clean){s->game_exit_clean_pending=false;printf("game_exit_clean destination=entertainment presentations=1 status=0\n");}}
    else if(status!=PN_CANCELLED && status!=PN_BUSY){s->feedback_known=false;(void)pn_feedback_discard(&s->feedback);}
    if(status==PN_OK && reading(s) && !s->toolbar && frame->width==684 && frame->height==1216 && frame->stride>=342){
        if(!s->page_px)s->page_px=pn_alloc_tagged(s->pool,PN_MEM_FRAME,342u*1216u);
        if(s->page_px)for(int y=0;y<1216;y++)memcpy(s->page_px+(size_t)y*342,frame->pixels+(size_t)y*frame->stride,342);
    }
    return status;
}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    stats_pause(ctx);
    pn_profile_span_t span;pn_profile_begin(&span,((library_t *)ctx)->pool,"display.receipt");
    pn_status_t status=present_impl(ctx,frame,profile);pn_profile_end(&span,status);((library_t *)ctx)->stats_display_failed=status!=PN_OK;return status;
}
static pn_status_t present_raw_main(void *ctx,const pn_frame_t *frame){
    library_t *s=ctx;
    bool known=s->feedback_known;int x0=684,x1=-1,y0=1216,y1=-1;
    uint64_t t0=SDL_GetTicks64();
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++){
        uint32_t value=pn_frame_get(frame,x,y)*17u,rgba=0xff000000u|value*0x010101u;
        if(known && s->argb[(size_t)y*frame->width+x]!=rgba){if(x<x0)x0=x;if(x>x1)x1=x;if(y<y0)y0=y;if(y>y1)y1=y;}
        s->argb[(size_t)y*frame->width+x]=rgba;
    }
    if(SDL_UpdateTexture(s->texture,NULL,s->argb,frame->width*(int)sizeof(uint32_t))!=0 || SDL_RenderClear(s->renderer)!=0 || SDL_RenderCopy(s->renderer,s->texture,NULL,NULL)!=0)return PN_IO;
    uint64_t t1=SDL_GetTicks64();SDL_RenderPresent(s->renderer);
    if(!known)s->content_area=(pn_feedback_box_t){0,0,684,1216};
    else if(x1<0)s->content_area=(pn_feedback_box_t){0};
    else{int left=x0>8?x0-8:0,right=x1+9<684?x1+9:684,top=(y0&~1)>8?(y0&~1)-8:0,bottom=(y1|1)+9<1216?(y1|1)+9:1216;s->content_area=(pn_feedback_box_t){left,top,right-left,bottom-top};}
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
/// 模拟器同样在管理前归还系统源，返回书架重新加载。/ The simulator releases the system source before management and reloads it on return.
static void system_font_ready(library_t *s){
    if(!s->font_dir || pn_font_system_available())return;
    pn_font_close(&s->font);char path[PN_FONT_REFERENCE_PATH_MAX];int n=snprintf(path,sizeof path,"%s/HarmonyOS_Sans_SC_Regular.ttf",s->font_dir);
    // 与设备同源：字体管理所选系统字体优先，失败回到默认。/ Same as the device: the font-management choice wins, falling back to the default on failure.
    char chosen[PN_FONT_REFERENCE_PATH_MAX];
    if(s->state_dir && *s->state_dir && pn_font_preferences_system_path(&s->state_media,s->state_dir,s->pool,s->font_dir,chosen,sizeof chosen)==PN_OK && pn_font_system_open(s->pool,&s->media,chosen)==PN_OK)return;
    if(n>0 && (size_t)n<sizeof path)(void)pn_font_system_open(s->pool,&s->media,path);
}
static pn_status_t list_motion_present(void *,pn_list_stage_t,const pn_frame_t *,pn_follow_box_t,const pn_follow_job_t *);
static void feedback_restore(library_t *);
static pn_status_t open_font_management(library_t *s){pn_status_t status=pn_hot_session_drop_for_switch(&s->hot);if(status!=PN_OK)return status;pn_settings_ui_close(&s->settings);pn_font_close(&s->font);(void)pn_font_system_close();status=pn_font_manage_open(&s->font_manage,s->pool,&s->media,s->font_dir,&s->state_media,s->state_dir,present,s);if(status==PN_OK && s->follow_enabled){pn_status_t enabled=pn_font_manage_motion(&s->font_manage,list_motion_present,s);printf("font_scroll enabled=%d status=%d\n",enabled==PN_OK,(int)enabled);}return status;}
static pn_status_t open_toc(library_t *s){pn_status_t status=s->follow_enabled?pn_toc_ui_open_motion(&s->toc,&s->epub,s->pool,present,s,list_motion_present,s):pn_toc_ui_open(&s->toc,&s->epub,present,s);if(s->follow_enabled)printf("toc_scroll enabled=%d status=%d\n",s->toc.motion_ctx!=NULL,(int)status);return status;}
typedef struct {library_t *s;pn_list_stage_t stage;const pn_frame_t *frame;pn_follow_box_t area;bool fail,started;} motion_window_t; ///< 原生窗口借用，主线程不读模型 / Native window borrow, main never reads models
static pn_status_t motion_window(void *ctx){
    motion_window_t *p=ctx;library_t *s=p->s;p->started=true;if(p->fail)return PN_IO;
    pn_status_t status=PN_OK;
    if(p->stage==PN_LIST_SETTLE || p->stage==PN_LIST_RECOVER)status=present_raw_main(s,p->frame);
    else{
        pn_follow_box_t b=p->area;for(int y=b.y;y<b.y+b.height;y++)for(int x=b.x;x<b.x+b.width;x++){uint32_t v=pn_frame_get(p->frame,x,y)*17u;s->argb[(size_t)y*684u+x]=0xff000000u|v*0x010101u;}
        if(SDL_UpdateTexture(s->texture,NULL,s->argb,684u*sizeof *s->argb)!=0 || SDL_RenderClear(s->renderer)!=0 || SDL_RenderCopy(s->renderer,s->texture,NULL,NULL)!=0)return PN_IO;
        SDL_RenderPresent(s->renderer);
    }
    const char *capture=getenv("PN_SIM_FOLLOW_CAPTURE");const char *directory=getenv("PN_SIM_FRAME_DIR");
    if(status==PN_OK && capture && *capture=='1' && directory && strlen(directory)<400){static unsigned sequence;char path[512];snprintf(path,sizeof path,"%s/follow-%04u-%u.pgm",directory,++sequence,(unsigned)p->stage);FILE *file=fopen(path,"wb");if(file){fprintf(file,"P5\n684 1216\n255\n");uint8_t row[684];for(int y=0;y<1216;y++){for(int x=0;x<684;x++)row[x]=(uint8_t)s->argb[(size_t)y*684+x];if(fwrite(row,1,sizeof row,file)!=sizeof row)break;}fclose(file);}}
    return status;
}
static pn_status_t list_motion_present(void *ctx,pn_list_stage_t stage,const pn_frame_t *frame,pn_follow_box_t area,const pn_follow_job_t *job){
    library_t *s=ctx;
    if(stage==PN_LIST_ABANDON){s->feedback_known=false;return PN_OK;}
    if(stage==PN_LIST_REFERENCE)return PN_OK;
    if(stage==PN_LIST_NORMALIZE){feedback_restore(s);s->feedback_tap=(pn_tap_t){0};s->feedback_release=false;}
    motion_window_t p={.s=s,.stage=stage,.frame=frame,.area=area,.fail=s->follow_fail && stage==PN_LIST_PHASE};
    pn_status_t status=pn_sdl_async_native(&s->async,motion_window,&p);
    if(p.started && p.fail)s->follow_fail=false;
    if(stage==PN_LIST_PHASE){pn_status_t completed=p.started?pn_follow_complete(job->owner,job,status==PN_OK):pn_follow_unsubmit(job->owner,job);if(completed==PN_STALE_JOB)return completed;}
    if(status==PN_OK){if(stage==PN_LIST_SETTLE || stage==PN_LIST_RECOVER)pn_feedback_idle_cover(&s->feedback_idle,(pn_feedback_box_t){0,0,684,1216});s->feedback_known=true;}else if(p.started)s->feedback_known=false;
    if(s->follow_trace){bool toc=s->toc.active && s->toc.motion_ctx;pn_list_motion_stats_t state=toc?pn_toc_ui_motion_stats(&s->toc):pn_font_manage_motion_stats(&s->font_manage);printf("%s_motion stage=%u status=%d offset=%d pending=%zu revision=%u\n",toc?"toc":"font",(unsigned)stage,(int)status,state.offset,state.pending,s->async.revision);}
    return status;
}
static pn_status_t draw_impl(library_t *s){
    s->shelf_draw_valid=false;
    system_font_ready(s);
    if(!s->entertainment)covers_ensure(s);
    pn_wallpaper_mask_close(&s->lock_mask);
    if(!s->frame.pixels){uint8_t *pixels=pn_alloc_tagged(s->pool,PN_MEM_FRAME,342u*1216u);if(!pixels || !pn_frame_bind(&s->frame,pixels,342u*1216u,684,1216)){pn_free(pixels);return PN_NO_MEMORY;}}
    if(!s->font.impl){pn_text_source_t builtin=pn_font_builtin_source();pn_status_t opened=pn_font_open(&s->font,s->pool,&builtin,24);if(opened!=PN_OK)return opened;}
    uint64_t t0=SDL_GetTicks64();
    pn_shelf_options_t options=shelf_options(s);
    if(!options.entertainment && favorites_ensure(s)==PN_OK)options.favorite_items=s->favorites;
    pn_status_t status=pn_shelf_render_with_font_file_ex(s->page,&s->font,&s->frame,s->shelf_focus_visible?s->selected:-1,s->recent_mode,false,s->covers,s->pool,&s->media,*s->font_real?s->font_real:NULL,&options);
    if(status==PN_OK && s->open_failed){pn_frame_rect(&s->frame,32,970,620,64,15);status=pn_font_size(&s->font,28);if(status==PN_OK)status=pn_w_text(&s->font,&s->frame,"打开失败，请重试",48,1012,580,PN_ALIGN_LEFT);}
    if(timing_on())fprintf(stderr,"timing shelf render=%llums\n",(unsigned long long)(SDL_GetTicks64()-t0));
    if(status==PN_OK)printf("navigation root=%s count=%zu\n",s->home?"home":"library",s->page->count);
    if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GC16);
    if(status==PN_OK){s->shelf_draw_valid=true;s->shelf_draw_revision=s->async.revision;s->shelf_draw_status=pn_w_status_snapshot();}
    return status;
}
static pn_status_t draw(library_t *s){
    pn_profile_span_t span;pn_profile_begin(&span,s->pool,"ui.shelf");
    pn_status_t status=draw_impl(s);pn_profile_end(&span,status);return status;
}
/// 游戏借用原窗口画布和字体；呈现成功后开始计时。/ The game borrows the existing canvas/font; timing starts after presentation.
static pn_status_t sudoku_draw(library_t *s){
    pn_profile_span_t timing;pn_profile_begin(&timing,s->pool,"ui.sudoku.render");
    pn_status_t r=pn_sudoku_render_update(&s->sudoku,&s->font,&s->frame);
    if(r==PN_UNSUPPORTED)r=pn_sudoku_render(&s->sudoku,&s->font,&s->frame);
    pn_profile_end(&timing,r);
    if(r==PN_OK)r=present(s,&s->frame,PN_REFRESH_GL16);
    pn_sudoku_displayed(&s->sudoku,r==PN_OK,SDL_GetTicks64());
    s->ui_retry=SDL_GetTicks64();s->shelf_draw_valid=false;
    printf("sudoku screen=%d status=%d used=%zu peak=%zu\n",pn_sudoku_screen(&s->sudoku),r,s->pool->used,s->pool->peak);
    return r;
}
static pn_status_t sudoku_open(library_t *s){
    pn_cover_work_abort(&s->cover_work);s->covers_inactive=true;
    pn_status_t r=pn_hot_session_drop_for_switch(&s->hot);if(r!=PN_OK)return r;
    r=pn_sudoku_open(&s->sudoku,s->pool,&s->state_media,s->state_dir,NULL,0,SDL_GetTicks64());
    if(r==PN_OK)r=sudoku_draw(s);
    return r;
}
/// 无定时重绘，借用同一窗口画布。/ No periodic repaint; borrow the same window canvas.
static pn_status_t sokoban_draw(library_t *s,bool clean){
    pn_status_t r=pn_sokoban_render_update(&s->sokoban,&s->font,&s->frame);
    if(r==PN_UNSUPPORTED)r=pn_sokoban_render(&s->sokoban,&s->font,&s->frame);
    if(r==PN_OK)r=present(s,&s->frame,clean?PN_REFRESH_GC16:PN_REFRESH_GL16);
    pn_sokoban_displayed(&s->sokoban,r==PN_OK);s->ui_retry=SDL_GetTicks64();s->shelf_draw_valid=false;
    printf("sokoban screen=%d status=%d used=%zu peak=%zu\n",pn_sokoban_screen(&s->sokoban),r,s->pool->used,s->pool->peak);return r;
}
static pn_status_t sokoban_open(library_t *s){
    pn_cover_work_abort(&s->cover_work);s->covers_inactive=true;pn_status_t r=pn_hot_session_drop_for_switch(&s->hot);if(r!=PN_OK)return r;
    r=pn_sokoban_open(&s->sokoban,s->pool,&s->state_media,s->state_dir,SDL_GetTicks64());if(r==PN_OK)r=sokoban_draw(s,false);return r;
}
/// 扫雷复用原画布和呈现回执，无周期绘制。/ Minesweeper reuses the canvas and acknowledgment, without periodic painting.
static pn_status_t minesweeper_draw(library_t *s,bool clean){
    pn_status_t r=pn_minesweeper_render(&s->minesweeper,&s->font,&s->frame);
    if(r==PN_OK)r=present(s,&s->frame,clean?PN_REFRESH_GC16:PN_REFRESH_GL16);
    pn_minesweeper_displayed(&s->minesweeper,r==PN_OK);s->ui_retry=SDL_GetTicks64();s->shelf_draw_valid=false;
    printf("minesweeper screen=%d status=%d used=%zu peak=%zu\n",pn_minesweeper_screen(&s->minesweeper),r,s->pool->used,s->pool->peak);return r;
}
static pn_status_t minesweeper_open(library_t *s){
    pn_cover_work_abort(&s->cover_work);s->covers_inactive=true;pn_status_t r=pn_hot_session_drop_for_switch(&s->hot);if(r!=PN_OK)return r;
    r=pn_minesweeper_open(&s->minesweeper,s->pool,&s->state_media,s->state_dir,SDL_GetTicks64());if(r==PN_OK)r=minesweeper_draw(s,false);return r;
}

/// 仅键盘焦点变动复用已确认画布，失败/叠层/封面或状态更新完整恢复。/ Reuse confirmed pixels only for keyboard focus; failures, overlays, cover/status changes restore fully.
static pn_status_t draw_focus(library_t *s,int before){
    feedback_restore(s);pn_w_status_snapshot_t state=pn_w_status_snapshot();
    if(!s->shelf_draw_valid || !s->feedback_known || s->shelf_draw_revision!=s->async.revision || s->covers_dirty || s->actions_mode || s->open_failed || state.battery!=s->shelf_draw_status.battery || state.wifi!=s->shelf_draw_status.wifi || strcmp(state.clock,s->shelf_draw_status.clock))return draw(s);
    pn_shelf_options_t options=shelf_options(s);if(s->favorites_valid)options.favorite_items=s->favorites;pn_status_t status=pn_shelf_focus_with_font_file(s->page,&s->font,&s->frame,before,s->selected,s->covers,s->pool,&s->media,*s->font_real?s->font_real:NULL,&options);
    if(status==PN_UNSUPPORTED)return draw(s);
    if(status==PN_OK){status=present(s,&s->frame,PN_REFRESH_GL16);if(status==PN_OK)s->shelf_draw_revision=s->async.revision;}
    if(status!=PN_OK && status!=PN_EMPTY)s->shelf_draw_valid=false;
    printf("shelf_focus before=%d after=%d partial=1 status=%d\n",before,s->selected,(int)status);return status==PN_EMPTY?PN_OK:status;
}
static pn_status_t recent_load(library_t *s);
/* 最近阅读第一本作为“继续阅读”卡；没有记录时清除。/ The first recent entry becomes the continue-reading card; cleared when there is none. */
static bool covers_last(library_t *s,pn_catalog_item_t *last){
    pn_catalog_page_t *tmp=pn_alloc_tagged(s->pool,PN_MEM_UI,sizeof *tmp);bool found=false;
    bool loaded=tmp && recent_load(s)==PN_OK;
    if(loaded && s->page)pn_catalog_apply_recent(s->page,s->recent);
    if(loaded && s->recent->count && pn_catalog_recent_page(s->recent,0,tmp)==PN_OK && tmp->count){*last=tmp->items[0];found=true;}
    pn_free(tmp);return found;
}
/* 首帧前同步读取已缓存封面，一次成画；未缓存的书留给可打断的空闲解码。/ Read cached covers before the first paint for a single complete frame; uncached books wait for interruptible idle decoding. */
static void covers_reset(library_t *s){
    pn_cover_work_abort(&s->cover_work);
    s->shelf_focus_visible=false;
    covers_ensure(s);if(!s->covers)return;
    bool changed;pn_catalog_item_t last;bool has_last=covers_last(s,&last);s->covers_shown_at=0;s->covers_dirty=false;s->covers_cache_done=true;
    if(pn_shelf_covers_bind(s->covers,s->page,has_last?&last:NULL,s->pool,&s->media,*s->cover_dir?s->cover_dir:NULL,&changed)!=PN_OK)pn_shelf_covers_reset(s->covers,NULL);
}
static void covers_tick(library_t *s,bool decode){
    if(!s->covers)return;
    if(!s->covers_cache_done){bool changed=false;pn_status_t status=pn_shelf_covers_cache_step(&s->cover_work,s->covers,s->page,s->pool,&s->media,*s->cover_dir?s->cover_dir:NULL,&changed);s->covers_dirty|=changed;
        if(status==PN_EMPTY){s->covers_cache_done=true;if(s->covers_dirty && draw(s)==PN_OK)s->covers_dirty=false;}return;}
    if(!decode)return;
    bool changed=false;
    // 仅在余量不足时让字形缓存让出，缓存命中不逐屏清空。/ Yield glyph caches only under pressure; cache hits no longer clear them on every screen.
    pn_cache_reclaim(s->pool,128u*1024u+s->pool->cache_reserve);
    pn_status_t status=PN_BUSY;unsigned steps=0;uint64_t began=SDL_GetTicks64();
    if(s->covers_dirty && !pn_shelf_covers_pending(s->covers,s->page))status=PN_EMPTY;
    else do{status=pn_shelf_covers_work(&s->cover_work,s->covers,s->page,s->pool,&s->media,*s->cover_dir?s->cover_dir:NULL,s->salt,(s->pool->limit>s->pool->used?s->pool->limit-s->pool->used:0)+pn_cache_reclaimable(s->pool,(1u<<PN_CACHE_BITMAP)|(1u<<PN_CACHE_SOURCE)|(1u<<PN_CACHE_NEIGHBOR)|(1u<<PN_CACHE_GLYPH)|(1u<<PN_CACHE_CHAPTER))+1,&changed);}while(status==PN_BUSY && !changed && pn_shelf_covers_pending(s->covers,s->page) && ++steps<32u && SDL_GetTicks64()-began<8u);
    if(changed)s->covers_dirty=true;
    if((status==PN_OK || status==PN_EMPTY || status==PN_BUSY) && pn_cover_refresh_due(s->covers_dirty,SDL_GetTicks64(),s->covers_shown_at)){size_t ready=0;for(size_t i=0;i<s->page->count;i++)ready+=s->covers->state[i]==PN_COVER_READY;
        pn_status_t shown=draw(s);if(shown==PN_OK){s->covers_dirty=false;s->covers_shown_at=SDL_GetTicks64();}printf("covers ready=%zu status=%d\n",ready,(int)shown);}
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
    // 像时钟一样允许像素测试固定显示输入；真实文件空间准入仍直接查询文件系统。
    // Like the clock, allow pixel tests to fix this display input; real file-space admission still queries the filesystem directly.
    const char *fixed=getenv("PN_SIM_STORAGE");if(fixed && *fixed){snprintf(s->storage,sizeof s->storage,"%s",fixed);return;}
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
    pn_status_t dropped=pn_hot_session_drop_for_switch(&s->hot);if(dropped!=PN_OK)return dropped;
    system_font_ready(s);
    pn_status_t status=pn_settings_ui_open_cached(&s->settings,s->pool,&s->state_media,s->state_dir,s->pool->limit>=4u*1024u*1024u?&s->font:NULL,present,s);
    if(status==PN_OK){pn_settings_about_t about={0};storage_note(s);snprintf(about.version,sizeof about.version,"版本 %s · PC 模拟器",PN_PRODUCT_VERSION);snprintf(about.storage,sizeof about.storage,"%s",s->storage);
        snprintf(about.internal,sizeof about.internal,"%s",s->state_dir?"可用（--state-dir）":"未指定 --state-dir");snprintf(about.screen,sizeof about.screen,"684×1216 · 16 级灰阶（模拟）");pn_settings_ui_set_about(&s->settings,&about);
        pn_settings_time_t clock={0};time_t utc=time(NULL);if(utc>=1704067200 && (uint64_t)utc<=4102444799u)(void)pn_settings_time_format(&clock,(uint32_t)utc);snprintf(clock.message,sizeof clock.message,"模拟器显示本机时间，联网校时需在阅读器验证。");pn_settings_ui_set_time(&s->settings,&clock);
        pn_settings_update_t update={0};snprintf(update.message,sizeof update.message,"PC 模拟器只展示同源升级界面，不连接 Gitee 或写入设备 Flash。设备 OTA 需完成签名配置与真机回滚验收。");pn_settings_ui_set_update(&s->settings,&update);}
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
    if(s->home)s->entertainment=0;
    if(s->entertainment){
        pn_cover_work_abort(&s->cover_work);s->covers_dirty=false;s->covers_inactive=true;s->selected=-1;
        pn_status_t status=draw(s);printf("entertainment page=%u status=%d used=%zu peak=%zu\n",s->entertainment,(int)status,s->pool->used,s->pool->peak);return status;
    }
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
/// 保存成功的游戏退出只提交一次娱乐目标帧，清屏意图由端口消费。/ A saved game exit submits the Entertainment destination once; its port consumes the clear intent.
static pn_status_t game_return_to_entertainment(library_t *s){
    s->entertainment=1;s->home=false;s->game_exit_clean_pending=true;return page(s,false,true);
}
/// 复用已验证封面，不因开场反馈重新解析书籍。/ Reuse verified covers without reparsing books for opening feedback.
static pn_status_t opening_paint(library_t *s,pn_font_t *font,pn_frame_t *frame,const pn_catalog_item_t *book,const char *note){
    pn_status_t status=pn_opening_view_resolve(&s->opening_view,s->pool,&s->media,s->cover_dir,book,s->covers);
    return status==PN_OK?pn_reader_opening_render(font,frame,book->name,s->opening_view.preview,note):status;
}
static void opening_loading(library_t *s,const pn_catalog_item_t *book){
    uint8_t *pixels=pn_alloc_optional(s->pool,PN_MEM_FRAME,415872);pn_frame_t frame;pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();char detail[128];snprintf(detail,sizeof detail,"已建立 %u 页 · 中间键取消",s->opening_index.pages);
    pn_status_t shown=pn_frame_bind(&frame,pixels,415872,684,1216)?pn_font_open(&font,s->pool,&source,28):PN_NO_MEMORY;
    if(shown==PN_OK)shown=opening_paint(s,&font,&frame,book,s->index_gate?detail:NULL);
    if(shown==PN_OK)(void)present(s,&frame,PN_REFRESH_GL16);
    pn_font_close(&font);pn_free(pixels);
}
static pn_status_t finish_open(library_t *s,const pn_catalog_item_t *book,uint64_t now,pn_status_t status){
    fprintf(stderr,"library_open prepare name=%s status=%d pool=%zu/%zu\n",book->name,(int)status,s->pool->used,s->pool->limit);
    if(status==PN_OK && !s->index_finishing && s->fallback_path)status=s->epub.impl?pn_epub_app_fallback_font(&s->epub,s->fallback_path):pn_reader_app_fallback_font(&s->reader,s->fallback_path);
    if(status==PN_OK && book->identified){pn_book_id_t actual;status=s->epub.impl?pn_epub_app_identity(&s->epub,&actual):pn_reader_app_identity(&s->reader,&actual);if(status==PN_OK && memcmp(actual.sha256,book->expected.sha256,32))status=PN_STALE_JOB;printf("recent_identity status=%d\n",(int)status);}
    if(status==PN_OK && !s->index_wait && s->epub.impl && pn_epub_app_restore_pending(&s->epub)){
        if(!s->opening_feedback_shown && pn_epub_app_verify_pending(&s->epub)){s->opening_feedback_shown=true;opening_loading(s,book);}
        pn_status_t restored=pn_epub_app_prepare_resume_unshown(&s->epub);
        if(restored!=PN_OK && restored!=PN_EMPTY)status=restored;
        else if(restored==PN_EMPTY && !s->opening_feedback_shown){s->opening_feedback_shown=true;opening_loading(s,book);}
    }
    if(status==PN_OK && !s->index_finishing && s->index_wait && (s->epub.impl?pn_epub_app_index_pending(&s->epub):pn_reader_app_index_pending(&s->reader))){
        s->opening_index=(pn_book_index_state_t){0};pn_status_t built=s->epub.impl?pn_epub_app_index_step(&s->epub,&s->opening_index):pn_reader_app_index_step(&s->reader,&s->opening_index);
        if(built==PN_CANCELLED || built==PN_STALE_MEDIA)return built;
        if(built==PN_BUSY){
            if(!s->opening_book){s->opening_book=pn_alloc_tagged(s->pool,PN_MEM_DOCUMENT,sizeof *s->opening_book);if(!s->opening_book)return PN_NO_MEMORY;*s->opening_book=*book;}
            s->index_gate=true;s->async.defer_ack=true;opening_loading(s,s->opening_book);s->index_progress_at=SDL_GetTicks64();printf("book_index wait begin\n");return PN_BUSY;
        }
    }
    if(status==PN_OK)status=active_step(s,PN_APP_OPEN,now,present,s);
    if(s->open_measure_us){uint64_t end=pn_profile_time();pn_profile_observe(s->pool,"sim.open.request",status,end-s->open_measure_us,NULL);s->open_measure_us=0;}
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
static pn_status_t open_item_impl(library_t *s,const pn_catalog_item_t *book,uint64_t now){
    pn_cover_work_abort(&s->cover_work);
    if(book->format!=PN_BOOK_TXT && book->format!=PN_BOOK_EPUB){pn_sdl_async_title(&s->async,PN_BRAND_NAME " - 此格式尚未接入");return PN_EMPTY;}
    pn_status_t status;
    // 进入阅读后不再使用书架备份，先释放，给首章图片留出解码预算。/ The shelf backup is unused after entering reading; release it before decoding first-section images.
    s->covers_inactive=true;
    s->open_failed=false;pn_free(s->page_px);s->page_px=NULL;
    pn_wallpaper_mask_close(&s->lock_mask);
    if(s->font.impl && s->frame.pixels && pn_hot_session_source_selected(&s->hot,&s->media,book->path,book->format,book->identified?&book->expected:NULL,1,now)){
        s->opening_feedback_shown=true;
        status=opening_paint(s,&s->font,&s->frame,book,NULL);if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GL16);if(status==PN_CANCELLED)return status;
        s->opening_book=pn_alloc_tagged(s->pool,PN_MEM_DOCUMENT,sizeof *s->opening_book);if(!s->opening_book)return PN_NO_MEMORY;*s->opening_book=*book;
        status=pn_hot_session_take_opening(&s->hot,&s->media,book->path,book->identified?&book->expected:NULL,1,now,&s->foreground_open);
        if(status==PN_OK){pn_font_close(&s->font);pn_free(s->frame.pixels);s->frame.pixels=NULL;return PN_BUSY;}
        pn_free(s->opening_book);s->opening_book=NULL;
    }
    status=pn_hot_session_take_prepared(&s->hot,&s->media,book->path,book->format,book->identified?&book->expected:NULL,1,now,&s->reader,&s->epub);
    if(status==PN_OK){if(s->epub.impl){pn_font_close(&s->font);pn_free(s->frame.pixels);s->frame.pixels=NULL;}printf("library_prepared take=%zu name=%s\n",s->hot.takes,book->name);goto first_page;}
    if(status==PN_CANCELLED)return status;
    status=s->index_wait?PN_EMPTY:pn_hot_session_resume(&s->hot,&s->media,book->path,book->format,book->identified?&book->expected:NULL,1,now,&s->reader,&s->epub,present,s);
    if(status==PN_OK){s->last_read=*book;s->last_read.identified=(s->epub.impl?pn_epub_app_identity(&s->epub,&s->last_read.expected):pn_reader_app_identity(&s->reader,&s->last_read.expected))==PN_OK;s->last_read_known=s->last_read.identified;printf("library_hot hit=%zu name=%s\n",s->hot.hits,book->name);if(book->identified)printf("recent_identity status=0\n");printf("library_open name=%s\n",book->name);
        if(s->epub.impl){pn_epub_progress_t location;if(pn_epub_app_progress(&s->epub,&location)==PN_OK)printf("epub_library_position path=%s element=%llu run=%u offset=%llu\n",location.location.path,(unsigned long long)location.location.position.element,location.location.position.run,(unsigned long long)location.location.position.offset);}
        else{pn_txt_progress_t progress;if(pn_reader_app_progress(&s->reader,&progress)==PN_OK)printf("library_position offset=%llu\n",(unsigned long long)progress.source_offset);}
        return PN_OK;}
    if(status==PN_IO || status==PN_CANCELLED)return status;
    status=pn_hot_session_drop_for_switch(&s->hot);if(status!=PN_OK)return status;
    if(book->format!=PN_BOOK_EPUB && pn_reader_loading_needed(book->format==PN_BOOK_TXT,book->size) && s->font.impl && s->frame.pixels){status=opening_paint(s,&s->font,&s->frame,book,NULL);if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GL16);if(status==PN_CANCELLED)return status;}
    if(book->format==PN_BOOK_EPUB){
        s->opening_book=pn_alloc_tagged(s->pool,PN_MEM_DOCUMENT,sizeof *s->opening_book);if(!s->opening_book)return PN_NO_MEMORY;*s->opening_book=*book;
        pn_font_close(&s->font);pn_free(s->frame.pixels);s->frame.pixels=NULL;
        status=pn_epub_app_open_job_begin(&s->foreground_open,s->pool,&s->media,book->path,s->font_path,s->state_dir,PN_STYLE_DEFAULT_PIXELS,now);
        if(status==PN_OK){if(pn_epub_app_open_job_feedback_due(&s->foreground_open)){s->opening_feedback_shown=true;opening_loading(s,s->opening_book);}return PN_BUSY;}
        (void)opening_abort(s);
    }
    else status=pn_reader_app_open_on_media(&s->reader,s->pool,&s->media,book->path,s->font_path,s->state_dir,PN_STYLE_DEFAULT_PIXELS,now);
first_page:
    return finish_open(s,book,now,status);
}
static pn_status_t open_item(library_t *s,const pn_catalog_item_t *book,uint64_t now){
    if((s->foreground_open.impl || s->index_gate))return PN_BUSY;
    pn_opening_view_close(&s->opening_view);
    s->opening_feedback_shown=false;
    s->open_measure_us=pn_profile_time();
    pn_async_block_controls(&s->async.input,true);pn_sdl_async_title(&s->async,PN_BRAND_NAME " - 正在打开书籍");
    s->async.context=18;pn_async_set_view(&s->async.input,18u|(s->async.revision<<16),false);
    printf("opening begin name=%s\n",book->name);pn_status_t status=open_item_impl(s,book,now);
    if(status==PN_BUSY && (s->foreground_open.impl || s->index_gate)){s->async.defer_ack=true;return status;}
    pn_opening_view_close(&s->opening_view);
    pn_async_block_controls(&s->async.input,false);pn_sdl_async_title(&s->async,PN_BRAND_NAME);
    if(s->open_measure_us){uint64_t end=pn_profile_time();pn_profile_observe(s->pool,"sim.open.request",status,end-s->open_measure_us,NULL);s->open_measure_us=0;}
    printf("opening end status=%d\n",(int)status);return status;
}
static void tick_opening(library_t *s,uint64_t now){
    if(s->index_gate){
        pn_book_index_state_t progress=s->opening_index;pn_status_t built;unsigned steps=0;uint64_t began=SDL_GetTicks64();
        do{built=s->epub.impl?pn_epub_app_index_step(&s->epub,&progress):pn_reader_app_index_step(&s->reader,&progress);}while(built==PN_BUSY && ++steps<PN_BOOK_INDEX_STEP_MAX && SDL_GetTicks64()-began<8);
        if(built==PN_BUSY || built==PN_CANCELLED){if(progress.pages/PN_BOOK_INDEX_BATCH_PAGES!=s->opening_index.pages/PN_BOOK_INDEX_BATCH_PAGES && now>=s->index_progress_at && now-s->index_progress_at>=2000){s->opening_index=progress;s->index_progress_at=now;opening_loading(s,s->opening_book);}return;}
        s->index_gate=false;s->index_finishing=true;pn_catalog_item_t *book=s->opening_book;s->opening_book=NULL;
        profile_index_flush();printf("book_index wait end status=%d pages=%u\n",built,progress.pages);(void)finish_open(s,book,now,built==PN_STALE_MEDIA?built:PN_OK);pn_free(book);s->index_finishing=false;
        pn_opening_view_close(&s->opening_view);s->async.defer_ack=false;pn_async_block_controls(&s->async.input,false);pn_sdl_async_title(&s->async,PN_BRAND_NAME);return;
    }

    pn_status_t status=pn_epub_app_open_job_advance(&s->foreground_open,&s->epub);
    if(status==PN_BUSY || (status==PN_CANCELLED && pn_epub_app_open_job_pending(&s->foreground_open))){
        if(status==PN_BUSY && !s->opening_feedback_shown && pn_epub_app_open_job_feedback_due(&s->foreground_open)){s->opening_feedback_shown=true;opening_loading(s,s->opening_book);}
        return;
    }
    pn_catalog_item_t *book=s->opening_book;s->opening_book=NULL;
    if((s->foreground_open.impl || s->index_gate))(void)pn_epub_app_open_job_close(&s->foreground_open);
    (void)finish_open(s,book,now,status);pn_free(book);
    if(!s->index_gate){pn_opening_view_close(&s->opening_view);s->async.defer_ack=false;pn_async_block_controls(&s->async.input,false);pn_sdl_async_title(&s->async,PN_BRAND_NAME);printf("opening end status=%d\n",(int)status);}
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
/// 原书/字体准备在首页安静且封面全部完成后进行，CLI额外补字仍走原显式配置。/ Prepare book/fonts only on a quiet Home after all covers finish; an extra CLI fallback retains explicit configuration.
static void prepare_continue(library_t *s,uint64_t now,uint64_t activity){
    if(!s->home || !s->shelf_draw_valid || !s->covers || !s->covers->has_last || !s->covers->last.identified || (s->covers->last.format!=PN_BOOK_TXT && s->covers->last.format!=PN_BOOK_EPUB) || s->fallback_path || now<activity || now-activity<1500 || pn_cover_work_pending(&s->cover_work) || pn_shelf_covers_pending(s->covers,s->page))return;
    if(pn_hot_session_source_pending(&s->hot)){pn_status_t status=pn_hot_session_source_step(&s->hot,now);if(status!=PN_BUSY && status!=PN_EMPTY)printf("book_prepare status=%d ready=%d preparations=%zu\n",(int)status,s->hot.prepared,s->hot.preparations);return;}
    if(pn_hot_session_layout_pending(&s->hot)){pn_status_t laid=pn_hot_session_layout(&s->hot,now);printf("book_layout status=%d\n",(int)laid);return;}
    if(pn_hot_session_paint_pending(&s->hot)){
        pn_status_t painted=pn_hot_session_paint(&s->hot,now,8);if(!pn_hot_session_paint_pending(&s->hot))printf("book_paint status=%d\n",(int)painted);return;
    }
    pn_status_t status=pn_hot_session_prepare_step(&s->hot,s->pool,&s->media,&s->covers->last,s->font_path,s->state_dir,PN_STYLE_DEFAULT_PIXELS,1,now,activity);
    if(status!=PN_EMPTY && status!=PN_BUSY)printf("book_prepare status=%d ready=%d preparations=%zu\n",(int)status,s->hot.prepared,s->hot.preparations);
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
    stats_flush(s);
    bookmark_lock_capture(s);
    // 与设备一致，退出首帧合并已验证RAM预览；后台日志提交不授予额外来源信任。/ Match the device by merging a verified RAM preview for the exit frame; deferred journal commits grant no extra source trust.
    pn_recent_item_t preview;bool has_preview=s->recent_loaded && s->recent &&
        (s->epub.impl?pn_epub_app_recent_item(&s->epub,&preview):pn_reader_app_recent_item(&s->reader,&preview))==PN_OK;
    pn_status_t status=PN_EMPTY;
    if(reading(s) && s->last_read_known && !s->toc.active && !s->fonts.active && !s->styles.active && !s->jumpui.active && s->bookmarks.mode==PN_BUI_CLOSED)
        status=pn_hot_session_park(&s->hot,s->pool,&s->media,s->last_read.path,1,now,&s->reader,&s->epub);
    if(status==PN_EMPTY || status==PN_BUSY)status=active_close(s,now);
    s->recent_loaded=status==PN_OK && has_preview && pn_recent_preview(s->recent,&preview,NULL)==PN_OK;
    printf("library_hot park=%zu retained=%d status=%d\n",s->hot.parks,s->hot.media!=NULL,(int)status);
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
/// 只把一级页真实底栏交给按下路由，内容区、子页与模态保留原点击语义。/ Route only real root navigation on press; content, subpages and modals retain their click semantics.
static int root_navigation_hit(library_t *s,int x,int y){
    if((s->foreground_open.impl || s->index_gate) || s->opening_book)return -1;
    if(s->minesweeper.active || s->sokoban.active || s->sudoku.active || s->locked || s->fonts.active || s->toc.active || s->jumpui.active || s->styles.active || s->bookmarks.mode!=PN_BUI_CLOSED || s->actions_mode || s->font_manage.impl || s->wallpaper.impl || s->search_open || s->index || s->toolbar || pn_w_navigation_hit(x,y)<0)return -1;
    if(s->settings.impl)return pn_settings_ui_screen(&s->settings)==0?pn_settings_ui_hit(&s->settings,x,y):-1;
    if(s->hub){pn_transfer_hub_info_t info=hub_info(s);return pn_transfer_hub_hit(&info,x,y);}
    return reading(s) || !s->page?-1:shelf_hit(s,x,y);
}
// 与设备端一样，快照返回后才呈现，释放/失焦先沿原接触所有权消费。
// As on device, present only after returning from the snapshot frame, consuming release/focus edges through original contact ownership first.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static pn_status_t root_navigation_merge(library_t *s,pn_tap_press_t *press,SDL_Event *chosen,int *selected){
    pn_async_event_t pending[PN_ASYNC_CONTROLS];size_t n=0;pn_status_t status=pn_sdl_async_drain_root(&s->async,pending,PN_ASYNC_CONTROLS,&n);
    if(status!=PN_OK){int ignored;(void)pn_tap_press_feed(press,1,-1,false,&ignored);return status;}
    unsigned selections=1;
    for(size_t i=0;i<n;i++){
        if(pending[i].bytes!=sizeof(SDL_Event))continue;
        SDL_Event event;memcpy(&event,pending[i].payload,sizeof event);int next=-1;
        if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
            (void)pn_tap_press_feed(press,1,-1,false,&next);if(!(event.window.data1&SDL_BUTTON_LMASK)){(void)pn_tap_press_feed(press,0,-1,true,&next);(void)pn_tap_press_feed(press,0,-1,true,&next);}continue;}
        bool motion=event.type==SDL_MOUSEMOTION,up=event.type==SDL_MOUSEBUTTONUP;
        if(!(motion && (event.motion.state&SDL_BUTTON_LMASK)) && !((event.type==SDL_MOUSEBUTTONDOWN || up) && event.button.button==SDL_BUTTON_LEFT))continue;
        int x=motion?event.motion.x:event.button.x,y=motion?event.motion.y:event.button.y;
        int hit=pn_async_root_current(&s->async.input,&pending[i])?root_navigation_hit(s,x,y):-1;
        (void)pn_tap_press_feed(press,up?0u:1u,hit,up || hit>=0,&next);if(up)(void)pn_tap_press_feed(press,0,-1,true,&next);
        if(next>=0){*selected=next;*chosen=event;selections++;}
    }
    if(selections>1)printf("navigation merged events=%zu selections=%u\n",n,selections);
    return PN_OK;
}
/// 从真实显示端口的已确认像素取区域，UI对象关闭后局部原图仍自有。/ Read confirmed port pixels; local originals remain owned after UI closure.
static pn_status_t feedback_read(void *ctx,pn_feedback_box_t b,pn_frame_t *out){
    library_t *s=ctx;if(!s->feedback_known)return PN_EMPTY;
    for(int y=0;y<b.height;y++)for(int x=0;x<b.width;x++)pn_frame_pixel(out,x,y,(uint8_t)((s->argb[(size_t)(b.y+y)*684+b.x+x]&255u)/17u));
    return PN_OK;
}
static pn_status_t feedback_submit(void *ctx,const pn_feedback_patch_t *p){library_t *s=ctx;pn_status_t r=pn_sdl_async_feedback(&s->async,p);if(r==PN_OK && !p->settle)pn_feedback_idle_note(&s->feedback_idle,p->box,SDL_GetTicks64());return r;}
/// SDL只由窗口owner调用；局部上传，不改变语义页面。/ SDL is called only by the window owner; upload a local patch without changing the semantic view.
static pn_status_t feedback_raw_main(void *ctx,const pn_feedback_patch_t *p){
    library_t *s=ctx;pn_feedback_box_t b=p->box;
    if(p->settle){static bool failed;if(getenv("PN_SIM_SETTLE_FAIL_ONCE") && !failed){failed=true;printf("feedback_settle status=%d\n",(int)PN_IO);return PN_IO;}}
    else for(int y=0;y<b.height;y++)for(int x=0;x<b.width;x++){unsigned value=pn_frame_get(&p->frame,x,y)*17u;s->argb[(size_t)(b.y+y)*684+b.x+x]=0xff000000u|value*0x010101u;}
    SDL_Rect rect={b.x,b.y,b.width,b.height};
    if(SDL_UpdateTexture(s->texture,&rect,s->argb+(size_t)b.y*684+b.x,684*(int)sizeof(uint32_t))!=0 || SDL_RenderClear(s->renderer)!=0 || SDL_RenderCopy(s->renderer,s->texture,NULL,NULL)!=0)return PN_IO;
    SDL_RenderPresent(s->renderer);printf("feedback pressed=%d hit=%d view=%llu revision=%u box=%d,%d,%d,%d settle=%d\n",(int)p->pressed,p->hit,(unsigned long long)p->view,s->async.revision,b.x,b.y,b.width,b.height,(int)p->settle);
    const char *dump=getenv("PN_SIM_FRAME_DIR");
    if(dump && *dump && strlen(dump)<400){static unsigned index;char path[512];snprintf(path,sizeof path,"%s/feedback-%04u-%s.pgm",dump,++index,p->settle?"settle":p->pressed?"press":"restore");FILE *file=fopen(path,"wb");
        if(file){fprintf(file,"P5\n684 1216\n255\n");uint8_t row[684];for(int y=0;y<1216;y++){for(int x=0;x<684;x++)row[x]=(uint8_t)(s->argb[(size_t)y*684+x]&255u);if(fwrite(row,1,sizeof row,file)!=sizeof row)break;}fclose(file);}}
    return PN_OK;
}
static pn_status_t feedback_idle_tick(library_t *s,uint64_t now,bool busy){
    if(!s->feedback_enabled || !s->feedback_known || s->feedback.active)return PN_EMPTY;
    pn_feedback_idle_plan_t plan;pn_status_t r=pn_feedback_idle_plan(&s->feedback_idle,now,busy,&plan);if(r!=PN_OK)return r;
    pn_feedback_patch_t p={.box=plan.area,.view=atomic_load(&s->async.input.view),.settle=true};r=feedback_submit(s,&p);pn_feedback_idle_receipt(&s->feedback_idle,plan,r,SDL_GetTicks64());return r;
}
/// 恢复是取消/收尾责任，临时屏蔽已处理的取消信号而不执行选择命令。/ Restoration is cancellation/cleanup work; mask the handled cancel signal without executing selection commands.
static void feedback_restore(library_t *s){
    if(!s->feedback.active)return;
    pn_cancel_scope_t shield;pn_cancel_enter(&shield,NULL,NULL);pn_status_t r=pn_feedback_present(&s->feedback,false,feedback_submit,s);pn_cancel_leave(&shield);
    if(r!=PN_OK)s->feedback_known=false;
    (void)pn_feedback_discard(&s->feedback);
}
/* 按下只显示；滑出/焦点丢失撤销；松手先让命令产生内容回执，无内容则下一轮恢复。
 * Press only displays; leaving/focus loss cancels; release lets the command produce content, restoring next round if no content replaces it. */
static bool feedback_event(library_t *s,const SDL_Event *e){
    if(!s->feedback_enabled)return true;
    if(e->type==SDL_KEYDOWN || e->type==SDL_QUIT || (e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_FOCUS_LOST)){
        feedback_restore(s);s->feedback_tap=(pn_tap_t){0};return true;}
    bool motion=e->type==SDL_MOUSEMOTION;
    if(!motion && e->type!=SDL_MOUSEBUTTONDOWN && e->type!=SDL_MOUSEBUTTONUP)return true;
    if((motion && !(e->motion.state&SDL_BUTTON_LMASK)) || (!motion && e->button.button!=SDL_BUTTON_LEFT))return true;
    int x=motion?e->motion.x:e->button.x,y=motion?e->motion.y:e->button.y;
    int hit=management_hit(s,x,y);bool supported=s->settings.impl || s->font_manage.impl || s->wallpaper.impl || s->hub || s->toolbar || s->actions_mode || s->fonts.active || s->toc.active || s->styles.active || s->jumpui.active || s->bookmarks.mode!=PN_BUI_CLOSED || s->search_open || s->index;
    if(!supported)return true;
    if(e->type!=SDL_MOUSEBUTTONDOWN && !s->feedback_tap.down)return false;
    int selected=-1;bool was_down=s->feedback_tap.down;
    (void)pn_tap_feed(&s->feedback_tap,1,hit,true,&selected);
    if(s->feedback_tap.cancelled){feedback_restore(s);if(motion)return false;}
    if(e->type==SDL_MOUSEBUTTONDOWN && !was_down && !s->feedback_tap.cancelled){pn_feedback_box_t box;int code;
        if(pn_feedback_region(management_hit,s,x,y,&box,&code)==PN_OK && pn_feedback_prepare(&s->feedback,s->pool,box,atomic_load(&s->async.input.view),code,feedback_read,s)==PN_OK){
            pn_status_t r=pn_feedback_present(&s->feedback,true,feedback_submit,s);if(r!=PN_OK)feedback_restore(s);}}
    if(e->type==SDL_MOUSEBUTTONUP){(void)pn_tap_feed(&s->feedback_tap,0,-1,true,&selected);bool commit=pn_tap_feed(&s->feedback_tap,0,-1,true,&selected);s->feedback_release=true;if(!commit)return false;}
    return true;
}
/// 数字1/2/3模拟左/中/右键；方向键仍可选择阅读菜单项。/ Digits 1/2/3 simulate left/middle/right keys; arrows still select reader-menu items.
static bool management_event(library_t *s,SDL_Event *event,uint64_t now){
    bool active=s->fonts.active || s->toc.active || s->jumpui.active || s->styles.active || s->bookmarks.mode!=PN_BUI_CLOSED || s->actions_mode || s->settings.impl || s->font_manage.impl || s->wallpaper.impl || s->hub || s->search_open || s->index || s->toolbar;
    if(!active || event->type!=SDL_KEYDOWN || event->key.keysym.sym<SDLK_1 || event->key.keysym.sym>SDLK_3)return false;
    if(event->key.repeat)return true;
    int key=(int)(event->key.keysym.sym-SDLK_1);
    // 与设备一致：排版中间键保存并留在当前书，预览态也应用草稿。/ Match the device: the typesetting middle key saves within the current book, including preview drafts.
    if(key==PN_KEY_2 && s->fonts.active){
        pn_status_t status=pn_font_ui_event(&s->fonts,s->fonts.mode==1?PN_FUI_APPLY:PN_FUI_BACK,now,present,s);
        if(!s->fonts.active){pn_font_ui_close(&s->fonts);if(s->styles.active)status=pn_style_ui_event(&s->styles,PN_SUI_APPLY,now,present,s);}
        printf("reading settings middle status=%d active=%d\n",(int)status,(int)(s->fonts.active || s->styles.active));return true;
    }
    if(key==PN_KEY_2 && s->styles.active){pn_status_t status=pn_style_ui_event(&s->styles,PN_SUI_APPLY,now,present,s);style_report(&s->styles,PN_SUI_APPLY,status);return true;}
    if(key==PN_KEY_2 && s->toolbar){pn_status_t status=toolbar_close(s,false);printf("reading settings middle status=%d active=%d\n",(int)status,(int)s->toolbar);return true;}
    if(key==PN_KEY_2){
        pn_status_t status=active_close(s,now);
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
    pn_status_t dropped=pn_hot_session_drop_for_switch(&s->hot);if(dropped!=PN_OK)return dropped;
    pn_wallpaper_mask_close(&s->lock_mask);
    char cache_dir[PN_WALLPAPER_PATH_MAX];const char *cache=NULL;
    if(s->wallpaper_store_argument && (size_t)snprintf(cache_dir,sizeof cache_dir,"%s/images",s->wallpaper_store_argument)<sizeof cache_dir)cache=cache_dir;
    pn_status_t status=pn_wallpaper_ui_open_cached(&s->wallpaper,s->pool,&s->media,s->wallpaper_dir,s->wallpaper_store_ok?&s->wallpaper_store:NULL,present,s,cache);
    if(status==PN_OK){pn_bookmark_lock_t snapshot=s->bookmark_lock;snapshot.battery=-1;time_t utc=time(NULL);snapshot.date_known=utc>=0 && (uint64_t)utc<=UINT32_MAX;snapshot.utc=snapshot.date_known?(uint32_t)utc:0;stats_snapshot(s,&snapshot);pn_wallpaper_ui_set_bookmark(&s->wallpaper,&snapshot);}
    return status;
}
/// PC电源边界：保存关闭正文后，用共用锁屏记录/绘制预览；不模拟硬件睡眠或功耗。/ PC power boundary: save/close the reader and preview shared lock records/rendering; no hardware sleep or power simulation.
static pn_status_t lock_preview(library_t *s,uint64_t now){
    if(s->minesweeper.active){pn_status_t saved=pn_minesweeper_pause(&s->minesweeper,now);if(saved!=PN_OK){(void)minesweeper_draw(s,false);return saved;}}
    if(s->sokoban.active){pn_status_t saved=pn_sokoban_pause(&s->sokoban,now);if(saved!=PN_OK){(void)sokoban_draw(s,false);return saved;}}
    if(s->sudoku.active){pn_status_t saved=pn_sudoku_pause(&s->sudoku,now);if(saved!=PN_OK){(void)sudoku_draw(s);return saved;}}
    stats_flush(s);
    bookmark_lock_capture(s);
    pn_status_t status=opening_abort(s);
    if(reading(s)){status=s->epub.impl?pn_epub_app_suspend(&s->epub,now):pn_reader_app_suspend(&s->reader,now);if(status==PN_BUSY)status=active_close(s,now);}
    if(status!=PN_OK)return status;
    s->toolbar=false;
    if(!s->frame.pixels){uint8_t *pixels=pn_alloc_tagged(s->pool,PN_MEM_FRAME,PN_WALLPAPER_BYTES);if(!pixels)return PN_NO_MEMORY;pn_frame_bind(&s->frame,pixels,PN_WALLPAPER_BYTES,684,1216);}
    if(!s->font.impl){pn_text_source_t font=pn_font_builtin_source();status=pn_font_open(&s->font,s->pool,&font,32);}
    pn_lock_selection_t selection={.mode=PN_LOCK_DEFAULT,.hint=false};
    if(status==PN_OK && s->wallpaper_store_ok){pn_media_lease_t lease;pn_status_t loaded=pn_media_acquire(&s->wallpaper_media,PN_MEDIA_READ,&lease);
        if(loaded==PN_OK){loaded=pn_wallpaper_load(&s->wallpaper_store,&lease,&selection,&s->frame);(void)pn_media_release(&s->wallpaper_media,&lease);}
        if(loaded!=PN_OK)selection=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=false};}
    if(selection.mode==PN_LOCK_SIMPLE || selection.mode==PN_LOCK_BOOK)selection=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=false};
    if(selection.mode==PN_LOCK_MASK){
        pn_status_t mask=PN_EMPTY;
        if(reading(s) && s->page_px){
            // 有封面缓存目录时把遮罩平面缓存放在同级masks。/ With a cover cache directory, keep mask planes in the sibling masks directory.
            char mask_dir[540]={0};const char *slash=*s->cover_dir?strrchr(s->cover_dir,'/'):NULL;
            if(slash && slash>s->cover_dir && (size_t)snprintf(mask_dir,sizeof mask_dir,"%.*s/masks",(int)(slash-s->cover_dir),s->cover_dir)>=sizeof mask_dir)mask_dir[0]=0;
            mask=pn_wallpaper_mask_open_cached(&s->lock_mask,s->pool,&s->media,&selection,*mask_dir?mask_dir:NULL);
            if(mask==PN_OK){memcpy(s->frame.pixels,s->page_px,PN_WALLPAPER_BYTES);mask=pn_wallpaper_mask_paint(&s->lock_mask,&s->frame);}
        }
        printf("lock_mask status=%d\n",(int)mask);pn_wallpaper_mask_close(&s->lock_mask);
        if(mask!=PN_OK)selection=(pn_lock_selection_t){.mode=PN_LOCK_DEFAULT,.hint=false};
    }
    if(!reading(s))pn_wallpaper_mask_close(&s->lock_mask);
    if(status==PN_OK && selection.mode==PN_LOCK_BOOKMARK){
        pn_bookmark_lock_t snapshot=s->bookmark_lock;snapshot.battery=-1;time_t utc=time(NULL);
        stats_snapshot(s,&snapshot);
        snapshot.date_known=utc>=0 && (uint64_t)utc<=UINT32_MAX;snapshot.utc=snapshot.date_known?(uint32_t)utc:0;
        uint8_t *cover_px=pn_alloc_tagged(s->pool,PN_MEM_UI,PN_COVER_BYTES);pn_frame_t cover;pn_book_display_t display;bool ready=false;
        if(cover_px && *s->bookmark_lock_item.path && pn_frame_bind(&cover,cover_px,PN_COVER_BYTES,PN_COVER_WIDTH,PN_COVER_HEIGHT))
            ready=pn_cover_cached_one(s->pool,&s->media,s->cover_dir,&s->bookmark_lock_item,&cover,&display)==PN_OK;
        if(ready){if(*display.title)snapshot.book=display;}
        status=pn_bookmark_lock_render(&snapshot,ready?&cover:NULL,&s->font,"",&s->frame);pn_free(cover_px);
    }else if(status==PN_OK)status=pn_lock_render(&selection,&s->font,"",&s->frame);
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
    if(getenv("PN_SIM_BOOT_PREVIEW")){status=pn_boot_art_draw(pool,&s->frame);if(status!=PN_OK)status=pn_brand_image_render(&s->frame);if(status==PN_OK)status=pn_brand_version_render(&s->frame,PN_PRODUCT_VERSION);if(status==PN_OK)status=present_raw(s,&s->frame);if(status!=PN_OK)goto cleanup;SDL_Delay(900);}
    pn_media_init(&s->media);status=pn_media_attach(&s->media,1);if(status!=PN_OK)goto cleanup;
    // 与设备使用同一系统字库加载器，模拟器只替换目录port。/ Use the same system-font loader as the device with only the directory port replaced.
    if(s->font_dir){char path[PN_FONT_REFERENCE_PATH_MAX];int n=snprintf(path,sizeof path,"%s/HarmonyOS_Sans_SC_Regular.ttf",s->font_dir);if(n>0 && (size_t)n<sizeof path)(void)pn_font_system_open(pool,&s->media,path);}
    if(pool->limit>=4u*1024u*1024u){status=pn_w_font_scope_begin(&s->ui_scope,pool,NULL);if(status!=PN_OK)goto cleanup;}
    pn_text_source_t source=pn_font_builtin_source();status=pn_font_open(&s->font,pool,&source,24);if(status!=PN_OK)goto cleanup;
    if(cover_cache){char cache[PN_CATALOG_PATH_MAX];int n=snprintf(cache,sizeof cache,"%s/library",cover_cache);if(n>0 && (size_t)n<sizeof cache)(void)pn_catalog_index_init(&s->catalog_index,pool,&s->media,s->directory,cache);}
    pn_media_init(&s->state_media);status=pn_media_attach(&s->state_media,1);if(status!=PN_OK)goto cleanup;
    s->shelf_prefs=pn_shelf_prefs_default();
    (void)pn_settings_load_shelf(&s->state_media,s->state_dir,&s->shelf_prefs);
    s->home=true;status=page(s,false,true);if(status!=PN_OK)goto cleanup;
    if(pn_settings_load_flags(&s->state_media,s->state_dir,&s->input_flags)!=PN_OK)s->input_flags=PN_INPUT_FLAGS_DEFAULT;
    (void)pn_settings_load_index(&s->state_media,s->state_dir,&s->index_wait);
    result=0;bool running=true,prefetch_work=false;pn_tap_t tap={0};pn_tap_press_t navigation_press={0};pn_shelf_gesture_t shelf_gesture={0};pn_reader_input_t input={0};uint64_t font_activity=SDL_GetTicks64();
    while(running){
        if(s->feedback_release){feedback_restore(s);s->feedback_release=false;}
        {static time_t last;const char *fixed=getenv("PN_SIM_CLOCK");time_t t=time(NULL);if(fixed)pn_w_set_clock(fixed);else if(t!=last){last=t;struct tm local;char text[8];if(localtime_r(&t,&local) && strftime(text,sizeof text,"%H:%M",&local))pn_w_set_clock(text);}} // 本机时钟；固定值仅用于可复跑测试 / Host clock; a fixed value is only for reproducible tests
        uint32_t view=s->locked?1:s->minesweeper.active?21:s->sokoban.active?20:s->sudoku.active?19:(s->foreground_open.impl || s->index_gate)?18:s->fonts.active?2:s->toc.active?3:s->styles.active?4:s->jumpui.active?5:s->bookmarks.mode!=PN_BUI_CLOSED?6:s->actions_mode?7:s->settings.impl?8:s->wallpaper.impl?9:s->font_manage.impl?10:s->hub?11:s->index?12:s->search_open?13:s->toolbar?14:reading(s)?15:s->home?16:17;
        stats_poll(s,view==15);
        if(!s->minesweeper.active && !s->sokoban.active && !s->sudoku.active && !(s->foreground_open.impl || s->index_gate) && !reading(s) && !s->home && s->page)view+=(uint32_t)s->page->index*32u;
        pn_list_motion_stats_t moving=s->toc.active?pn_toc_ui_motion_stats(&s->toc):pn_font_manage_motion_stats(&s->font_manage);
        uint64_t paint_now=SDL_GetTicks64();bool paint_work=view==16 && !s->pointer_down && atomic_load(&s->async.mouse)==0 && paint_now>=font_activity && paint_now-font_activity>=1500 && (pn_hot_session_source_pending(&s->hot) || pn_hot_session_layout_pending(&s->hot) || pn_hot_session_paint_pending(&s->hot));
        bool deferred_work=(view%32u==16u || view%32u==17u) && !s->pointer_down && atomic_load(&s->async.mouse)==0 && paint_now>=font_activity && paint_now-font_activity>=800 && pn_hot_session_deferred_pending(&s->hot);
        // EPUB候选分步待续时使用短等待，每轮仍先处理真实输入。/ Use a short wait while an EPUB candidate continues, still handling real input first each tick.
        bool cache_work=pn_sudoku_pending(&s->sudoku) || s->reader_turn_pending || pn_wallpaper_ui_pending(&s->wallpaper) || (s->foreground_open.impl || s->index_gate) || (view==15 && (prefetch_work || pn_epub_app_cache_pending(&s->epub))) || pn_cover_work_pending(&s->cover_work) || (!s->entertainment && (view%32u==16u || view%32u==17u) && pn_shelf_covers_pending(s->covers,s->page)) || paint_work || deferred_work;
        SDL_Event event;bool got=pn_sdl_async_wait(&s->async,&event,moving.active || moving.coasting || cache_work?1u:100u,view,root_navigation_hit(s,80,1150)>=0);uint64_t now=SDL_GetTicks64();prefetch_work=false;
        if(got)stats_pause(s);
        else if(!s->pointer_down && atomic_load(&s->async.mouse)==0 && s->reading_stats.media && now>=font_activity && now-font_activity>=1500u)(void)pn_reading_stats_save(&s->reading_stats,now,false);
        if(got && (event.type==SDL_KEYDOWN || event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION || event.type==SDL_QUIT || (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST))){pn_epub_app_cache_activity(&s->epub,now);pn_reader_app_cache_activity(&s->reader,now);font_activity=now;}
        if((s->foreground_open.impl || s->index_gate)){
            if(!got)tick_opening(s,now);
            else if(event.type==SDL_QUIT){(void)opening_abort(s);running=false;printf("opening end status=%d\n",(int)PN_CANCELLED);}
            else if(event.type==SDL_KEYDOWN && !event.key.repeat){SDL_Keycode key=event.key.keysym.sym;
                if(key==SDLK_ESCAPE || key==SDLK_1 || key==SDLK_2 || key==SDLK_h || key==SDLK_l || key==SDLK_k){
                    pn_status_t dropped=opening_abort(s);if(dropped==PN_OK){if(key==SDLK_l || key==SDLK_k)status=lock_preview(s,now);else{if(key==SDLK_2 || key==SDLK_h)s->home=true;status=page(s,false,true);}}
                    printf("opening end status=%d\n",(int)PN_CANCELLED);
                }
            }
            cancel_pointer(&input,&tap,true);navigation_press=(pn_tap_press_t){0};shelf_gesture=(pn_shelf_gesture_t){0};continue;
        }
        if(got){bool cover_focus=(view%32u==16u || view%32u==17u) && event.type==SDL_KEYDOWN && (event.key.keysym.sym==SDLK_UP || event.key.keysym.sym==SDLK_DOWN) && s->pool->used<=s->pool->limit && s->pool->limit-s->pool->used>=512u*1024u;
            if(cover_focus)pn_cover_write_abort(&s->cover_work.writer);else pn_cover_work_abort(&s->cover_work);pn_feedback_idle_activity(&s->feedback_idle,now);}
        if(got && event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){s->pointer_down=false;s->toc_pointer=false;s->font_pointer=false;cancel_pointer(&input,&tap,true);}
        int navigation=-1;bool navigation_consumed=false,navigation_selected=false;
        if(got && event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
            navigation_consumed=pn_tap_press_feed(&navigation_press,1,-1,false,&navigation);
            if(s->async.discarded_release || !(event.window.data1&SDL_BUTTON_LMASK)){
                if(s->async.discarded_release && navigation_press.claimed)printf("navigation stale release consumed\n");
                (void)pn_tap_press_feed(&navigation_press,0,-1,true,&navigation);(void)pn_tap_press_feed(&navigation_press,0,-1,true,&navigation);}}
        else if(got && ((event.type==SDL_MOUSEMOTION && (event.motion.state&SDL_BUTTON_LMASK)) || ((event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP) && event.button.button==SDL_BUTTON_LEFT))){
            bool motion=event.type==SDL_MOUSEMOTION,up=event.type==SDL_MOUSEBUTTONUP;int x=motion?event.motion.x:event.button.x,y=motion?event.motion.y:event.button.y;
            navigation_consumed=pn_tap_press_feed(&navigation_press,up?0u:1u,root_navigation_hit(s,x,y),true,&navigation);
            if(up)navigation_consumed=pn_tap_press_feed(&navigation_press,0,-1,true,&navigation) || navigation_consumed;
            if(navigation>=0){
                if(root_navigation_merge(s,&navigation_press,&event,&navigation)!=PN_OK){feedback_restore(s);s->feedback_tap=(pn_tap_t){0};s->pointer_down=false;tap=(pn_tap_t){0};pn_reader_input_cancel(&input);continue;}
                // 已确认的导航沿原松手分支执行一次；真实松手仍由独立所有权消费。/ Execute confirmed navigation once through its existing release route; separate ownership consumes the real release.
                feedback_restore(s);s->feedback_tap=(pn_tap_t){0};tap=(pn_tap_t){0};pn_reader_input_cancel(&input);s->pointer_down=navigation_press.down || navigation_press.blocked;
                event.type=SDL_MOUSEBUTTONUP;event.button.type=SDL_MOUSEBUTTONUP;navigation_selected=true;printf("navigation press hit=%d\n",navigation);}}
        else if(!got && !(atomic_load(&s->async.mouse)&SDL_BUTTON_LMASK)){
            (void)pn_tap_press_feed(&navigation_press,0,-1,true,&navigation);(void)pn_tap_press_feed(&navigation_press,0,-1,true,&navigation);}
        if(navigation_consumed)shelf_gesture=(pn_shelf_gesture_t){0};
        if(navigation_consumed && !navigation_selected){feedback_restore(s);s->feedback_tap=(pn_tap_t){0};s->pointer_down=false;tap=(pn_tap_t){0};pn_reader_input_cancel(&input);continue;}
        {bool active=!s->entertainment && !reading(s) && !s->locked && !s->hub && !s->index && !s->search_open && !s->actions_mode && !s->settings.impl && !s->wallpaper.impl && !s->font_manage.impl;
         int command=-1;bool consumed=false;
         bool pointer=got && ((event.type==SDL_MOUSEMOTION && (event.motion.state&SDL_BUTTON_LMASK)) || ((event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP) && event.button.button==SDL_BUTTON_LEFT));
         if(!navigation_selected && pointer){bool motion=event.type==SDL_MOUSEMOTION,up=event.type==SDL_MOUSEBUTTONUP;int x=motion?event.motion.x:event.button.x,y=motion?event.motion.y:event.button.y,book=active?shelf_hit(s,x,y):-1;
             consumed=pn_shelf_gesture_feed(&shelf_gesture,active,!s->home,1,x,y,true,book>=0 && book<(int)s->page->count?book:-1,now,&command);
             if(up){consumed=pn_shelf_gesture_feed(&shelf_gesture,active,!s->home,0,x,y,true,-1,now,&command) || consumed;consumed=pn_shelf_gesture_feed(&shelf_gesture,active,!s->home,0,x,y,true,-1,now,&command) || consumed;}}
         else if(!got && shelf_gesture.down && (atomic_load(&s->async.mouse)&SDL_BUTTON_LMASK))consumed=pn_shelf_gesture_feed(&shelf_gesture,active,!s->home,1,shelf_gesture.last_x,shelf_gesture.last_y,true,shelf_gesture.hit,now,&command);
         else if(got && event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST)shelf_gesture=(pn_shelf_gesture_t){.blocked=(atomic_load(&s->async.mouse)&SDL_BUTTON_LMASK)!=0};
         if(command>=PN_SHELF_MORE && command<PN_SHELF_MORE+PN_CATALOG_PAGE_MAX){open_actions(s,command-PN_SHELF_MORE);printf("shelf long press book=%d\n",command-PN_SHELF_MORE);}
         else if((command==PN_SHELF_NEXT && s->page->more) || (command==PN_SHELF_PREVIOUS && s->page->index>0)){status=page(s,command==PN_SHELF_PREVIOUS,false);printf("shelf swipe command=%d index=%zu status=%d\n",command,s->page->index,(int)status);}
         if(consumed){feedback_restore(s);s->feedback_tap=(pn_tap_t){0};tap=(pn_tap_t){0};pn_reader_input_cancel(&input);s->pointer_down=pointer && event.type!=SDL_MOUSEBUTTONUP;continue;}}
        if(got && s->follow_enabled && (s->font_manage.impl || s->toc.active) && (((event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP) && event.button.button==SDL_BUTTON_LEFT) || (event.type==SDL_MOUSEMOTION && (event.motion.state&SDL_BUTTON_LMASK)) || (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST))){
            unsigned count=event.type==SDL_MOUSEBUTTONUP || event.type==SDL_WINDOWEVENT?0u:1u;int px=event.type==SDL_MOUSEMOTION?event.motion.x:event.button.x,py=event.type==SDL_MOUSEMOTION?event.motion.y:event.button.y;
            bool consumed=s->toc.active?pn_toc_ui_pointer(&s->toc,count,px,py,event.type!=SDL_WINDOWEVENT,now,present,s,&status):pn_font_manage_pointer(&s->font_manage,count,px,py,event.type!=SDL_WINDOWEVENT,now,present,s,&status);
            if(consumed){
                bool active=s->toc.active?pn_toc_ui_motion_stats(&s->toc).active:pn_font_manage_motion_stats(&s->font_manage).active;
                if(event.type==SDL_MOUSEBUTTONDOWN && !active)(void)feedback_event(s,&event);
                if(!count || event.type==SDL_WINDOWEVENT){feedback_restore(s);s->feedback_tap=(pn_tap_t){0};s->feedback_release=false;}
                cancel_pointer(&input,&tap,true);if(s->toc.active)(void)pn_toc_ui_tick(&s->toc,now);else (void)pn_font_manage_tick(&s->font_manage,now);continue;
            }
        }
        if(got && !navigation_selected && !feedback_event(s,&event))continue;

        if(reading(s)){status=s->epub.impl?pn_epub_app_tick(&s->epub,now):pn_reader_app_tick(&s->reader,now);if(status!=PN_OK && status!=PN_BUSY)pn_sdl_async_title(&s->async,PN_BRAND_NAME " - 保存失败，关闭前请重试");}
        if(s->bookmarks.mode!=PN_BUI_CLOSED && !s->bookmarks.presented && now-s->ui_retry>=1000){s->ui_retry=now;status=pn_bookmark_ui_present(&s->bookmarks,present,s);}
        // 页面请求不等待下一次输入，空闲轮询也完成导航。/ Navigation requests complete during idle polling without waiting for another input.
        if(!s->locked && s->styles.request_fonts){s->styles.request_fonts=false;status=pn_font_ui_open(&s->fonts,s->pool,s->epub.impl?NULL:&s->reader,s->epub.impl?&s->epub:NULL,s->font_dir,present,s);pn_font_ui_return_to(&s->fonts,pn_style_ui_render,&s->styles);printf("font_ui open status=%d active=%d\n",status,s->fonts.active);}
        if(!s->locked && s->minesweeper.active){
            (void)pn_minesweeper_tick(&s->minesweeper,now);
            if(!s->minesweeper.presented && now-s->ui_retry>=100)(void)minesweeper_draw(s,false);
            if(!got)continue;
        }
        if(!s->locked && s->sokoban.active){
            (void)pn_sokoban_tick(&s->sokoban,now);
            if(!s->sokoban.presented && now-s->ui_retry>=100)(void)sokoban_draw(s,false);
            if(!got)continue;
        }
        if(!s->locked && s->sudoku.active){
            (void)pn_sudoku_tick(&s->sudoku,now);
            if(!s->sudoku.presented && now-s->ui_retry>=100)(void)sudoku_draw(s);
            if(!got)continue;
        }
        if(!got){bool body=reading(s) && !s->toolbar && !s->styles.active && !s->fonts.active && !s->toc.active && !s->jumpui.active && s->bookmarks.mode==PN_BUI_CLOSED;
            if(!s->locked && !s->pointer_down && !s->feedback_tap.down && atomic_load(&s->async.mouse)==0 && now>=font_activity && now-font_activity>=800){pn_status_t sampled=pn_font_manage_samples_idle(&s->font_manage,now,present,s);if(sampled==PN_OK)printf("font samples presented\n");}
            (void)pn_font_manage_tick(&s->font_manage,now);
            (void)pn_toc_ui_tick(&s->toc,now);
            (void)feedback_idle_tick(s,now,s->locked || body || moving.active || s->pointer_down || s->feedback_tap.down || atomic_load(&s->async.mouse)!=0);
            if(s->reader_turn_pending && (!body || s->locked)){s->reader_turn_pending=false;s->async.defer_ack=false;}
            if(s->reader_turn_pending && !s->pointer_down && atomic_load(&s->async.mouse)==0){status=active_step(s,PN_APP_NEXT,now,present,s);prefetch_work=true;if(status!=PN_BUSY){printf("library_turn pointer=0 status=%d\n",(int)status);pn_sdl_async_title(&s->async,status==PN_OK?PN_BRAND_NAME:PN_BRAND_NAME " - 操作未完成，请重试");}continue;}
            if(!s->locked && body && !s->pointer_down && atomic_load(&s->async.mouse)==0){
                // 与设备空闲路径使用相同步数和软预算。/ Match the device idle step count and soft budget.
                uint64_t batch_started=SDL_GetTicks64();unsigned steps=0;
                do{status=s->epub.impl?pn_epub_app_prefetch(&s->epub):pn_reader_app_prefetch(&s->reader);}
                while(s->epub.impl && status==PN_OK && ++steps<32u && SDL_GetTicks64()-batch_started<8u);
                prefetch_work=status==PN_OK;if(status==PN_OK)printf("neighbor prefetch work\n");
                pn_status_t resumed;steps=0;batch_started=SDL_GetTicks64();
                do{resumed=s->epub.impl?pn_epub_app_resume_cache_idle(&s->epub):pn_reader_app_resume_cache_idle(&s->reader);}
                while(resumed==PN_BUSY && ++steps<32u && SDL_GetTicks64()-batch_started<8u);
                prefetch_work=prefetch_work || resumed==PN_BUSY;pn_status_t cached=resumed==PN_EMPTY && s->epub.impl && (pn_epub_app_cache_pending(&s->epub) || !pn_epub_app_prefetch_pending(&s->epub))?pn_epub_app_cache_idle(&s->epub,now):PN_EMPTY;if(cached==PN_OK)printf("semantic cache stored\n");
                if(now>=font_activity && now-font_activity>=1500u){pn_status_t recent=s->epub.impl?pn_epub_app_recent_idle(&s->epub,now):pn_reader_app_recent_idle(&s->reader,now);if(recent==PN_OK)printf("recent record stored when idle\n");
                    if(!(s->epub.impl && pn_epub_app_bootstrap_pending(&s->epub)) && !(s->epub.impl?pn_epub_app_resume_cache_pending(&s->epub):pn_reader_app_resume_cache_pending(&s->reader)) && !(s->epub.impl?pn_epub_app_prefetch_pending(&s->epub):pn_reader_app_warm_pending(&s->reader)) && (s->epub.impl?pn_epub_app_index_pending(&s->epub):pn_reader_app_index_pending(&s->reader))){pn_book_index_state_t progress;pn_status_t indexed;unsigned steps=0;uint64_t began=SDL_GetTicks64();do{indexed=s->epub.impl?pn_epub_app_index_step(&s->epub,&progress):pn_reader_app_index_step(&s->reader,&progress);}while(indexed==PN_BUSY && ++steps<PN_BOOK_INDEX_STEP_MAX && SDL_GetTicks64()-began<8);prefetch_work=prefetch_work || indexed==PN_BUSY;if(indexed==PN_OK){profile_index_flush();printf("book_index background complete pages=%u\n",progress.pages);}}
}}
            if(!s->locked && s->styles.active && !s->pointer_down && atomic_load(&s->async.mouse)==0)(void)pn_style_ui_tick(&s->styles,present,s);
            if(!s->locked && s->wallpaper.impl && !s->pointer_down && atomic_load(&s->async.mouse)==0)(void)pn_wallpaper_ui_tick(&s->wallpaper,present,s);
            if(!s->locked && !reading(s) && !s->pointer_down && atomic_load(&s->async.mouse)==0 && !s->hub && !s->index && !s->search_open && !s->actions_mode && !s->wallpaper.impl && !s->font_manage.impl && !s->settings.impl && now>=font_activity && now-font_activity>=80){
                // 与设备一致：真实输入已处理，安静后才到期释放，不为释放新写缓存。/ Match the device: expire only after real input is handled and quiet, without writing caches just to release them.
                if(now-font_activity>=1500)(void)pn_hot_session_expire(&s->hot,now);
                if(s->home && !s->reading_stats.loaded && s->reading_stats.media && !s->covers_dirty && s->covers && !pn_shelf_covers_pending(s->covers,s->page) && now-font_activity>=1500u && now>=s->home_stats_retry){s->home_stats_retry=now+3000u;
                    if(pn_reading_stats_load(&s->reading_stats)==PN_OK){if(draw(s)!=PN_OK)s->covers_dirty=true;printf("home statistics ready\n");}}
                if(now-font_activity>=800){
                    if(pn_hot_session_deferred_pending(&s->hot)){pn_status_t saved=pn_hot_session_deferred_idle(&s->hot,profile_clock,NULL);if(saved!=PN_OK && saved!=PN_BUSY && saved!=PN_EMPTY && saved!=PN_CANCELLED)printf("deferred save status=%d\n",(int)saved);}
                    else if(!s->entertainment){pn_status_t indexed=catalog_build_idle(s);if(indexed==PN_OK)printf("catalog index built\n");}
                }
                if(!s->entertainment && !pn_hot_session_checkpoint_pending(&s->hot)){covers_tick(s,now-font_activity>=250);if(!pn_hot_session_deferred_pending(&s->hot))prepare_continue(s,now,font_activity);}}
            continue;}

        if(!s->minesweeper.active && !s->sokoban.active && !s->sudoku.active && !s->locked && management_event(s,&event,now)){cancel_pointer(&input,&tap,true);continue;}
        if(s->locked){
            if(event.type==SDL_QUIT){running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat && (event.key.keysym.sym==SDLK_l || event.key.keysym.sym==SDLK_RETURN || event.key.keysym.sym==SDLK_ESCAPE)){
                s->locked=false;
                if(s->minesweeper.active)status=minesweeper_draw(s,false);
                else if(s->sokoban.active)status=sokoban_draw(s,false);
                else if(s->sudoku.active)status=sudoku_draw(s);
                else if(reading(s))status=s->epub.impl?pn_epub_app_resume(&s->epub,now,present,s):pn_reader_app_resume(&s->reader,now,present,s);
                else status=s->last_read_known?open_item(s,&s->last_read,now):draw(s);
                printf("lock_preview wake status=%d\n",(int)status);}
            continue;
        }
        if(s->minesweeper.active){
            int command=0;
            if(event.type==SDL_QUIT){status=pn_minesweeper_pause(&s->minesweeper,now);if(status==PN_OK)running=false;else (void)minesweeper_draw(s,false);continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat){
                SDL_Keycode key=event.key.keysym.sym;
                if(key==SDLK_l){status=lock_preview(s,now);continue;}
                if(key==SDLK_ESCAPE)command=PN_MI_BACK;
                else if(key==SDLK_RETURN)command=pn_minesweeper_screen(&s->minesweeper)==PN_MI_SELECT?PN_MI_START:PN_MI_EXIT;
                else if(key==SDLK_f){const pn_mi_state_t *q=pn_minesweeper_state(&s->minesweeper);command=q && q->flag_mode?PN_MI_REVEAL_MODE:PN_MI_FLAG_MODE;}
                else if(key==SDLK_r)command=PN_MI_NEW;
                else if(key==SDLK_c)command=PN_MI_CLEAN;
                else if(key>=SDLK_1 && key<=SDLK_3)command=PN_MI_TIER+(int)(key-SDLK_1);
            }else if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION){
                bool motion=event.type==SDL_MOUSEMOTION,up=event.type==SDL_MOUSEBUTTONUP;int x=motion?event.motion.x:event.button.x,y=motion?event.motion.y:event.button.y;
                if(event.type==SDL_MOUSEBUTTONDOWN)s->pointer_down=true;
                if(s->pointer_down){int target=pn_minesweeper_hit(&s->minesweeper,x,y);(void)pn_tap_feed(&tap,1,target>0?target:-1,true,&command);if(up){(void)pn_tap_feed(&tap,0,-1,true,&command);(void)pn_tap_feed(&tap,0,-1,true,&command);s->pointer_down=false;}}
            }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){cancel_pointer(&input,&tap,true);s->pointer_down=false;}
            if(command>0){status=pn_minesweeper_event(&s->minesweeper,command,now);printf("minesweeper command=%d status=%d\n",command,status);if(!s->minesweeper.active){pn_minesweeper_close(&s->minesweeper);status=game_return_to_entertainment(s);}else if(!s->minesweeper.presented)status=minesweeper_draw(s,command==PN_MI_CLEAN);}
            continue;
        }
        if(s->sokoban.active){
            int command=0;
            if(event.type==SDL_QUIT){status=pn_sokoban_pause(&s->sokoban,now);if(status==PN_OK)running=false;else (void)sokoban_draw(s,false);continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat){
                SDL_Keycode key=event.key.keysym.sym;
                if(key==SDLK_l){status=lock_preview(s,now);continue;}
                if(key==SDLK_ESCAPE)command=PN_SO_BACK;
                else if(key==SDLK_RETURN)command=pn_sokoban_screen(&s->sokoban)==PN_SO_SELECT?PN_SO_START:PN_SO_EXIT;
                else if(key==SDLK_UP)command=PN_SO_UP;else if(key==SDLK_RIGHT)command=PN_SO_RIGHT;else if(key==SDLK_DOWN)command=PN_SO_DOWN;else if(key==SDLK_LEFT)command=PN_SO_LEFT;
                else if(key==SDLK_z || key==SDLK_BACKSPACE)command=PN_SO_UNDO_MOVE;
                else if(key==SDLK_r)command=PN_SO_RESTART;else if(key==SDLK_c)command=PN_SO_CLEAN;else if(key==SDLK_p)command=PN_SO_CHOOSE;
                else if(key>=SDLK_1 && key<=SDLK_3)command=PN_SO_TIER+(int)(key-SDLK_1);
            }else if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION){
                bool motion=event.type==SDL_MOUSEMOTION,up=event.type==SDL_MOUSEBUTTONUP;int x=motion?event.motion.x:event.button.x,y=motion?event.motion.y:event.button.y;
                if(event.type==SDL_MOUSEBUTTONDOWN)s->pointer_down=true;
                if(s->pointer_down){(void)pn_sokoban_touch(&s->sokoban,1,x,y,true);if(up){command=pn_sokoban_touch(&s->sokoban,0,x,y,true);s->pointer_down=false;}}
            }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){(void)pn_sokoban_touch(&s->sokoban,0,0,0,false);(void)pn_sokoban_touch(&s->sokoban,0,0,0,true);s->pointer_down=false;}
            if(command>0){status=pn_sokoban_event(&s->sokoban,command,now);printf("sokoban command=%d status=%d\n",command,status);if(!s->sokoban.active){pn_sokoban_close(&s->sokoban);status=game_return_to_entertainment(s);}else if(!s->sokoban.presented)status=sokoban_draw(s,command==PN_SO_CLEAN);}
            continue;
        }
        if(s->sudoku.active){
            int command=0;
            if(event.type==SDL_QUIT){status=pn_sudoku_pause(&s->sudoku,now);if(status==PN_OK)running=false;else (void)sudoku_draw(s);continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat){
                SDL_Keycode key=event.key.keysym.sym;
                if(key==SDLK_l){status=lock_preview(s,now);continue;}
                if(key==SDLK_ESCAPE)command=PN_SU_BACK;
                else if(key>=SDLK_1 && key<=SDLK_9)command=PN_SU_DIGIT+(int)(key-SDLK_0);
                else if(key==SDLK_BACKSPACE || key==SDLK_DELETE)command=PN_SU_ERASE;
                else if(key==SDLK_n)command=PN_SU_NOTES;
                else if(key==SDLK_z)command=PN_SU_UNDO;
                else if(key==SDLK_RETURN)command=PN_SU_EXIT;
                else if(key==SDLK_LEFT || key==SDLK_RIGHT || key==SDLK_UP || key==SDLK_DOWN){const pn_sudoku_state_t *q=pn_sudoku_state(&s->sudoku);if(q)command=PN_SU_CELL+(q->selected+81+(key==SDLK_LEFT?-1:key==SDLK_RIGHT?1:key==SDLK_UP?-9:9))%81;}
            }else if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION){
                bool motion=event.type==SDL_MOUSEMOTION,up=event.type==SDL_MOUSEBUTTONUP;int x=motion?event.motion.x:event.button.x,y=motion?event.motion.y:event.button.y;
                if(event.type==SDL_MOUSEBUTTONDOWN)s->pointer_down=true;
                if(s->pointer_down){int target=pn_sudoku_hit(&s->sudoku,x,y);(void)pn_tap_feed(&tap,1,target>0?target:-1,true,&command);if(up){(void)pn_tap_feed(&tap,0,-1,true,&command);(void)pn_tap_feed(&tap,0,-1,true,&command);s->pointer_down=false;}}
            }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){cancel_pointer(&input,&tap,true);s->pointer_down=false;}
            if(command>0){status=pn_sudoku_event(&s->sudoku,command,now);printf("sudoku command=%d status=%d dirty=%d\n",command,status,pn_sudoku_dirty(&s->sudoku));if(!s->sudoku.active){pn_sudoku_close(&s->sudoku);status=game_return_to_entertainment(s);}else if(!s->sudoku.presented)status=sudoku_draw(s);}
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
                pn_media_lease_t lease;status=pn_hot_session_drop_for_switch(&s->hot);if(status==PN_OK)status=pn_media_acquire(&s->media,PN_MEDIA_WRITE,&lease);
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
            if(command>=0){status=pn_settings_ui_event(&s->settings,command,present,s);if(command==PN_SETUI_FULL_REFRESH && status==PN_OK){pn_reader_app_refresh_reset(&s->reader);pn_epub_app_refresh_reset(&s->epub);}s->input_flags=s->settings.flags;s->index_wait=s->settings.index_wait;s->shelf_prefs=s->settings.shelf;printf("settings command=%d status=%d flags=%u active=%d\n",command,(int)status,(unsigned)s->settings.flags,(int)s->settings.active);
                // PC没有无线：连接测试回报不能测试，用户可选择仍然保存。/ The PC has no radio: report the test as unsupported so the user may save anyway.
                if(s->settings.request==PN_SETUI_WIFI_TEST || s->settings.request==PN_SETUI_WIFI_TEST_CANCEL){int test=s->settings.request;s->settings.request=0;status=pn_settings_ui_wifi_result(&s->settings,test==PN_SETUI_WIFI_TEST?PN_UNSUPPORTED:PN_CANCELLED,present,s);printf("settings wifi_test status=%d\n",(int)status);}
                int request=s->settings.request;
                if(request || !s->settings.active){pn_settings_ui_close(&s->settings);
                    s->back_to_settings=(request==PN_SETUI_FONTS && s->font_dir) || (request==PN_SETUI_WALLPAPER && s->wallpaper_dir);
                    if(request==PN_SETUI_TRANSFER)status=open_hub(s);
                    else if(request==PN_SETUI_FONTS && s->font_dir)status=open_font_management(s);
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
            else if(tool==PN_TOOL_TOC){s->toolbar=false;status=open_toc(s);toc_report(&s->toc,0,status);}
            else if(tool==PN_TOOL_BOOKMARKS){s->toolbar=false;status=s->epub.impl?pn_bookmark_ui_open_epub(&s->bookmarks,&s->epub,present,s):pn_bookmark_ui_open(&s->bookmarks,&s->reader,present,s);printf("bookmark_ui open status=%d mode=%d count=%zu\n",(int)status,(int)s->bookmarks.mode,s->bookmarks.count);pn_sdl_async_text(&s->async,false);}
            else if(tool==PN_TOOL_JUMP){s->toolbar=false;status=s->epub.impl?pn_jump_ui_open_epub(&s->jumpui,&s->epub,present,s):pn_jump_ui_open(&s->jumpui,&s->reader,present,s);}
            else if(tool==PN_TOOL_TYPESET){s->toolbar=false;status=s->epub.impl?pn_style_ui_open_epub(&s->styles,&s->epub,present,s):pn_style_ui_open(&s->styles,&s->reader,present,s);printf("style_ui open status=%d pixels=%u\n",(int)status,s->styles.draft.pixels);}
            else if(tool==PN_TOOL_SHELF){s->toolbar=false;status=return_to_shelf(s,now);}
            cancel_pointer(&input,&tap,(atomic_load(&s->async.mouse)&SDL_BUTTON_LMASK)==0);
            continue;
        }
        if(s->entertainment && !s->home && event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_ESCAPE){s->entertainment=s->entertainment==2?1:0;status=page(s,false,true);continue;}
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
                if(key==SDLK_2 || key==SDLK_F10){
                    pn_key_reading_command_t command=pn_key_reading_middle(key==SDLK_F10?PN_KEY_LONG:PN_KEY_SHORT);
                    if(command==PN_KEY_READING_HOME){s->toolbar=false;s->home=true;status=return_to_shelf(s,now);printf("reading middle home status=%d\n",(int)status);}
                    else if(command==PN_KEY_READING_SETTINGS){status=toolbar_show(s);printf("reading middle long settings status=%d\n",(int)status);}
                    continue;
                }
                if(key==SDLK_g){status=s->epub.impl?pn_jump_ui_open_epub(&s->jumpui,&s->epub,present,s):pn_jump_ui_open(&s->jumpui,&s->reader,present,s);printf("jump_ui open status=%d\n",(int)status);continue;}
                if(s->bookmarks.mode!=PN_BUI_CLOSED)continue;
                if(key==SDLK_t && s->epub.impl){status=open_toc(s);toc_report(&s->toc,0,status);continue;}
                if(key==SDLK_m){status=s->epub.impl?pn_bookmark_ui_open_epub(&s->bookmarks,&s->epub,present,s):pn_bookmark_ui_open(&s->bookmarks,&s->reader,present,s);printf("bookmark_ui open status=%d mode=%d count=%zu\n",(int)status,(int)s->bookmarks.mode,s->bookmarks.count);pn_sdl_async_text(&s->async,false);continue;}
                if(key==SDLK_s){status=s->epub.impl?pn_style_ui_open_epub(&s->styles,&s->epub,present,s):pn_style_ui_open(&s->styles,&s->reader,present,s);printf("style_ui open status=%d pixels=%u\n",(int)status,s->styles.draft.pixels);continue;}
                if(key==SDLK_k){status=toolbar_show(s);continue;}
                if(key==SDLK_u){status=s->epub.impl?pn_epub_app_bookmark_return(&s->epub,now,present,s):pn_reader_app_bookmark_return(&s->reader,now,present,s);printf("bookmark_return status=%d\n",(int)status);continue;}
                if(key==SDLK_RIGHT || key==SDLK_PAGEDOWN || key==SDLK_SPACE){action=PN_APP_NEXT;turn=true;}
                else if(key==SDLK_LEFT || key==SDLK_PAGEUP){action=PN_APP_PREVIOUS;turn=true;}
                else if(key==SDLK_PLUS || key==SDLK_EQUALS){action=PN_APP_LARGER;turn=true;}
                else if(key==SDLK_MINUS){action=PN_APP_SMALLER;turn=true;}
            }else{
                if(key==SDLK_h || key==SDLK_b){s->entertainment=0;s->home=key==SDLK_h;status=page(s,false,true);printf("navigation root=%s status=%d\n",s->home?"home":"library",(int)status);continue;}
                if(s->entertainment){
                    if(key==SDLK_RETURN || key==SDLK_2){status=sudoku_open(s);}
                    else if(key==SDLK_3){status=sokoban_open(s);}
                    else if(key==SDLK_4){status=minesweeper_open(s);}
                    else if(key==SDLK_1){s->entertainment=s->entertainment==2?1:0;status=page(s,false,true);}
                    continue;
                }
                if(key==SDLK_j && !s->recent_mode){status=pn_shelf_index_render(&s->font,&s->frame);if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GL16);s->index=status==PN_OK;printf("index open status=%d\n",(int)status);continue;}
                if(key==SDLK_p){status=open_settings(s);printf("settings open status=%d flags=%u\n",(int)status,(unsigned)s->settings.flags);continue;}
                if(key==SDLK_f && s->font_dir){status=open_font_management(s);printf("font_manage open status=%d\n",(int)status);continue;}
                if(key==SDLK_w && s->wallpaper_dir){status=wallpaper_open(s);printf("wallpaper_ui open status=%d\n",(int)status);continue;}
                if(key==SDLK_r){s->entertainment=0;s->home=false;s->recent_mode=!s->recent_mode;*s->query=0;status=page(s,false,true);continue;}
                if(key==SDLK_c){status=continue_book(s,now);continue;}
                if(key==SDLK_RETURN){hit=s->home?(s->page->count?(s->shelf_focus_visible?s->selected:PN_SHELF_CONTINUE):PN_SHELF_HOME):s->selected;commit=true;}
                else if(key==SDLK_PAGEDOWN){hit=PN_SHELF_NEXT;commit=true;}
                else if(key==SDLK_PAGEUP){hit=PN_SHELF_PREVIOUS;commit=true;}
                else if(key==SDLK_DOWN || key==SDLK_UP){int next=s->selected+(s->shelf_focus_visible?(key==SDLK_DOWN?1:-1):0);if(next>=0 && (size_t)next<s->page->count){int before=s->shelf_focus_visible?s->selected:-1;s->selected=next;s->shelf_focus_visible=true;status=draw_focus(s,before);}}
            }
        }else if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION){
            if(!reading(s) && event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_RIGHT){int h=shelf_hit(s,event.button.x,event.button.y);if(h>=0 && h<PN_CATALOG_PAGE_MAX)open_actions(s,h);continue;} // 右键=长按 / Right click = long press
            bool motion=event.type==SDL_MOUSEMOTION;
            if((motion && !(event.motion.state&SDL_BUTTON_LMASK)) || (!motion && event.button.button!=SDL_BUTTON_LEFT))continue;
            if(event.type==SDL_MOUSEBUTTONDOWN)s->pointer_down=true;
            // 批次已释放也执行这次已确认导航；真正持有仍由独立底栏所有权保留。
            // Execute confirmed navigation even after a drained release; separate navigation ownership still preserves a real hold.
            if(!s->pointer_down && !navigation_selected)continue;
            int x=motion?event.motion.x:event.button.x,y=motion?event.motion.y:event.button.y;

            int target=s->styles.active?pn_style_ui_hit(&s->styles,x,y):s->bookmarks.mode!=PN_BUI_CLOSED?pn_bookmark_ui_hit(&s->bookmarks,x,y):reading(s)?(y>=1144 && x<420 && (s->epub.impl?pn_epub_app_bookmark_can_return(&s->epub):pn_reader_app_bookmark_can_return(&s->reader))?11:y>=1144 && x>=420?14:-1):shelf_hit(s,x,y);
            (void)pn_tap_feed(&tap,1,target,true,&hit);
            if(reading(s) && s->bookmarks.mode==PN_BUI_CLOSED && !s->styles.active)(void)pn_reader_input_feed(&input,1,x,y,true,&action);
            if(event.type==SDL_MOUSEBUTTONUP){
                s->pointer_down=navigation_selected && (navigation_press.down || navigation_press.blocked);
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
            else if(reading(s) && hit==13 && s->epub.impl){status=open_toc(s);toc_report(&s->toc,0,status);turn=false;}
            else if(reading(s) && hit==8){status=return_to_shelf(s,now);turn=false;}
            else if(!reading(s)){
                if(hit==PN_SHELF_TAB_ENTERTAINMENT || hit==PN_SHELF_ENTERTAINMENT_BACK || hit==PN_SHELF_SUDOKU || hit==PN_SHELF_SOKOBAN || hit==PN_SHELF_MINESWEEPER){s->home=false;s->entertainment=1;status=hit==PN_SHELF_SUDOKU?sudoku_open(s):hit==PN_SHELF_SOKOBAN?sokoban_open(s):hit==PN_SHELF_MINESWEEPER?minesweeper_open(s):page(s,false,true);}
                else if(hit==PN_SHELF_READING_HOME){s->home=true;status=page(s,false,true);}
                else if(hit==PN_SHELF_LAYOUT){s->list_mode=!s->list_mode;status=page(s,false,true);}
                else if(hit==PN_SHELF_TOGGLE){s->recent_mode=!s->recent_mode;*s->query=0;status=page(s,false,true);}
                else if(hit==PN_SHELF_SEARCH && *s->query && !s->recent_mode){*s->query=0;status=page(s,false,true);}
                else if(hit==PN_SHELF_SEARCH){pn_search_ui_open(&s->search,NULL);s->search_open=true;status=pn_search_ui_render(&s->search,&s->font,&s->frame);if(status==PN_OK)status=present(s,&s->frame,PN_REFRESH_GL16);}
                else if(hit==PN_SHELF_TAB_ALL || hit==PN_SHELF_TAB_RECENT || hit==PN_SHELF_HOME || hit==PN_SHELF_TAB_FAVORITES){bool want=hit==PN_SHELF_TAB_RECENT,fav=hit==PN_SHELF_TAB_FAVORITES;if(hit==PN_SHELF_HOME || s->entertainment || want!=s->recent_mode || fav!=s->fav_mode){s->entertainment=0;s->home=false;s->recent_mode=want;s->fav_mode=fav;s->fav_start=0;*s->query=0;status=page(s,false,true);}}
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
        if(status!=PN_OK && status!=PN_EMPTY && status!=PN_CANCELLED && !(status==PN_BUSY && s->reader_turn_pending)){
            bool confirmed=turn && reading(s) && (s->epub.impl?pn_epub_app_last_confirmed(&s->epub):pn_reader_app_last_confirmed(&s->reader));
            pn_sdl_async_title(&s->async,confirmed?PN_BRAND_NAME " - 当前页已显示，保存失败，请重试":PN_BRAND_NAME " - 操作未完成，请重试");
        }
    }
cleanup:;
    pn_cancel_scope_t shield;pn_cancel_enter(&shield,NULL,NULL);
    // 最终关窗保留原检查点收口；前台交接仍不新增可丢页包。/ Preserve checkpoint finalization on final window shutdown; foreground handoffs still avoid new disposable packets.
    (void)pn_hot_session_drop(&s->hot);
    (void)pn_feedback_discard(&s->feedback);
    (void)active_close(s,SDL_GetTicks64());
    pn_minesweeper_close(&s->minesweeper);
    pn_sokoban_close(&s->sokoban);
    pn_sudoku_close(&s->sudoku);
    pn_catalog_index_close(&s->catalog_index);
    pn_wallpaper_mask_close(&s->lock_mask);
    pn_wallpaper_ui_close(&s->wallpaper);pn_font_manage_close(&s->font_manage);pn_settings_ui_close(&s->settings);
    pn_font_ui_close(&s->fonts);
    pn_cover_work_abort(&s->cover_work);pn_w_font_scope_end(&s->ui_scope);pn_cache_detach(&s->cover_cache);pn_font_close(&s->font);pn_free(s->frame.pixels);pn_free(s->page_px);pn_free(s->favorites);pn_free(s->page);pn_free(s->recent);pn_free(s->covers);
    (void)pn_font_system_close();
    profile_index_flush();free(s->argb);s->argb=NULL;pn_cancel_leave(&shield);return result;
}

int pn_sim_library_window(pn_pool_t *pool,const char *directory,const char *font_path,const char *fallback_path,const char *state_dir,const char *cover_cache,const char *wallpaper_dir,const char *wallpaper_store,const char *font_dir){
    library_t *s=pn_alloc_tagged(pool,PN_MEM_UI,sizeof *s);if(!s)return 1;memset(s,0,sizeof *s);
    s->pool=pool;s->directory=directory;s->font_path=font_path;s->fallback_path=fallback_path;s->state_dir=state_dir;s->cover_argument=cover_cache;s->wallpaper_dir=wallpaper_dir;s->wallpaper_store_argument=wallpaper_store;s->font_dir=font_dir;
    const char *follow=getenv("PN_SIM_FOLLOW"),*trace=getenv("PN_SIM_FOLLOW_TRACE"),*fail=getenv("PN_SIM_FOLLOW_FAIL_ONCE");s->follow_enabled=follow && *follow=='1';s->follow_trace=trace && *trace=='1';s->follow_fail=fail && *fail=='1';
    if(!pn_sdl_async_init(&s->async,present_raw_main,s)){pn_sdl_async_close(&s->async);pn_free(s);return 1;}
    s->async.feedback=feedback_raw_main;const char *feedback=getenv("PN_SIM_FEEDBACK");s->feedback_enabled=feedback && *feedback=='1';
    s->window=s->async.window;s->renderer=s->async.renderer;s->texture=s->async.texture;
    int result=pn_sdl_async_run(&s->async,library_worker,s);
    printf("async mailbox coalesced=%llu dropped=%llu\n",(unsigned long long)s->async.input.coalesced,(unsigned long long)s->async.input.dropped);
    pn_sdl_async_close(&s->async);pn_free(s);return result;
}
