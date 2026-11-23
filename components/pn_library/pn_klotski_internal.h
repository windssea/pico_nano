/* SPDX-License-Identifier: Apache-2.0
 * 三国华容道内部共享结构与编解码接口。/ Internal Klotski controller struct and codec declarations. */
#pragma once
#include "pn_journal.h"
#include "pn_klotski.h"

#define PN_KL_SAVE_BYTES 600u

typedef struct {
    pn_pool_t *pool;
    pn_media_t *media;
    char root[PN_JOURNAL_PATH_MAX];
    const char *notice;
    pn_kl_state_t state, candidate;
    uint8_t shown_pos[PN_KL_MAX_PIECES];
    uint8_t shown_selected;
    uint8_t shown_hint_piece, shown_hint_dir, shown_hint_steps;
    pn_kl_screen_t screen;
    unsigned tier, selected_level;
    bool has, dirty, running, fresh_clock, restart, pending_start, load_error, patch, full;
    bool touch_down, touch_cancel;
    int touch_x0, touch_y0, touch_x1, touch_y1;
    uint64_t last, changed, retry;
} kl_t;

uint32_t pn_kl_bank_crc(void);
void pn_kl_piece_dims(uint8_t shape, unsigned *w, unsigned *h);
void pn_kl_reset_board(pn_kl_state_t *s, const pn_kl_level_t *lv, uint32_t bank_crc);
pn_status_t pn_kl_encode_state(const pn_kl_state_t *s, bool has_saved, uint8_t out[PN_KL_SAVE_BYTES]);
pn_status_t pn_kl_decode_state(const uint8_t *in, size_t n, uint32_t current_crc, pn_kl_state_t *out, bool *has_saved);
