/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书架搜索页：输入框与屏幕键盘（a–z、0–9），只绘制与命中，不读写存储；匹配规则见pn_catalog_match。
 * English: shelf search page: query box and on-screen keyboard (a–z, 0–9); draws and hit-tests only, never touching storage; matching rules live in pn_catalog_match.
 */
#pragma once
#include "pn_catalog.h"
#include "pn_font.h"
#include "pn_frame.h"

#define PN_SEARCH_NONE (-1) ///< 空白 / Blank space
#define PN_SEARCH_BACK 1 ///< 返回书架，不改搜索词 / Back to the shelf without changing the query
#define PN_SEARCH_DELETE 2 ///< 删除末位 / Delete the last character
#define PN_SEARCH_CLEAR 3 ///< 清空 / Clear the query
#define PN_SEARCH_DONE 4 ///< 按搜索词列出结果 / List results for the query
/// 字符键命中时直接返回其ASCII码（'a'–'z'、'0'–'9'）。/ Character keys return their ASCII code ('a'–'z', '0'–'9').

/// 搜索页状态：只含搜索词。/ Search page state: just the query.
typedef struct {
    char query[PN_CATALOG_QUERY_MAX+1]; ///< 小写a–z/0–9 / Lowercase a–z/0–9
} pn_search_ui_t;

/// 清空状态。/ Reset the state.
void pn_search_ui_open(pn_search_ui_t *ui,const char *query);
/// 末尾加入一个字符键；超长或非法返回false。/ Append a character key; false when full or invalid.
bool pn_search_ui_append(pn_search_ui_t *ui,char key);
/// 删除末位；本来就空返回false。/ Delete the last character; false when already empty.
bool pn_search_ui_delete(pn_search_ui_t *ui);
/// 绘制整页（684×1216）。/ Draw the whole 684×1216 page.
pn_status_t pn_search_ui_render(const pn_search_ui_t *ui,pn_font_t *font,pn_frame_t *frame);
/// 命中：字符键返回ASCII，操作键返回PN_SEARCH_*，空白返回PN_SEARCH_NONE。
/// Hit test: character keys return ASCII, action keys return PN_SEARCH_*, blank space returns PN_SEARCH_NONE.
int pn_search_ui_hit(int x,int y);
