/* SPDX-License-Identifier: Apache-2.0
 * 有界中国象棋残局规则、强制杀法核验、顽强应着与同源纯绘制。/ Bounded Xiangqi endgame rules, forced-mate verification, stubborn AI defense and shared pure rendering. */
#pragma once
#include "pn_blob.h"
#include "pn_font.h"

#define PN_XQ_COLS 9
#define PN_XQ_ROWS 10
#define PN_XQ_CELLS 90
#define PN_XQ_UNDO 64
#define PN_XQ_ENDGAMES 60
#define PN_XQ_NONE_SQ 0xFFu

enum {
    PN_XQ_EMPTY = 0,    ///< 空交叉点 / Empty intersection
    PN_XQ_KING = 1,     ///< 帅 / 将 / Red or Black General
    PN_XQ_ADVISOR = 2,  ///< 仕 / 士 / Advisor
    PN_XQ_ELEPHANT = 3, ///< 相 / 象 / Elephant
    PN_XQ_HORSE = 4,    ///< 马 / Horse
    PN_XQ_ROOK = 5,     ///< 车 / Chariot (Rook)
    PN_XQ_CANNON = 6,   ///< 炮 / Cannon
    PN_XQ_PAWN = 7,     ///< 兵 / 卒 / Soldier (Pawn)
    PN_XQ_BLACK = 8     ///< 黑方阵营位（0红8黑） / Black side bit (0 Red, 8 Black)
};

typedef enum {
    PN_XQ_ONGOING = 0, ///< 对局进行中（轮红方走） / Ongoing turn (Red to move)
    PN_XQ_WON = 1,     ///< 红方绝杀或困毙黑方破阵成功 / Red checkmated or stalemated Black
    PN_XQ_LOST = 2     ///< 红方被反杀困毙或步数耗尽 / Red checkmated/stalemated or out of moves
} pn_xq_outcome_t;

typedef struct {
    uint8_t id, tier, piece_count, proof_plies; ///< 题号、分档、子数与主变半步数 / ID, tier, piece count and main-line plies
    uint8_t max_moves, mate_moves, r0, r1;      ///< 步数上限、最短杀步与保留位 / Move limit, shortest mate moves and reserved zeros
    char title[24];                             ///< 古谱杀型或残局标题 / Classic tactical or endgame title
    uint8_t board[PN_XQ_CELLS];                 ///< 展开后的90格初始局面 / Unpacked 90-point initial board
    uint8_t proof[32];                          ///< 内置主变着法序列（至多16半步） / Embedded proof line (up to 16 plies)
} pn_xq_level_t;

typedef struct {
    uint8_t red_from, red_to, red_cap; ///< 红方起止点与所吃黑子 / Red move endpoints and captured Black piece
    uint8_t blk_from, blk_to, blk_cap; ///< 黑方应着起止点与所吃红子（无应着时blk_from=0xFF） / Black reply endpoints and captured piece (0xFF if none)
    uint16_t reserved;                 ///< 固定为零 / Reserved zero
} pn_xq_undo_t;

typedef struct {
    pn_xq_level_t level;                ///< 当前残局自有副本，续玩不依赖外部文件 / Owned endgame copy independent of external files
    uint8_t board[PN_XQ_CELLS];         ///< 当前90格实时棋盘 / Live 90-point board
    uint8_t completed[8];               ///< 60关破阵位图 / 60-endgame completion bitmap
    uint8_t selected, cursor;           ///< 当前选中红子（0xFF无）与键盘光标点 / Selected Red piece (0xFF none) and cursor square
    uint8_t last_from, last_to;         ///< 最近一步起止点（0xFF无） / Latest move endpoints (0xFF none)
    uint8_t hint_from, hint_to;         ///< 当前提示着法起止点（0xFF无） / Active hint endpoints (0xFF none)
    uint8_t outcome, undo_count;        ///< 胜负状态与已存回合撤销数 / Outcome and recorded turn-undo count
    uint16_t moves, hints;              ///< 已走回合数与已用提示数 / Red turns played and hints used
    uint32_t bank_crc;                  ///< 破阵历史所属题库CRC / Bank CRC owning completion history
    uint64_t elapsed_ms;                ///< 仅活动对局用时 / Active solving time only
    pn_xq_undo_t undo[PN_XQ_UNDO];      ///< 每回合8B有界撤销栈 / Eight-byte per-turn bounded undo stack
} pn_xq_state_t;

typedef struct {
    void *impl;             ///< 池内按需状态 / Pool-owned on-demand state
    bool active, presented; ///< 会话活动与成功呈现回执 / Active session and successful presentation receipt
} pn_xiangqi_t;

typedef enum {
    PN_XQ_SELECT,
    PN_XQ_PLAY,
    PN_XQ_CONFIRM,
    PN_XQ_COMPLETE,
    PN_XQ_ERROR
} pn_xq_screen_t;

