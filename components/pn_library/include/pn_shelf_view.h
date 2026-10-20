/* 书目列表的同源绘制与命中。/ Shared catalog-list drawing and hit testing. */
#pragma once
#include "pn_cover.h"
#include "pn_book_actions.h"
#include "pn_font.h"
#include "pn_favorites.h"
#include "pn_shelf_prefs.h"
#include "pn_reading_stats.h"
#define PN_SHELF_PREVIOUS 40
#define PN_SHELF_NEXT 41
#define PN_SHELF_TOGGLE 43
#define PN_SHELF_READING_HOME 42 ///< 独立阅读首页 / Independent reading home
#define PN_SHELF_CONTINUE 44 ///< 点继续阅读卡 / Tap the continue-reading card
/// 绘制六行文字/无封面卡，纯绘制，不打开书籍或修改状态。
/// Draw six text/cover-fallback rows only, without opening books or changing state.
pn_status_t pn_shelf_render(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame);
/// 命中已存在的条目或导航，否则-1；分页由内容手势输出命令。
/// Hit existing entries or navigation, otherwise -1; content gestures emit paging commands.
int pn_shelf_hit(const pn_catalog_page_t *page,int x,int y);

/// 可选键盘选中行，-1表示无选中。/ Optional keyboard-selected row; -1 means none.
pn_status_t pn_shelf_render_selected(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected);
/// 最近模式标题，绘图不查询存储。/ Recent-mode heading; painting never queries storage.
pn_status_t pn_shelf_render_mode(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent);

