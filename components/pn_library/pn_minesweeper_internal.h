/* SPDX-License-Identifier: Apache-2.0
 * 小型会话，不保存像素。/ Small session without pixel storage. */
#pragma once
#include "pn_minesweeper.h"
typedef struct {
    pn_pool_t *pool;
    pn_media_t *media;
    char root[PN_JOURNAL_PATH_MAX];
    pn_mi_state_t state, candidate;
    pn_mi_screen_t screen;
    unsigned tier, pending_tier;
    uint32_t next_seed;
    uint64_t changed, retry, last;
    bool has, dirty, pending_start, running, fresh_clock;
    const char *notice;
} mi_t;
static inline bool mi_bit(const uint8_t *p, unsigned c) { return c < PN_MI_CELLS && (p[c / 8] & (1u << (c % 8))) != 0; }
static inline void mi_set(uint8_t *p, unsigned c, bool v) {
    if (v) p[c / 8] |= (uint8_t)(1u << (c % 8));
    else p[c / 8] &= (uint8_t)~(1u << (c % 8));
}
void mi_geometry(const pn_mi_state_t *, int *, int *, int *);
