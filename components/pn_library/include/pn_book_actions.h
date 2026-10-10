/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书籍操作面板（长按封面打开）：打开、收藏/取消收藏、书籍信息与“删除文件”二次确认；只绘制与命中，不动文件。
 * English: book actions sheet (opened by a long press on a cover): open, add/remove favorite, book information and a confirmed "delete file"; drawing and hit testing only, never touching files.
 * 冻结：删除文件必须经确认页；取消获得不小于删除的点击区；默认焦点是取消。
 * Frozen: deleting a file always goes through the confirmation page; Cancel gets a hit area at least as large as Delete; the default focus is Cancel.
 */
#pragma once
#include "pn_catalog.h"
#include "pn_font.h"
#include "pn_frame.h"
#define PN_BA_OPEN 1 ///< 打开 / Open
#define PN_BA_FAVORITE 2 ///< 收藏或取消收藏 / Add or remove favorite
#define PN_BA_DELETE 3 ///< 进入删除确认 / Go to delete confirmation
#define PN_BA_CANCEL 4 ///< 关闭面板或取消删除 / Close the sheet or cancel deletion
#define PN_BA_CONFIRM 5 ///< 确认删除文件 / Confirm deleting the file
typedef struct {
    pn_catalog_item_t item; ///< 操作对象 / The book acted on
    bool favorite; ///< 当前是否已收藏 / Currently a favorite
    bool favorites_known; ///< 收藏状态可读 / Favorite state is readable
    bool confirming; ///< 正在确认删除 / Confirming deletion
    const char *notice; ///< 静态反馈，可NULL / Static feedback, may be NULL
} pn_book_actions_t;
/// 在frame下部画操作面板（上方保留原书架作为背景），或整页删除确认。/ Draw the actions sheet over the lower frame (the shelf stays above as context), or the full deletion confirmation.
pn_status_t pn_book_actions_render(const pn_book_actions_t *actions,pn_font_t *font,pn_frame_t *frame);
/// 命中：PN_BA_*；面板以上的空白为PN_BA_CANCEL。/ Hit test: PN_BA_*; blank space above the sheet is PN_BA_CANCEL.
int pn_book_actions_hit(const pn_book_actions_t *actions,int x,int y);
