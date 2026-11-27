/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享可视化状态栏设置页（纯绘制、命中测试与配置状态更新），供系统设置与阅读内排版设置共用。
 * English: Shared visual status-bar settings UI (pure drawing, hit testing, and state transitions) reused by Settings and in-reading Typesetting.
 */
#pragma once
#include "pn_font.h"
#include "pn_frame.h"
#include "pn_input_prefs.h"
#include <stdbool.h>

#define PN_SBAR_UI_BACK 0 ///< 返回上级页面 / Return to previous page
#define PN_SBAR_UI_SLOT_BASE 1 ///< 1..5：顶栏（或单栏）5个预览格子 / 1..5: top (or single) bar 5 preview slots
#define PN_SBAR_UI_BSLOT_BASE 6 ///< 6..10：双栏底栏5个预览格子 / 6..10: dual bottom-bar 5 preview slots
#define PN_SBAR_UI_PICK_BASE 11 ///< 11..17：弹窗内选择插槽内容 0..6 / 11..17: modal slot content choice 0..6
#define PN_SBAR_UI_POS_TOP 18 ///< 布局：顶部单栏 / Layout: single top bar
#define PN_SBAR_UI_POS_BOTTOM 19 ///< 布局：底部单栏 / Layout: single bottom bar
#define PN_SBAR_UI_POS_OFF 20 ///< 布局：关闭状态栏 / Layout: status bar off
#define PN_SBAR_UI_POS_DUAL 21 ///< 布局：自定义上下双栏 / Layout: custom dual bars
#define PN_SBAR_UI_FONT_BASE 22 ///< 22..23：字号（小/中）/ 22..23: font size (small/medium)
#define PN_SBAR_UI_PROG_BASE 24 ///< 24..26：进度条（全书/本章/关闭）/ 24..26: progress bar (book/chapter/off)
#define PN_SBAR_UI_MARGIN_DEC 27 ///< 距离屏幕间距减 2px / Decrease edge margin by 2 px
#define PN_SBAR_UI_MARGIN_INC 28 ///< 距离屏幕间距加 2px / Increase edge margin by 2 px
#define PN_SBAR_UI_RESET 29 ///< 恢复默认经典双栏 / Restore default classic dual bars
#define PN_SBAR_UI_MODAL_CLOSE 30 ///< 关闭插槽内容选择弹窗 / Dismiss slot content picker modal
#define PN_SBAR_UI_USE_GLOBAL 31 ///< 清除本书独立状态栏并恢复跟随全局设置 / Clear per-book status bar override and follow global

/// 状态栏设置页作用域 / Scope mode for the shared visual status-bar settings UI
typedef enum {
    PN_SBAR_SCOPE_GLOBAL=0, ///< 系统设置：编辑全局默认状态栏 / System Settings: editing global default status bar
    PN_SBAR_SCOPE_BOOK_GLOBAL=1, ///< 阅读内：本书当前跟随全局状态栏 / In-reading: book currently follows global status bar
    PN_SBAR_SCOPE_BOOK_CUSTOM=2 ///< 阅读内：本书当前使用独立状态栏 / In-reading: book currently has independent status bar
} pn_sbar_ui_scope_t;

/// 返回状态栏当前配置的简明中文摘要；book_hide_bars 为真时优先提示“本书隐藏”。
/// Return a concise Chinese summary of the status bar config; prefers "book hidden" when book_hide_bars is true.
const char *pn_status_bar_ui_summary(const pn_status_bar_prefs_t *sb,bool book_hide_bars);

/// 绘制居中浮层单选菜单（供状态栏格子与手势动作弹窗共用，纯绘制）。
/// Draw a centered floating single-select modal (shared by status-bar slots and gesture actions; pure drawing).
pn_status_t pn_status_bar_ui_picker_modal(pn_font_t *font,pn_frame_t *frame,const char *title,const char *subtitle,
    const char *const *items,unsigned count,int current);

/// 居中浮层单选菜单坐标命中，点击弹窗外返回 close_cmd。
/// Hit-test a centered floating single-select modal; taps outside return close_cmd.
int pn_status_bar_ui_picker_hit(unsigned count,int x,int y,int base_cmd,int close_cmd);

/// 绘制可视化状态栏设置全页（纯绘制，不写存储或改全局状态）。
/// Render the full visual status bar settings screen (pure drawing, no storage or global side effects).
pn_status_t pn_status_bar_ui_render(pn_font_t *font,pn_frame_t *frame,const pn_status_bar_prefs_t *sb,
    int pick_slot,pn_sbar_ui_scope_t scope,const char *back_label,const char *notice);

/// 可视化状态栏设置页坐标命中，返回 PN_SBAR_UI_* 或 -1。
/// Hit-test the visual status bar settings screen, returning PN_SBAR_UI_* or -1.
int pn_status_bar_ui_hit(const pn_status_bar_prefs_t *sb,int pick_slot,pn_sbar_ui_scope_t scope,int x,int y);

/// 纯状态更新：将 PN_SBAR_UI_* 动作应用到配置草稿与弹窗槽位；若需持久化则 *changed=true。
/// Pure state transition applying a PN_SBAR_UI_* action to prefs and pick_slot; sets *changed=true when persistence is needed.
bool pn_status_bar_ui_apply(pn_status_bar_prefs_t *prefs,int *pick_slot,int action,bool *changed);
