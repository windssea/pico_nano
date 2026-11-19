/* SPDX-License-Identifier: Apache-2.0
 * 有界三国华容道规则、BFS最优提示、A/B事务存档与同源纯绘制。/ Bounded Three Kingdoms Klotski rules, optimal hints, A/B transactional saves and shared pure rendering. */
#pragma once
#include "pn_blob.h"
#include "pn_font.h"

#define PN_KL_COLS 4
#define PN_KL_ROWS 5
#define PN_KL_CELLS 20
#define PN_KL_MAX_PIECES 12
#define PN_KL_UNDO 128
#define PN_KL_LEVELS 60
#define PN_KL_NONE 0xFFu

enum {
    PN_KL_SHAPE_1X1 = 0, ///< 1×1 卒 / 1x1 Soldier
    PN_KL_SHAPE_1X2 = 1, ///< 1×2 竖将 / 1x2 Vertical General
    PN_KL_SHAPE_2X1 = 2, ///< 2×1 横将 / 2x1 Horizontal General
    PN_KL_SHAPE_2X2 = 3  ///< 2×2 曹操 / 2x2 Cao Cao
};

enum {
    PN_KL_DIR_UP = 0,
    PN_KL_DIR_DOWN = 1,
    PN_KL_DIR_LEFT = 2,
    PN_KL_DIR_RIGHT = 3
};

typedef struct {
    uint8_t id, tier, piece_count, par_moves; ///< 关号、分档、棋块数与BFS最优步数 / Level ID, tier, piece count and exact BFS par
    uint16_t sol_offset, sol_len;             ///< 内置最优解偏移与长度 / Offset and length in embedded optimal solution pool
    char title[20];                           ///< 经典残局标题 / Classic puzzle title
    uint8_t shapes[PN_KL_MAX_PIECES];         ///< 各块形制（0号恒为曹操2×2） / Piece shapes (index 0 is always 2x2 Cao Cao)
    uint8_t start_pos[PN_KL_MAX_PIECES];      ///< 各块初始左上角格号（0..19，空槽0xFF） / Initial top-left cell (0..19, unused 0xFF)
} pn_kl_level_t;

typedef struct {
    uint8_t piece, from_pos, to_pos, reserved; ///< 撤销单步记录（4B） / Four-byte undo record
} pn_kl_undo_t;

typedef struct {
    pn_kl_level_t level;                ///< 当前关卡自有副本 / Owned level copy
    uint8_t pos[PN_KL_MAX_PIECES];      ///< 当前各块左上角格号 / Live top-left cell per piece
    uint8_t completed[8];               ///< 60关通关位图 / 60-level completion bitmap
    uint8_t selected;                   ///< 当前选中块索引（0xFF无） / Selected piece index (0xFF none)
    uint8_t hint_piece, hint_dir, hint_steps; ///< 提示目标块、方向与步数（hint_piece=0xFF无） / Active hint target (0xFF none)
    uint8_t undo_count;                 ///< 已记录撤销步数 / Recorded undo count
    bool solved;                        ///< 曹操是否抵达出口(col=1,row=3) / Whether Cao Cao reached exit (1,3)
    uint16_t moves, hints;              ///< 已走步数与已用提示数 / Moves played and hints used
    uint32_t bank_crc;                  ///< 通关记录所属题库CRC / Bank CRC owning completion history
    uint64_t elapsed_ms;                ///< 仅活动对局用时 / Active solving time only
    pn_kl_undo_t undo[PN_KL_UNDO];      ///< 有界撤销栈 / Bounded undo stack
} pn_kl_state_t;

typedef struct {
    void *impl;             ///< 池内按需状态 / Pool-owned on-demand state
    bool active, presented; ///< 会话活动与成功呈现回执 / Active session and presentation receipt
} pn_klotski_t;

typedef enum {
    PN_KL_SELECT,
    PN_KL_PLAY,
    PN_KL_CONFIRM,
    PN_KL_COMPLETE,
    PN_KL_ERROR
} pn_kl_screen_t;

enum {
    PN_KL_BACK = 1,
    PN_KL_EXIT,
    PN_KL_START,
    PN_KL_CHOOSE,
    PN_KL_RESTART,
    PN_KL_ACCEPT,
    PN_KL_CANCEL,
    PN_KL_RETRY,
    PN_KL_RESUME,
    PN_KL_UNDO_MOVE,
    PN_KL_HINT,
    PN_KL_CLEAN,
    PN_KL_NEXT,
    PN_KL_PREV_LEVEL,
    PN_KL_NEXT_LEVEL,
    PN_KL_UP,
    PN_KL_DOWN,
    PN_KL_LEFT,
    PN_KL_RIGHT,
    PN_KL_ACTIVATE,
    PN_KL_TIER = 100,
    PN_KL_CELL = 200,
    PN_KL_SLIDE = 300 ///< 300 + piece*8 + dir*2 + (steps-1)
};

