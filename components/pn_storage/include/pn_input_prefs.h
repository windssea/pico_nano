/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：全局翻页偏好（左右手、滑动、边缘点按、三键）的内部A/B记录。
 * English: internal A/B record of global page-turn preferences (hand, swipe, edge taps, keys).
 * 冻结：未知版本或标志位拒绝；无记录即默认。/ Frozen: unknown versions or flag bits are rejected; no record means defaults.
 */
#pragma once
#include "pn_journal.h"
#include "pn_identity.h"
#define PN_INPUT_LEFT_HAND 0x01u ///< 左手模式 / Left-hand mode
#define PN_INPUT_NO_SWIPE 0x02u ///< 关闭滑动 / Swipe off
#define PN_INPUT_NO_EDGE_TAP 0x04u ///< 关闭边缘点按 / Edge taps off
#define PN_INPUT_NO_KEYS 0x08u ///< 关闭三键翻页 / Key turns off
#define PN_INPUT_REFRESH_MASK 0x30u ///< 刷新策略（位4–5：0均衡、1清晰、2省电）/ Refresh policy (bits 4–5: 0 balanced, 1 crisp, 2 saver)
#define PN_INPUT_FLAGS_ALL 0x3Fu
#define PN_INPUT_FLAGS_DEFAULT 0xc0u ///< 无记录时的默认：水波纹翻页，其余开关为原默认 / Default without a record: ripple turns, other switches at their original defaults
#define PN_INPUT_TURN_MASK 0xc0u ///< 位6–7：0直接（优化消残影）、1高清无闪（48相GL16）、2极速（DU二值）、3水波纹 / Bits 6–7: 0 direct (anti-ghost), 1 crisp (48-phase GL16), 2 fast (DU binary), 3 ripple
/// 绑定root下input.a/b，不建目录。/ Bind input.a/b under root without creating directories.
pn_status_t pn_input_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io);
/// 读取标志；无记录EMPTY；错误不改输出。/ Load flags; EMPTY without a record; errors keep the output.
pn_status_t pn_input_prefs_load(const pn_journal_io_t *io,uint8_t *flags);
/// 保存标志，失败可能已提交。/ Save flags; failures may still have committed.
pn_status_t pn_input_prefs_save(const pn_journal_io_t *io,uint8_t flags);

/// 独立整书索引偏好A/B，不占用翻页标志；无记录默认后台。/ Separate whole-book indexing preference, without consuming turn flags; absent records default to background.
pn_status_t pn_index_prefs_files(pn_journal_files_t *,pn_media_t *,const pn_media_lease_t *,const char *,pn_journal_io_t *);
pn_status_t pn_index_prefs_load(const pn_journal_io_t *,bool *);
/// 同步保存，未知或损坏旧记录拒绝覆盖。/ Save synchronously, refusing to overwrite unsupported or corrupt existing records.
pn_status_t pn_index_prefs_save(const pn_journal_io_t *,bool);

/// 独立翻页方向偏好A/B（false左右翻页，true上下翻页），不占用翻页标志；无记录默认左右翻页。
/// Separate page-turn direction A/B preference (false horizontal, true vertical) without consuming turn flags; absent records default to horizontal.
pn_status_t pn_turn_dir_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io);
pn_status_t pn_turn_dir_prefs_load(const pn_journal_io_t *io,bool *vertical);
/// 同步保存，未知或损坏旧记录拒绝覆盖。/ Save synchronously, refusing to overwrite unsupported or corrupt existing records.
pn_status_t pn_turn_dir_prefs_save(const pn_journal_io_t *io,bool vertical);

