/* SPDX-License-Identifier: Apache-2.0
 * 地牢纸牌内部控制器结构、局部刷新基准与编解码声明。
 * Internal Scoundrel controller state, patch baseline and save codec declarations. */
#pragma once
#include "pn_journal.h"
#include "pn_scoundrel.h"

/* 320B有界存档：含状态头、44张牌队列、武器斩杀链、本房内2步撤销栈与历史战绩。
 * Bounded 320-byte save: header, 44-card queue, weapon/slain stack, 2-step in-room undo and lifetime stats. */
#define PN_SC_SAVE_BYTES 320u

typedef struct {
    pn_pool_t *pool;
    pn_media_t *media;
    char root[PN_JOURNAL_PATH_MAX];
    const char *notice;
    pn_sc_state_t state;
    /* 局部增量刷新基线（仅呈现成功后更新）/ Partial-update baseline updated only after confirmed presentation. */
    uint8_t shown_room[PN_SC_ROOM_SLOTS];
    uint8_t shown_selected;
    uint8_t shown_weapon;
    uint8_t shown_slain[PN_SC_MAX_SLAIN];
    uint8_t shown_slain_count;
    uint8_t shown_potion_used;
    uint8_t shown_can_flee;
    uint8_t shown_undo_count;
    int8_t shown_hp;
    uint8_t shown_deck_count;
    uint8_t shown_room_no;
    uint8_t shown_room_cleared;
    const char *shown_notice;
    pn_sc_screen_t screen;
    uint8_t chosen_mode;
    bool has, dirty, running, fresh_clock, pending_start, load_error, patch, full;
    uint64_t last, changed, retry;
} sc_t;

pn_status_t pn_sc_encode_state(const pn_sc_state_t *s, bool has_saved, uint8_t chosen_mode,
                               uint8_t out[PN_SC_SAVE_BYTES]);
pn_status_t pn_sc_decode_state(const uint8_t *in, size_t n, pn_sc_state_t *out, bool *has_saved, uint8_t *chosen_mode);