enum {
    PN_XQ_BACK = 1,
    PN_XQ_EXIT,
    PN_XQ_START,
    PN_XQ_CHOOSE,
    PN_XQ_RESTART,
    PN_XQ_ACCEPT,
    PN_XQ_CANCEL,
    PN_XQ_RETRY,
    PN_XQ_RESUME,
    PN_XQ_UNDO_MOVE,
    PN_XQ_HINT,
    PN_XQ_CLEAN,
    PN_XQ_NEXT,
    PN_XQ_PREV_LEVEL,
    PN_XQ_NEXT_LEVEL,
    PN_XQ_UP,
    PN_XQ_DOWN,
    PN_XQ_LEFT,
    PN_XQ_RIGHT,
    PN_XQ_ACTIVATE,
    PN_XQ_TIER = 100,
    PN_XQ_CELL = 200
};

/// 校验内置整包CRC、合法初始局面并回放主变确认终盘绝杀/困毙。/ Validate whole built-in bank CRC, legal initial position and replay proof confirming mate/stalemate.
pn_status_t pn_xiangqi_level(unsigned id, pn_xq_level_t *out);
/// 判断指定方（0红1黑）帅/将当前是否被将军。/ Return whether side (0 Red, 1 Black) currently has its General in check.
bool pn_xiangqi_in_check(const uint8_t board[PN_XQ_CELLS], unsigned side);
/// 严格按象棋规则（含塞象眼、蹩马腿、炮架、过河兵、白脸将及禁送将）判定单步合法性。/ Test exact Xiangqi move legality including blocks, cannon screens, flying general and self-check.
bool pn_xiangqi_legal_move(const uint8_t board[PN_XQ_CELLS], unsigned side, unsigned from, unsigned to);
/// 枚举指定方全部合法着法，写入out_pairs[2*i..2*i+1]，返回合法着数。/ Enumerate all legal moves for side into out_pairs[2*i..2*i+1], returning move count.
unsigned pn_xiangqi_legal_moves(const uint8_t board[PN_XQ_CELLS], unsigned side, uint8_t out_pairs[256]);
/// 有界极大极小搜索黑方最顽强应着（优先反杀或避杀拖延步数）。/ Bounded minimax search for Black's stubbornest legal reply (counter-mate or maximum survival).
bool pn_xiangqi_ai_reply(const pn_xq_state_t *s, uint8_t *out_from, uint8_t *out_to);
/// 搜索红方当前最短强制杀着（找不到短杀时退回主变或最佳将军着）。/ Search Red's shortest forced mating move from current board.
bool pn_xiangqi_best_red_move(const pn_xq_state_t *s, uint8_t *out_from, uint8_t *out_to);
/// 生成标准中文纵线着法记谱（如“车八进四”“马三退五”）。/ Format standard Chinese file-and-rank move notation.
void pn_xiangqi_move_text(const uint8_t board[PN_XQ_CELLS], unsigned from, unsigned to, char out[24]);
/// 执行红方一步合法着法并自动触发黑方顽强应着，记录单回合撤销。/ Execute one legal Red move, trigger Black's reply if ongoing, and record turn undo.
pn_status_t pn_xiangqi_step(pn_xq_state_t *s, unsigned from, unsigned to);
/// 悔棋整回合（撤回黑方应着与红方上一步，支持从败局直接悔回续走）。/ Undo a full Red+Black turn, including recovering directly from PN_XQ_LOST.
pn_status_t pn_xiangqi_undo(pn_xq_state_t *s);
/// 计算并高亮红方当前最佳破阵着法。/ Compute and highlight Red's current best mating move.
pn_status_t pn_xiangqi_hint(pn_xq_state_t *s);

/// 内部A/B事务存档；无存档进入选关，损坏保留原文件并提示。/ Internal A/B transactional saves; missing opens level select, corrupt preserves files and reports.
pn_status_t pn_xiangqi_open(pn_xiangqi_t *u, pn_pool_t *pool, pn_media_t *media, const char *root, uint64_t now);
pn_status_t pn_xiangqi_event(pn_xiangqi_t *u, int cmd, uint64_t now); ///< 输入不绘制 / Input never paints
pn_status_t pn_xiangqi_tick(pn_xiangqi_t *u, uint64_t now);           ///< 合并空闲保存，无定时刷屏 / Coalesce idle saves without periodic redraw
pn_status_t pn_xiangqi_pause(pn_xiangqi_t *u, uint64_t now);          ///< 保存失败阻止退出或锁屏 / Save failures block exit or lock
void pn_xiangqi_displayed(pn_xiangqi_t *u, bool ok);                  ///< 呈现成功回执更新局部基准 / Presentation receipt updates patch baseline
void pn_xiangqi_close(pn_xiangqi_t *u);                               ///< 仅释放内存，调用方先过保存屏障 / Release memory only after save barrier
const pn_xq_state_t *pn_xiangqi_state(const pn_xiangqi_t *u);         ///< 只读棋局快照 / Read-only game state
pn_xq_screen_t pn_xiangqi_screen(const pn_xiangqi_t *u);              ///< 当前界面状态 / Current screen

pn_status_t pn_xiangqi_render(const pn_xiangqi_t *u, pn_font_t *f, pn_frame_t *p);        ///< 纯整页绘制 / Pure full-frame rendering
pn_status_t pn_xiangqi_render_update(const pn_xiangqi_t *u, pn_font_t *f, pn_frame_t *p); ///< 变动交叉点与状态栏局部增量绘制 / Changed-intersection and metadata partial update
int pn_xiangqi_hit(const pn_xiangqi_t *u, int x, int y);                                  ///< 与绘制共用几何 / Shared drawing geometry