#define PN_AUTO_LOCK_NEVER 0u ///< 从不自动锁屏（仅电源键手动锁屏）/ Never auto-lock (manual power-key lock still works)
#define PN_AUTO_LOCK_DEFAULT_MIN 5u ///< 默认无操作 5 分钟自动锁屏 / Default 5-minute idle auto-lock
#define PN_AUTO_LOCK_MAX_MIN 120u ///< 允许保存的最大自动锁屏分钟数 / Maximum valid auto-lock timeout in minutes
/// 独立自动锁屏时间偏好A/B（单位：分钟，0表示从不，默认5）；不占用翻页标志。
/// Separate auto-lock timeout A/B preference (minutes; 0=never, default 5) without consuming turn flags.
pn_status_t pn_auto_lock_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io);
/// 读取自动锁屏分钟数；无记录EMPTY；错误不改输出。/ Load auto-lock minutes; EMPTY if absent; errors preserve output.
pn_status_t pn_auto_lock_prefs_load(const pn_journal_io_t *io,uint16_t *minutes);
/// 同步保存自动锁屏分钟数；未知或损坏旧记录拒绝覆盖。/ Save auto-lock minutes synchronously, refusing to overwrite unsupported or corrupt existing records.
pn_status_t pn_auto_lock_prefs_save(const pn_journal_io_t *io,uint16_t minutes);

/// 阅读状态栏位置 / Reading status bar position
typedef enum {
    PN_SBAR_POS_TOP=0, ///< 单条状态栏居顶 / Single bar at top
    PN_SBAR_POS_BOTTOM=1, ///< 单条状态栏居底 / Single bar at bottom
    PN_SBAR_POS_OFF=2, ///< 关闭状态栏 / Hidden
    PN_SBAR_POS_DUAL=3 ///< 上下双栏（上下各 5 格可定制）/ Dual top+bottom bars (5 configurable slots each)
} pn_sbar_pos_t;

/// 阅读状态栏 5 格插槽可选内容 / Selectable content for each of the 5 status bar slots
typedef enum {
    PN_SBAR_SLOT_NONE=0, ///< 空（不显示）/ Empty
    PN_SBAR_SLOT_TIME, ///< 时间 / Clock
    PN_SBAR_SLOT_CHAPTER, ///< 章节名 / Chapter title
    PN_SBAR_SLOT_BOOK, ///< 书名 / Book title
    PN_SBAR_SLOT_PROGRESS, ///< 阅读进度百分比 / Progress percent
    PN_SBAR_SLOT_SECTION, ///< 章节节号 / Section index
    PN_SBAR_SLOT_BATTERY, ///< 电量图标与百分比 / Battery icon and percent
    PN_SBAR_SLOT_COUNT
} pn_sbar_slot_t;

/// 状态栏进度条模式 / Status bar thin progress line mode
typedef enum {
    PN_SBAR_PROG_BOOK=0, ///< 全书进度条 / Whole-book progress
    PN_SBAR_PROG_CHAPTER=1, ///< 本章/本节进度条 / Section progress
    PN_SBAR_PROG_OFF=2 ///< 关闭进度条 / Off
} pn_sbar_prog_t;

#define PN_SBAR_SLOTS 5u ///< 状态栏单行可配置格子数 / Configurable slots per status bar
#define PN_SBAR_MARGIN_MAX 24u ///< 状态栏距离屏幕边沿最大像素 / Max edge margin in pixels
#define PN_RETURN_BAR_HEIGHT 56 ///< 无底栏时底部「返回跳转前位置」独立保留高度 / Reserved bottom height for return strip when no bottom bar exists
/// 全局阅读状态栏配置（所有书共用，独立 A/B 日志 status_bar.{a,b}）。
/// Global reading status bar configuration (all books, separate A/B journal status_bar.{a,b}).
typedef struct {
    uint8_t enabled; ///< 0默认经典双栏，1用户自定义配置 / 0 default classic dual bars, 1 user customized
    uint8_t position; ///< pn_sbar_pos_t: 0上方、1下方、2关闭、3双栏 / Top, bottom, off, or dual
    uint8_t font_size; ///< 0小(20px)、1中(24px) / 0 small (20px), 1 medium (24px)
    uint8_t progress_bar; ///< pn_sbar_prog_t: 0全书、1本章、2关闭 / Book, chapter, or off
    uint8_t margin_px; ///< 距离屏幕边沿间距 0..24px（偶数，默认8）/ Edge spacing 0..24 even px (default 8)
    uint8_t slots[PN_SBAR_SLOTS]; ///< 顶栏（或单栏）5格插槽内容（pn_sbar_slot_t）/ Top (or single) bar 5 slots
    uint8_t bottom_slots[PN_SBAR_SLOTS]; ///< 双栏模式下底栏5格插槽内容（pn_sbar_slot_t）/ Bottom bar 5 slots in dual mode
} pn_status_bar_prefs_t;