#define PN_SHELF_TRANSFER 14
#define PN_SHELF_MENU 15 ///< 点品牌标题打开菜单 / Tap the brand title to open the menu
#define PN_SHELF_INDEX 16 ///< 点“书架”标题或A-Z打开字母跳转 / Tap the shelf title or A-Z to open the letter index
#define PN_SHELF_HOME 17 ///< 点底栏“书架” / Tap the bottom-bar Shelf entry
#define PN_SHELF_TAB_ALL 18 ///< 点“全部” / Tap All
#define PN_SHELF_TAB_RECENT 19 ///< 点“最近” / Tap Recent
#define PN_SHELF_SEARCH 20 ///< 点“搜索” / Tap Search
#define PN_SHELF_TAB_FAVORITES 23 ///< 点“收藏” / Tap Favorites
#define PN_SHELF_MORE 24 ///< 24+序号：长按该书的操作菜单 / 24+index: long-press book actions
#define PN_SHELF_IMPORT 22 ///< 点“导入图书”（进入传书）/ Tap "import books" (opens transfer)
#define PN_SHELF_LAYOUT 21 ///< 点网格/列表切换 / Tap the grid/list toggle
/// 内容手势只输出命令，不绘制或访问存储；跨页保持至释放。/ Content gestures emit commands only, without drawing/storage; retain across page changes until release.
typedef struct {
    int x,y,last_x,last_y,hit; ///< 起点、终点和初始书籍 / Origin, endpoint and original book
    uint64_t began; ///< 单调按下时间 / Monotonic press time
    unsigned empty; ///< 释放确认样本数 / Release confirmation sample count
    bool down,blocked,claimed,moved,swipe; ///< 接触所有权与取消状态 / Contact ownership and cancellation state
} pn_shelf_gesture_t;
/// active仅普通书架内容，swipe在首页关闭；book仅0..PAGE_MAX-1。返回是否消费，command仅触发时改写。
/// Active only for ordinary shelf content; disable swipe on home. Book is 0..PAGE_MAX-1. Return consumption; change command only on activation.
bool pn_shelf_gesture_feed(pn_shelf_gesture_t *,bool active,bool swipe,unsigned count,int x,int y,bool valid,int book,uint64_t now,int *command);
/// 书架的可选外观与入口；全零（battery_percent=-1）等价于旧行为。
/// Optional shelf appearance and entries; all-zero with battery_percent=-1 matches the legacy behaviour.
typedef struct {
    bool home; ///< 首页读取实际最近快照，书库不画继续卡 / Home reads actual recent snapshots; library omits the continue card
    char home_date[40]; ///< owner准备的当地日期，空串表示未校时 / Owner-prepared local date; empty when unsynchronized
    pn_reading_summary_t reading; ///< 已加载的统计快照；known只对应继续卡同一本书 / Loaded statistics; known applies only to the Continue book
    int battery_percent; ///< 0–100，<0不显示 / 0–100, hidden when negative
    bool list_mode; ///< 列表模式（每页5行）/ List mode, five rows per page
    uint8_t columns; ///< 网格3或4列，0默认3列 / Grid with three or four columns; zero defaults to three
    uint8_t style; ///< pn_shelf_style_t静态风格 / Static pn_shelf_style_t appearance
    const pn_book_actions_t *actions; ///< 非NULL时在书架上叠画书籍操作面板（借用同一补字字体）/ When non-NULL, the book actions sheet is drawn over the shelf (sharing the fallback font)
    const pn_favorites_t *favorite_items; ///< 已验证的收藏快照，NULL不画标识 / Validated favorites snapshot; NULL hides markers
    bool favorites_tab; ///< 分类里显示“收藏” / Show Favorites among the categories
    bool favorites; ///< 当前为收藏视图 / Showing the favorites view
    bool import_tile; ///< 最后一页空格放“导入图书”卡、空书架给“导入图书”按钮 / An import tile in a free last-page slot and an import button on an empty shelf
    bool layout_toggle; ///< 显示网格/列表切换 / Show the grid/list toggle
    bool search; ///< 兼容保留，书架不提供搜索入口 / Retained for compatibility; no shelf search entry
    bool query; ///< 当前为搜索结果，标题改为“搜索结果”、入口改为“清除” / Showing search results: heading says results and the entry says Clear
} pn_shelf_options_t;
/// 同源的可见容量与样式缩略预览，纯绘制。/ Shared visible capacity and miniature style preview; drawing only.
size_t pn_shelf_capacity(const pn_shelf_options_t *options);
void pn_shelf_style_preview(pn_frame_t *frame,unsigned style,unsigned columns,int x,int y,int w,int h);
/// 带选项的绘制与命中（options可为NULL）。list_mode下每页5行，条目序号按行计。
/// Drawing and hit testing with options (may be NULL). In list mode a page has five rows and item numbers count rows.
pn_status_t pn_shelf_render_ex(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers,const pn_shelf_options_t *options);
/// 仅焦点改变时更新已确认底图；页面/封面/选项须保持相同，EMPTY免呈现，叠层返回UNSUPPORTED。不打开文件。
/// Update a confirmed base when only focus changes; page/covers/options must match, EMPTY skips presentation, and overlays return UNSUPPORTED. Never opens files.
pn_status_t pn_shelf_render_focus(const pn_catalog_page_t *,pn_font_t *,pn_frame_t *,int,int,const pn_shelf_covers_t *,const pn_shelf_options_t *);
int pn_shelf_hit_ex(const pn_catalog_page_t *page,int x,int y,const pn_shelf_options_t *options);
/// 字母跳转页：A–Z、#与返回，纯绘制。/ Letter-index page with A–Z, # and Back, paint only.
pn_status_t pn_shelf_index_render(pn_font_t *font,pn_frame_t *frame);
/// 命中返回'a'–'z'或'#'，返回键为'<'，空白0。/ Hits return 'a'–'z' or '#', '<' for Back, 0 for blank space.
char pn_shelf_index_hit(int x,int y);
/// 传输已装配的平台显式启用按钮，旧入口保持隐藏。/ Explicitly enable the button on wired platforms; legacy entries keep it hidden.
pn_status_t pn_shelf_render_mode_with_transfer(const pn_catalog_page_t *,pn_font_t *,pn_frame_t *,int,bool,bool);
int pn_shelf_hit_with_transfer(const pn_catalog_page_t *,int,int,bool);
/// 同上并绘制covers中已就绪的缩略图；covers可为NULL，纯绘制不提取封面。
/// Same as above, also drawing ready thumbnails from covers; covers may be NULL; drawing never extracts covers.
pn_status_t pn_shelf_render_covers(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers);

/// 同pn_shelf_render_covers，另按font_path打开一份字体作书名缺字回退（NULL或打开失败则只用UI字体）；绘制后立即关闭，不长期持有租约。
/// Same as pn_shelf_render_covers, additionally opening font_path as a title glyph fallback (UI font only when NULL or on failure); closed right after drawing so no lease is held long-term.
pn_status_t pn_shelf_render_with_font_file(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers,
    pn_pool_t *pool,pn_media_t *media,const char *font_path);
/// 同上，带选项。/ Same with options.
pn_status_t pn_shelf_render_with_font_file_ex(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers,
    pn_pool_t *pool,pn_media_t *media,const char *font_path,const pn_shelf_options_t *options);
/// 控制层为焦点准备与完整绘制相同的补字作用域；纯列表标记无需打开外置字体。/ Controller prepares the same fallback scope for focus as full painting; list markers need no external font opens.
pn_status_t pn_shelf_focus_with_font_file(const pn_catalog_page_t *,pn_font_t *,pn_frame_t *,int,int,const pn_shelf_covers_t *,pn_pool_t *,pn_media_t *,const char *,const pn_shelf_options_t *);

