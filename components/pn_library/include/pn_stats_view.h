/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：详细阅读数据页（点击主页“阅读节奏”进入），纯绘制与命中，不读文件。
 * English: detailed reading statistics view (entered by tapping Home Reading Rhythm); pure rendering and hit testing without file I/O.
 */
#pragma once
#include "pn_font.h"
#include "pn_frame.h"
#include "pn_reading_stats.h"
#include "pn_recent.h"
#include "pn_read_marks.h"
#define PN_STATS_BACK 1 ///< 返回主页 / Return to Home
typedef struct {
    pn_reading_summary_t summary; ///< 本周/今日/当前书统计快照 / Weekly, today and current-book statistics snapshot
    char current_title[128]; ///< 当前在读书名（可为空）/ Current book title (optional)
    char date_label[40]; ///< 当地日期文案（空表示未校时）/ Local date text (empty when unsynchronized)
    const pn_recent_snapshot_t *recent; ///< 最近阅读快照（可为NULL）/ Recent books snapshot (may be NULL)
    const pn_read_marks_t *read_marks; ///< 手动已读标记（可为NULL）/ Manual read marks (may be NULL)
} pn_stats_view_t;
/// 绘制详细阅读数据页（今日/本周/日均/书库统计、7日柱状图、当前书数据与近期进度）。
/// Render the detailed reading statistics page (today/week/average/library cards, 7-day bar chart, current book stats and recent progress).
pn_status_t pn_stats_view_render(const pn_stats_view_t *view,pn_font_t *font,pn_frame_t *frame);
/// 命中检测：返回PN_STATS_BACK或-1。/ Hit test: returns PN_STATS_BACK or -1.
int pn_stats_view_hit(int x,int y);