/// 默认状态栏配置（enabled=0 默认经典双栏，position=DUAL，顶栏 [时间, 空, 空, 空, 电量]，底栏 [章节, 空, 空, 空, 进度]）。
/// Default status bar config (enabled=0 classic dual bars, position=DUAL, top [Time,,,Battery], bottom [Chapter,,,Progress]).
pn_status_bar_prefs_t pn_status_bar_prefs_default(void);
/// 校验状态栏配置合法性。/ Validate status bar preferences.
pn_status_t pn_status_bar_prefs_validate(const pn_status_bar_prefs_t *prefs);
/// 绑定 root 下 status_bar.{a,b}（全局默认状态栏）。/ Bind global default status_bar.{a,b} under root.
pn_status_t pn_status_bar_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io);
/// 绑定单书独立状态栏日志 <root>/<book_sha>.sbar.{a,b}（不建目录）。/ Bind per-book status bar journal <root>/<book_sha>.sbar.{a,b} without creating directories.
pn_status_t pn_status_bar_book_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,const pn_book_id_t *book,pn_journal_io_t *io);
/// 删除单书独立状态栏覆盖文件（.sbar.a/b），使该书恢复跟随全局状态栏。/ Remove per-book status bar override files (.sbar.a/b) so the book follows the global status bar again.
pn_status_t pn_status_bar_clear_files(const pn_journal_files_t *files);
/// 读取状态栏配置；无记录返回 PN_EMPTY，错误不改输出。/ Load status bar prefs; EMPTY if absent; errors preserve output.
pn_status_t pn_status_bar_prefs_load(const pn_journal_io_t *io,pn_status_bar_prefs_t *prefs);
/// 同步保存状态栏配置；未知或损坏旧记录拒绝覆盖。/ Save status bar prefs synchronously, refusing to overwrite unknown/corrupt records.
pn_status_t pn_status_bar_prefs_save(const pn_journal_io_t *io,const pn_status_bar_prefs_t *prefs);
/// 更新/读取全局与当前生效的阅读状态栏配置、书籍独立覆盖、底部返回条保留态与正文上下留白（单 owner 线程访问）。
/// Set/get global and effective runtime status bar configs, per-book overrides, return-bar reservation, and reserved top/bottom insets (single owner thread).
void pn_status_bar_set_active(const pn_status_bar_prefs_t *prefs);
pn_status_bar_prefs_t pn_status_bar_active(void);
/// 读取全局默认状态栏配置（不受当前书籍独立覆盖影响）。/ Read global default status bar config (unaffected by any active per-book override).
pn_status_bar_prefs_t pn_status_bar_global(void);
/// 设置或清除当前打开书籍的独立状态栏覆盖（NULL 表示跟随全局）。/ Set or clear the active book's per-book status bar override (NULL inherits global).
void pn_status_bar_set_book_override(const pn_status_bar_prefs_t *prefs);
/// 当前是否有生效的书籍独立状态栏覆盖。/ Whether a per-book status bar override is currently active.
bool pn_status_bar_has_book_override(void);
void pn_status_bar_set_return_bar(bool active);
bool pn_status_bar_return_bar(void);
void pn_status_bar_insets(bool hide_bars,int *top_px,int *bottom_px);

/// 屏幕手势绑定动作 / Action bound to a reading screen gesture
typedef enum {
    PN_GACT_NONE=0, ///< 关闭 / Off
    PN_GACT_PREV_PAGE, ///< 上一页 / Previous page
    PN_GACT_NEXT_PAGE, ///< 下一页 / Next page
    PN_GACT_TOOLBAR, ///< 阅读菜单 / Reading toolbar
    PN_GACT_TOC, ///< 阅读目录 / Table of contents
    PN_GACT_JUMP, ///< 阅读进度 / Reading progress jump
    PN_GACT_BOOKMARK, ///< 添加书签 / Quick bookmark
    PN_GACT_DICT, ///< 离线查词 / Dictionary search
    PN_GACT_REFRESH, ///< 立即全刷 / Full refresh
    PN_GACT_AUTO_TURN, ///< 自动翻页 / Auto page-turn
    PN_GACT_COUNT
} pn_gesture_action_t;

