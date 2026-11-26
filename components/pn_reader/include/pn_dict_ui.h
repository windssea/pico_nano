/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源离线词典浮层卡片、多选词典勾选面板与手动查词界面；主机/固件复用，支持任意勾选 1..N 本词典聚合与持久化，不修改阅读进度。
 * English: Shared offline dictionary floating card, multi-select dictionary checklist, and manual search UI; supports selecting any subset of 1..N dicts with persistence and local overlay without mutating reading progress.
 */
#pragma once
#include "pn_epub_app.h"
#include "pn_reader_app.h"
#include "pn_stardict.h"

#define PN_DICT_UI_CAND_MAX 6u   ///< 结果卡片最多保留的查词候选数 / Maximum candidate chips stored for lookup
#define PN_DICT_UI_PAGES_MAX 64u ///< 聚合释义最大分页数 / Maximum pages per aggregated definition
#define PN_DICT_CARD_X 48        ///< 浮层卡片左上 X（左右留白露出正文）/ Floating card left X (margins keep body text visible)
#define PN_DICT_CARD_W 588       ///< 浮层卡片宽度（像素）/ Floating card width in pixels
#define PN_DICT_SHEET_H 540      ///< 浮层卡片高度（像素）/ Floating card height in pixels
#define PN_DICT_BOTTOM_Y 620     ///< 居底浮层卡片起始 Y（620..1160）/ Bottom-docked card top Y
#define PN_DICT_TOP_Y 60         ///< 居顶浮层卡片起始 Y（60..600）/ Top-docked card top Y

/// 词典界面当前视图模式。/ Active dictionary UI view mode.
typedef enum {
    PN_DUI_CLOSED = 0, ///< 已关闭 / Closed
    PN_DUI_RESULT,     ///< 查词结果浮层卡片 / Lookup result floating card
    PN_DUI_SEARCH,     ///< 手动查词与屏幕键盘页 / Manual search with on-screen keyboard
    PN_DUI_PICKER      ///< 多选启用词典勾选浮层 / Multi-select enabled dictionaries checklist card
} pn_dict_ui_mode_t;

/// 词典界面命令编码。/ Dictionary UI command IDs.
enum {
    PN_DUI_BACK = 1,             ///< 关闭弹窗返回阅读，或从搜索/勾选页返回结果卡片 / Close to reading or return to result card
    PN_DUI_OPEN_SEARCH,          ///< 打开手动查词页 / Switch to manual search screen
    PN_DUI_NEXT_DICT,            ///< 快捷轮换词典（兼容接口）/ Cycle active dictionary (compatibility command)
    PN_DUI_PREV_PAGE,            ///< 释义上一页 / Previous definition page
    PN_DUI_NEXT_PAGE,            ///< 释义下一页 / Next definition page
    PN_DUI_OPEN_PICKER,          ///< 打开词典多选勾选面板 / Open multi-select dictionary checklist
    PN_DUI_PICKER_ALL,           ///< 勾选全部已安装词典 / Check all installed dictionaries
    PN_DUI_PICKER_DONE,          ///< 完成勾选并返回结果卡片 / Finish selection and return to result card
    PN_DUI_PICKER_PREV,          ///< 词典列表上一页 / Previous dictionary list page
    PN_DUI_PICKER_NEXT,          ///< 词典列表下一页 / Next dictionary list page
    PN_DUI_TOGGLE_HIGHLIGHT,     ///< 切换当前长按句子的高亮摘录 / Toggle sentence highlight at long-press anchor
    PN_DUI_CAND_BASE = 20,       ///< 点击候选词 0..5：PN_DUI_CAND_BASE + i / Select candidate chip 0..5
    PN_DUI_SEARCH_GO = 40,       ///< 执行当前输入框查询 / Run lookup on draft text
    PN_DUI_SEARCH_CLEAR,         ///< 清空输入框 / Clear search draft
    PN_DUI_DISMISS = 60,         ///< 点按卡片外正文区域关闭（不入三键焦点/无按压闪烁）/ Tap outside card to dismiss (excluded from focus/feedback)
    PN_DUI_BODY_PREV,            ///< 释义正文左区翻上一页（触摸快捷）/ Tap left body area for previous page
    PN_DUI_BODY_NEXT,            ///< 释义正文右区翻下一页（触摸快捷）/ Tap right body area for next page
    PN_DUI_PICKER_TOGGLE_BASE = 80, ///< 勾选/取消词典 0..15：PN_DUI_PICKER_TOGGLE_BASE + i / Toggle dictionary i (0..15)
    PN_DUI_KB_BASE = 200         ///< 屏幕键盘编码基址 / Base offset for encoded pn_keyboard codes
};

