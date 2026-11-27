/* SPDX-License-Identifier: Apache-2.0
 * 地牢纸牌纯绘制、命中与局部增量更新逐字节等价测试。
 * Scoundrel rendering, hit testing and byte-identical partial update verification. */
#define _POSIX_C_SOURCE 200809L
#include "pn_scoundrel.h"
#include "../../components/pn_library/pn_scoundrel_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void assert_patch_matches_full(pn_scoundrel_t *game, pn_font_t *font, pn_frame_t *patch_fb, uint8_t *full_px) {
    pn_frame_t full_fb;
    assert(pn_frame_bind(&full_fb, full_px, 415872u, 684u, 1216u));
    assert(pn_scoundrel_render_update(game, font, patch_fb) == PN_OK);
    assert(pn_scoundrel_render(game, font, &full_fb) == PN_OK);
    assert(memcmp(patch_fb->pixels, full_fb.pixels, 415872u) == 0);
    pn_scoundrel_displayed(game, true);
}

int main(void) {
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 4u * 1024u * 1024u, NULL, NULL, NULL) == PN_OK);
    pn_media_t media;
    pn_media_init(&media);
    assert(pn_media_attach(&media, 1u) == PN_OK);

    pn_font_t font = {0};
    pn_text_source_t src = pn_font_builtin_source();
    assert(pn_font_open(&font, &pool, &src, 24) == PN_OK);

    uint8_t *px_a = pn_alloc_tagged(&pool, PN_MEM_FRAME, 415872u);
    uint8_t *px_b = pn_alloc_tagged(&pool, PN_MEM_FRAME, 415872u);
    assert(px_a && px_b);
    pn_frame_t fb;
    assert(pn_frame_bind(&fb, px_a, 415872u, 684u, 1216u));

    char root[] = "/tmp/pn-test-scoundrel-render-XXXXXX";
    assert(mkdtemp(root));
    pn_scoundrel_t game = {0};
    assert(pn_scoundrel_open(&game, &pool, &media, root, 100) == PN_OK);
    assert(pn_scoundrel_render(&game, &font, &fb) == PN_OK);
    pn_scoundrel_displayed(&game, true);

    /* 验证选模式页命中坐标 / Verify select-screen hits */
    assert(pn_scoundrel_hit(&game, 100, 70) == PN_SC_BACK);
    assert(pn_scoundrel_hit(&game, 180, 240) == PN_SC_MODE_BASE + PN_SC_MODE_STANDARD);
    assert(pn_scoundrel_hit(&game, 500, 240) == PN_SC_MODE_BASE + PN_SC_MODE_HARDCORE);
    assert(pn_scoundrel_hit(&game, 340, 880) == PN_SC_START);

    /* 开始新局并注入可控房间测试全部局部增量路径 / Start run and inject controlled room for full patch parity checks */
    assert(pn_scoundrel_event(&game, PN_SC_START, 200) == PN_OK);
    sc_t *g = (sc_t *)game.impl;
    g->state.room[0] = pn_sc_make_card(PN_SC_DIAMOND, 8);
    g->state.room[1] = pn_sc_make_card(PN_SC_CLUB, 13);
    g->state.room[2] = pn_sc_make_card(PN_SC_HEART, 7);
    g->state.room[3] = pn_sc_make_card(PN_SC_SPADE, 6);
    assert(pn_scoundrel_render(&game, &font, &fb) == PN_OK);
    pn_scoundrel_displayed(&game, true);

    /* 验证房间4牌槽与底栏命中坐标 / Verify room card and toolbar hits */
    assert(pn_scoundrel_hit(&game, 180, 330) == PN_SC_SLOT_BASE + 0);
    assert(pn_scoundrel_hit(&game, 500, 330) == PN_SC_SLOT_BASE + 1);
    assert(pn_scoundrel_hit(&game, 180, 610) == PN_SC_SLOT_BASE + 2);
    assert(pn_scoundrel_hit(&game, 500, 610) == PN_SC_SLOT_BASE + 3);
    assert(pn_scoundrel_hit(&game, 100, 1070) == PN_SC_FLEE);
    assert(pn_scoundrel_hit(&game, 260, 1070) == PN_SC_UNDO_MOVE);
    assert(pn_scoundrel_hit(&game, 420, 1070) == PN_SC_RESTART);
    assert(pn_scoundrel_hit(&game, 570, 1070) == PN_SC_CLEAN);

    /* 1. 装备武器 -> 局部更新与整绘逐字节一致 */
    assert(pn_scoundrel_event(&game, PN_SC_SLOT_BASE + 0, 300) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 2. 点选♣K怪物进入同页二选一态 -> 局部更新逐字节一致 */
    assert(pn_scoundrel_event(&game, PN_SC_SLOT_BASE + 1, 400) == PN_OK);
    assert(pn_scoundrel_hit(&game, 180, 970) == PN_SC_FIGHT_WEAPON);
    assert(pn_scoundrel_hit(&game, 500, 970) == PN_SC_FIGHT_BARE);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 3. 武器斩杀♣K -> 局部更新逐字节一致 */
    assert(pn_scoundrel_event(&game, PN_SC_FIGHT_WEAPON, 500) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 4. 本房内撤销上一步 -> 局部更新逐字节一致 */
    assert(pn_scoundrel_event(&game, PN_SC_UNDO_MOVE, 600) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 5. 空手硬扛♣K再喝♥7触发补牌 -> 局部更新逐字节一致 */
    assert(pn_scoundrel_event(&game, PN_SC_SLOT_BASE + 1, 700) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);
    assert(pn_scoundrel_event(&game, PN_SC_FIGHT_BARE, 800) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);
    assert(pn_scoundrel_event(&game, PN_SC_SLOT_BASE + 2, 900) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 6. 逃跑重翻4张新牌 -> 局部更新逐字节一致 */
    assert(pn_scoundrel_event(&game, PN_SC_FLEE, 1000) == PN_OK);
    assert_patch_matches_full(&game, &font, &fb, px_b);

    /* 7. 重开弹窗与结算界面整绘通过 / Confirm dialog and complete screen render */
    assert(pn_scoundrel_event(&game, PN_SC_RESTART, 1100) == PN_OK);
    assert(pn_scoundrel_render(&game, &font, &fb) == PN_OK);
    assert(pn_scoundrel_hit(&game, 200, 580) == PN_SC_CANCEL);
    assert(pn_scoundrel_hit(&game, 470, 580) == PN_SC_ACCEPT);

    pn_scoundrel_close(&game);
    pn_font_close(&font);
    pn_free(px_a);
    pn_free(px_b);
    char p[512];
    snprintf(p, sizeof p, "%s/scoundrel.a", root);
    (void)unlink(p);
    snprintf(p, sizeof p, "%s/scoundrel.b", root);
    (void)unlink(p);
    assert(rmdir(root) == 0);
    assert(pool.used == 0 && pool.live == 0);
    puts("test_scoundrel_render passed");
    return 0;
}
