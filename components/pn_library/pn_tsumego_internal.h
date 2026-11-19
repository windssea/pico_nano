/* SPDX-License-Identifier: Apache-2.0
 * 围棋死活题内部共享结构与编解码接口。/ Internal Go Tsumego controller struct and codec declarations. */
#pragma once
#include "pn_journal.h"
#include "pn_tsumego.h"

/* 存档头48B + 实时/劫盘面42B + 至多12条每条4B着法历史(回放恢复) = 160B，精简可靠。
 * Compact 160-byte save: 48B header + 42B packed boards + replayable move history. */
#define PN_TG_SAVE_BYTES 256u

typedef struct {
    pn_pool_t *pool;
    pn_media_t *media;
    char root[PN_JOURNAL_PATH_MAX];
    const char *notice;
    pn_tg_state_t state;
    uint8_t shown_board[PN_TG_CELLS];
    uint8_t shown_last_black, shown_last_white, shown_hint, shown_cursor;
    uint8_t played_moves[PN_TG_UNDO]; ///< 回放恢复用的黑棋着法序列 / Played Black moves for deterministic save replay
    pn_tg_screen_t screen;
    unsigned tier, selected_level;
    bool has, dirty, running, fresh_clock, restart, pending_start, patch, full;
    uint64_t last, changed, retry;
} tg_t;

uint32_t pn_tg_bank_crc(void);
void pn_tg_reset_board(pn_tg_state_t *s, const pn_tg_level_t *lv, uint32_t bank_crc);
pn_status_t pn_tg_encode_state(const pn_tg_state_t *s, const uint8_t played[PN_TG_UNDO], bool has_saved,
                               uint8_t out[PN_TG_SAVE_BYTES]);
pn_status_t pn_tg_decode_state(const uint8_t *in, size_t n, uint32_t current_crc, pn_tg_state_t *out,
                               uint8_t played[PN_TG_UNDO], bool *has_saved);
