/* 书目列表的同源绘制与命中。/ Shared catalog-list drawing and hit testing. */
#pragma once
#include "pn_catalog.h"
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
