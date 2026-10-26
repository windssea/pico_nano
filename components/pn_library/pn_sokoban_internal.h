/* SPDX-License-Identifier: Apache-2.0
 * 小工作集与有界存档。/ Small working set and bounded saves. */
#pragma once
#include "pn_sokoban.h"
typedef struct {
    pn_pool_t *pool;
    pn_media_t *media;
    char root[PN_JOURNAL_PATH_MAX];
    pn_so_state_t state, candidate;
    pn_so_screen_t screen;
    uint8_t shown_boxes[16], shown_player;
    unsigned selected, tier;
    uint64_t changed, retry;
    bool has, dirty, patch, full, restart, pending_start;
    const char *notice;
    int touch_x, touch_y, last_x, last_y;
    bool down, blocked;
} so_t;
static inline bool so_bit(const uint8_t *p, unsigned c) { return c < 128 && (p[c / 8] & (1u << (c % 8))) != 0; }
static inline void so_set(uint8_t *p, unsigned c, bool value) {
    if (value)
        p[c / 8] |= (uint8_t)(1u << (c % 8));
    else
        p[c / 8] &= (uint8_t) ~(1u << (c % 8));
}
int so_neighbor(const pn_so_level_t *, unsigned, unsigned);
bool so_dead(const pn_so_state_t *);
