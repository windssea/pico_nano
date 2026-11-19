/* SPDX-License-Identifier: Apache-2.0
 * 中国象棋残局内部共享结构与编解码接口。/ Internal Xiangqi endgame controller struct and codec declarations. */
#pragma once
#include "pn_journal.h"
#include "pn_xiangqi.h"

#define PN_XQ_SAVE_BYTES 796u

typedef struct {
    pn_pool_t *pool;
    pn_media_t *media;
    char root[PN_JOURNAL_PATH_MAX];
    const char *notice;
    pn_xq_state_t state, candidate;
    uint8_t shown_board[PN_XQ_CELLS];
    uint8_t shown_legal[PN_XQ_CELLS];
    uint8_t shown_selected, shown_cursor;
    uint8_t shown_last_from, shown_last_to;
    uint8_t shown_hint_from, shown_hint_to;
    pn_xq_screen_t screen;
    unsigned tier, selected_level;
    bool has, dirty, running, fresh_clock, restart, pending_start, patch, full;
    uint64_t last, changed, retry;
} xq_t;

uint32_t pn_xq_bank_crc(void);
bool pn_xq_position_valid(const uint8_t board[PN_XQ_CELLS]);
void pn_xq_compute_legal_dests(const pn_xq_state_t *s, uint8_t out_dest[PN_XQ_CELLS]);
void pn_xq_reset_board(pn_xq_state_t *s, const pn_xq_level_t *lv, uint32_t bank_crc);
pn_status_t pn_xq_encode_state(const pn_xq_state_t *s, bool has_saved, uint8_t out[PN_XQ_SAVE_BYTES]);
pn_status_t pn_xq_decode_state(const uint8_t *in, size_t n, uint32_t current_crc, pn_xq_state_t *out, bool *has_saved);
