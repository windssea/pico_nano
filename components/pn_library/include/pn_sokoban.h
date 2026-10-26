/* SPDX-License-Identifier: Apache-2.0
 * 有界推箱子，同源逻辑与纯绘制。/ Bounded Sokoban with shared logic and pure rendering. */
#pragma once
#include "pn_blob.h"
#include "pn_font.h"
#define PN_SO_CELLS 128
#define PN_SO_UNDO 128
#define PN_SO_LEVELS 90
typedef struct {
    uint8_t width, height, tier, box_count, initial_player, proof_count, min_pushes,
        reserved;               ///< 有界关卡几何 / Bounded level geometry
    uint16_t id;                ///< 稳定题号 / Stable level ID
    uint8_t switches, off_goal; ///< 离线最优解分档信息 / Offline optimal-solution grading
    uint8_t walls[16], goals[16], initial_boxes[16], solution[96],
        padding[4]; ///< 位图与至多48次推箱证明 / Bitmaps and at most 48 proof pushes
} pn_so_level_t;
typedef struct {
    uint8_t from, to, box_from, box_to; ///< 角色/箱子前后位置 / Player/box positions before and after
    uint16_t steps;                     ///< 此命令的步数 / Walking steps in this command
    uint8_t pushed, reserved;           ///< 是否推箱 / Whether the command pushed a box
} pn_so_undo_t;
typedef struct {
    pn_so_level_t
        level; ///< 当前关卡自有副本，续局不依赖外部文件 / Owned level copy, independent of external files on resume
    uint8_t boxes[16], player, undo_count, undo_head,
        completed[12]; ///< 箱子、角色、撤销环及通关历史 / Boxes, player, undo ring and completion history
    bool complete;     ///< 是否归位 / Whether all boxes are on goals
    uint32_t moves, pushes, bank_crc; ///< 步数、推数和历史所属题库 / Move/push counts and bank owning progress
    pn_so_undo_t undo[PN_SO_UNDO];    ///< 每条8B，不保存画面 / Eight bytes per command, never image snapshots
} pn_so_state_t;
typedef struct {
    void *impl; ///< 池内私有状态 / Pool-owned private state
    bool active, presented; ///< 会话与显示回执 / Session and display acknowledgment
} pn_sokoban_t; ///< 按需控制器及成功显示标记 / On-demand controller and successful-display marker
typedef enum { PN_SO_SELECT, PN_SO_PLAY, PN_SO_CONFIRM, PN_SO_COMPLETE, PN_SO_ERROR } pn_so_screen_t;
enum {
    PN_SO_UP = 1,
    PN_SO_RIGHT,
    PN_SO_DOWN,
    PN_SO_LEFT,
    PN_SO_UNDO_MOVE,
    PN_SO_RESTART,
    PN_SO_CHOOSE,
    PN_SO_START,
    PN_SO_BACK,
    PN_SO_EXIT,
    PN_SO_CANCEL,
    PN_SO_ACCEPT,
    PN_SO_CLEAN,
    PN_SO_RETRY,
    PN_SO_NEXT,
    PN_SO_PREV_LEVEL,
    PN_SO_NEXT_LEVEL,
    PN_SO_TIER = 100,
    PN_SO_CELL = 200
};
/// 校验内置全包CRC及所选关卡的完整可行解证明。/ Validate the whole built-in bank CRC and the selected level's complete
/// solution witness.
pn_status_t pn_sokoban_level(unsigned, pn_so_level_t *);
/// 纯规则：方向0上1右2下3左；走路到空地不推箱。/ Pure rules: directions 0 up, 1 right, 2 down, 3 left; walking to floor
/// never pushes.
pn_status_t pn_sokoban_move(pn_so_state_t *, unsigned);
pn_status_t pn_sokoban_walk(pn_so_state_t *,
                            unsigned); ///< 最短可达路只形成一个命令 / A shortest reachable walk is one command
pn_status_t pn_sokoban_undo(pn_so_state_t *); ///< 精确恢复位置与计数 / Restore positions and counters exactly
/// 内部A/B存档，缺存档进入选关，坏档保留并提示。/ Internal A/B saves; missing saves open selection, corrupt saves are
/// preserved and reported.
pn_status_t pn_sokoban_open(pn_sokoban_t *, pn_pool_t *, pn_media_t *, const char *, uint64_t);
pn_status_t pn_sokoban_event(pn_sokoban_t *, int, uint64_t); ///< 输入不绘制 / Input never paints
pn_status_t pn_sokoban_tick(pn_sokoban_t *,
                            uint64_t); ///< 只合并保存，无周期重绘 / Coalesce saves without periodic redraw
pn_status_t pn_sokoban_pause(pn_sokoban_t *, uint64_t); ///< 保存失败阻止退出/锁屏 / Save failures block exit/lock
void pn_sokoban_displayed(
    pn_sokoban_t *, bool); ///< 仅成功回执更新局部基准 / Update the patch baseline only on successful acknowledgment
void pn_sokoban_close(pn_sokoban_t *); ///< 仅释放，调用方先保存 / Release only after the caller saves
const pn_so_state_t *pn_sokoban_state(const pn_sokoban_t *); ///< 只读棋局 / Read-only game
pn_so_screen_t pn_sokoban_screen(const pn_sokoban_t *);      ///< 当前页 / Current screen
pn_status_t pn_sokoban_render(const pn_sokoban_t *, pn_font_t *,
                              pn_frame_t *); ///< 借用既有画布 / Borrow the existing canvas
pn_status_t
pn_sokoban_render_update(const pn_sokoban_t *, pn_font_t *,
                         pn_frame_t *); ///< 只画相对成功基准的变格 / Paint cells changed from the confirmed baseline
int pn_sokoban_hit(const pn_sokoban_t *, int, int); ///< 同源命中，无副作用 / Shared hit testing without side effects
int pn_sokoban_touch(
    pn_sokoban_t *, unsigned, int, int,
    bool); ///< 单指点击/滑动，坏采样或多指阻断到松手 / Single-finger tap/swipe; invalid/multitouch blocks until release