/// 校验内置题库CRC、布局合法性并读取指定关卡。/ Validate built-in bank CRC, layout bounds and load a level.
pn_status_t pn_klotski_level(unsigned id, pn_kl_level_t *out);
/// 展开棋块占位表（空位为0xFF），不合法返回false。/ Expand occupancy grid (0xFF empty); returns false on overlap/out-of-bounds.
bool pn_klotski_occupancy(const pn_kl_level_t *lv, const uint8_t pos[PN_KL_MAX_PIECES], uint8_t occ[PN_KL_CELLS]);
/// 判定指定块沿方向滑行steps步（1或2）是否合法。/ Test whether piece may legally slide steps (1 or 2) in dir.
bool pn_klotski_can_slide(const pn_kl_state_t *s, unsigned piece, unsigned dir, unsigned steps);
/// 执行合法滑行并记录撤销栈；曹操到出口(1,3)即标记通关。/ Execute a legal slide, push undo, and mark solved when Cao Cao reaches (1,3).
pn_status_t pn_klotski_slide(pn_kl_state_t *s, unsigned piece, unsigned dir, unsigned steps);
/// 撤销一步滑行。/ Undo one slide move.
pn_status_t pn_klotski_undo(pn_kl_state_t *s);
/// 基于内置BFS最优解序列查询当前提示着法（支持偏离1步自动纠偏）。/ Find optimal next move hint from embedded BFS path (including 1-move detour recovery).
pn_status_t pn_klotski_hint(pn_kl_state_t *s);
/// 返回指定棋块的中文名牌（曹操/关羽/张飞/赵云/马超/黄忠/卒等）。/ Return Chinese display label for a piece.
const char *pn_klotski_piece_label(const pn_kl_level_t *lv, unsigned piece);

/// 内部A/B事务存档；无存档进入选关，损坏保留原文件并提示。/ Internal A/B transactional saves; missing opens select, corrupt preserves files and reports.
pn_status_t pn_klotski_open(pn_klotski_t *u, pn_pool_t *pool, pn_media_t *media, const char *root, uint64_t now);
pn_status_t pn_klotski_event(pn_klotski_t *u, int cmd, uint64_t now); ///< 输入不绘制 / Input never paints
pn_status_t pn_klotski_tick(pn_klotski_t *u, uint64_t now);           ///< 合并空闲保存 / Coalesce idle saves
pn_status_t pn_klotski_pause(pn_klotski_t *u, uint64_t now);          ///< 保存失败阻止退出或锁屏 / Save failures block exit or lock
void pn_klotski_displayed(pn_klotski_t *u, bool ok);                  ///< 呈现成功回执更新局部基准 / Presentation receipt updates patch baseline
void pn_klotski_close(pn_klotski_t *u);                               ///< 仅释放内存 / Release memory only
const pn_kl_state_t *pn_klotski_state(const pn_klotski_t *u);         ///< 只读棋局快照 / Read-only game state
pn_kl_screen_t pn_klotski_screen(const pn_klotski_t *u);              ///< 当前界面状态 / Current screen

pn_status_t pn_klotski_render(const pn_klotski_t *u, pn_font_t *f, pn_frame_t *p);        ///< 纯整页绘制 / Pure full-frame rendering
pn_status_t pn_klotski_render_update(const pn_klotski_t *u, pn_font_t *f, pn_frame_t *p); ///< 变动棋块与状态栏局部增量绘制 / Changed-piece and status partial update
int pn_klotski_hit(const pn_klotski_t *u, int x, int y);                                  ///< 点击命中 / Tap hit test
/// 根据起止触摸坐标推导滑块滑动命令（仅PN_KL_PLAY且拖拽距离足够时返回PN_KL_SLIDE+...，否则0）。/ Resolve drag swipe into a slide command (returns 0 if not a swipe).
int pn_klotski_swipe_command(const pn_klotski_t *u, int x0, int y0, int x1, int y1);
/// 统一触摸状态机：支持拖拽滑块与点击自动滑行/按钮，松手返回命令或0。/ Unified touch recognizer supporting piece drag-sliding and taps; returns command on release or 0.
int pn_klotski_touch(pn_klotski_t *u, unsigned count, int x, int y, bool valid);
