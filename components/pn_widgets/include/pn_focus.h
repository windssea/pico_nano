/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：三键焦点导航（docs/UI_UX.md第7节）：不改各页面，直接扫描页面的命中函数得到可操作区域，KEY1/KEY3移动焦点、KEY2确认，焦点用圆角粗框标出。
 * English: three-key focus navigation (docs/UI_UX.md section 7): without touching the pages, scan a page's hit function for its actionable areas; KEY1/KEY3 move the focus, KEY2 confirms and a thick rounded ring marks the focus.
 * 冻结：只读命中函数，不产生副作用；焦点项的code就是命中函数返回的code，确认等价于点击该区域。
 * Frozen: only the hit function is read, with no side effects; an item's code is exactly what the hit function returns and confirming is equivalent to tapping that area.
 */
#pragma once
#include <stddef.h>
#include "pn_widgets.h"
#define PN_FOCUS_MAX 48 ///< 一页最多可聚焦的区域数 / Most focusable areas on one page
typedef struct {int code,x,y,width,height;} pn_focus_item_t; ///< 命中码及其外接矩形 / A hit code and its bounding box
typedef struct {
    pn_focus_item_t items[PN_FOCUS_MAX]; ///< 按从上到下、从左到右排序 / Ordered top to bottom, left to right
    size_t count; ///< 有效项数 / Valid items
    int index; ///< 当前焦点，-1表示没有 / Current focus, -1 for none
} pn_focus_t;
/// 命中函数：返回命中码，空白-1。/ Hit function returning a hit code, or -1 for blank space.
typedef int (*pn_focus_hit_fn)(void *ctx,int x,int y);
/// 扫描整屏收集可聚焦区域；skip列出不参与导航的码（例如整屏的“关闭面板”区域）。焦点复位为-1。
/// Scan the whole screen for focusable areas; skip lists codes that never take part in navigation (such as a full-screen close area). The focus resets to -1.
void pn_focus_scan(pn_focus_t *focus,pn_focus_hit_fn hit,void *ctx,const int *skip,size_t skip_count);
/// 焦点移动一格（delta为正向前、否则向后），首尾循环；没有可聚焦项返回-1，否则返回新下标。
/// Move the focus by one (forward for a positive delta, otherwise backward) with wrap-around; returns -1 when nothing is focusable, otherwise the new index.
int pn_focus_move(pn_focus_t *focus,int delta);
/// 当前焦点项，没有则NULL。/ The focused item, or NULL.
const pn_focus_item_t *pn_focus_current(const pn_focus_t *focus);
/// 在frame上画焦点环（圆角粗框，外扩4px）。/ Draw the focus ring on frame (a thick rounded outline, 4 px outside the area).
void pn_focus_draw(pn_frame_t *frame,const pn_focus_item_t *item);
