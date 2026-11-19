/* SPDX-License-Identifier: Apache-2.0
 * 有界围棋死活题引擎（完整气/提子/禁自杀/单劫规则）、变化树判题与同源纯绘制。
 * Bounded Go Tsumego engine (full liberties/capture/suicide/Ko rules), variation-tree grading and shared pure rendering. */
#pragma once
#include "pn_blob.h"
#include "pn_font.h"

#define PN_TG_SIZE 9
#define PN_TG_CELLS 81
#define PN_TG_LEVELS 60
#define PN_TG_MAX_NODES 48
#define PN_TG_UNDO 12
#define PN_TG_NONE 0xFFu

enum {
    PN_TG_EMPTY = 0, ///< 空交叉点 / Empty intersection
    PN_TG_BLACK = 1, ///< 黑子 / Black stone
    PN_TG_WHITE = 2  ///< 白子 / White stone
};

enum {
    PN_TG_EDGE_TOP = 1,
    PN_TG_EDGE_RIGHT = 2,
    PN_TG_EDGE_BOTTOM = 4,
    PN_TG_EDGE_LEFT = 8
};

enum {
    PN_TG_GOAL_KILL = 0,    ///< 黑先杀白 / Black to kill
    PN_TG_GOAL_LIVE = 1,    ///< 黑先做活 / Black to live
    PN_TG_GOAL_CAPTURE = 2, ///< 黑先吃子 / Black to capture
    PN_TG_GOAL_SEKI = 3,    ///< 黑先双活 / Black to make seki
    PN_TG_GOAL_KO = 4       ///< 黑先打劫 / Black to make ko
};

enum {
    PN_TG_NODE_ROOT = 0,
    PN_TG_NODE_CORRECT_LEAF = 1,
    PN_TG_NODE_CORRECT_CONT = 2,
    PN_TG_NODE_WRONG_REFUTE = 3
};

typedef enum {
    PN_TG_ONGOING = 0, ///< 答题进行中 / Solving in progress
    PN_TG_SOLVED = 1,  ///< 正解过关 / Solved cleanly
    PN_TG_FAILED = 2   ///< 落子失误被白棋反击，可悔棋重试 / Refuted by White; undoable
} pn_tg_outcome_t;

typedef struct {
    uint8_t black_pos;   ///< 黑棋落子点（0..80，根节点0xFF） / Black move (0..80, 0xFF for root)
    uint8_t flags;       ///< PN_TG_NODE_* 类型 / Node type
    uint8_t white_reply; ///< 白棋应手或默认反击点（0xFF无） / White reply or default refutation (0xFF none)
    uint8_t parent;      ///< 父节点索引（根节点0xFF） / Parent node index (0xFF for root)
} pn_tg_node_t;

typedef struct {
    uint8_t id, tier, goal, edge_mask;          ///< 题号、分档、目标与真边掩码 / ID, tier, goal and true-edge mask
    uint8_t node_count, main_moves, r0, r1;     ///< 变化树节点数与主变黑棋步数 / Node count and main-line Black moves
    char title[24];                             ///< 经典死活题标题 / Classic tsumego title
    uint8_t board[PN_TG_CELLS];                 ///< 展开后的9×9初始盘面 / Unpacked 9x9 initial board
    pn_tg_node_t nodes[PN_TG_MAX_NODES];        ///< 内置变化树节点 / Embedded variation tree
} pn_tg_level_t;

typedef struct {
    uint8_t board[PN_TG_CELLS];   ///< 回合前盘面快照 / Board snapshot before turn
    uint8_t ko_board[PN_TG_CELLS];///< 回合前单劫禁止盘面 / Prior Ko-ban snapshot
    uint8_t node_idx;             ///< 回合前变化树节点索引 / Tree cursor before turn
    uint8_t last_black, last_white; ///< 回合前最近落子点 / Previous last-move points
    uint8_t has_ko;               ///< ko_board是否有效 / Whether ko_board is active
} pn_tg_undo_t;

typedef struct {
    pn_tg_level_t level;           ///< 当前死活题自有副本 / Owned puzzle copy
    uint8_t board[PN_TG_CELLS];    ///< 当前9×9实时盘面 / Live 9x9 board
    uint8_t ko_board[PN_TG_CELLS]; ///< 紧前一步盘面（判单劫） / Previous board for simple Ko check
    uint8_t completed[8];          ///< 60题通关位图 / 60-puzzle completion bitmap
    uint8_t node_idx;              ///< 当前变化树节点（初始0） / Active variation-tree cursor
    uint8_t outcome, undo_count;   ///< 答题结果与悔棋层数 / Outcome and undo depth
    uint8_t has_ko;                ///< ko_board是否生效 / Whether ko_board is active
    uint8_t last_black, last_white;///< 最近黑/白落子点（0xFF无） / Latest Black/White move points
    uint8_t hint_pos, cursor;      ///< 提示点与键盘光标点（0xFF无） / Hint point and cursor
    uint16_t moves, hints;         ///< 黑棋已下步数与已用提示数 / Black moves played and hints used
    uint16_t captured_black, captured_white; ///< 本局累计提子数 / Captured stone counters
    uint32_t bank_crc;             ///< 通关记录所属题库CRC / Bank CRC owning completion history
    uint64_t elapsed_ms;           ///< 仅活动答题用时 / Active solving time only
    pn_tg_undo_t undo[PN_TG_UNDO]; ///< 有界悔棋栈 / Bounded turn-undo stack
} pn_tg_state_t;