/// 屏幕手势槽位（对应机身线框示意的 9 个方位）/ Nine visual gesture slots matching the screen wireframe
typedef enum {
    PN_GSLOT_SWIPE_UP=0, ///< 屏幕内上滑 / Center swipe up
    PN_GSLOT_SWIPE_DOWN, ///< 屏幕内下滑 / Center swipe down
    PN_GSLOT_TOP_DOWN, ///< 顶部下拉 / Top edge swipe down
    PN_GSLOT_LEFT_EDGE_IN, ///< 左上边缘右滑 / Left upper edge swipe inward
    PN_GSLOT_RIGHT_EDGE_IN, ///< 右上边缘左滑 / Right upper edge swipe inward
    PN_GSLOT_LEFT_EDGE_VERT, ///< 左边缘上下滑 / Left edge vertical drag
    PN_GSLOT_RIGHT_EDGE_VERT, ///< 右边缘上下滑 / Right edge vertical drag
    PN_GSLOT_BOT_LEFT_UP, ///< 左下边缘上滑 / Bottom-left edge swipe up
    PN_GSLOT_BOT_RIGHT_UP, ///< 右下边缘上滑 / Bottom-right edge swipe up
    PN_GSLOT_COUNT
} pn_gesture_slot_t;

/// 阅读轻点区域方案 / Reading tap-zone layout
typedef enum {
    PN_TAP_ZONE_LR=0, ///< 左右两分（默认）/ Left-Right split (default)
    PN_TAP_ZONE_TB, ///< 上下两分 / Top-Bottom split
    PN_TAP_ZONE_THREE_COL, ///< 左中右三区（中栏呼出菜单）/ 3-column (Left / Toolbar / Right)
    PN_TAP_ZONE_ONE_HAND_R, ///< 单手右手（右半上下分翻、左半上页）/ Right-hand ergonomic
    PN_TAP_ZONE_ALL_NEXT, ///< 全屏下一页 / Tap anywhere = Next
    PN_TAP_ZONE_ONE_HAND_L, ///< 单手左手（左半上下分翻、右半上页）/ Left-hand ergonomic
    PN_TAP_ZONE_OFF, ///< 关闭点按翻页（仅滑动或按键翻页）/ Disable tap-to-turn
    PN_TAP_ZONE_COUNT
} pn_tap_zone_t;

/// 阅读滑动翻页方向 / Reading swipe-to-turn direction
typedef enum {
    PN_SWIPE_TURN_LR=0, ///< 左右滑动翻页（默认）/ Horizontal swipe turns (default)
    PN_SWIPE_TURN_TB=1, ///< 上下滑动翻页 / Vertical swipe turns
    PN_SWIPE_TURN_OFF=2, ///< 关闭滑动翻页 / Disable swipe turns
    PN_SWIPE_TURN_COUNT
} pn_swipe_turn_t;

/// 底部三键阅读映射 / Bottom 3-key reading map
typedef enum {
    PN_KEY_MAP_DEFAULT=0, ///< KEY1上页 / KEY2默认回首页 / KEY3下页 / Default
    PN_KEY_MAP_SWAP_LR, ///< KEY1下页 / KEY2默认回首页 / KEY3上页 / Swapped L/R
    PN_KEY_MAP_BOTH_NEXT, ///< KEY1与KEY3均下页 / Both L/R turn next
    PN_KEY_MAP_OFF, ///< 关闭 KEY1/KEY3 翻页（保留 KEY2）/ Disable KEY1/KEY3 turns
    PN_KEY_MAP_COUNT
} pn_key_map_t;

/// 正文长按默认行为 / Body long-press default behavior
typedef enum {
    PN_LP_DICT=0, ///< 长按查词（弹窗内支持一键摘录）/ Long-press dictionary (with excerpt button)
    PN_LP_HIGHLIGHT=1, ///< 长按直接按句高亮摘录（再长按取消）/ Long-press sentence highlight toggle
    PN_LP_OFF=2, ///< 关闭长按 / Disabled
    PN_LP_MENU=3, ///< 长按双选浮条（摘录 | 查词，默认）/ Long-press dual action pill (Excerpt | Dict, default)
    PN_LP_COUNT
} pn_long_press_mode_t;

