/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：全局「我的摘录」跨书查询与管理界面（按书聚合、关键词筛选、详情跳转、删除确认）；主机与固件共用，render 只绘制不产生硬件副作用。
 * English: Global cross-book "My Excerpts" query and management UI (aggregated across books, keyword filter, jump-to-book detail, delete confirmation); shared across host and firmware, render paints only without hardware side effects.
 */
#pragma once
#include "pn_font.h"
#include "pn_frame.h"
#include "pn_highlights.h"
#include "pn_recent.h"

#define PN_EXCERPTS_UI_MAX 128u   ///< 单次加载的最大全局摘录条数 / Maximum cross-book excerpts loaded in one view
#define PN_EXCERPTS_UI_ROWS 5u    ///< 列表每页展示条数 / Excerpt cards shown per list page
#define PN_EXCERPTS_QUERY_MAX 64u ///< 关键词搜索缓冲区上限 / Max UTF-8 search query buffer size

/// 全局摘录界面模式。/ Active mode of the global excerpts UI.
typedef enum {
    PN_EXUI_CLOSED = 0, ///< 已关闭 / Closed
    PN_EXUI_LIST,       ///< 摘录列表（支持关键词过滤与分页）/ Excerpt list (filterable and paginated)
    PN_EXUI_SEARCH,     ///< 关键词搜索与屏幕键盘 / Keyword search with on-screen keyboard
    PN_EXUI_DETAIL,     ///< 单条摘录详情与跳转/删除操作 / Single excerpt detail with jump and delete actions
    PN_EXUI_DELETE      ///< 删除二次确认 / Delete confirmation screen
} pn_excerpts_ui_mode_t;

/// 全局摘录界面命令编码。/ Global excerpts UI command IDs.
enum {
    PN_EXUI_BACK = 1,      ///< 返回上级（列表返主页，子页返列表）/ Return up one level
    PN_EXUI_PREV,          ///< 上一页 / Previous page
    PN_EXUI_NEXT,          ///< 下一页 / Next page
    PN_EXUI_OPEN_SEARCH,   ///< 打开关键词搜索页 / Open keyword search screen
    PN_EXUI_CLEAR_FILTER,  ///< 清除当前关键词筛选 / Clear active keyword filter
    PN_EXUI_SEARCH_GO,     ///< 执行关键词查询 / Apply search query
    PN_EXUI_SEARCH_CLEAR,  ///< 清空搜索输入框 / Clear search draft input
    PN_EXUI_JUMP,          ///< 请求打开对应书籍并跳转至该摘录 / Request opening the book and jumping to this excerpt
    PN_EXUI_SET_MOTTO,     ///< 将当前摘录设为全局左上角文案 / Set current excerpt as the global top-left motto
    PN_EXUI_REMOVE,        ///< 进入删除确认页 / Enter delete confirmation
    PN_EXUI_CONFIRM,       ///< 确认删除当前摘录 / Confirm deleting the selected excerpt
    PN_EXUI_CANCEL,        ///< 取消删除返回详情 / Cancel deletion and return to detail
    PN_EXUI_ROW_BASE = 20, ///< 20..24 对应本页第 0..4 条摘录 / 20..24 select excerpt row 0..4 on current page
    PN_EXUI_KB_BASE = 200  ///< 屏幕键盘编码基址 / Base offset for encoded pn_keyboard codes
};

/// 将 pn_keyboard 命中码编码为 pn_excerpts_ui 命令。/ Encode a pn_keyboard hit code into a pn_excerpts_ui command.
#define PN_EXUI_KB_ENCODE(code) (PN_EXUI_KB_BASE + ((code) + 100))
/// 将 pn_excerpts_ui 键盘命令还原为 pn_keyboard 命中码。/ Decode a pn_excerpts_ui keyboard command back to a pn_keyboard code.
#define PN_EXUI_KB_DECODE(cmd) ((cmd) - PN_EXUI_KB_BASE - 100)

/// 全局摘录界面状态对象；初始全零，活动状态不可复制，须由 pn_excerpts_ui_close 释放。
/// Global excerpts UI handle; zero-initialize, never copy while open, release with pn_excerpts_ui_close.
typedef struct {
    void *impl;                       ///< 池分配的私有状态 / Pool-allocated private state
    pn_excerpts_ui_mode_t mode;       ///< 当前界面模式 / Active view mode
    bool open_request;                ///< 用户点「跳转阅读」时置 true，由控制层消费并打开书籍 / True when caller should open the book & jump
    char open_path[PN_RECENT_PATH_MAX]; ///< 待打开的书籍完整路径 / Full path of book to open
    pn_book_id_t open_book;           ///< 待跳转摘录所属书内容身份 / Book identity of the target highlight
    uint64_t open_highlight_id;       ///< 待跳转的摘录 ID / Target highlight ID to jump to after opening
    bool open_is_epub;                ///< 目标摘录是否为 EPUB 格式 / Whether target highlight belongs to an EPUB
    bool motto_changed;               ///< 摘录已设为左上角文案，控制层可同步 home_prefs 缓存 / True after setting top-left motto so caller can sync cached home_prefs
} pn_excerpts_ui_t;

/// 扫描状态目录下全部 *.highlights 摘录日志并打开列表；结合 recent 解析书名与跳转路径。
/// Scan all *.highlights journals under state_dir and open the list, matching titles/paths from recent.
pn_status_t pn_excerpts_ui_open(pn_excerpts_ui_t *ui, pn_pool_t *pool, pn_media_t *state_media,
    const char *state_dir, const pn_recent_snapshot_t *recent);

/// 纯绘制：将当前摘录页面绘制到 684×1216 帧缓冲，不产生存储或硬件副作用。
/// Pure paint: draw the active excerpts screen onto a 684x1216 frame without storage or hardware side effects.
pn_status_t pn_excerpts_ui_render(const pn_excerpts_ui_t *ui, pn_font_t *font, pn_frame_t *frame);

/// 屏幕坐标命中测试，返回 PN_EXUI_* 或 -1。/ Hit-test logical coordinates, returning PN_EXUI_* or -1.
int pn_excerpts_ui_hit(const pn_excerpts_ui_t *ui, int x, int y);

/// 处理界面命令（翻页、搜索筛选、删除摘录或发起跳书请求）；返回 PN_OK 表示需重绘，PN_EMPTY 表示无变化。
/// Handle a UI command (paging, search filter, delete, or jump request); PN_OK means repaint, PN_EMPTY means no-op.
pn_status_t pn_excerpts_ui_event(pn_excerpts_ui_t *ui, int command);

/// 只读诊断与测试辅助：查询加载的总摘录数与当前筛选命中的条数。
/// Read-only inspection helpers: total loaded excerpts and filtered match count.
size_t pn_excerpts_ui_total_count(const pn_excerpts_ui_t *ui);
size_t pn_excerpts_ui_filtered_count(const pn_excerpts_ui_t *ui);

/// 幂等释放池内状态并关闭界面。/ Idempotently free pooled state and close the UI.
void pn_excerpts_ui_close(pn_excerpts_ui_t *ui);
