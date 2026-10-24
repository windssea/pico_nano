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
    uint64_t last,changed,retry,saved_elapsed;
    const char *notice;
} su_t;
