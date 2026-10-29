/* SPDX-License-Identifier: Apache-2.0
 * 有界扫雷规则与同源界面。/ Bounded Minesweeper rules and shared UI. */
#pragma once
#include "pn_blob.h"
#include "pn_font.h"
#define PN_MI_CELLS 252
#define PN_MI_BITS 32
typedef enum { PN_MI_WAITING, PN_MI_RUNNING, PN_MI_WON, PN_MI_LOST } pn_mi_result_t;
typedef struct {
    uint8_t width, height, tier, mine_count; ///< 棋盘与难度 / Board and difficulty
    uint8_t mines[PN_MI_BITS], opened[PN_MI_BITS], flags[PN_MI_BITS]; ///< 固定大小位图 / Fixed-size bitmaps
    uint16_t selected, first, exploded; ///< 光标、首步与踩雷位置，252为空 / Cursor, first move and explosion; 252 means none
    uint32_t seed; ///< 自有确定性种子 / Owned deterministic seed
    uint64_t elapsed_ms; ///< 仅活动游戏用时 / Active gameplay time only
    pn_mi_result_t result; ///< 尚未种雷、进行、胜或负 / Unseeded, running, won or lost
    bool flag_mode; ///< 点按插旗模式 / Tap-to-flag mode
} pn_mi_state_t;
typedef struct {
    void *impl; ///< 池内私有状态 / Pool-owned private state
    bool active, presented; ///< 会话与成功呈现回执 / Session and successful presentation receipt
} pn_minesweeper_t;
typedef enum { PN_MI_SELECT, PN_MI_PLAY, PN_MI_CONFIRM, PN_MI_COMPLETE, PN_MI_ERROR } pn_mi_screen_t;
enum {
    PN_MI_BACK = 1, PN_MI_EXIT, PN_MI_START, PN_MI_NEW, PN_MI_ACCEPT, PN_MI_CANCEL,
    PN_MI_RETRY, PN_MI_RESUME, PN_MI_REVEAL_MODE, PN_MI_FLAG_MODE, PN_MI_CLEAN,
    PN_MI_TIER = 100, PN_MI_CELL = 200
};
/// 纯规则，不分配、不访问端口；首步延迟种雷且邻域安全。/ Pure rules, no allocation/ports; deferred mines keep the first neighborhood safe.
pn_status_t pn_minesweeper_new(pn_mi_state_t *, unsigned tier, uint32_t seed);
pn_status_t pn_minesweeper_reveal(pn_mi_state_t *, unsigned); ///< 翻开与有界洪泛 / Reveal and bounded flood fill
pn_status_t pn_minesweeper_flag(pn_mi_state_t *, unsigned); ///< 翻转未开格旗子 / Toggle a covered flag
pn_status_t pn_minesweeper_chord(pn_mi_state_t *, unsigned); ///< 旗数相符时展开；错旗可踩雷 / Open neighbors when flag counts agree; wrong flags can lose
unsigned pn_minesweeper_adjacent(const pn_mi_state_t *, unsigned); ///< 周围雷数 / Adjacent mine count
unsigned pn_minesweeper_opened(const pn_mi_state_t *); ///< 已开安全格数 / Opened safe-cell count
unsigned pn_minesweeper_flags(const pn_mi_state_t *); ///< 旗子数 / Flag count
pn_status_t pn_minesweeper_open(pn_minesweeper_t *, pn_pool_t *, pn_media_t *, const char *, uint64_t); ///< 内部A/B存档 / Internal A/B saves
pn_status_t pn_minesweeper_event(pn_minesweeper_t *, int, uint64_t); ///< 输入不绘制 / Input never paints
pn_status_t pn_minesweeper_tick(pn_minesweeper_t *, uint64_t); ///< 合并保存，无周期刷屏 / Coalesced saves without periodic redraw
pn_status_t pn_minesweeper_pause(pn_minesweeper_t *, uint64_t); ///< 保存失败阻止退出或锁屏 / Save failures block exit or lock
void pn_minesweeper_displayed(pn_minesweeper_t *, bool); ///< 呈现回执 / Presentation receipt
void pn_minesweeper_close(pn_minesweeper_t *); ///< 调用方先保存再释放 / Caller saves before release
const pn_mi_state_t *pn_minesweeper_state(const pn_minesweeper_t *); ///< 只读棋局 / Read-only game
pn_mi_screen_t pn_minesweeper_screen(const pn_minesweeper_t *); ///< 当前页面 / Current screen
pn_status_t pn_minesweeper_render(const pn_minesweeper_t *, pn_font_t *, pn_frame_t *); ///< 纯绘制，借既有帧 / Pure drawing into existing frame
pn_status_t pn_minesweeper_render_update(const pn_minesweeper_t *, pn_font_t *, pn_frame_t *); ///< 整绘退路 / Full-render fallback
int pn_minesweeper_hit(const pn_minesweeper_t *, int, int); ///< 与绘制共用几何 / Shared drawing geometry
