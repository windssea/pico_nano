/* SPDX-License-Identifier: Apache-2.0
 * 地牢纸牌主机测试：44张牌组、武器磨损(<=与<)、单房限药、逃跑防连逃、本房2步撤销、计分与A/B存档屏障。
 * Host tests for Scoundrel: 44-card deck, weapon wear (<= & <), 1-potion/room, flee, 2-step in-room undo, scoring and A/B barriers. */
#define _POSIX_C_SOURCE 200809L
#include "pn_scoundrel.h"
#include "../../components/pn_library/pn_scoundrel_internal.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int __real_fsync(int fd);
static bool fail_fsync;
int __wrap_fsync(int fd) {
    if (fail_fsync) {
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}

static void clean_dir(const char *root) {
    char p[512];
    snprintf(p, sizeof p, "%s/scoundrel.a", root);
    (void)unlink(p);
    snprintf(p, sizeof p, "%s/scoundrel.b", root);
    (void)unlink(p);
    assert(rmdir(root) == 0);
}

int main(void) {
    /* 1. 验证44张牌组唯一性与确定性洗牌 / Verify 44-card deck uniqueness and deterministic shuffle */
    {
        pn_sc_state_t a = {0}, b = {0};
        pn_scoundrel_new_run(&a, 0x12345678u, PN_SC_MODE_STANDARD);
        pn_scoundrel_new_run(&b, 0x12345678u, PN_SC_MODE_STANDARD);
        assert(memcmp(&a, &b, sizeof a) == 0);
        assert(a.hp == 20 && a.deck_count == 40u && a.room_no == 1u && a.can_flee == 1u);
        uint64_t seen = 0;
        unsigned count = 0;
        for (unsigned i = 0; i < PN_SC_ROOM_SLOTS; i++) {
            assert(pn_scoundrel_card_valid(a.room[i]));
            uint8_t c = a.room[i];
            unsigned s = pn_sc_card_suit(c), r = pn_sc_card_rank(c);
            unsigned idx = s == 0 ? (r - 2) : s == 1 ? (13 + r - 2) : s == 2 ? (26 + r - 2) : (35 + r - 2);
            assert((seen & (1ull << idx)) == 0);
            seen |= 1ull << idx;
            count++;
        }
        for (unsigned i = 0; i < a.deck_count; i++) {
            uint8_t c = a.deck[(a.deck_head + i) % PN_SC_DECK_SIZE];
            assert(pn_scoundrel_card_valid(c));
            unsigned s = pn_sc_card_suit(c), r = pn_sc_card_rank(c);
            unsigned idx = s == 0 ? (r - 2) : s == 1 ? (13 + r - 2) : s == 2 ? (26 + r - 2) : (35 + r - 2);
            assert((seen & (1ull << idx)) == 0);
            seen |= 1ull << idx;
            count++;
        }
        assert(count == PN_SC_DECK_SIZE);
        /* 红桃/方块J/Q/K/A非法 / Red face cards and red Aces are invalid */
        assert(!pn_scoundrel_card_valid(pn_sc_make_card(PN_SC_DIAMOND, 11)));
        assert(!pn_scoundrel_card_valid(pn_sc_make_card(PN_SC_HEART, 14)));
        assert(pn_scoundrel_card_valid(pn_sc_make_card(PN_SC_SPADE, 14)));
    }

    /* 2. 武器磨损(<=与<)、空手硬扛、单房限药、本房2步撤销与补牌清空撤销 / Weapon wear, barehanded, potion cap, 2-step in-room undo */
    {
        pn_sc_state_t s = {0};
        pn_scoundrel_new_run(&s, 42u, PN_SC_MODE_STANDARD);
        /* 构造可控房间：♦8, ♣K(13), ♠K(13), ♥7 / Controlled room */
        s.room[0] = pn_sc_make_card(PN_SC_DIAMOND, 8);
        s.room[1] = pn_sc_make_card(PN_SC_CLUB, 13);
        s.room[2] = pn_sc_make_card(PN_SC_SPADE, 13);
        s.room[3] = pn_sc_make_card(PN_SC_HEART, 7);
        s.deck_count = 3;
        s.deck_head = 0;
        s.deck[0] = pn_sc_make_card(PN_SC_HEART, 5);
        s.deck[1] = pn_sc_make_card(PN_SC_SPADE, 4);
        s.deck[2] = pn_sc_make_card(PN_SC_CLUB, 2);

        /* 第1步：装备♦8 / Step 1: equip ♦8 */
        assert(pn_scoundrel_resolve(&s, 0, false) == PN_OK);
        assert(s.weapon == pn_sc_make_card(PN_SC_DIAMOND, 8) && s.undo_count == 1u && s.can_flee == 0u);
        assert(pn_scoundrel_can_use_weapon(&s, s.room[1]) && pn_scoundrel_weapon_damage(&s, s.room[1]) == 5);

        /* 第2步：用♦8斩♣K(13)，掉5血至15，上限变<=13 / Step 2: slay ♣K with ♦8 */
        assert(pn_scoundrel_resolve(&s, 1, true) == PN_OK);
        assert(s.hp == 15 && s.slain_count == 1u && s.undo_count == 2u);
        /* 标准模式允许<=13，故♠K(13)仍可用武器；不可斩♠A(14) / Standard allows <=13, rejects 14 */
        assert(pn_scoundrel_can_use_weapon(&s, s.room[2]));
        assert(!pn_scoundrel_can_use_weapon(&s, pn_sc_make_card(PN_SC_SPADE, 14)));

        /* 撤销第2步再撤销第1步，回到房间起点 / Undo step 2 then step 1 */
        assert(pn_scoundrel_undo(&s) == PN_OK && s.hp == 20 && s.slain_count == 0u && s.undo_count == 1u);
        assert(pn_scoundrel_undo(&s) == PN_OK && s.weapon == PN_SC_CARD_NONE && s.undo_count == 0u && s.can_flee == 1u);
        assert(pn_scoundrel_undo(&s) == PN_EMPTY);

        /* 重做3步触发补牌，补牌后本房撤销清空且遗留牌保持原槽 / 3rd step refills room and clears undo */
        assert(pn_scoundrel_resolve(&s, 0, false) == PN_OK);
        assert(pn_scoundrel_resolve(&s, 1, true) == PN_OK);
        assert(pn_scoundrel_resolve(&s, 2, true) == PN_OK);
        assert(s.hp == 10 && s.room_no == 2u && s.undo_count == 0u && s.can_flee == 1u && s.deck_count == 0u);
        assert(s.room[3] == pn_sc_make_card(PN_SC_HEART, 7)); /* 遗留牌保留在槽3 / Survivor stayed in slot 3 */
        assert(pn_scoundrel_undo(&s) == PN_EMPTY);

        /* 单房限喝1瓶药水：先喝♥7回至17，再选♥5被弃置不回血 / One potion per room */
        assert(pn_scoundrel_resolve(&s, 3, false) == PN_OK && s.hp == 17 && s.potion_used == 1u);
        assert(pn_scoundrel_resolve(&s, 0, false) == PN_OK && s.hp == 17);
        /* 末房清完两只小怪：空手扛4掉至13，武器砍2无伤 -> 通关得分+13 / Final room victory */
        assert(pn_scoundrel_resolve(&s, 1, false) == PN_OK && s.hp == 13);
        assert(pn_scoundrel_resolve(&s, 2, true) == PN_OK);
        assert(s.outcome == PN_SC_VICTORY && s.score == 13 && s.total_games == 1u && s.total_wins == 1u);
    }

    /* 3. 硬核模式(<上限且禁撤销)、满血药水通关彩蛋(+20+药水)与阵亡负分 / Hardcore < cap, full-HP potion bonus, defeat score */
    {
        pn_sc_state_t s = {0};
        pn_scoundrel_new_run(&s, 77u, PN_SC_MODE_HARDCORE);
        s.deck_count = 0;
        s.room[0] = pn_sc_make_card(PN_SC_DIAMOND, 9);
        s.room[1] = pn_sc_make_card(PN_SC_SPADE, 9);
        s.room[2] = pn_sc_make_card(PN_SC_CLUB, 9);
        s.room[3] = pn_sc_make_card(PN_SC_HEART, 6);

        assert(pn_scoundrel_resolve(&s, 0, false) == PN_OK);
        assert(s.undo_count == 0u && pn_scoundrel_undo(&s) == PN_EMPTY); /* 硬核禁撤销 / No undo in hardcore */
        assert(pn_scoundrel_resolve(&s, 1, true) == PN_OK && s.hp == 20);
        /* 硬核要求严格 < 9，故♣9不能再用武器 / Hardcore requires strictly < 9 */
        assert(!pn_scoundrel_can_use_weapon(&s, s.room[2]));
        assert(pn_scoundrel_resolve(&s, 2, true) == PN_INVALID);
        /* 空手扛♣9掉至11，再喝♥6回至17通关 / Barehanded ♣9 -> 11, potion -> 17 */
        assert(pn_scoundrel_resolve(&s, 2, false) == PN_OK && s.hp == 11);
        assert(pn_scoundrel_resolve(&s, 3, false) == PN_OK && s.outcome == PN_SC_VICTORY && s.score == 17);

        /* 满血20且最后一张是♥8 -> 官方彩蛋得分 28 / Full-HP last-card potion bonus = 20 + 8 */
        pn_scoundrel_new_run(&s, 88u, PN_SC_MODE_STANDARD);
        s.deck_count = 0;
        s.room[0] = pn_sc_make_card(PN_SC_HEART, 8);
        s.room[1] = PN_SC_CARD_NONE;
        s.room[2] = PN_SC_CARD_NONE;
        s.room[3] = PN_SC_CARD_NONE;
        assert(pn_scoundrel_resolve(&s, 0, false) == PN_OK);
        assert(s.outcome == PN_SC_VICTORY && s.hp == 20 && s.score == 28);

        /* 阵亡负分验证 / Defeat negative score = -sum(remaining monsters) */
        pn_scoundrel_new_run(&s, 99u, PN_SC_MODE_STANDARD);
        s.hp = 5;
        s.deck_count = 2;
        s.deck_head = 0;
        s.deck[0] = pn_sc_make_card(PN_SC_SPADE, 10);
        s.deck[1] = pn_sc_make_card(PN_SC_HEART, 9);
        s.room[0] = pn_sc_make_card(PN_SC_CLUB, 12);
        s.room[1] = pn_sc_make_card(PN_SC_SPADE, 7);
        s.room[2] = PN_SC_CARD_NONE;
        s.room[3] = PN_SC_CARD_NONE;
        assert(pn_scoundrel_resolve(&s, 0, false) == PN_OK);
        assert(s.outcome == PN_SC_DEFEAT && s.hp == 0 && s.score == -17);
    }

    /* 4. 逃跑规则（4牌按序沉底、禁连续逃跑） / Flee moves 4 cards to bottom in order and forbids consecutive flee */
    {
        pn_sc_state_t s = {0};
        pn_scoundrel_new_run(&s, 101u, PN_SC_MODE_STANDARD);
        uint8_t orig[4];
        memcpy(orig, s.room, 4);
        assert(pn_scoundrel_flee(&s) == PN_OK);
        assert(s.fled_last == 1u && s.can_flee == 0u && s.flees_used == 1u && s.deck_count == 40u);
        assert(pn_scoundrel_flee(&s) == PN_EMPTY);
        /* 验证刚才4张牌在牌堆尾部 / Verify the 4 fled cards sit at deck tail in order */
        for (unsigned i = 0; i < 4u; i++) {
            uint8_t tail_c = s.deck[(s.deck_head + s.deck_count - 4u + i) % PN_SC_DECK_SIZE];
            assert(tail_c == orig[i]);
        }
    }

    /* 5. 控制器生命周期、A/B存档往返、fsync失败屏障与坏档保护 / Controller lifecycle, A/B save, fsync barrier and corrupt preservation */
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 2u * 1024u * 1024u, NULL, NULL, NULL) == PN_OK);
    pn_media_t media;
    pn_media_init(&media);
    assert(pn_media_attach(&media, 1u) == PN_OK);

    char root[] = "/tmp/pn-test-scoundrel-XXXXXX";
    assert(mkdtemp(root));
    pn_scoundrel_t game = {0};
    assert(pn_scoundrel_open(&game, &pool, &media, NULL, 100) == PN_INVALID);
    assert(pn_scoundrel_open(&game, &pool, &media, root, 100) == PN_OK);
    assert(pn_scoundrel_screen(&game) == PN_SC_SELECT);

    assert(pn_scoundrel_event(&game, PN_SC_START, 200) == PN_OK);
    assert(pn_scoundrel_screen(&game) == PN_SC_PLAY);
    uint32_t seed0 = pn_scoundrel_state(&game)->seed;
    pn_scoundrel_displayed(&game, true);

    /* 逃跑并重开验证持久恢复 / Flee then close & reopen */
    assert(pn_scoundrel_event(&game, PN_SC_FLEE, 300) == PN_OK);
    assert(pn_scoundrel_state(&game)->flees_used == 1u);
    pn_scoundrel_displayed(&game, true);
    pn_scoundrel_close(&game);

    assert(pn_scoundrel_open(&game, &pool, &media, root, 400) == PN_OK);
    assert(pn_scoundrel_screen(&game) == PN_SC_PLAY);
    assert(pn_scoundrel_state(&game)->seed == seed0 && pn_scoundrel_state(&game)->flees_used == 1u);
    pn_scoundrel_displayed(&game, true);

    /* 模拟fsync失败：走一步后退场与锁屏必须被阻断 / Simulate fsync failure blocking exit and lock */
    int chosen_slot = -1;
    for (int i = 0; i < 4; i++) {
        if (!pn_sc_is_monster(pn_scoundrel_state(&game)->room[i]) ||
            (int)pn_sc_card_rank(pn_scoundrel_state(&game)->room[i]) < (int)pn_scoundrel_state(&game)->hp) {
            chosen_slot = i;
            break;
        }
    }
    assert(chosen_slot >= 0);
    fail_fsync = true;
    assert(pn_scoundrel_event(&game, PN_SC_SLOT_BASE + chosen_slot, 500) == PN_OK);
    pn_scoundrel_displayed(&game, true);
    assert(pn_scoundrel_pause(&game, 600) != PN_OK && pn_scoundrel_screen(&game) == PN_SC_ERROR && game.active);
    assert(pn_scoundrel_event(&game, PN_SC_EXIT, 700) != PN_OK && game.active);

    fail_fsync = false;
    assert(pn_scoundrel_event(&game, PN_SC_RETRY, 800) == PN_OK && pn_scoundrel_screen(&game) == PN_SC_PLAY);
    assert(pn_scoundrel_event(&game, PN_SC_EXIT, 900) == PN_OK && !game.active);
    pn_scoundrel_close(&game);

    /* 破坏双槽存档：加载进入ERROR且保留原文件 / Corrupt both A/B slots: enters ERROR and preserves files */
    char fa[512], fb[512];
    snprintf(fa, sizeof fa, "%s/scoundrel.a", root);
    snprintf(fb, sizeof fb, "%s/scoundrel.b", root);
    FILE *f = fopen(fa, "wb");
    assert(f && fwrite("BAD!", 1, 4, f) == 4 && fclose(f) == 0);
    f = fopen(fb, "wb");
    assert(f && fwrite("BAD!", 1, 4, f) == 4 && fclose(f) == 0);

    assert(pn_scoundrel_open(&game, &pool, &media, root, 1000) == PN_OK);
    assert(pn_scoundrel_screen(&game) == PN_SC_ERROR);
    assert(pn_scoundrel_event(&game, PN_SC_RETRY, 1100) != PN_OK && pn_scoundrel_screen(&game) == PN_SC_ERROR);
    assert(pn_scoundrel_event(&game, PN_SC_CHOOSE, 1200) == PN_EMPTY && pn_scoundrel_screen(&game) == PN_SC_ERROR);
    pn_scoundrel_close(&game);

    clean_dir(root);
    assert(pool.used == 0 && pool.live == 0);
    puts("test_scoundrel passed");
    return 0;
}
