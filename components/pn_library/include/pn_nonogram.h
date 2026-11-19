/* SPDX-License-Identifier: Apache-2.0
 * 有界数织（Nonogram）规则、唯一解核验与同源纯绘制。/ Bounded Nonogram rules, uniqueness verification and shared pure rendering. */
#pragma once
#include "pn_blob.h"
#include "pn_font.h"

#define PN_NO_MAX_SIDE 15
#define PN_NO_CELLS 225
#define PN_NO_UNDO 64
#define PN_NO_PUZZLES 90
#define PN_NO_MAX_CLUES 8
#define PN_NO_STROKE 1000

typedef enum {
    PN_NO_BLANK = 0, ///< 空白未定 / Blank undecided cell
    PN_NO_FILL = 1,  ///< 涂黑格 / Filled ink cell
    PN_NO_MARK = 2   ///< 排除叉号 / Marked-empty cross
} pn_no_cell_t;

typedef struct {
    uint8_t width, height, tier, target_count; ///< 棋盘尺寸、难度与目标涂黑总数 / Grid dimensions, tier and target filled count
    uint16_t id;                               ///< 稳定题号 0..89 / Stable puzzle ID 0..89
    uint8_t passes, reserved;                  ///< 离线纯行推理轮数与保留位 / Offline line-propagation pass count and reserved zero
    char title[24];                            ///< 通关后揭晓的UTF-8标题 / UTF-8 title revealed upon completion
    uint8_t solution[32];                      ///< 225位小端答案位图 / 225-bit little-endian solution bitmap
} pn_no_puzzle_t;

typedef struct {
    uint8_t start;     ///< 起点格 0..224 / Starting cell 0..224
    uint8_t dir_len;   ///< 最高位0横1竖，低7位长度1..15 / Bit 7: 0 row, 1 col; bits 0..6: length 1..15
    uint16_t reserved; ///< 固定为零 / Reserved zero
    uint32_t before;   ///< 至多15格的修改前2位状态 / Prior 2-bit states for up to 15 cells
} pn_no_undo_t;

typedef struct {
    pn_no_puzzle_t puzzle;         ///< 当前题目自有副本，续玩不依赖外部文件 / Owned puzzle copy independent of external files
    uint8_t cells[60];             ///< 每格2位，最多225格 / Two bits per cell, up to 225 cells
    uint8_t completed[12];         ///< 90关通关位图 / 90-puzzle completion bitmap
    uint8_t undo_count, undo_head; ///< 64步有界撤销环 / 64-step bounded undo ring
    bool cross_mode, complete;     ///< 叉号模式与完成标志 / Cross mode and completion flag
    uint16_t selected, hints;      ///< 光标格与已用提示数 / Selected cell and hints used
    uint32_t moves, bank_crc;      ///< 步数与通关历史所属题库CRC / Move count and bank CRC owning completion history
    uint64_t elapsed_ms;           ///< 仅活动解谜用时 / Active solving time only
    pn_no_undo_t undo[PN_NO_UNDO]; ///< 每条8B，无画面快照 / Eight bytes per entry without frame snapshots
} pn_no_state_t;

typedef struct {
    void *impl;              ///< 池内按需状态 / Pool-owned on-demand state
    bool active, presented;  ///< 会话活动与成功呈现回执 / Active session and successful presentation receipt
} pn_nonogram_t;

typedef enum {
    PN_NO_SELECT,
    PN_NO_PLAY,
    PN_NO_CONFIRM,
    PN_NO_COMPLETE,
    PN_NO_ERROR
} pn_no_screen_t;

enum {
    PN_NO_BACK = 1,
    PN_NO_EXIT,
    PN_NO_START,
    PN_NO_CHOOSE,
    PN_NO_RESTART,
    PN_NO_ACCEPT,
    PN_NO_CANCEL,
    PN_NO_RETRY,
    PN_NO_RESUME,
    PN_NO_FILL_MODE,
    PN_NO_CROSS_MODE,
    PN_NO_UNDO_MOVE,
    PN_NO_HINT,
    PN_NO_CLEAN,
    PN_NO_NEXT,
    PN_NO_PREV_LEVEL,
    PN_NO_NEXT_LEVEL,
    PN_NO_TIER = 100,
    PN_NO_CELL = 200
};

