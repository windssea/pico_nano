/* 书目列表的同源绘制与命中。/ Shared catalog-list drawing and hit testing. */
#pragma once
#include "pn_cover.h"
#include "pn_font.h"
#define PN_SHELF_PREVIOUS 6
#define PN_SHELF_NEXT 7
#define PN_SHELF_TOGGLE 8
#define PN_SHELF_CONTINUE 9
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
#define PN_SHELF_INDEX 16 ///< 点“书架”打开字母跳转 / Tap the shelf title to open the letter index
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