typedef struct {
    void *impl;             ///< 池内按需状态 / Pool-owned on-demand state
    bool active, presented; ///< 会话活动与成功呈现回执 / Active session and presentation receipt
} pn_tsumego_t;

typedef enum {
    PN_TG_SELECT,
    PN_TG_PLAY,
    PN_TG_CONFIRM,
    PN_TG_COMPLETE,
    PN_TG_ERROR
} pn_tg_screen_t;

enum {
    PN_TG_BACK = 1,
    PN_TG_EXIT,
    PN_TG_START,
    PN_TG_CHOOSE,
    PN_TG_RESTART,
    PN_TG_ACCEPT,
    PN_TG_CANCEL,
    PN_TG_RETRY,
    PN_TG_RESUME,
    PN_TG_UNDO_MOVE,
    PN_TG_HINT,
    PN_TG_CLEAN,
    PN_TG_NEXT,
    PN_TG_PREV_LEVEL,
    PN_TG_NEXT_LEVEL,
    PN_TG_UP,
    PN_TG_DOWN,
    PN_TG_LEFT,
    PN_TG_RIGHT,
    PN_TG_ACTIVATE,
    PN_TG_TIER = 100,
    PN_TG_CELL = 200
};

/// 校验内置题库CRC、盘面无死子及变化树结构并加载指定死活题。/ Validate built-in bank CRC, initial stones and tree structure, loading one puzzle.
pn_status_t pn_tsumego_level(unsigned id, pn_tg_level_t *out);
/// 完整围棋落子规则：判空、自动提无气敌子、禁自杀与禁单劫同形；合法时写入out_board并返回提子数。
/// Full Go move legality: empty check, capture, no-suicide and simple Ko; writes out_board and capture count when legal.
bool pn_tsumego_apply_move(const uint8_t board[PN_TG_CELLS], const uint8_t *ko_prev, unsigned pos, uint8_t color,
                           uint8_t out_board[PN_TG_CELLS], unsigned *out_captured);
/// 执黑落子一步：校验围棋合法性、按变化树判定正解/续行并自动触发白棋应手或反击。
/// Play one Black move: validate Go rules, grade against variation tree, and trigger White's reply or refutation.
pn_status_t pn_tsumego_play(pn_tg_state_t *s, unsigned pos);
/// 悔棋一步（支持从败局直接悔回续答）。/ Undo one turn (including recovering directly from PN_TG_FAILED).
pn_status_t pn_tsumego_undo(pn_tg_state_t *s);
/// 在当前变化树节点标出正解下一手急所。/ Highlight the optimal next Black move at the current variation node.
pn_status_t pn_tsumego_hint(pn_tg_state_t *s);
/// 返回目标中文标签（黑先杀白/黑先做活/黑先吃子/黑先双活/黑先打劫）。/ Return Chinese goal label.
const char *pn_tsumego_goal_label(unsigned goal);

/// 内部A/B事务存档；无存档进入选关，损坏保留原文件并提示。/ Internal A/B transactional saves; missing opens select, corrupt preserves files and reports.
pn_status_t pn_tsumego_open(pn_tsumego_t *u, pn_pool_t *pool, pn_media_t *media, const char *root, uint64_t now);
pn_status_t pn_tsumego_event(pn_tsumego_t *u, int cmd, uint64_t now); ///< 输入不绘制 / Input never paints
pn_status_t pn_tsumego_tick(pn_tsumego_t *u, uint64_t now);           ///< 合并空闲保存 / Coalesce idle saves
pn_status_t pn_tsumego_pause(pn_tsumego_t *u, uint64_t now);          ///< 保存失败阻止退出或锁屏 / Save failures block exit or lock
void pn_tsumego_displayed(pn_tsumego_t *u, bool ok);                  ///< 呈现成功回执更新局部基准 / Presentation receipt updates patch baseline
void pn_tsumego_close(pn_tsumego_t *u);                               ///< 仅释放内存 / Release memory only
const pn_tg_state_t *pn_tsumego_state(const pn_tsumego_t *u);         ///< 只读题局快照 / Read-only state
pn_tg_screen_t pn_tsumego_screen(const pn_tsumego_t *u);              ///< 当前界面状态 / Current screen

pn_status_t pn_tsumego_render(const pn_tsumego_t *u, pn_font_t *f, pn_frame_t *p);        ///< 纯整页绘制 / Pure full-frame rendering
pn_status_t pn_tsumego_render_update(const pn_tsumego_t *u, pn_font_t *f, pn_frame_t *p); ///< 变动交叉点与状态栏局部增量绘制 / Changed-point and status partial update
int pn_tsumego_hit(const pn_tsumego_t *u, int x, int y);                                  ///< 点击命中 / Tap hit test
