/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设备端在线素材库清单解析、路径安全校验、缩略图解码与四屏绘制/命中单测。
 * English: host unit tests for on-device online asset manifest parsing, path validation, preview decoding, and 4-screen render/hit.
 */
#include "pn_online_ui.h"
#include "pn_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SAMPLE_MANIFEST =
    "{"
    "\"schema\":1,"
    "\"categories\":["
      "{\"id\":\"mask\",\"items\":["
        "{\"name\":\"徽标-PicoNano.png\",\"stem\":\"徽标-PicoNano\",\"path\":\"assets/share/image/mask/徽标-PicoNano.png\","
         "\"size\":12345,\"width\":684,\"height\":1216,"
         "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"},"
        "{\"name\":\"../evil.png\",\"path\":\"assets/share/image/mask/evil.png\",\"size\":100},"
        "{\"name\":\"badpath.png\",\"path\":\"../secret.png\",\"size\":100},"
        "{\"name\":\"badext.exe\",\"path\":\"assets/share/image/mask/badext.exe\",\"size\":100}"
      "]},"
      "{\"id\":\"wallpaper\",\"items\":[]},"
      "{\"id\":\"book\",\"items\":["
        "{\"name\":\"sample.epub\",\"stem\":\"示例书\",\"path\":\"assets/share/book/sample.epub\",\"size\":54321,"
         "\"sha256\":\"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\"}"
      "]}"
    "]"
    "}";

int main(void) {
    // 1. URL 百分号编码（保留 '/' 与 ASCII，编码中文 UTF-8）/ URL path encoding
    char enc[256];
    assert(pn_online_url_encode_path("assets/share/image/mask/徽标-PicoNano.png", enc, sizeof enc) == PN_OK);
    assert(strncmp(enc, "assets/share/image/mask/%", 25u) == 0);
    assert(strstr(enc, "-PicoNano.png") != NULL);

    // 2. 清单解析与恶意路径/非法扩展名拦截 / Manifest parsing and security filtering
    pn_online_catalog_t *cat = calloc(1, sizeof *cat);
    assert(cat != NULL);
    assert(pn_online_parse_manifest(SAMPLE_MANIFEST, strlen(SAMPLE_MANIFEST), cat) == PN_OK);
    assert(cat->cat_count == PN_ONLINE_MAX_CATS);
    assert(strcmp(cat->categories[0].id, "wallpaper") == 0 && cat->categories[0].count == 0);
    assert(strcmp(cat->categories[1].id, "mask") == 0 && cat->categories[1].count == 1);
    assert(strcmp(cat->categories[6].id, "book") == 0 && cat->categories[6].count == 1);
    assert(cat->item_count == 2);
    const pn_online_item_t *mask = &cat->items[cat->categories[1].first_item];
    assert(mask->kind == PN_UPLOAD_MASK);
    assert(mask->has_sha256 && mask->sha256[0] == 0x01u && mask->sha256[31] == 0xefu);
    assert(mask->width == 684 && mask->height == 1216);

    // 3. 四种界面渲染与命中测试 / 4-screen render and hit testing
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 2 * 1024 * 1024, NULL, NULL, NULL) == 0);
    pn_font_t font = {0};
    pn_text_source_t src = pn_font_builtin_source();
    assert(pn_font_open(&font, &pool, &src, 24) == PN_OK);
    uint8_t *px = pn_alloc(&pool, PN_UI_WIDTH * PN_UI_HEIGHT / 2u);
    pn_frame_t frame;
    assert(pn_frame_bind(&frame, px, PN_UI_WIDTH * PN_UI_HEIGHT / 2u, PN_UI_WIDTH, PN_UI_HEIGHT));

    pn_online_view_t v = {0};
    v.screen = PN_ONLINE_SCREEN_CONNECTING;
    v.catalog = cat;
    v.network_name = "CD37";
    v.status_title = "正在连接网络";
    v.status_line = "连接中";
    v.can_retry = true;
    assert(pn_online_ui_render(&font, &frame, &v) == PN_OK && font.pixels == 24);
    assert(pn_online_ui_hit(&v, 80, 60) == PN_ONLINE_CMD_BACK);
    assert(pn_online_ui_hit(&v, 340, 800) == PN_ONLINE_CMD_RETRY);

    v.screen = PN_ONLINE_SCREEN_CATEGORIES;
    strcpy(cat->source_label, "国内 CDN 镜像");
    assert(pn_online_ui_render(&font, &frame, &v) == PN_OK);
    assert(pn_online_ui_hit(&v, 620, 60) == PN_ONLINE_CMD_REFRESH);
    assert(pn_online_ui_hit(&v, 300, 220) == PN_ONLINE_CMD_CAT_BASE + 0);
    assert(pn_online_ui_hit(&v, 300, 320) == PN_ONLINE_CMD_CAT_BASE + 1);

    v.screen = PN_ONLINE_SCREEN_GRID;
    v.cat_index = 1; // mask
    uint8_t *thumb_px = pn_alloc(&pool, PN_ONLINE_THUMB_BYTES);
    memset(thumb_px, 0xCC, PN_ONLINE_THUMB_BYTES);
    v.thumbs[0] = thumb_px;
    v.thumbs_loaded = 1;
    assert(pn_online_ui_render(&font, &frame, &v) == PN_OK);
    assert(pn_online_ui_hit(&v, 100, 300) == PN_ONLINE_CMD_ITEM_BASE + 0);
    assert(pn_online_ui_hit(&v, 340, 300) == -1); // only 1 item on page

    v.screen = PN_ONLINE_SCREEN_DETAIL;
    v.item_index = 0;
    v.downloading = false;
    assert(pn_online_ui_render(&font, &frame, &v) == PN_OK);
    assert(pn_online_ui_hit(&v, 340, 1090) == PN_ONLINE_CMD_DOWNLOAD);
    v.downloading = true;
    v.download_percent = 55;
    assert(pn_online_ui_render(&font, &frame, &v) == PN_OK);
    assert(pn_online_ui_hit(&v, 340, 1090) == PN_ONLINE_CMD_CANCEL_DL);

    pn_free(thumb_px);
    pn_free(px);
    pn_font_close(&font);
    free(cat);
    assert(!pool.live && !pool.used);
    return 0;
}