/// 校验内置整包CRC、字段边界及纯行列传播唯一可解性。/ Validate whole built-in bank CRC, bounds and pure line-propagation uniqueness.
pn_status_t pn_nonogram_puzzle(unsigned id, pn_no_puzzle_t *out);
/// 读取单格当前状态（0空、1涂、2叉）。/ Read a cell's current state (0 blank, 1 fill, 2 mark).
uint8_t pn_nonogram_cell(const pn_no_state_t *s, unsigned cell);
/// 已涂黑格总数。/ Count currently filled cells.
unsigned pn_nonogram_filled_count(const pn_no_state_t *s);
/// 提取指定行的提示段数组，返回段数（全空行返回1且out[0]=0）。/ Extract clue runs for a row, returning run count (empty row returns 1 with out[0]=0).
unsigned pn_nonogram_row_clues(const pn_no_puzzle_t *p, unsigned row, uint8_t out[PN_NO_MAX_CLUES]);
/// 提取指定列的提示段数组，返回段数。/ Extract clue runs for a column, returning run count.
unsigned pn_nonogram_col_clues(const pn_no_puzzle_t *p, unsigned col, uint8_t out[PN_NO_MAX_CLUES]);
/// 当前行已涂黑段是否与该行提示完全一致。/ Return whether the row's current filled runs match its clues.
bool pn_nonogram_row_satisfied(const pn_no_state_t *s, unsigned row);
/// 当前列已涂黑段是否与该列提示完全一致。/ Return whether the column's current filled runs match its clues.
bool pn_nonogram_col_satisfied(const pn_no_state_t *s, unsigned col);
/// 纯规则单格操作，按当前cross_mode切换涂黑或叉号。/ Pure single-cell action toggling fill or cross according to cross_mode.
pn_status_t pn_nonogram_act(pn_no_state_t *s, unsigned cell);
/// 纯规则同行或同列直线批涂；自动保护反向标记并合成单步撤销。/ Pure straight-line stroke along one row or column, coalesced into one undo entry.
pn_status_t pn_nonogram_stroke(pn_no_state_t *s, unsigned from, unsigned to);
/// 撤销最近一次单格或直线操作。/ Undo the latest single-cell or straight-line command.
pn_status_t pn_nonogram_undo(pn_no_state_t *s);
/// 填入或纠正一格标准答案并记入撤销栈。/ Reveal or correct one cell from the verified solution and record undo.
pn_status_t pn_nonogram_hint(pn_no_state_t *s);

/// 内部A/B事务存档；无存档进入选关，损坏保留原文件并提示。/ Internal A/B transactional saves; missing opens level select, corrupt preserves files and reports.
pn_status_t pn_nonogram_open(pn_nonogram_t *u, pn_pool_t *pool, pn_media_t *media, const char *root, uint64_t now);
pn_status_t pn_nonogram_event(pn_nonogram_t *u, int cmd, uint64_t now); ///< 输入不绘制 / Input never paints
pn_status_t pn_nonogram_tick(pn_nonogram_t *u, uint64_t now);           ///< 合并空闲保存，无定时刷屏 / Coalesce idle saves without periodic redraw
pn_status_t pn_nonogram_pause(pn_nonogram_t *u, uint64_t now);          ///< 保存失败阻止退出或锁屏 / Save failures block exit or lock
void pn_nonogram_displayed(pn_nonogram_t *u, bool ok);                  ///< 呈现成功回执更新局部基准 / Presentation receipt updates patch baseline
void pn_nonogram_close(pn_nonogram_t *u);                               ///< 仅释放内存，调用方先过保存屏障 / Release memory only after save barrier
const pn_no_state_t *pn_nonogram_state(const pn_nonogram_t *u);         ///< 只读棋局快照 / Read-only game state
pn_no_screen_t pn_nonogram_screen(const pn_nonogram_t *u);              ///< 当前界面状态 / Current screen

pn_status_t pn_nonogram_render(const pn_nonogram_t *u, pn_font_t *f, pn_frame_t *p);        ///< 纯整页绘制 / Pure full-frame rendering
pn_status_t pn_nonogram_render_update(const pn_nonogram_t *u, pn_font_t *f, pn_frame_t *p); ///< 变格与对应行列提示增量绘制 / Changed-cell and clue-line partial update
int pn_nonogram_hit(const pn_nonogram_t *u, int x, int y);                                  ///< 与绘制共用几何 / Shared drawing geometry
int pn_nonogram_touch(pn_nonogram_t *u, unsigned count, int x, int y, bool valid);          ///< 单指点按与同行/列直线拖涂 / Single-finger tap and straight row/col stroke
