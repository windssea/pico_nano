/* SPDX-License-Identifier: Apache-2.0
 * 中国象棋残局60局核验、特殊棋规、顽强防守对弈、败局悔棋与A/B事务故障回归。/ Xiangqi 60-endgame verification, special rules, stubborn AI defense, loss-undo and A/B fault regressions. */
#define _POSIX_C_SOURCE 200809L
#include "../../components/pn_library/pn_xiangqi_internal.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool fail_sync;
int __real_fsync(int);
int __wrap_fsync(int fd) {
    int r = __real_fsync(fd);
    if (r == 0 && fail_sync) {
        errno = EIO;
        return -1;
    }
    return r;
}

static void clear_files(const char *root) {
    char path[512];
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/xiangqi.%c", root, 'a' + i);
        unlink(path);
    }
}

static void forge_record(const char *root, pn_pool_t *pool, pn_media_t *media, unsigned mode) {
    pn_media_lease_t lease = {0};
    pn_journal_files_t files;
    pn_journal_io_t io;
    uint8_t buf[PN_XQ_SAVE_BYTES];
    size_t sz = 0;
    char a[512], b[512];
    snprintf(a, sizeof a, "%s/xiangqi.a", root);
    snprintf(b, sizeof b, "%s/xiangqi.b", root);
    assert(pn_media_acquire(media, PN_MEDIA_WRITE, &lease) == PN_OK);
    assert(pn_journal_files_init(&files, media, &lease, a, b, &io) == PN_OK);
    assert(pn_blob_load_bounded(&io, pool, buf, PN_XQ_SAVE_BYTES, &sz) == PN_OK && sz == PN_XQ_SAVE_BYTES);
    if (mode == 0) buf[4] = 7;          /* 非法版本号 / Bad version */
    if (mode == 1) buf[7] = 9;          /* 非法结局枚举 / Bad outcome */
    if (mode == 2) buf[178] ^= 0x05;    /* 实时盘面与撤销历史失配 / Unreachable live board */
    if (mode == 3) buf[146] = 89;       /* 篡改主变证明使之非法 / Corrupt proof move */
    assert(pn_blob_save_bounded(&io, pool, buf, sz, PN_XQ_SAVE_BYTES) == PN_OK);
    assert(pn_media_release(media, &lease) == PN_OK);
}

static void test_special_rules(void) {
    uint8_t b[PN_XQ_CELLS] = {0};
    b[0 * 9 + 4] = PN_XQ_BLACK | PN_XQ_KING;
    b[9 * 9 + 4] = PN_XQ_KING;

    /* 白脸将：同列无遮挡时中路车不能横移，但可直走将死。/ Flying General: sole blocker on shared King file cannot leave file. */
    b[5 * 9 + 4] = PN_XQ_ROOK;
    assert(!pn_xiangqi_legal_move(b, 0, 5 * 9 + 4, 5 * 9 + 3));
    assert(pn_xiangqi_legal_move(b, 0, 5 * 9 + 4, 1 * 9 + 4));

    /* 蹩马腿与塞象眼核验。/ Horse-leg block and Elephant-eye block verification. */
    memset(b, 0, sizeof b);
    b[0 * 9 + 3] = PN_XQ_BLACK | PN_XQ_KING;
    b[9 * 9 + 5] = PN_XQ_KING;
    b[4 * 9 + 4] = PN_XQ_HORSE;
    b[3 * 9 + 4] = PN_XQ_PAWN; /* 堵住上方马腿 / Block north horse leg */
    assert(!pn_xiangqi_legal_move(b, 0, 4 * 9 + 4, 2 * 9 + 3));
    assert(!pn_xiangqi_legal_move(b, 0, 4 * 9 + 4, 2 * 9 + 5));
    assert(pn_xiangqi_legal_move(b, 0, 4 * 9 + 4, 3 * 9 + 6));

    b[9 * 9 + 2] = PN_XQ_ELEPHANT;
    b[8 * 9 + 3] = PN_XQ_ADVISOR; /* 塞住右上象眼 / Block NE elephant eye */
    assert(!pn_xiangqi_legal_move(b, 0, 9 * 9 + 2, 7 * 9 + 4));
    assert(pn_xiangqi_legal_move(b, 0, 9 * 9 + 2, 7 * 9 + 0));
    b[5 * 9 + 2] = PN_XQ_ELEPHANT;
    assert(!pn_xiangqi_legal_move(b, 0, 5 * 9 + 2, 3 * 9 + 4)); /* 相不过河 / Elephant cannot cross river */

    /* 炮隔山打牛与兵过河规则核验。/ Cannon screen capture and Pawn river-crossing rules. */
    memset(b, 0, sizeof b);
    b[0 * 9 + 3] = PN_XQ_BLACK | PN_XQ_KING;
    b[9 * 9 + 5] = PN_XQ_KING;
    b[7 * 9 + 1] = PN_XQ_CANNON;
    b[4 * 9 + 1] = PN_XQ_PAWN;
    b[1 * 9 + 1] = PN_XQ_BLACK | PN_XQ_ROOK;
    assert(pn_xiangqi_legal_move(b, 0, 7 * 9 + 1, 5 * 9 + 1));
    assert(!pn_xiangqi_legal_move(b, 0, 7 * 9 + 1, 4 * 9 + 1));
    assert(pn_xiangqi_legal_move(b, 0, 7 * 9 + 1, 1 * 9 + 1));
    b[3 * 9 + 1] = PN_XQ_BLACK | PN_XQ_PAWN; /* 双重炮架不可吃 / Two screens block cannon capture */
    assert(!pn_xiangqi_legal_move(b, 0, 7 * 9 + 1, 1 * 9 + 1));

    b[6 * 9 + 6] = PN_XQ_PAWN; /* 未过河兵不可横走 / Uncrossed pawn cannot move sideways */
    assert(pn_xiangqi_legal_move(b, 0, 6 * 9 + 6, 5 * 9 + 6));
    assert(!pn_xiangqi_legal_move(b, 0, 6 * 9 + 6, 6 * 9 + 5));
    assert(!pn_xiangqi_legal_move(b, 0, 4 * 9 + 1, 5 * 9 + 1)); /* 兵不后退 / Pawn never retreats */
    assert(pn_xiangqi_legal_move(b, 0, 4 * 9 + 1, 4 * 9 + 2));  /* 过河兵可平走 / Crossed pawn moves sideways */

    char mv[24];
    pn_xiangqi_move_text(b, 7 * 9 + 1, 3 * 9 + 1, mv);
    assert(strcmp(mv, "炮八进四") == 0);
}

