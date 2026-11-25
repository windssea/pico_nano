/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源离线词典界面（查词结果页 + 手动查词全拼键盘页）；主机/固件复用，只绘制和调度词典引擎，不修改阅读进度。
 * English: Shared offline dictionary UI (lookup result view + manual search with Pinyin keyboard); shared across host and firmware, never mutating reading progress.
 */
#pragma once
#include "pn_epub_app.h"
#include "pn_reader_app.h"
#include "pn_stardict.h"

#define PN_DICT_UI_CAND_MAX 6u ///< 结果页最多保留的查词候选数 / Maximum candidate chips stored for lookup
#define PN_DICT_UI_PAGES_MAX 32u ///< 单条释义最大分页数 / Maximum pages per definition

/// 词典界面当前视图模式。/ Active dictionary UI view mode.
typedef enum {
    PN_DUI_CLOSED = 0, ///< 已关闭 / Closed
    PN_DUI_RESULT,     ///< 查词结果页 / Lookup result screen
    PN_DUI_SEARCH      ///< 手动查词与屏幕键盘页 / Manual search with on-screen keyboard
} pn_dict_ui_mode_t;

/// 词典界面命令编码。/ Dictionary UI command IDs.
enum {
    PN_DUI_BACK = 1,       ///< 返回阅读或从手动搜索退回结果页 / Back to reading (or back to result from search)
    PN_DUI_OPEN_SEARCH,    ///< 打开手动查词页 / Switch to manual search screen
    PN_DUI_NEXT_DICT,      ///< 切换下一本词典 / Switch to next installed dictionary
    PN_DUI_PREV_PAGE,      ///< 释义上一页 / Previous definition page
    PN_DUI_NEXT_PAGE,      ///< 释义下一页 / Next definition page
    PN_DUI_CAND_BASE = 20, ///< 点击候选词 0..5：PN_DUI_CAND_BASE + i / Select candidate chip 0..5
    PN_DUI_SEARCH_GO = 40, ///< 执行当前输入框查询 / Run lookup on draft text
    PN_DUI_SEARCH_CLEAR,   ///< 清空输入框 / Clear search draft
    PN_DUI_KB_BASE = 200   ///< 屏幕键盘编码基址 / Base offset for encoded pn_keyboard codes
};

/// 将 pn_keyboard 命中码编码为 pn_dict_ui 命令。/ Encode a pn_keyboard hit code into a pn_dict_ui command.
#define PN_DUI_KB_ENCODE(code) (PN_DUI_KB_BASE + ((code) + 100))
/// 将 pn_dict_ui 键盘命令还原为 pn_keyboard 命中码。/ Decode a pn_dict_ui keyboard command back to a pn_keyboard code.
#define PN_DUI_KB_DECODE(cmd) ((cmd) - PN_DUI_KB_BASE - 100)

/// 词典界面状态对象；初始全零，活动状态不可复制，须由 pn_dict_ui_close 释放。
/// Dictionary UI handle; zero-initialize, never copy while live, release via pn_dict_ui_close.
typedef struct {
    void *impl;             ///< 池分配的私有状态（避免占用任务栈）/ Pool-allocated private state avoiding task stack bloat
    pn_dict_ui_mode_t mode; ///< 当前视图模式 / Active view mode
} pn_dict_ui_t;

/// 打开离线词典界面：扫描 dicts_dir，若有候选词且 open_search 为 false 则自动按候选顺序查词并显示结果页，否则进入手动查词页。
/// Open the offline dictionary UI: scans dicts_dir, auto-queries candidates in order when open_search is false, or opens manual search.
/// reader 与 epub 至多传其一（均可为 NULL 以便主机单测直接调用 pn_dict_ui_render）。
/// Pass at most one of reader/epub (or both NULL in host unit tests using pn_dict_ui_render directly).
pn_status_t pn_dict_ui_open(pn_dict_ui_t *ui, pn_pool_t *pool, pn_reader_app_t *reader, pn_epub_app_t *epub,
    const char *dicts_dir, const char cands[][PN_DICT_WORD_MAX], size_t cand_count, bool open_search,
    pn_reader_present_fn present, void *ctx);

/// 纯绘制回调：将当前词典视图绘制到 684x1216 帧缓冲，不触发硬件副作用或状态提交。
/// Pure paint entry drawing the active dictionary view onto a 684x1216 frame without hardware side effects.
pn_status_t pn_dict_ui_render(pn_dict_ui_t *ui, pn_font_t *font, pn_font_t *fallback, pn_frame_t *frame);

/// 通过绑定的阅读会话呈现当前词典页面（GL16 刷新）；未绑定阅读会话时返回 PN_OK。
/// Present the active dictionary view via the bound reader overlay (GL16); returns PN_OK if headless.
pn_status_t pn_dict_ui_present(pn_dict_ui_t *ui, pn_reader_present_fn present, void *ctx);

/// 坐标命中测试，返回 PN_DUI_* 命令或 -1。/ Hit test screen coordinate (x, y), returning PN_DUI_* or -1.
int pn_dict_ui_hit(const pn_dict_ui_t *ui, int x, int y);

/// 分发命令并重绘；当用户退出词典时自动重绘原阅读页、释放内部状态并把 ui->mode 置为 PN_DUI_CLOSED。
/// Dispatch a command and re-present; when leaving, repaints the reading page, frees private state, and sets ui->mode = PN_DUI_CLOSED.
pn_status_t pn_dict_ui_event(pn_dict_ui_t *ui, int command, uint64_t now_ms, pn_reader_present_fn present, void *ctx);

/// 查询当前已加载词典数与命中词头（只读诊断/测试辅助）。/ Read-only inspection of loaded dictionary count and matched headword.
size_t pn_dict_ui_dict_count(const pn_dict_ui_t *ui);
const char *pn_dict_ui_matched_word(const pn_dict_ui_t *ui);
const char *pn_dict_ui_definition(const pn_dict_ui_t *ui);
size_t pn_dict_ui_page_index(const pn_dict_ui_t *ui);
size_t pn_dict_ui_page_count(const pn_dict_ui_t *ui);

/// 幂等释放池内状态并关闭界面。/ Idempotently free pooled state and close the UI.
void pn_dict_ui_close(pn_dict_ui_t *ui);