/// 将 pn_keyboard 命中码编码为 pn_dict_ui 命令。/ Encode a pn_keyboard hit code into a pn_dict_ui command.
#define PN_DUI_KB_ENCODE(code) (PN_DUI_KB_BASE + ((code) + 100))
/// 将 pn_dict_ui 键盘命令还原为 pn_keyboard 命中码。/ Decode a pn_dict_ui keyboard command back to a pn_keyboard code.
#define PN_DUI_KB_DECODE(cmd) ((cmd) - PN_DUI_KB_BASE - 100)

/// 词典弹窗可选配置：触控锚点、底层阅读页帧与词典勾选持久化位置。
/// Optional dictionary popup configuration: tap anchor Y, backing reading frame, and preference storage.
typedef struct {
    int anchor_x;           ///< 触控锚点横坐标（>0 且 anchor_y>0 时启用整句摘录联动）/ Tap anchor X (>0 with anchor_y>0 enables sentence highlight toggle)
    int anchor_y;           ///< 触控锚点纵坐标（弹窗统一居底显示，此坐标用于定位长按句摘录）/ Tap anchor Y (card always docks bottom; used for sentence highlight lookup)
    pn_frame_t *page_frame; ///< 当前阅读页帧副本（传入后浮层局部叠加且关闭快速恢复）/ Backing reading page frame for overlay & instant restore
    pn_media_t *pref_media; ///< 内部状态存储介质（可 NULL）/ Internal state media for persisting checked dicts (optional)
    const char *pref_dir;   ///< 内部状态目录路径（可 NULL）/ State directory path for persisting checked dicts (optional)
} pn_dict_ui_options_t;

/// 词典界面状态对象；初始全零，活动状态不可复制，须由 pn_dict_ui_close 释放。
/// Dictionary UI handle; zero-initialize, never copy while live, release via pn_dict_ui_close.
typedef struct {
    void *impl;             ///< 池分配的私有状态（避免占用任务栈）/ Pool-allocated private state avoiding task stack bloat
    pn_dict_ui_mode_t mode; ///< 当前视图模式 / Active view mode
} pn_dict_ui_t;

/// 打开离线词典浮层卡片/手动查词界面（带可选锚点、底层帧与词典多选持久化）。
/// Open offline dictionary card/search UI with optional anchor, backing page frame, and persisted multi-dict selection.
pn_status_t pn_dict_ui_open_ex(pn_dict_ui_t *ui, pn_pool_t *pool, pn_reader_app_t *reader, pn_epub_app_t *epub,
    const char *dicts_dir, const char cands[][PN_DICT_WORD_MAX], size_t cand_count, bool open_search,
    const pn_dict_ui_options_t *options, pn_reader_present_fn present, void *ctx);

/// 打开离线词典界面（默认选项兼容入口）。
/// Open offline dictionary UI (compatibility wrapper with default options).
pn_status_t pn_dict_ui_open(pn_dict_ui_t *ui, pn_pool_t *pool, pn_reader_app_t *reader, pn_epub_app_t *epub,
    const char *dicts_dir, const char cands[][PN_DICT_WORD_MAX], size_t cand_count, bool open_search,
    pn_reader_present_fn present, void *ctx);