static void test_bank_and_gameplay(void) {
    pn_xq_level_t bad;
    assert(pn_xiangqi_level(PN_XQ_ENDGAMES, &bad) == PN_INVALID);

    for (unsigned id = 0; id < PN_XQ_ENDGAMES; id++) {
        pn_xq_level_t lv;
        assert(pn_xiangqi_level(id, &lv) == PN_OK);
        assert(lv.id == id && lv.tier == id / 20u && lv.title[0] != '\0');

        /* 实战模拟：每步采用pn_xiangqi_best_red_move对抗黑方顽强应着，必须在mate_moves回合内破阵获胜！
         * Live combat: playing pn_xiangqi_best_red_move against pn_xiangqi_ai_reply must win within mate_moves! */
        pn_xq_state_t s = {0};
        pn_xq_reset_board(&s, &lv, pn_xq_bank_crc());
        for (unsigned turn = 0; turn < lv.mate_moves && s.outcome == PN_XQ_ONGOING; turn++) {
            uint8_t rf = PN_XQ_NONE_SQ, rt = PN_XQ_NONE_SQ;
            assert(pn_xiangqi_best_red_move(&s, &rf, &rt));
            assert(pn_xiangqi_step(&s, rf, rt) == PN_OK);
        }
        assert(s.outcome == PN_XQ_WON && s.moves <= lv.mate_moves);
    }

    /* 败局反杀与悔棋一步恢复测试。/ Blunder into Black counter-mate and recover directly via pn_xiangqi_undo. */
    bool tested_loss_undo = false;
    for (unsigned id = 20; id < PN_XQ_ENDGAMES && !tested_loss_undo; id++) {
        pn_xq_level_t lv;
        assert(pn_xiangqi_level(id, &lv) == PN_OK);
        pn_xq_state_t s = {0};
        pn_xq_reset_board(&s, &lv, pn_xq_bank_crc());
        uint8_t pairs[256];
        unsigned n = pn_xiangqi_legal_moves(s.board, 0, pairs);
        for (unsigned i = 0; i < n; i++) {
            pn_xq_state_t trial = s;
            assert(pn_xiangqi_step(&trial, pairs[i * 2], pairs[i * 2 + 1]) == PN_OK);
            if (trial.outcome == PN_XQ_LOST) {
                assert(pn_xiangqi_undo(&trial) == PN_OK);
                assert(trial.outcome == PN_XQ_ONGOING && trial.moves == 0);
                assert(!memcmp(trial.board, s.board, PN_XQ_CELLS));
                tested_loss_undo = true;
                break;
            }
        }
    }
    assert(tested_loss_undo);
}

