/* 私有小工作集，均计原应用池。/ Private small working set, charged to the existing app pool. */
#pragma once
#include "pn_sudoku.h"
typedef struct {
    uint8_t grid[81],solution[81],cell[81],placed[81];
    uint16_t rows[9],cols[9],boxes[9],remaining[81];
    int depth;
    unsigned solutions,steps;
    bool enter,done;
} su_solver_t;
typedef struct {
    pn_pool_t *pool;
    pn_media_t *media;
    char root[PN_JOURNAL_PATH_MAX];
    const uint8_t *bank;
    size_t bank_size,bank_at;
    uint32_t bank_crc;
    pn_sudoku_state_t state,candidate;
    su_solver_t solver;
    uint8_t solution[81];
    pn_sudoku_screen_t screen;
    unsigned tier;
    bool has,dirty,running,new_job,bank_ok,bank_check,errors[81];
    bool restart_from_play; ///< 直接重玩取消回棋盘，菜单入口取消回菜单 / Direct restart cancellation returns to play; menu entry returns to menu
    uint64_t last,changed,retry,saved_elapsed;
    uint8_t changed_cells[11]; ///< 自上次成功呈现后的变格位图 / Changed cells since the last successful presentation
    bool patch_ready,full_dirty; ///< 基准必须已确认；换页或失败后整绘 / Require a confirmed base; redraw fully after transitions or failures
    const char *notice;
} su_t;
