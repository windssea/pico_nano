/* 书目列表的同源绘制与命中。/ Shared catalog-list drawing and hit testing. */
#pragma once
#include "pn_cover.h"
#include "pn_font.h"
#define PN_SHELF_PREVIOUS 6
#define PN_SHELF_NEXT 7
#define PN_SHELF_TOGGLE 8
#define PN_SHELF_CONTINUE 9 ///< 点继续阅读卡 / Tap the continue-reading card
/// 绘制六行文字/无封面卡，纯绘制，不打开书籍或修改状态。
/// Draw six text/cover-fallback rows only, without opening books or changing state.
pn_status_t pn_shelf_render(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame);
/// 命中已存在的条目/两个翻页控件，否则-1。
/// Hit existing entries or the two page controls, otherwise -1.
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
#define PN_SHELF_IMPORT 22 ///< 点“导入图书”（进入传书）/ Tap "import books" (opens transfer)
#define PN_SHELF_LAYOUT 21 ///< 点网格/列表切换 / Tap the grid/list toggle
/// 书架的可选外观与入口；全零（battery_percent=-1）等价于旧行为。
/// Optional shelf appearance and entries; all-zero with battery_percent=-1 matches the legacy behaviour.
typedef struct {
    int battery_percent; ///< 0–100，<0不显示 / 0–100, hidden when negative
    bool list_mode; ///< 列表模式（每页5行）/ List mode, five rows per page
    bool favorites_tab; ///< 分类里显示“收藏” / Show Favorites among the categories
    bool favorites; ///< 当前为收藏视图 / Showing the favorites view
    bool import_tile; ///< 最后一页空格放“导入图书”卡、空书架给“导入图书”按钮 / An import tile in a free last-page slot and an import button on an empty shelf
    bool layout_toggle; ///< 显示网格/列表切换 / Show the grid/list toggle
    bool search; ///< 显示“搜索”入口 / Show the Search entry
    bool query; ///< 当前为搜索结果，标题改为“搜索结果”、入口改为“清除” / Showing search results: heading says results and the entry says Clear
} pn_shelf_options_t;
/// 带选项的绘制与命中（options可为NULL）。list_mode下每页5行，条目序号按行计。
/// Drawing and hit testing with options (may be NULL). In list mode a page has five rows and item numbers count rows.
pn_status_t pn_shelf_render_ex(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers,const pn_shelf_options_t *options);
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

