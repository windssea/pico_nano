/* SPDX-License-Identifier: Apache-2.0
 * 地牢纸牌（Scoundrel）规则引擎、本房内2步撤销与有界A/B事务存档。
 * Scoundrel dungeon-card engine, 2-step in-room undo and bounded A/B transactional saves. */
#include "pn_scoundrel_internal.h"
#include <stdio.h>
#include <string.h>

#define SC_SAVE_MAGIC 0x31435350u /* "PSC1" */
#define SC_SAVE_VER 1u
#define SC_SAVE_IDLE_MS 1500u
#define SC_SAVE_RETRY_MS 3000u
#define SC_DEFAULT_SEED 0x5C01D3E1u

static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }
static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static void wr64(uint8_t *p, uint64_t v) {
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t next_rng(uint32_t *st) {
    uint32_t x = *st ? *st : SC_DEFAULT_SEED;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *st = x;
    return x;
}

static uint32_t mix_seed(uint64_t now, uint32_t prev_seed, uint32_t games) {
    uint32_t s = (uint32_t)(now ^ (now >> 32)) ^ (prev_seed * 0x9E3779B1u) ^ ((games + 1u) * 0x85EBCA6Bu);
    s ^= s >> 16;
    s *= 0x7FEB352Du;
    s ^= s >> 15;
    return s ? s : SC_DEFAULT_SEED;
}

bool pn_scoundrel_card_valid(uint8_t card) {
    if (card == PN_SC_CARD_NONE) return false;
    unsigned suit = pn_sc_card_suit(card);
    unsigned rank = pn_sc_card_rank(card);
    if (suit <= PN_SC_CLUB) return rank >= 2u && rank <= 14u;
    if (suit == PN_SC_DIAMOND || suit == PN_SC_HEART) return rank >= 2u && rank <= 10u;
    return false;
}

static unsigned card_bit_index(uint8_t card) {
    unsigned suit = pn_sc_card_suit(card);
    unsigned rank = pn_sc_card_rank(card);
    if (suit == PN_SC_SPADE) return rank - 2u;
    if (suit == PN_SC_CLUB) return 13u + (rank - 2u);
    if (suit == PN_SC_DIAMOND) return 26u + (rank - 2u);
    return 35u + (rank - 2u);
}

static uint8_t deck_pop(pn_sc_state_t *s) {
    if (s->deck_count == 0u) return PN_SC_CARD_NONE;
    uint8_t c = s->deck[s->deck_head];
    s->deck[s->deck_head] = PN_SC_CARD_NONE;
    s->deck_head = (uint8_t)((s->deck_head + 1u) % PN_SC_DECK_SIZE);
    s->deck_count--;
    return c;
}

static void deck_push_back(pn_sc_state_t *s, uint8_t card) {
    if (s->deck_count >= PN_SC_DECK_SIZE) return;
    unsigned tail = ((unsigned)s->deck_head + s->deck_count) % PN_SC_DECK_SIZE;
    s->deck[tail] = card;
    s->deck_count++;
}

static unsigned room_remaining(const pn_sc_state_t *s) {
    unsigned n = 0;
    for (unsigned i = 0; i < PN_SC_ROOM_SLOTS; i++) {
        if (s->room[i] != PN_SC_CARD_NONE) n++;
    }
    return n;
}

static int remaining_monster_sum(const pn_sc_state_t *s) {
    int sum = 0;
    for (unsigned i = 0; i < PN_SC_ROOM_SLOTS; i++) {
        if (pn_sc_is_monster(s->room[i])) sum += (int)pn_sc_card_rank(s->room[i]);
    }
    for (unsigned i = 0; i < s->deck_count; i++) {
        uint8_t c = s->deck[((unsigned)s->deck_head + i) % PN_SC_DECK_SIZE];
        if (pn_sc_is_monster(c)) sum += (int)pn_sc_card_rank(c);
    }
    return sum;
}

static void record_finished(pn_sc_state_t *s, bool won) {
    s->total_games++;
    if (won) s->total_wins++;
    if (!s->has_best || s->score > s->best_score) {
        s->best_score = s->score;
        s->has_best = 1u;
    }
}

unsigned pn_scoundrel_weapon_cap(const pn_sc_state_t *s) {
    if (!s || s->weapon == PN_SC_CARD_NONE) return 0u;
    if (s->slain_count == 0u) return 15u; /* 全新武器可斩任意点数 / Fresh weapon can slay any rank */
    unsigned prev = pn_sc_card_rank(s->slain[s->slain_count - 1u]);
    if (s->mode == PN_SC_MODE_HARDCORE) return prev > 0u ? prev - 1u : 0u;
    return prev;
}

bool pn_scoundrel_can_use_weapon(const pn_sc_state_t *s, uint8_t monster_card) {
    if (!s || !pn_sc_is_monster(monster_card) || s->weapon == PN_SC_CARD_NONE) return false;
    if (s->slain_count == 0u) return true;
    unsigned prev = pn_sc_card_rank(s->slain[s->slain_count - 1u]);
    unsigned rank = pn_sc_card_rank(monster_card);
    return (s->mode == PN_SC_MODE_HARDCORE) ? (rank < prev) : (rank <= prev);
}

int pn_scoundrel_weapon_damage(const pn_sc_state_t *s, uint8_t monster_card) {
    if (!s || !pn_sc_is_monster(monster_card)) return 0;
    int m = (int)pn_sc_card_rank(monster_card);
    if (s->weapon == PN_SC_CARD_NONE) return m;
    int w = (int)pn_sc_card_rank(s->weapon);
    return m > w ? (m - w) : 0;
}

void pn_scoundrel_new_run(pn_sc_state_t *s, uint32_t seed, uint8_t mode) {
    if (!s) return;
    uint32_t total_games = s->total_games;
    uint32_t total_wins = s->total_wins;
    int16_t best_score = s->best_score;
    uint8_t has_best = s->has_best;

    memset(s, 0, sizeof *s);
    s->seed = seed ? seed : SC_DEFAULT_SEED;
    s->mode = (mode == PN_SC_MODE_HARDCORE) ? PN_SC_MODE_HARDCORE : PN_SC_MODE_STANDARD;
    s->outcome = PN_SC_ONGOING;
    s->hp = PN_SC_MAX_HP;
    s->weapon = PN_SC_CARD_NONE;
    s->selected_slot = PN_SC_NONE;
    s->last_card = PN_SC_CARD_NONE;
    s->room_no = 1u;
    s->can_flee = 1u;
    s->total_games = total_games;
    s->total_wins = total_wins;
    s->best_score = has_best ? best_score : PN_SC_NO_BEST;
    s->has_best = has_best;

    /* 构造标准44张牌地牢牌组 / Build the official 44-card Scoundrel deck */
    unsigned idx = 0;
    for (unsigned r = 2u; r <= 14u; r++) s->deck[idx++] = pn_sc_make_card(PN_SC_SPADE, r);
    for (unsigned r = 2u; r <= 14u; r++) s->deck[idx++] = pn_sc_make_card(PN_SC_CLUB, r);
    for (unsigned r = 2u; r <= 10u; r++) s->deck[idx++] = pn_sc_make_card(PN_SC_DIAMOND, r);
    for (unsigned r = 2u; r <= 10u; r++) s->deck[idx++] = pn_sc_make_card(PN_SC_HEART, r);

    /* Fisher-Yates 确定性洗牌 / Deterministic Fisher-Yates shuffle */
    uint32_t rng = s->seed;
    for (unsigned i = PN_SC_DECK_SIZE - 1u; i > 0u; i--) {
        unsigned j = next_rng(&rng) % (i + 1u);
        uint8_t tmp = s->deck[i];
        s->deck[i] = s->deck[j];
        s->deck[j] = tmp;
    }
    s->deck_head = 0u;
    s->deck_count = PN_SC_DECK_SIZE;
    for (unsigned i = 0; i < PN_SC_ROOM_SLOTS; i++) s->room[i] = deck_pop(s);
}

static void capture_undo(const pn_sc_state_t *s, pn_sc_undo_t *u) {
    memset(u, 0, sizeof *u);
    u->hp = s->hp;
    memcpy(u->room, s->room, sizeof u->room);
    u->room_cleared = s->room_cleared;
    u->potion_used = s->potion_used;
    u->can_flee = s->can_flee;
    u->weapon = s->weapon;
    memcpy(u->slain, s->slain, sizeof u->slain);
    u->slain_count = s->slain_count;
    u->last_card = s->last_card;
    u->monsters_slain = s->monsters_slain;
    u->actions = s->actions;
}

pn_status_t pn_scoundrel_resolve(pn_sc_state_t *s, unsigned slot, bool use_weapon) {
    if (!s || slot >= PN_SC_ROOM_SLOTS || s->outcome != PN_SC_ONGOING) return PN_INVALID;
    uint8_t card = s->room[slot];
    if (card == PN_SC_CARD_NONE || !pn_scoundrel_card_valid(card)) return PN_INVALID;
    if (pn_sc_is_monster(card) && use_weapon && !pn_scoundrel_can_use_weapon(s, card)) return PN_INVALID;

    /* 本步前快照（仅标准模式入栈；若随后补发新牌或终局则清空） / Snapshot for standard-mode in-room undo. */
    pn_sc_undo_t snap;
    bool keep_undo = (s->mode == PN_SC_MODE_STANDARD);
    if (keep_undo) capture_undo(s, &snap);

    s->room[slot] = PN_SC_CARD_NONE;
    s->selected_slot = PN_SC_NONE;
    s->last_card = card;
    s->room_cleared++;
    s->can_flee = 0u; /* 动过房间内任意牌后本房不可再逃跑 / Cannot flee once any card is touched */
    if (s->actions < 65535u) s->actions++;

    if (pn_sc_is_weapon(card)) {
        s->weapon = card;
        memset(s->slain, 0, sizeof s->slain);
        s->slain_count = 0u;
    } else if (pn_sc_is_potion(card)) {
        if (!s->potion_used) {
            int healed = (int)s->hp + (int)pn_sc_card_rank(card);
            s->hp = (int8_t)(healed > PN_SC_MAX_HP ? PN_SC_MAX_HP : healed);
            s->potion_used = 1u;
        }
    } else {
        int dmg = use_weapon ? pn_scoundrel_weapon_damage(s, card) : (int)pn_sc_card_rank(card);
        if (use_weapon && s->slain_count < PN_SC_MAX_SLAIN) {
            s->slain[s->slain_count++] = card;
        }
        int next_hp = (int)s->hp - dmg;
        if (next_hp <= 0) {
            s->hp = 0;
            s->outcome = PN_SC_DEFEAT;
            s->undo_count = 0u;
            /* 官方阵亡计分：当前生命(0) - 所有未清理怪物点数之和 / Defeat score: -sum(remaining monsters) */
            s->score = (int16_t)(-remaining_monster_sum(s));
            record_finished(s, false);
            return PN_OK;
        }
        s->hp = (int8_t)next_hp;
        if (s->monsters_slain < 255u) s->monsters_slain++;
    }

    unsigned rem = room_remaining(s);
    if (rem == 0u && s->deck_count == 0u) {
        s->outcome = PN_SC_VICTORY;
        s->undo_count = 0u;
        /* 官方通关计分：剩余生命；满血20且末张为血瓶则20+药水点数 / Official victory scoring + full-HP potion bonus */
        if (s->hp == PN_SC_MAX_HP && pn_sc_is_potion(s->last_card)) {
            s->score = (int16_t)(PN_SC_MAX_HP + (int)pn_sc_card_rank(s->last_card));
        } else {
            s->score = (int16_t)s->hp;
        }
        record_finished(s, true);
        return PN_OK;
    }

    if (rem == 1u && s->deck_count > 0u) {
        /* 留1张牌在原位，向其余空槽补发最多3张新牌，清空本房撤销栈 / Keep survivor in place, refill up to 3, clear undo */
        for (unsigned i = 0; i < PN_SC_ROOM_SLOTS && s->deck_count > 0u; i++) {
            if (s->room[i] == PN_SC_CARD_NONE) s->room[i] = deck_pop(s);
        }
        s->room_cleared = 0u;
        s->potion_used = 0u;
        s->fled_last = 0u;
        s->can_flee = 1u;
        s->undo_count = 0u;
        if (s->room_no < 255u) s->room_no++;
        return PN_OK;
    }

    /* 仍在同一房间且未翻出新牌：保留至多2步本房内撤销 / Still inside current room without new cards: keep up to 2 undo steps */
    if (keep_undo) {
        if (s->undo_count < PN_SC_UNDO_MAX) {
            s->undo[s->undo_count++] = snap;
        } else {
            s->undo[0] = s->undo[1];
            s->undo[1] = snap;
        }
    }
    return PN_OK;
}

pn_status_t pn_scoundrel_flee(pn_sc_state_t *s) {
    if (!s || s->outcome != PN_SC_ONGOING) return PN_INVALID;
    if (!s->can_flee || s->fled_last || s->room_cleared != 0u) return PN_EMPTY;
    unsigned rem = room_remaining(s);
    if (rem == 0u || s->deck_count == 0u) return PN_EMPTY;

    /* 将当前房间全部牌按原序沉入牌堆底部，再重翻最多4张新牌 / Move room cards in order to deck bottom, then deal fresh room */
    uint8_t old_room[PN_SC_ROOM_SLOTS];
    memcpy(old_room, s->room, sizeof old_room);
    for (unsigned i = 0; i < PN_SC_ROOM_SLOTS; i++) {
        s->room[i] = PN_SC_CARD_NONE;
        if (old_room[i] != PN_SC_CARD_NONE) deck_push_back(s, old_room[i]);
    }
    for (unsigned i = 0; i < PN_SC_ROOM_SLOTS && s->deck_count > 0u; i++) {
        s->room[i] = deck_pop(s);
    }
    s->selected_slot = PN_SC_NONE;
    s->room_cleared = 0u;
    s->potion_used = 0u;
    s->fled_last = 1u;
    s->can_flee = 0u; /* 不可连续两房逃跑 / Cannot flee two rooms in a row */
    s->undo_count = 0u;
    if (s->flees_used < 255u) s->flees_used++;
    if (s->room_no < 255u) s->room_no++;
    return PN_OK;
}

pn_status_t pn_scoundrel_undo(pn_sc_state_t *s) {
    if (!s || s->outcome != PN_SC_ONGOING || s->mode == PN_SC_MODE_HARDCORE || s->undo_count == 0u) return PN_EMPTY;
    const pn_sc_undo_t *u = &s->undo[--s->undo_count];
    s->hp = u->hp;
    memcpy(s->room, u->room, sizeof s->room);
    s->room_cleared = u->room_cleared;
    s->potion_used = u->potion_used;
    s->can_flee = u->can_flee;
    s->weapon = u->weapon;
    memcpy(s->slain, u->slain, sizeof s->slain);
    s->slain_count = u->slain_count;
    s->last_card = u->last_card;
    s->monsters_slain = u->monsters_slain;
    s->actions = u->actions;
    s->selected_slot = PN_SC_NONE;
    return PN_OK;
}

static bool validate_weapon_slain(uint8_t mode, uint8_t weapon, const uint8_t *slain, uint8_t slain_count) {
    if (slain_count > PN_SC_MAX_SLAIN) return false;
    if (weapon == PN_SC_CARD_NONE) return slain_count == 0u;
    if (!pn_sc_is_weapon(weapon)) return false;
    for (unsigned i = 0; i < slain_count; i++) {
        if (!pn_sc_is_monster(slain[i])) return false;
        if (i > 0u) {
            unsigned prev = pn_sc_card_rank(slain[i - 1u]);
            unsigned cur = pn_sc_card_rank(slain[i]);
            if (mode == PN_SC_MODE_HARDCORE ? !(cur < prev) : !(cur <= prev)) return false;
        }
    }
    return true;
}

pn_status_t pn_sc_encode_state(const pn_sc_state_t *s, bool has_saved, uint8_t chosen_mode,
                               uint8_t out[PN_SC_SAVE_BYTES]) {
    if (!s || !out) return PN_INVALID;
    memset(out, 0, PN_SC_SAVE_BYTES);
    wr32(out + 0, SC_SAVE_MAGIC);
    wr16(out + 4, SC_SAVE_VER);
    out[6] = has_saved ? 1u : 0u;
    out[7] = (chosen_mode == PN_SC_MODE_HARDCORE) ? PN_SC_MODE_HARDCORE : PN_SC_MODE_STANDARD;
    wr32(out + 8, s->seed);
    out[12] = s->mode;
    out[13] = s->outcome;
    out[14] = (uint8_t)(s->hp < 0 ? 0 : s->hp);
    out[15] = s->deck_head;
    out[16] = s->deck_count;
    out[17] = s->room_cleared;
    out[18] = s->room_no;
    out[19] = s->potion_used;
    out[20] = s->can_flee;
    out[21] = s->fled_last;
    out[22] = s->weapon;
    out[23] = s->slain_count;
    out[24] = s->last_card;
    out[25] = s->flees_used;
    out[26] = s->monsters_slain;
    out[27] = s->undo_count;
    out[28] = s->has_best;
    out[29] = 0u;
    wr16(out + 30, s->actions);
    wr16(out + 32, (uint16_t)s->score);
    wr16(out + 34, (uint16_t)s->best_score);
    wr32(out + 36, s->total_games);
    wr32(out + 40, s->total_wins);
    wr64(out + 44, s->elapsed_ms);
    memcpy(out + 52, s->room, PN_SC_ROOM_SLOTS);
    memcpy(out + 56, s->deck, PN_SC_DECK_SIZE);
    memcpy(out + 100, s->slain, PN_SC_MAX_SLAIN);

    for (unsigned i = 0; i < s->undo_count && i < PN_SC_UNDO_MAX; i++) {
        uint8_t *p = out + 128u + i * 40u;
        const pn_sc_undo_t *u = &s->undo[i];
        p[0] = (uint8_t)(u->hp < 0 ? 0 : u->hp);
        memcpy(p + 1, u->room, PN_SC_ROOM_SLOTS);
        p[5] = u->room_cleared;
        p[6] = u->potion_used;
        p[7] = u->can_flee;
        p[8] = u->weapon;
        p[9] = u->slain_count;
        p[10] = u->last_card;
        p[11] = u->monsters_slain;
        wr16(p + 12, u->actions);
        memcpy(p + 14, u->slain, PN_SC_MAX_SLAIN);
    }
    return PN_OK;
}

pn_status_t pn_sc_decode_state(const uint8_t *in, size_t n, pn_sc_state_t *out, bool *has_saved, uint8_t *chosen_mode) {
    if (!in || !out || !has_saved || !chosen_mode || n != PN_SC_SAVE_BYTES) return PN_CORRUPT;
    if (rd32(in + 0) != SC_SAVE_MAGIC || rd16(in + 4) != SC_SAVE_VER) return PN_CORRUPT;
    uint8_t has = in[6], cmode = in[7];
    uint8_t mode = in[12], outcome = in[13], hp = in[14], dhead = in[15], dcnt = in[16];
    uint8_t rclr = in[17], rno = in[18], pused = in[19], cflee = in[20], flast = in[21];
    uint8_t weapon = in[22], scnt = in[23], lcard = in[24], ucnt = in[27], hbest = in[28];
    if (has > 1u || cmode > 1u || mode > 1u || outcome > PN_SC_DEFEAT || hp > PN_SC_MAX_HP) return PN_CORRUPT;
    if (dhead >= PN_SC_DECK_SIZE || dcnt > PN_SC_DECK_SIZE || rclr > PN_SC_ROOM_SLOTS) return PN_CORRUPT;
    if (pused > 1u || cflee > 1u || flast > 1u || scnt > PN_SC_MAX_SLAIN || ucnt > PN_SC_UNDO_MAX || hbest > 1u)
        return PN_CORRUPT;
    if (in[29] != 0u || in[126] != 0u || in[127] != 0u) return PN_CORRUPT;

    pn_sc_state_t st;
    memset(&st, 0, sizeof st);
    st.seed = rd32(in + 8);
    st.mode = mode;
    st.outcome = outcome;
    st.hp = (int8_t)hp;
    st.deck_head = dhead;
    st.deck_count = dcnt;
    st.room_cleared = rclr;
    st.room_no = rno;
    st.potion_used = pused;
    st.can_flee = cflee;
    st.fled_last = flast;
    st.weapon = weapon;
    st.slain_count = scnt;
    st.last_card = lcard;
    st.flees_used = in[25];
    st.monsters_slain = in[26];
    st.undo_count = ucnt;
    st.has_best = hbest;
    st.actions = rd16(in + 30);
    st.score = (int16_t)rd16(in + 32);
    st.best_score = (int16_t)rd16(in + 34);
    st.total_games = rd32(in + 36);
    st.total_wins = rd32(in + 40);
    st.elapsed_ms = rd64(in + 44);
    st.selected_slot = PN_SC_NONE;
    memcpy(st.room, in + 52, PN_SC_ROOM_SLOTS);
    memcpy(st.deck, in + 56, PN_SC_DECK_SIZE);
    memcpy(st.slain, in + 100, PN_SC_MAX_SLAIN);

    if (st.total_wins > st.total_games) return PN_CORRUPT;

    if (has) {
        if (st.seed == 0u || st.room_no == 0u) return PN_CORRUPT;
        if (st.last_card != PN_SC_CARD_NONE && !pn_scoundrel_card_valid(st.last_card)) return PN_CORRUPT;
        if (!validate_weapon_slain(st.mode, st.weapon, st.slain, st.slain_count)) return PN_CORRUPT;
        if (st.outcome == PN_SC_DEFEAT && st.hp != 0) return PN_CORRUPT;
        if (st.outcome != PN_SC_DEFEAT && st.hp == 0) return PN_CORRUPT;
        if ((st.mode == PN_SC_MODE_HARDCORE || st.outcome != PN_SC_ONGOING) && st.undo_count != 0u) return PN_CORRUPT;

        /* 核对所有仍在场/队列/武器/斩杀链上的牌均合法且无重复 / Verify all live cards are legal and unique */
        uint64_t seen = 0u;
        for (unsigned i = 0; i < st.deck_count; i++) {
            uint8_t c = st.deck[((unsigned)st.deck_head + i) % PN_SC_DECK_SIZE];
            if (!pn_scoundrel_card_valid(c)) return PN_CORRUPT;
            uint64_t bit = 1ull << card_bit_index(c);
            if (seen & bit) return PN_CORRUPT;
            seen |= bit;
        }
        for (unsigned i = 0; i < PN_SC_ROOM_SLOTS; i++) {
            if (st.room[i] == PN_SC_CARD_NONE) continue;
            if (!pn_scoundrel_card_valid(st.room[i])) return PN_CORRUPT;
            uint64_t bit = 1ull << card_bit_index(st.room[i]);
            if (seen & bit) return PN_CORRUPT;
            seen |= bit;
        }
        if (st.weapon != PN_SC_CARD_NONE) {
            uint64_t bit = 1ull << card_bit_index(st.weapon);
            if (seen & bit) return PN_CORRUPT;
            seen |= bit;
        }
        for (unsigned i = 0; i < st.slain_count; i++) {
            uint64_t bit = 1ull << card_bit_index(st.slain[i]);
            if (seen & bit) return PN_CORRUPT;
            seen |= bit;
        }

        for (unsigned i = 0; i < st.undo_count; i++) {
            const uint8_t *p = in + 128u + i * 40u;
            pn_sc_undo_t *u = &st.undo[i];
            if (p[0] == 0u || p[0] > PN_SC_MAX_HP || p[5] > PN_SC_ROOM_SLOTS || p[6] > 1u || p[7] > 1u ||
                p[9] > PN_SC_MAX_SLAIN)
                return PN_CORRUPT;
            u->hp = (int8_t)p[0];
            memcpy(u->room, p + 1, PN_SC_ROOM_SLOTS);
            u->room_cleared = p[5];
            u->potion_used = p[6];
            u->can_flee = p[7];
            u->weapon = p[8];
            u->slain_count = p[9];
            u->last_card = p[10];
            u->monsters_slain = p[11];
            u->actions = rd16(p + 12);
            memcpy(u->slain, p + 14, PN_SC_MAX_SLAIN);
            if (u->last_card != PN_SC_CARD_NONE && !pn_scoundrel_card_valid(u->last_card)) return PN_CORRUPT;
            if (!validate_weapon_slain(st.mode, u->weapon, u->slain, u->slain_count)) return PN_CORRUPT;
            for (unsigned k = 0; k < PN_SC_ROOM_SLOTS; k++) {
                if (u->room[k] != PN_SC_CARD_NONE && !pn_scoundrel_card_valid(u->room[k])) return PN_CORRUPT;
            }
        }
    } else {
        st.hp = PN_SC_MAX_HP;
        st.weapon = PN_SC_CARD_NONE;
        st.last_card = PN_SC_CARD_NONE;
        st.undo_count = 0u;
    }

    *out = st;
    *has_saved = (has != 0u);
    *chosen_mode = cmode;
    return PN_OK;
}

static bool save_paths(const sc_t *g, char a[PN_JOURNAL_PATH_MAX], char b[PN_JOURNAL_PATH_MAX]) {
    if (!g->root[0]) return false;
    int na = snprintf(a, PN_JOURNAL_PATH_MAX, "%s/scoundrel.a", g->root);
    int nb = snprintf(b, PN_JOURNAL_PATH_MAX, "%s/scoundrel.b", g->root);
    return na > 0 && (size_t)na < PN_JOURNAL_PATH_MAX && nb > 0 && (size_t)nb < PN_JOURNAL_PATH_MAX;
}

static void tick_clock(sc_t *g, uint64_t now) {
    if (g->running && g->state.outcome == PN_SC_ONGOING) {
        if (!g->fresh_clock && now > g->last) g->state.elapsed_ms += now - g->last;
        g->last = now;
        g->fresh_clock = false;
    }
}

static pn_status_t sc_storage(sc_t *g, pn_sc_state_t *s, bool *has_saved, uint8_t *cmode, bool write) {
    char a[PN_JOURNAL_PATH_MAX], b[PN_JOURNAL_PATH_MAX];
    if (!save_paths(g, a, b)) return PN_IO;
    uint8_t *buf = pn_alloc_tagged(g->pool, PN_MEM_UI, PN_SC_SAVE_BYTES);
    if (!buf) return PN_NO_MEMORY;
    pn_media_lease_t lease = {0};
    pn_journal_files_t files;
    pn_journal_io_t io;
    pn_status_t r = pn_media_acquire(g->media, write ? PN_MEDIA_WRITE : PN_MEDIA_READ, &lease);
    if (r == PN_OK) r = pn_journal_files_init(&files, g->media, &lease, a, b, &io);
    if (r == PN_OK) {
        if (write) {
            r = pn_sc_encode_state(s, g->has, g->chosen_mode, buf);
            if (r == PN_OK) r = pn_blob_save_bounded(&io, g->pool, buf, PN_SC_SAVE_BYTES, PN_SC_SAVE_BYTES);
        } else {
            size_t got = 0;
            r = pn_blob_load_bounded(&io, g->pool, buf, PN_SC_SAVE_BYTES, &got);
            if (r == PN_OK) r = pn_sc_decode_state(buf, got, s, has_saved, cmode);
        }
    }
    if (lease.ticket) (void)pn_media_release(g->media, &lease);
    pn_free(buf);
    return r;
}

static pn_status_t save_now(sc_t *g, uint64_t now) {
    if (!g->dirty) return PN_OK;
    if (!g->media || !g->root[0]) {
        g->dirty = false;
        return PN_OK;
    }
    pn_status_t s = sc_storage(g, &g->state, NULL, NULL, true);
    if (s == PN_OK) g->dirty = false;
    else g->retry = now;
    return s;
}

static void mark_full(pn_scoundrel_t *u, sc_t *g) {
    g->full = true;
    g->patch = false;
    u->presented = false;
}

static void mark_patch(pn_scoundrel_t *u, sc_t *g) {
    if (!g->full) g->patch = true;
    u->presented = false;
}

static void snapshot_shown(sc_t *g) {
    memcpy(g->shown_room, g->state.room, sizeof g->shown_room);
    g->shown_selected = g->state.selected_slot;
    g->shown_weapon = g->state.weapon;
    memcpy(g->shown_slain, g->state.slain, sizeof g->shown_slain);
    g->shown_slain_count = g->state.slain_count;
    g->shown_potion_used = g->state.potion_used;
    g->shown_can_flee = g->state.can_flee;
    g->shown_undo_count = g->state.undo_count;
    g->shown_hp = g->state.hp;
    g->shown_deck_count = g->state.deck_count;
    g->shown_room_no = g->state.room_no;
    g->shown_room_cleared = g->state.room_cleared;
    g->shown_notice = g->notice;
}

static pn_status_t start_run(pn_scoundrel_t *u, sc_t *g, uint32_t seed, uint8_t mode, uint64_t now) {
    pn_scoundrel_new_run(&g->state, seed, mode);
    g->chosen_mode = g->state.mode;
    g->has = true;
    g->dirty = true;
    g->changed = now;
    g->running = true;
    g->fresh_clock = true;
    g->last = now;
    g->screen = PN_SC_PLAY;
    g->notice = NULL;
    g->pending_start = false;
    /* 开局/重开立即落盘，避免空闲窗内断电丢局 / Persist immediately on start so power loss keeps the run. */
    if (save_now(g, now) != PN_OK) g->notice = "保存失败，退出前将重试";
    mark_full(u, g);
    return PN_OK;
}

pn_status_t pn_scoundrel_open(pn_scoundrel_t *u, pn_pool_t *pool, pn_media_t *media, const char *root, uint64_t now) {
    if (!u || !pool || !media || !root || !*root || strlen(root) >= PN_JOURNAL_PATH_MAX - 20u) return PN_INVALID;
    if (u->impl) return PN_BUSY;

    sc_t *g = pn_alloc_tagged(pool, PN_MEM_UI, sizeof *g);
    if (!g) return PN_NO_MEMORY;
    memset(g, 0, sizeof *g);
    g->pool = pool;
    g->media = media;
    strcpy(g->root, root);
    g->state.hp = PN_SC_MAX_HP;
    g->state.weapon = PN_SC_CARD_NONE;
    g->state.selected_slot = PN_SC_NONE;
    g->state.last_card = PN_SC_CARD_NONE;
    g->state.best_score = PN_SC_NO_BEST;
    g->screen = PN_SC_SELECT;
    g->full = true;
    g->last = now;
    g->fresh_clock = true;

    bool has = false;
    uint8_t cmode = PN_SC_MODE_STANDARD;
    pn_status_t s = sc_storage(g, &g->state, &has, &cmode, false);
    if (s == PN_OK) {
        g->has = has;
        g->chosen_mode = cmode;
        if (has && g->state.outcome == PN_SC_ONGOING) {
            g->screen = PN_SC_PLAY;
            g->running = true;
        }
    } else if (s == PN_CORRUPT) {
        g->load_error = true;
        g->screen = PN_SC_ERROR;
        g->notice = "存档已损坏，已保留原文件";
    } else if (s != PN_EMPTY) {
        g->load_error = true;
        g->screen = PN_SC_ERROR;
        g->notice = "存档读取失败，请重试";
    }

    u->impl = g;
    u->active = true;
    u->presented = false;
    return PN_OK;
}

static pn_status_t apply_play_resolution(pn_scoundrel_t *u, sc_t *g, unsigned slot, bool use_weapon, uint64_t now) {
    uint8_t card = g->state.room[slot];
    bool wasted_potion = pn_sc_is_potion(card) && g->state.potion_used != 0u;
    pn_status_t r = pn_scoundrel_resolve(&g->state, slot, use_weapon);
    if (r != PN_OK) return r;
    g->dirty = true;
    g->changed = now;
    g->notice = wasted_potion ? "本房已饮过药水，该血瓶已弃置" : NULL;
    if (g->state.outcome != PN_SC_ONGOING) {
        g->running = false;
        g->screen = PN_SC_COMPLETE;
        if (save_now(g, now) != PN_OK) g->notice = "战绩保存失败，退出前将重试";
        mark_full(u, g);
    } else {
        mark_patch(u, g);
    }
    return PN_OK;
}

pn_status_t pn_scoundrel_event(pn_scoundrel_t *u, int cmd, uint64_t now) {
    if (!u || !u->impl) return PN_INVALID;
    sc_t *g = u->impl;
    tick_clock(g, now);

    if (g->screen == PN_SC_ERROR) {
        if (cmd == PN_SC_BACK || cmd == PN_SC_EXIT) {
            if (g->dirty) {
                pn_status_t s = save_now(g, now);
                if (s != PN_OK) {
                    g->notice = "存储仍不可用，请重试";
                    mark_full(u, g);
                    return s;
                }
            }
            u->active = false;
            return PN_OK;
        }
        if (cmd == PN_SC_RETRY || cmd == PN_SC_START) {
            if (g->load_error) {
                bool has = false;
                uint8_t cmode = PN_SC_MODE_STANDARD;
                pn_status_t s = sc_storage(g, &g->state, &has, &cmode, false);
                if (s == PN_OK) {
                    g->load_error = false;
                    g->has = has;
                    g->chosen_mode = cmode;
                    g->screen = (has && g->state.outcome == PN_SC_ONGOING) ? PN_SC_PLAY : PN_SC_SELECT;
                    g->running = (g->screen == PN_SC_PLAY);
                    g->fresh_clock = true;
                    g->notice = NULL;
                    mark_full(u, g);
                    return PN_OK;
                }
                if (s == PN_EMPTY) {
                    g->load_error = false;
                    g->has = false;
                    g->screen = PN_SC_SELECT;
                    g->notice = NULL;
                    mark_full(u, g);
                    return PN_OK;
                }
                g->notice = (s == PN_CORRUPT) ? "存档已损坏，已保留原文件" : "存档读取失败，请重试";
                mark_full(u, g);
                return s;
            }
            pn_status_t s = save_now(g, now);
            if (s != PN_OK) {
                g->notice = "存储仍不可用，请重试";
                mark_full(u, g);
                return s;
            }
            g->screen = (g->has && g->state.outcome == PN_SC_ONGOING) ? PN_SC_PLAY : PN_SC_SELECT;
            g->running = (g->screen == PN_SC_PLAY);
            g->notice = NULL;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_SC_CHOOSE && !g->load_error) {
            g->screen = PN_SC_SELECT;
            g->notice = NULL;
            mark_full(u, g);
            return PN_OK;
        }
        return PN_EMPTY;
    }

    if (g->screen == PN_SC_CONFIRM) {
        if (cmd == PN_SC_EXIT) {
            g->running = false;
            pn_status_t s = save_now(g, now);
            if (s != PN_OK) {
                g->screen = PN_SC_ERROR;
                g->notice = "退出前保存失败，请重试";
                mark_full(u, g);
                return s;
            }
            u->active = false;
            return PN_OK;
        }
        if (cmd == PN_SC_CANCEL || cmd == PN_SC_BACK) {
            g->screen = (g->has && g->state.outcome == PN_SC_ONGOING) ? PN_SC_PLAY : PN_SC_SELECT;
            g->running = (g->screen == PN_SC_PLAY);
            g->fresh_clock = true;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_SC_ACCEPT) {
            uint32_t seed = mix_seed(now, g->state.seed, g->state.total_games);
            return start_run(u, g, seed, g->state.mode, now);
        }
        return PN_EMPTY;
    }

    if (g->screen == PN_SC_SELECT) {
        if (cmd == PN_SC_BACK || cmd == PN_SC_EXIT) {
            pn_status_t s = save_now(g, now);
            if (s != PN_OK) {
                g->screen = PN_SC_ERROR;
                g->notice = "退出前保存失败，请重试";
                mark_full(u, g);
                return s;
            }
            u->active = false;
            return PN_OK;
        }
        if (cmd == PN_SC_MODE_BASE + PN_SC_MODE_STANDARD || cmd == PN_SC_MODE_BASE + PN_SC_MODE_HARDCORE) {
            uint8_t next = (uint8_t)(cmd - PN_SC_MODE_BASE);
            if (g->chosen_mode != next) {
                g->chosen_mode = next;
                mark_full(u, g);
                return PN_OK;
            }
            return PN_EMPTY;
        }
        if (cmd == PN_SC_RESUME) {
            if (!g->has || g->state.outcome != PN_SC_ONGOING) return PN_EMPTY;
            g->screen = PN_SC_PLAY;
            g->running = true;
            g->fresh_clock = true;
            g->notice = NULL;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_SC_REPLAY_SEED) {
            if (!g->has || g->state.seed == 0u) return PN_EMPTY;
            return start_run(u, g, g->state.seed, g->chosen_mode, now);
        }
        if (cmd == PN_SC_START) {
            uint32_t seed = mix_seed(now, g->state.seed, g->state.total_games);
            return start_run(u, g, seed, g->chosen_mode, now);
        }
        return PN_EMPTY;
    }

    if (g->screen == PN_SC_COMPLETE) {
        if (cmd == PN_SC_BACK || cmd == PN_SC_EXIT) {
            pn_status_t s = save_now(g, now);
            if (s != PN_OK) {
                g->screen = PN_SC_ERROR;
                g->notice = "退出前保存失败，请重试";
                mark_full(u, g);
                return s;
            }
            u->active = false;
            return PN_OK;
        }
        if (cmd == PN_SC_START) {
            uint32_t seed = mix_seed(now, g->state.seed, g->state.total_games);
            return start_run(u, g, seed, g->state.mode, now);
        }
        if (cmd == PN_SC_REPLAY_SEED) {
            return start_run(u, g, g->state.seed, g->state.mode, now);
        }
        if (cmd == PN_SC_CHOOSE) {
            g->screen = PN_SC_SELECT;
            g->notice = NULL;
            mark_full(u, g);
            return PN_OK;
        }
        if (cmd == PN_SC_CLEAN) {
            mark_full(u, g);
            return PN_OK;
        }
        return PN_EMPTY;
    }

    /* PN_SC_PLAY 主游玩态（含同页怪物二选一子态） / Main play screen including inline monster-choice sub-state */
    if (cmd == PN_SC_BACK || cmd == PN_SC_EXIT) {
        g->running = false;
        g->state.selected_slot = PN_SC_NONE;
        pn_status_t s = save_now(g, now);
        if (s != PN_OK) {
            g->screen = PN_SC_ERROR;
            g->notice = "退出前保存失败，请重试";
            mark_full(u, g);
            return s;
        }
        u->active = false;
        return PN_OK;
    }
    if (cmd == PN_SC_CHOOSE) {
        g->running = false;
        g->state.selected_slot = PN_SC_NONE;
        g->screen = PN_SC_SELECT;
        g->notice = NULL;
        (void)save_now(g, now);
        mark_full(u, g);
        return PN_OK;
    }
    if (cmd == PN_SC_RESTART) {
        g->running = false;
        g->state.selected_slot = PN_SC_NONE;
        g->screen = PN_SC_CONFIRM;
        mark_full(u, g);
        return PN_OK;
    }
    if (cmd == PN_SC_CLEAN) {
        mark_full(u, g);
        return PN_OK;
    }
    if (cmd == PN_SC_DESELECT) {
        if (g->state.selected_slot == PN_SC_NONE) return PN_EMPTY;
        g->state.selected_slot = PN_SC_NONE;
        g->notice = NULL;
        mark_patch(u, g);
        return PN_OK;
    }

    /* 未收到上一帧成功呈现回执前拒收游戏动作，避免状态超前画面 / Gate moves until previous frame receipt arrives */
    if (!u->presented) return PN_BUSY;

    if (cmd == PN_SC_UNDO_MOVE) {
        if (g->state.selected_slot != PN_SC_NONE) {
            g->state.selected_slot = PN_SC_NONE;
            g->notice = NULL;
            mark_patch(u, g);
            return PN_OK;
        }
        pn_status_t r = pn_scoundrel_undo(&g->state);
        if (r != PN_OK) {
            const char *msg = (g->state.mode == PN_SC_MODE_HARDCORE) ? "硬核模式不可撤销" : "仅支持本房内撤销（发牌后清空）";
            if (g->notice != msg) {
                g->notice = msg;
                mark_patch(u, g);
                return PN_OK;
            }
            return PN_EMPTY;
        }
        g->dirty = true;
        g->changed = now;
        g->notice = "已撤销本房上一步";
        mark_patch(u, g);
        return PN_OK;
    }

    if (cmd == PN_SC_FLEE) {
        pn_status_t r = pn_scoundrel_flee(&g->state);
        if (r != PN_OK) {
            const char *msg = g->state.fled_last ? "不可连续两间房逃跑"
                              : (g->state.room_cleared > 0u) ? "本房已处理过牌，无法逃跑"
                                                             : "牌堆已空，无法逃跑";
            if (g->notice != msg) {
                g->notice = msg;
                mark_patch(u, g);
                return PN_OK;
            }
            return PN_EMPTY;
        }
        g->dirty = true;
        g->changed = now;
        g->notice = "已逃离该房间（4张牌沉底，下房禁逃）";
        (void)save_now(g, now);
        mark_patch(u, g);
        return PN_OK;
    }

    if (cmd == PN_SC_FIGHT_WEAPON || cmd == PN_SC_FIGHT_BARE) {
        unsigned slot = g->state.selected_slot;
        if (slot >= PN_SC_ROOM_SLOTS || !pn_sc_is_monster(g->state.room[slot])) return PN_EMPTY;
        bool use_w = (cmd == PN_SC_FIGHT_WEAPON);
        if (use_w && !pn_scoundrel_can_use_weapon(&g->state, g->state.room[slot])) return PN_EMPTY;
        return apply_play_resolution(u, g, slot, use_w, now);
    }

    if (cmd >= PN_SC_SLOT_BASE && cmd < PN_SC_SLOT_BASE + (int)PN_SC_ROOM_SLOTS) {
        unsigned slot = (unsigned)(cmd - PN_SC_SLOT_BASE);
        uint8_t card = g->state.room[slot];
        if (card == PN_SC_CARD_NONE) return PN_EMPTY;

        /* 再次点击已选怪物则取消选中 / Tap selected monster again to deselect */
        if (g->state.selected_slot == slot) {
            g->state.selected_slot = PN_SC_NONE;
            g->notice = NULL;
            mark_patch(u, g);
            return PN_OK;
        }

        /* 武器与血瓶点击即结算 / Weapons and potions resolve immediately */
        if (!pn_sc_is_monster(card)) {
            return apply_play_resolution(u, g, slot, false, now);
        }

        /* 怪物：可用武器时进入二选一；不可用武器但致死时二次确认防手滑；其余非致死单点空手直结 */
        bool can_w = pn_scoundrel_can_use_weapon(&g->state, card);
        bool lethal_bare = ((int)g->state.hp <= (int)pn_sc_card_rank(card));
        if (can_w || lethal_bare) {
            g->state.selected_slot = (uint8_t)slot;
            g->notice = NULL;
            mark_patch(u, g);
            return PN_OK;
        }
        return apply_play_resolution(u, g, slot, false, now);
    }

    return PN_EMPTY;
}

pn_status_t pn_scoundrel_tick(pn_scoundrel_t *u, uint64_t now) {
    if (!u || !u->impl) return PN_INVALID;
    sc_t *g = u->impl;
    if (!g->dirty || !u->presented) return PN_EMPTY;
    if (now < g->changed || now - g->changed < SC_SAVE_IDLE_MS) return PN_EMPTY;
    if (g->retry && now >= g->retry && now - g->retry < SC_SAVE_RETRY_MS) return PN_EMPTY;
    return save_now(g, now);
}

pn_status_t pn_scoundrel_pause(pn_scoundrel_t *u, uint64_t now) {
    if (!u || !u->impl) return PN_INVALID;
    sc_t *g = u->impl;
    tick_clock(g, now);
    g->fresh_clock = true;
    pn_status_t s = save_now(g, now);
    if (s != PN_OK) {
        g->screen = PN_SC_ERROR;
        g->notice = "休眠前保存失败，请重试";
        mark_full(u, g);
    }
    return s;
}

void pn_scoundrel_displayed(pn_scoundrel_t *u, bool ok) {
    if (!u || !u->impl) return;
    sc_t *g = u->impl;
    u->presented = ok;
    if (ok) {
        g->full = false;
        g->patch = false;
        snapshot_shown(g);
    } else {
        g->full = true;
        g->patch = false;
    }
}

void pn_scoundrel_close(pn_scoundrel_t *u) {
    if (!u) return;
    if (u->impl) {
        pn_free(u->impl);
        u->impl = NULL;
    }
    u->active = false;
    u->presented = false;
}

const pn_sc_state_t *pn_scoundrel_state(const pn_scoundrel_t *u) {
    return (u && u->impl) ? &((const sc_t *)u->impl)->state : NULL;
}

pn_sc_screen_t pn_scoundrel_screen(const pn_scoundrel_t *u) {
    return (u && u->impl) ? ((const sc_t *)u->impl)->screen : PN_SC_SELECT;
}
