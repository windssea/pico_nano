/* SPDX-License-Identifier: Apache-2.0
 * 原创90关证明回放、精确撤销和小内存持久化。/ Replay 90 original witnesses, exact undo and small-memory persistence. */
#include "pn_crc32.h"
#include "pn_sokoban.h"
#include "pn_sokoban_internal.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// 实际同步后注入失败，覆盖提交结果不确定的情况。/ Inject failure after a real sync to cover uncertain commits.
static bool fail_sync;
int __real_fsync(int fd);
int __wrap_fsync(int fd) {
    int r = __real_fsync(fd);
    if (fail_sync && r == 0) {
        fail_sync = false;
        errno = EIO;
        return -1;
    }
    return r;
}
static void clear_saves(const char *root) {
    char path[512];
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/sokoban.%c", root, 'a' + i);
        unlink(path);
    }
}
static void edge_rules(void) {
    pn_so_state_t s = {0};
    assert(pn_sokoban_level(0, &s.level) == PN_OK);
    memcpy(s.boxes, s.level.initial_boxes, 16);
    s.player = s.level.initial_player;
    unsigned a = 0, b = 0;
    for (unsigned c = 0; c < s.level.width * s.level.height && !b; c++)
        if (!so_bit(s.level.walls, c) && !so_bit(s.boxes, c))
            for (unsigned d = 0; d < 4; d++) {
                int n = so_neighbor(&s.level, c, d);
                if (n >= 0 && !so_bit(s.level.walls, (unsigned)n) && !so_bit(s.boxes, (unsigned)n)) {
                    a = c;
                    b = (unsigned)n;
                    break;
                }
            }
    assert(b);
    s.player = (uint8_t)a;
    for (unsigned i = 0; i < 200; i++)
        assert(pn_sokoban_walk(&s, i % 2 ? a : b) == PN_OK);
    assert(s.undo_count == 128 && s.moves == 200);
    for (unsigned i = 0; i < 128; i++)
        assert(pn_sokoban_undo(&s) == PN_OK);
    assert(s.moves == 72 && s.player == a && pn_sokoban_undo(&s) == PN_EMPTY);
    pn_so_state_t before = s;
    assert(pn_sokoban_walk(&s, 0) == PN_EMPTY && !memcmp(&before, &s, sizeof s));
    s.moves = UINT32_MAX;
    before = s;
    assert(pn_sokoban_walk(&s, b) == PN_LIMIT && !memcmp(&before, &s, sizeof s));
}
static void faults_and_touch(pn_pool_t *pool, pn_media_t *media, const char *root) {
    pn_sokoban_t g = {0};
    pn_media_lease_t held;
    clear_saves(root);
    assert(pn_sokoban_open(&g, pool, media, root, 0) == PN_OK);
    assert(pn_media_acquire(media, PN_MEDIA_READ, &held) == PN_OK);
    assert(pn_sokoban_event(&g, PN_SO_START, 1) != PN_OK && pn_sokoban_screen(&g) == PN_SO_ERROR);
    assert(pn_sokoban_event(&g, PN_SO_EXIT, 2) == PN_BUSY && pn_sokoban_pause(&g, 3) == PN_BUSY);
    assert(pn_media_release(media, &held) == PN_OK);
    assert(pn_sokoban_event(&g, PN_SO_RETRY, 4) == PN_OK && pn_sokoban_screen(&g) == PN_SO_PLAY);
    pn_sokoban_displayed(&g, true);
    assert(!pn_sokoban_touch(&g, 1, 300, 500, true));
    assert(!pn_sokoban_touch(&g, 1, 400, 500, true));
    assert(pn_sokoban_touch(&g, 0, 0, 0, true) == PN_SO_RIGHT);
    assert(!pn_sokoban_touch(&g, 0, 0, 0, true));
    assert(!pn_sokoban_touch(&g, 1, 300, 500, true));
    assert(!pn_sokoban_touch(&g, 2, 400, 500, true));
    assert(!pn_sokoban_touch(&g, 1, 450, 500, true));
    assert(!pn_sokoban_touch(&g, 0, 0, 0, true));
    assert(!pn_sokoban_touch(&g, 1, 300, 500, true));
    assert(!pn_sokoban_touch(&g, 1, 300, 600, false));
    assert(!pn_sokoban_touch(&g, 0, 0, 0, true));
    assert(pn_sokoban_event(&g, PN_SO_CHOOSE, 5) == PN_OK);
    assert(pn_sokoban_event(&g, PN_SO_ACCEPT, 6) == PN_OK);
    assert(pn_sokoban_event(&g, PN_SO_TIER + 1, 7) == PN_OK);
    fail_sync = true;
    assert(pn_sokoban_event(&g, PN_SO_START, 8) == PN_IO && !fail_sync);
    assert(pn_sokoban_event(&g, PN_SO_BACK, 9) == PN_BUSY && pn_sokoban_pause(&g, 10) == PN_BUSY);
    assert(pn_sokoban_event(&g, PN_SO_RETRY, 11) == PN_OK && pn_sokoban_state(&g)->level.id == 30);
    pn_sokoban_close(&g);
    assert(!pool->used);
    // 截断最新A，完整B回退；未知外壳版本不允许覆盖。/ Truncate newest A and recover intact B; never overwrite unknown
    // envelope versions.
    char path[512];
    snprintf(path, sizeof path, "%s/sokoban.a", root);
    FILE *cut = fopen(path, "wb");
    assert(cut && fwrite("truncated", 1, 9, cut) == 9 && !fclose(cut));
    assert(pn_sokoban_open(&g, pool, media, root, 0) == PN_OK && pn_sokoban_state(&g)->level.id == 30);
    pn_sokoban_close(&g);
    snprintf(path, sizeof path, "%s/sokoban.b", root);
    FILE *f = fopen(path, "r+b");
    assert(f);
    assert(!fseek(f, 4, SEEK_SET));
    assert(fputc(2, f) == 2);
    assert(!fclose(f));
    assert(pn_sokoban_open(&g, pool, media, root, 0) == PN_OK && pn_sokoban_screen(&g) == PN_SO_ERROR);
    assert(pn_sokoban_event(&g, PN_SO_START, 12) == PN_EMPTY);
    assert(pn_sokoban_event(&g, PN_SO_EXIT, 13) == PN_OK);
    pn_sokoban_close(&g);
    f = fopen(path, "rb");
    assert(f);
    assert(!fseek(f, 4, SEEK_SET));
    assert(fgetc(f) == 2);
    assert(!fclose(f));
    clear_saves(root);
    assert(!pool->used && !pool->live);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(sizeof(pn_so_undo_t) == 8);
    edge_rules();
    for (unsigned id = 0; id < 90; id++) {
        pn_so_state_t s = {0};
        assert(pn_sokoban_level(id, &s.level) == PN_OK);
        memcpy(s.boxes, s.level.initial_boxes, 16);
        s.player = s.level.initial_player;
        for (unsigned j = 0; j < s.level.proof_count; j++) {
            unsigned b = s.level.solution[j * 2], d = s.level.solution[j * 2 + 1];
            int stand = so_neighbor(&s.level, b, (d + 2) % 4);
            assert(stand >= 0);
            if (s.player != (unsigned)stand)
                assert(pn_sokoban_walk(&s, (unsigned)stand) == PN_OK);
            pn_so_state_t before = s;
            assert(pn_sokoban_move(&s, d) == PN_OK);
            assert(pn_sokoban_undo(&s) == PN_OK);
            assert(!memcmp(s.boxes, before.boxes, 16) && s.player == before.player && s.moves == before.moves &&
                   s.pushes == before.pushes);
            assert(pn_sokoban_move(&s, d) == PN_OK);
        }
        assert(s.complete && s.pushes == s.level.min_pushes && !memcmp(s.boxes, s.level.goals, 16));
    }
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 8192, NULL, NULL, NULL) == PN_OK);
    pn_media_t media;
    pn_media_init(&media);
    assert(pn_media_attach(&media, 1) == PN_OK);
    pn_sokoban_t g = {0};
    assert(pn_sokoban_open(&g, &pool, &media, argv[1], 0) == PN_OK);
    assert(pn_sokoban_screen(&g) == PN_SO_SELECT);
    pn_sokoban_displayed(&g, true);
    assert(pn_sokoban_event(&g, PN_SO_START, 1) == PN_OK);
    pn_sokoban_displayed(&g, true);
    const pn_so_state_t *s = pn_sokoban_state(&g);
    assert(s);
    unsigned b = s->level.solution[0], d = s->level.solution[1];
    int stand = so_neighbor(&s->level, b, (d + 2) % 4);
    if (s->player != (unsigned)stand) {
        assert(pn_sokoban_event(&g, PN_SO_CELL + stand, 2) == PN_OK);
        pn_sokoban_displayed(&g, true);
    }
    assert(pn_sokoban_event(&g, PN_SO_UP + (int)d, 3) == PN_OK);
    pn_sokoban_displayed(&g, true);
    assert(pn_sokoban_tick(&g, 2000) == PN_OK);
    uint8_t boxes[16];
    memcpy(boxes, pn_sokoban_state(&g)->boxes, 16);
    unsigned player = pn_sokoban_state(&g)->player;
    assert(pn_sokoban_tick(&g, 90000) == PN_EMPTY && g.presented);
    pn_sokoban_close(&g);
    assert(!pool.used && !pool.live);
    assert(pn_sokoban_open(&g, &pool, &media, argv[1], 0) == PN_OK);
    s = pn_sokoban_state(&g);
    assert(s && s->player == player && !memcmp(boxes, s->boxes, 16));
    pn_sokoban_displayed(&g, true);
    assert(pn_sokoban_event(&g, PN_SO_UNDO_MOVE, 4) == PN_OK);
    pn_sokoban_displayed(&g, true);
    // WRITE租约冲突必须阻止退出，失败不改成已保存。/ A conflicting lease must block exit without claiming a successful
    // save.
    pn_media_lease_t held;
    assert(pn_media_acquire(&media, PN_MEDIA_READ, &held) == PN_OK);
    assert(pn_sokoban_event(&g, PN_SO_EXIT, 5) != PN_OK && g.active);
    assert(pn_media_release(&media, &held) == PN_OK);
    pn_sokoban_displayed(&g, true);
    assert(pn_sokoban_event(&g, PN_SO_EXIT, 6) == PN_OK && !g.active);
    pn_sokoban_close(&g);
    assert(!pool.used && !pool.live);
    char path[512];
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/sokoban.%c", argv[1], 'a' + i);
        unlink(path);
    }
    printf(
        "Sokoban: 90 complete proofs, undo/save/reopen, idle no redraw; controller=%zu state=%zu peak=%zu limit=%zu\n",
        sizeof(so_t), sizeof(pn_so_state_t), pool.peak, pool.limit);
    faults_and_touch(&pool, &media, argv[1]);
    puts("Sokoban: 128-command ring, overflow, invalid/multitouch, uncertain commit, truncated fallback and unknown "
         "schema passed");
    return 0;
}