/// 体感晃动翻页灵敏度 / Shake-to-turn sensitivity
typedef enum {
    PN_SHAKE_SENS_LOW=0, ///< 低灵敏度（防误触，需明显甩动）/ Low (anti-false-trigger)
    PN_SHAKE_SENS_MED=1, ///< 中灵敏度（默认均衡）/ Medium (balanced default)
    PN_SHAKE_SENS_HIGH=2, ///< 高灵敏度（轻抖即翻）/ High (light flick)
    PN_SHAKE_SENS_COUNT
} pn_shake_sens_t;

#define PN_AUTO_TURN_DEFAULT_SEC 15u ///< 默认自动翻页间隔（秒）/ Default auto page-turn interval (s)
#define PN_AUTO_TURN_MIN_SEC 3u
#define PN_AUTO_TURN_MAX_SEC 120u
/// 屏幕手势、触控区、底部三键、长按摘录、自动翻页与体感翻页偏好（独立 A/B 日志 gestures.{a,b}）。
/// Screen gestures, tap zones, bottom keys, long-press mode, auto-turn & shake-turn prefs (separate A/B journal gestures.{a,b}).
typedef struct {
    uint8_t slots[PN_GSLOT_COUNT]; ///< 9个手势槽绑定动作（pn_gesture_action_t）/ Actions bound to 9 gesture slots
    uint8_t tap_zone; ///< pn_tap_zone_t / Tap-zone scheme (0..6)
    uint8_t key_map; ///< pn_key_map_t / Bottom 3-key reading map (0..3)
    uint8_t long_press_mode; ///< pn_long_press_mode_t / Long-press mode (dict / highlight / off / dual menu)
    uint8_t auto_turn_sec; ///< 自动翻页间隔（秒，3..120，默认15）/ Auto page-turn interval in seconds
    uint8_t key2_short_action; ///< 中键短按动作：0返回首页（默认）、1阅读菜单、2立即全刷、3添加书签、4自动翻页 / KEY2 short action: 0 home (default), 1 menu, 2 refresh, 3 bookmark, 4 auto-turn
    uint8_t swipe_turn; ///< pn_swipe_turn_t：0左右滑动（默认）、1上下滑动、2关闭滑动 / Swipe page-turn mode (0 LR, 1 TB, 2 off)
    uint8_t shake_turn; ///< 体感晃动翻页开关：0关闭（默认）、1开启 / Shake-to-turn toggle: 0 off (default), 1 on
    uint8_t shake_sensitivity; ///< pn_shake_sens_t：0低、1中（默认）、2高 / Shake sensitivity: 0 low, 1 medium (default), 2 high
} pn_gesture_prefs_t;

/// 默认手势与交互配置（屏幕内上滑=目录、下滑=阅读菜单、顶部下拉=阅读菜单，其余边缘关闭；左右两分；默认三键；长按查词；自动翻页15秒）。
/// Default gesture/interaction preferences.
pn_gesture_prefs_t pn_gesture_prefs_default(void);
/// 校验手势与交互配置。/ Validate gesture/interaction preferences.
pn_status_t pn_gesture_prefs_validate(const pn_gesture_prefs_t *prefs);
/// 绑定 root 下 gestures.{a,b}。/ Bind gestures.{a,b} under root.
pn_status_t pn_gesture_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io);
/// 读取手势与交互配置；无记录返回 PN_EMPTY，错误不改输出。/ Load gesture prefs; EMPTY if absent; errors preserve output.
pn_status_t pn_gesture_prefs_load(const pn_journal_io_t *io,pn_gesture_prefs_t *prefs);
/// 同步保存手势与交互配置；未知或损坏旧记录拒绝覆盖。/ Save gesture prefs synchronously, refusing to overwrite unknown/corrupt records.
pn_status_t pn_gesture_prefs_save(const pn_journal_io_t *io,const pn_gesture_prefs_t *prefs);