int main(void) {
    test_special_rules();
    test_bank_and_gameplay();

    char root[] = "/tmp/pn-xiangqi-XXXXXX";
    assert(mkdtemp(root));
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 64 * 1024, NULL, NULL, NULL) == PN_OK);
    pn_media_t media;
    pn_media_init(&media);
    assert(pn_media_attach(&media, 1) == PN_OK);

    pn_xiangqi_t u = {0};
    assert(pn_xiangqi_open(&u, &pool, &media, root, 100) == PN_OK && pn_xiangqi_screen(&u) == PN_XQ_SELECT);
    assert(pn_xiangqi_event(&u, PN_XQ_TIER + 1, 101) == PN_OK);
    assert(pn_xiangqi_event(&u, PN_XQ_START, 102) == PN_OK && pn_xiangqi_screen(&u) == PN_XQ_PLAY);
    assert(pn_xiangqi_state(&u)->level.id == 20);
    assert(pn_xiangqi_event(&u, PN_XQ_HINT, 103) == PN_BUSY);
    pn_xiangqi_displayed(&u, true);

    assert(pn_xiangqi_event(&u, PN_XQ_HINT, 110) == PN_OK);
    pn_xiangqi_displayed(&u, true);
    uint8_t ht = pn_xiangqi_state(&u)->hint_to;
    assert(pn_xiangqi_event(&u, PN_XQ_CELL + (int)ht, 120) == PN_OK);
    pn_xiangqi_displayed(&u, true);
    assert(pn_xiangqi_tick(&u, 2000) == PN_OK);
    size_t active_used = pool.used;
    assert(pn_xiangqi_tick(&u, 3000) == PN_EMPTY && pool.used == active_used);

    assert(pn_xiangqi_pause(&u, 3200) == PN_OK);
    pn_xq_state_t saved = *pn_xiangqi_state(&u);
    assert(saved.elapsed_ms > 0 && saved.moves == 1);
    pn_xiangqi_close(&u);
    assert(!pool.used);

    assert(pn_xiangqi_open(&u, &pool, &media, root, 50000) == PN_OK);
    assert(!memcmp(&saved, pn_xiangqi_state(&u), sizeof saved));
    pn_xiangqi_displayed(&u, true);

    /* 保存失败屏障：阻止退出、锁屏与覆盖。/ Save-failure barriers block exit, lock and overwrite. */
    assert(pn_xiangqi_event(&u, PN_XQ_UNDO_MOVE, 50010) == PN_OK);
    pn_xiangqi_displayed(&u, true);
    fail_sync = true;
    assert(pn_xiangqi_event(&u, PN_XQ_EXIT, 50020) != PN_OK && u.active);
    assert(pn_xiangqi_pause(&u, 50025) != PN_OK);
    fail_sync = false;
    assert(pn_xiangqi_event(&u, PN_XQ_EXIT, 50030) == PN_OK && !u.active);
    pn_xiangqi_close(&u);

    for (unsigned mode = 0; mode < 4; mode++) {
        clear_files(root);
        assert(pn_xiangqi_open(&u, &pool, &media, root, 10) == PN_OK);
        assert(pn_xiangqi_event(&u, PN_XQ_TIER + 1, 11) == PN_OK);
        assert(pn_xiangqi_event(&u, PN_XQ_START, 12) == PN_OK);
        pn_xiangqi_displayed(&u, true);
        assert(pn_xiangqi_event(&u, PN_XQ_HINT, 13) == PN_OK);
        pn_xiangqi_displayed(&u, true);
        assert(pn_xiangqi_event(&u, PN_XQ_CELL + (int)pn_xiangqi_state(&u)->hint_to, 14) == PN_OK);
        assert(pn_xiangqi_pause(&u, 15) == PN_OK);
        pn_xiangqi_close(&u);
        forge_record(root, &pool, &media, mode);
        assert(pn_xiangqi_open(&u, &pool, &media, root, 16) == PN_OK && pn_xiangqi_screen(&u) == PN_XQ_ERROR);
        assert(pn_xiangqi_event(&u, PN_XQ_START, 17) == PN_EMPTY);
        pn_xiangqi_close(&u);
    }
    clear_files(root);

    pool.fail_at = pool.attempts + 1;
    assert(pn_xiangqi_open(&u, &pool, &media, root, 1) == PN_NO_MEMORY);
    pool.fail_at = 0;
    assert(!pool.used && !pool.live && !pn_media_active(&media));
    assert(rmdir(root) == 0);
    printf("xiangqi 60 endgames, special rules, stubborn AI, loss-undo and A/B barriers passed; controller=%zu peak=%zu state=%zu\n",
           active_used, pool.peak, sizeof(pn_xq_state_t));
    return 0;
}
