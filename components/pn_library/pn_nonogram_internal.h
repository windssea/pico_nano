/* SPDX-License-Identifier: Apache-2.0
 * 数织控制器内部结构与位访问辅助。/ Nonogram controller internal state and bit helpers. */
#pragma once
#include "pn_nonogram.h"

#define NO_SAVE_BYTES 704u

typedef struct {
    pn_pool_t *pool;
    pn_media_t *media;
    char root[PN_JOURNAL_PATH_MAX];
    pn_no_state_t state, candidate;
    pn_no_screen_t screen;
    unsigned tier, selected_level;
    uint8_t shown_cells[60];
    uint16_t shown_selected;
    bool shown_cross_mode;
    uint64_t last, changed, retry;
    const char *notice;
    int touch_x, touch_y, last_x, last_y;
    bool has, dirty, running, fresh_clock, pending_start, restart, patch, full, down, blocked;
} no_t;

static inline bool no_sol_bit(const uint8_t *bits, unsigned c) {
    return ((bits[c / 8] >> (c % 8)) & 1u) != 0;
}

static inline uint8_t no_cell_get(const uint8_t *cells, unsigned c) {
    return (uint8_t)((cells[c / 4] >> ((c % 4) * 2)) & 3u);
}

static inline void no_cell_set(uint8_t *cells, unsigned c, uint8_t v) {
    unsigned shift = (c % 4) * 2;
    cells[c / 4] = (uint8_t)((cells[c / 4] & ~(3u << shift)) | ((v & 3u) << shift));
}