/// 从内部状态目录读取持久化的启用词典位掩码（兼容 v1 单本与 v2 多选；全部失效时回退全选）。
/// Load persisted enabled dictionary bitmask (compatible with v1 single-dict and v2 multi-select; falls back to all if none remain).
pn_status_t pn_dict_ui_pref_load_mask(pn_media_t *media, const char *state_dir,
    const pn_stardict_catalog_t *catalog, uint32_t *mask_out);

/// 将当前启用的词典集合持久化到双槽日志 dict_pref.{a,b}（全选存为“全部”模式，子集保存对应 .ifo 文件名列表）。
/// Persist enabled dictionary set to dual-slot journal dict_pref.{a,b} (all-checked saved as ALL; subset stores .ifo basenames).
pn_status_t pn_dict_ui_pref_save_mask(pn_media_t *media, const char *state_dir,
    const pn_stardict_catalog_t *catalog, uint32_t enabled_mask);

/// 兼容单本读写辅助（空串表示“全部”）。/ Single-key preference helpers (empty string means "All").
pn_status_t pn_dict_ui_pref_load(pn_media_t *media, const char *state_dir, char *out_key, size_t cap);
pn_status_t pn_dict_ui_pref_save(pn_media_t *media, const char *state_dir, const char *key);

/// 纯绘制回调：将当前词典视图绘制到 684x1216 帧缓冲，不触发硬件副作用或状态提交。
/// Pure paint entry drawing the active dictionary view onto a 684x1216 frame without hardware side effects.
pn_status_t pn_dict_ui_render(pn_dict_ui_t *ui, pn_font_t *font, pn_font_t *fallback, pn_frame_t *frame);

/// 通过绑定的阅读会话呈现当前词典页面（GL16 刷新）；未绑定阅读会话时返回 PN_OK。
/// Present the active dictionary view via the bound reader overlay (GL16); returns PN_OK if headless.
pn_status_t pn_dict_ui_present(pn_dict_ui_t *ui, pn_reader_present_fn present, void *ctx);

/// 坐标命中测试，返回 PN_DUI_* 命令或 -1。/ Hit test screen coordinate (x, y), returning PN_DUI_* or -1.
int pn_dict_ui_hit(const pn_dict_ui_t *ui, int x, int y);

/// 分发命令并重绘；退出词典时优先用底层阅读帧局部 GL16 恢复，释放内部状态并置 PN_DUI_CLOSED。
/// Dispatch a command and re-present; when closing, restores backing page via local GL16 if available, frees private state, and sets PN_DUI_CLOSED.
pn_status_t pn_dict_ui_event(pn_dict_ui_t *ui, int command, uint64_t now_ms, pn_reader_present_fn present, void *ctx);

/// 只读诊断/测试辅助接口。/ Read-only inspection and test helpers.
size_t pn_dict_ui_dict_count(const pn_dict_ui_t *ui);
uint32_t pn_dict_ui_enabled_mask(const pn_dict_ui_t *ui);
size_t pn_dict_ui_enabled_count(const pn_dict_ui_t *ui);
size_t pn_dict_ui_selected_dict(const pn_dict_ui_t *ui);
size_t pn_dict_ui_matched_dicts(const pn_dict_ui_t *ui);
const char *pn_dict_ui_matched_word(const pn_dict_ui_t *ui);
const char *pn_dict_ui_definition(const pn_dict_ui_t *ui);
size_t pn_dict_ui_page_index(const pn_dict_ui_t *ui);
size_t pn_dict_ui_page_count(const pn_dict_ui_t *ui);
int pn_dict_ui_sheet_y(const pn_dict_ui_t *ui);

/// 幂等释放池内状态并关闭界面。/ Idempotently free pooled state and close the UI.
void pn_dict_ui_close(pn_dict_ui_t *ui);
