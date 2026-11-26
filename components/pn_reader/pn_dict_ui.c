/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源离线词典浮层卡片、多选勾选面板与手动查词界面实现，见 pn_dict_ui.h。
 * English: Shared offline dictionary floating card, multi-select checklist, and manual search UI implementation, see pn_dict_ui.h.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_dict_ui.h"
#include "pn_journal.h"
#include "pn_keyboard.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <string.h>

#define CHIP_MAX_SHOWN 4u
#define BODY_FONT_PX 24
#define BODY_PITCH_NO_CHIPS 36
#define BODY_PITCH_WITH_CHIPS 35
#define BODY_LINES_NO_CHIPS 10u
#define BODY_LINES_WITH_CHIPS 9u
#define CARD_PAD_X 22
#define BODY_W (PN_DICT_CARD_W - CARD_PAD_X * 2 - 12) /* 532px */
#define PICKER_PER_PAGE 6u
#define SEARCH_INPUT_Y 204
#define SEARCH_INPUT_H 84
#define SEARCH_BTN_Y 310
#define SEARCH_BTN_H 80
#define SEARCH_KB_Y 680

#define DICT_PREF_KEY_MAX 127u

typedef struct {
    pn_pool_t *pool;
    pn_reader_app_t *reader;
    pn_epub_app_t *epub;
    pn_stardict_catalog_t catalog;
    uint32_t enabled_mask; ///< 启用的词典位掩码（bit i 对应 catalog.items[i]）/ Bitmask of enabled dictionaries
    pn_media_t *pref_media;
    char pref_dir[PN_JOURNAL_PATH_MAX];
    bool has_page_frame;
    pn_frame_t page_frame;
    int sheet_y;
    bool top_docked;
    int anchor_x, anchor_y;
    bool has_anchor, sentence_highlighted, highlight_changed;
    char cands[PN_DICT_UI_CAND_MAX][PN_DICT_WORD_MAX];
    size_t cand_count;
    int cand_selected;
    char query[PN_DICT_WORD_MAX];
    char draft[PN_DICT_WORD_MAX];
    pn_keyboard_t kb;
    pn_stardict_multi_hit_t hit;
    bool has_hit;
    bool from_search;
    bool pages_dirty;
    size_t page_starts[PN_DICT_UI_PAGES_MAX];
    size_t page_count;
    size_t page_index;
    size_t picker_page;
    char picker_hint[96];
    char notice[192];
} dict_ui_impl_t;

/// 计算 UTF-8 单字符字节数。/ Byte length of one UTF-8 character.
static size_t utf8_char_len(const char *s) {
    unsigned char c = (unsigned char)*s;
    if (!c) return 0;
    if (c < 0x80u) return 1;
    if ((c & 0xE0u) == 0xC0u) return s[1] ? 2 : 1;
    if ((c & 0xF0u) == 0xE0u) return (s[1] && s[2]) ? 3 : 1;
    if ((c & 0xF8u) == 0xF0u) return (s[1] && s[2] && s[3]) ? 4 : 1;
    return 1;
}

static uint32_t all_dicts_mask(size_t count) {
    if (count == 0u) return 0u;
    if (count >= 32u) return UINT32_MAX;
    return (1u << count) - 1u;
}

static size_t mask_popcount(uint32_t mask, size_t count) {
    uint32_t m = mask & all_dicts_mask(count);
    size_t n = 0;
    for (size_t i = 0; i < count && i < 32u; i++) {
        if (m & (1u << i)) n++;
    }
    return n;
}

static size_t mask_single_index(uint32_t mask, size_t count) {
    uint32_t m = mask & all_dicts_mask(count);
    size_t found = SIZE_MAX;
    for (size_t i = 0; i < count && i < 32u; i++) {
        if (m & (1u << i)) {
            if (found != SIZE_MAX) return SIZE_MAX;
            found = i;
        }
    }
    return found;
}

/// 提取词典持久标识（优先 .ifo 文件名，跨重启与目录重排稳定）。
/// Extract stable dictionary key (prefer .ifo basename so it survives reboots and catalog reordering).
static const char *dict_key_of(const pn_stardict_info_t *info) {
    if (!info) return "";
    const char *p = info->ifo_path;
    const char *slash = strrchr(p, '/');
    const char *bslash = strrchr(p, '\\');
    const char *sep = slash > bslash ? slash : bslash;
    const char *base = (sep && sep[1]) ? (sep + 1) : p;
    return base[0] ? base : info->name;
}

static pn_status_t pref_paths(const char *state_dir, char a[PN_JOURNAL_PATH_MAX], char b[PN_JOURNAL_PATH_MAX]) {
    if (!state_dir || !*state_dir) return PN_INVALID;
    int na = snprintf(a, PN_JOURNAL_PATH_MAX, "%s/dict_pref.a", state_dir);
    int nb = snprintf(b, PN_JOURNAL_PATH_MAX, "%s/dict_pref.b", state_dir);
    if (na < 0 || (size_t)na >= PN_JOURNAL_PATH_MAX || nb < 0 || (size_t)nb >= PN_JOURNAL_PATH_MAX) return PN_LIMIT;
    return PN_OK;
}

static size_t catalog_find_key(const pn_stardict_catalog_t *catalog, const char *key, size_t klen) {
    if (!catalog || !key || !klen) return SIZE_MAX;
    for (size_t i = 0; i < catalog->count; i++) {
        const char *k = dict_key_of(&catalog->items[i]);
        if (strlen(k) == klen && memcmp(k, key, klen) == 0) return i;
        if (strlen(catalog->items[i].name) == klen && memcmp(catalog->items[i].name, key, klen) == 0) return i;
    }
    return SIZE_MAX;
}

pn_status_t pn_dict_ui_pref_load_mask(pn_media_t *media, const char *state_dir,
    const pn_stardict_catalog_t *catalog, uint32_t *mask_out) {
    if (!media || !state_dir || !*state_dir || !catalog || !mask_out) return PN_INVALID;
    uint32_t full = all_dicts_mask(catalog->count);
    *mask_out = full;
    if (!full) return PN_OK;
    char a[PN_JOURNAL_PATH_MAX], b[PN_JOURNAL_PATH_MAX];
    pn_status_t status = pref_paths(state_dir, a, b);
    if (status != PN_OK) return status;
    pn_media_lease_t lease = {0};
    status = pn_media_acquire(media, PN_MEDIA_READ, &lease);
    if (status != PN_OK) return status;
    pn_journal_files_t files;
    pn_journal_io_t io;
    pn_record_t rec = {0};
    status = pn_journal_files_init(&files, media, &lease, a, b, &io);
    if (status == PN_OK) status = pn_journal_load(&io, &rec);
    (void)pn_media_release(media, &lease);
    if (status != PN_OK) return status;
    if (rec.size < 6u || memcmp(rec.payload, "PNDP", 4) != 0) return PN_CORRUPT;

    uint8_t ver = rec.payload[4];
    if (ver == 1u) {
        size_t len = rec.payload[5];
        if (len > DICT_PREF_KEY_MAX || rec.size != 6u + len) return PN_CORRUPT;
        if (len == 0u) {
            *mask_out = full;
            return PN_OK;
        }
        size_t idx = catalog_find_key(catalog, (const char *)(rec.payload + 6u), len);
        *mask_out = (idx < catalog->count) ? (1u << idx) : full;
        return PN_OK;
    }
    if (ver == 2u) {
        if (rec.size < 7u) return PN_CORRUPT;
        uint8_t mode = rec.payload[5];
        uint8_t cnt = rec.payload[6];
        if (mode == 0u) {
            *mask_out = full;
            return PN_OK;
        }
        if (mode != 1u) return PN_UNSUPPORTED;
        uint32_t mask = 0u;
        size_t pos = 7u;
        for (unsigned i = 0; i < cnt; i++) {
            if (pos >= rec.size) return PN_CORRUPT;
            size_t len = rec.payload[pos++];
            if (!len || pos + len > rec.size) return PN_CORRUPT;
            size_t idx = catalog_find_key(catalog, (const char *)(rec.payload + pos), len);
            if (idx < catalog->count && idx < 32u) mask |= (1u << idx);
            pos += len;
        }
        if (pos != rec.size) return PN_CORRUPT;
        // 若所有已勾选词典均已被删除，安全回退为全选。/ Fall back safely to all if every checked dict was deleted.
        *mask_out = mask ? mask : full;
        return PN_OK;
    }
    return PN_UNSUPPORTED;
}

pn_status_t pn_dict_ui_pref_save_mask(pn_media_t *media, const char *state_dir,
    const pn_stardict_catalog_t *catalog, uint32_t enabled_mask) {
    if (!media || !state_dir || !*state_dir || !catalog) return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX], b[PN_JOURNAL_PATH_MAX];
    pn_status_t status = pref_paths(state_dir, a, b);
    if (status != PN_OK) return status;

    uint32_t full = all_dicts_mask(catalog->count);
    uint32_t clean = enabled_mask & full;
    uint8_t payload[PN_JOURNAL_PAYLOAD_MAX];
    memcpy(payload, "PNDP", 4);
    payload[4] = 2u;
    size_t pos = 7u;
    if (!clean || clean == full) {
        payload[5] = 0u; // 全部 / All
        payload[6] = 0u;
    } else {
        payload[5] = 1u; // 自定义多选子集 / Custom checked subset
        uint8_t cnt = 0u;
        for (size_t i = 0; i < catalog->count && i < 32u; i++) {
            if (!(clean & (1u << i))) continue;
            const char *k = dict_key_of(&catalog->items[i]);
            size_t kl = strlen(k);
            if (kl == 0u || kl > DICT_PREF_KEY_MAX) continue;
            if (pos + 1u + kl > sizeof(payload)) break;
            payload[pos++] = (uint8_t)kl;
            memcpy(payload + pos, k, kl);
            pos += kl;
            cnt++;
        }
        payload[6] = cnt;
        if (cnt == 0u) {
            payload[5] = 0u;
            pos = 7u;
        }
    }

    pn_media_lease_t lease = {0};
    status = pn_media_acquire(media, PN_MEDIA_WRITE, &lease);
    if (status != PN_OK) return status;
    pn_journal_files_t files;
    pn_journal_io_t io;
    status = pn_journal_files_init(&files, media, &lease, a, b, &io);
    if (status == PN_OK) status = pn_journal_save(&io, payload, pos);
    pn_status_t rel = pn_media_release(media, &lease);
    return status == PN_OK ? rel : status;
}

pn_status_t pn_dict_ui_pref_load(pn_media_t *media, const char *state_dir, char *out_key, size_t cap) {
    if (out_key && cap) out_key[0] = '\0';
    if (!media || !state_dir || !*state_dir || !out_key || !cap) return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX], b[PN_JOURNAL_PATH_MAX];
    pn_status_t status = pref_paths(state_dir, a, b);
    if (status != PN_OK) return status;
    pn_media_lease_t lease = {0};
    status = pn_media_acquire(media, PN_MEDIA_READ, &lease);
    if (status != PN_OK) return status;
    pn_journal_files_t files;
    pn_journal_io_t io;
    pn_record_t rec = {0};
    status = pn_journal_files_init(&files, media, &lease, a, b, &io);
    if (status == PN_OK) status = pn_journal_load(&io, &rec);
    (void)pn_media_release(media, &lease);
    if (status != PN_OK) return status;
    if (rec.size < 6u || memcmp(rec.payload, "PNDP", 4) != 0) return PN_CORRUPT;
    if (rec.payload[4] == 1u) {
        size_t len = rec.payload[5];
        if (len > DICT_PREF_KEY_MAX || rec.size != 6u + len) return PN_CORRUPT;
        if (len >= cap) return PN_LIMIT;
        if (len > 0u) memcpy(out_key, rec.payload + 6u, len);
        out_key[len] = '\0';
        return PN_OK;
    }
    if (rec.payload[4] == 2u) {
        if (rec.size < 7u) return PN_CORRUPT;
        if (rec.payload[5] == 0u || rec.payload[6] != 1u) {
            out_key[0] = '\0';
            return PN_OK;
        }
        if (rec.size < 8u) return PN_CORRUPT;
        size_t len = rec.payload[7];
        if (8u + len > rec.size) return PN_CORRUPT;
        if (len >= cap) return PN_LIMIT;
        memcpy(out_key, rec.payload + 8u, len);
        out_key[len] = '\0';
        return PN_OK;
    }
    return PN_UNSUPPORTED;
}

pn_status_t pn_dict_ui_pref_save(pn_media_t *media, const char *state_dir, const char *key) {
    if (!media || !state_dir || !*state_dir) return PN_INVALID;
    const char *k = key ? key : "";
    size_t len = strlen(k);
    if (len > DICT_PREF_KEY_MAX) len = DICT_PREF_KEY_MAX;
    while (len > 0u && (((unsigned char)k[len] & 0xC0u) == 0x80u)) len--;
    char a[PN_JOURNAL_PATH_MAX], b[PN_JOURNAL_PATH_MAX];
    pn_status_t status = pref_paths(state_dir, a, b);
    if (status != PN_OK) return status;
    uint8_t payload[8u + DICT_PREF_KEY_MAX];
    memcpy(payload, "PNDP", 4);
    payload[4] = 2u;
    size_t total = 7u;
    if (len == 0u) {
        payload[5] = 0u;
        payload[6] = 0u;
    } else {
        payload[5] = 1u;
        payload[6] = 1u;
        payload[7] = (uint8_t)len;
        memcpy(payload + 8u, k, len);
        total = 8u + len;
    }
    pn_media_lease_t lease = {0};
    status = pn_media_acquire(media, PN_MEDIA_WRITE, &lease);
    if (status != PN_OK) return status;
    pn_journal_files_t files;
    pn_journal_io_t io;
    status = pn_journal_files_init(&files, media, &lease, a, b, &io);
    if (status == PN_OK) status = pn_journal_save(&io, payload, total);
    pn_status_t rel = pn_media_release(media, &lease);
    return status == PN_OK ? rel : status;
}

/// 切换字号绘制单行文本后恢复原字号。/ Draw single-line text at a temporary font size and restore.
static pn_status_t sized_text_ex(pn_font_t *font, pn_frame_t *frame, const char *value, int size,
    int x, int baseline, int width, pn_align_t align, uint8_t ink, bool bold) {
    int original = font->pixels;
    pn_status_t s = pn_font_size(font, size);
    if (s == PN_OK) s = pn_w_text_ex(font, frame, value, x, baseline, width, align, ink, bold);
    pn_status_t restored = pn_font_size(font, original);
    return s == PN_OK ? restored : s;
}

static pn_status_t sized_button(pn_font_t *font, pn_frame_t *frame, const char *label, int size,
    int x, int y, int w, int h, unsigned style) {
    int original = font->pixels;
    pn_status_t s = pn_font_size(font, size);
    if (s == PN_OK) s = pn_w_button(font, frame, label, x, y, w, h, style);
    pn_status_t restored = pn_font_size(font, original);
    return s == PN_OK ? restored : s;
}

/// 绘制清晰 2px 粗关闭叉号图标。/ Draw a crisp 2px-thick close "×" icon.
static void draw_close_x(pn_frame_t *frame, int cx, int cy, int half, uint8_t ink) {
    for (int d = -half; d <= half; d++) {
        pn_frame_rect(frame, cx + d, cy + d, 2, 2, ink);
        pn_frame_rect(frame, cx + d, cy - d, 2, 2, ink);
    }
}

/// 绘制 24x24 多选复选框（选中为纯黑底白对勾，未选为空心描边框）。
/// Draw a crisp 24x24 checkbox (solid ink with white checkmark when checked, stroked outline when unchecked).
static void draw_checkbox(pn_frame_t *frame, int x, int y, bool checked) {
    if (checked) {
        pn_w_round_fill(frame, x, y, 24, 24, 5, PN_UI_INK);
        // 白色对勾（两段 2px 折线）。/ Crisp white checkmark (two 2px strokes).
        for (int i = 0; i < 5; i++) {
            pn_frame_rect(frame, x + 5 + i, y + 11 + i, 2, 2, PN_UI_PAPER);
        }
        for (int i = 0; i < 9; i++) {
            pn_frame_rect(frame, x + 9 + i, y + 15 - i, 2, 2, PN_UI_PAPER);
        }
    } else {
        pn_w_round_fill(frame, x, y, 24, 24, 5, PN_UI_PAPER);
        pn_w_round_stroke(frame, x, y, 24, 24, 5, 1.5f, 7);
    }
}

/// 复制底层阅读帧并绘制带柔和投影的居中浮层卡片外壳。
/// Copy backing reading frame and draw the centered floating card shell with soft drop shadow.
static void draw_card_shell(const dict_ui_impl_t *d, pn_frame_t *frame) {
    if (d->has_page_frame && d->page_frame.pixels) {
        if (frame->pixels != d->page_frame.pixels) {
            for (int y = 0; y < PN_UI_HEIGHT; y++) {
                memcpy(frame->pixels + (size_t)y * frame->stride,
                       d->page_frame.pixels + (size_t)y * d->page_frame.stride, 342u);
            }
        }
    } else {
        pn_frame_clear(frame, PN_UI_PAPER);
    }
    const int cx = PN_DICT_CARD_X, cy = d->sheet_y, cw = PN_DICT_CARD_W, ch = PN_DICT_SHEET_H;
    // 柔和右下投影 + 纯白圆角卡片 + 清晰细描边，保留四周阅读正文可见。
    // Soft drop shadow + crisp white rounded card + thin dark border, leaving reading text visible on all sides.
    pn_w_round_fill(frame, cx + 5, cy + 5, cw, ch, 14, 13);
    pn_w_round_fill(frame, cx + 2, cy + 2, cw, ch, 14, 11);
    pn_w_round_fill(frame, cx, cy, cw, ch, 14, PN_UI_PAPER);
    pn_w_round_stroke(frame, cx, cy, cw, ch, 14, 1.5f, 3);
}

static void body_metrics(const dict_ui_impl_t *d, int *y0_out, unsigned *lines_out, int *pitch_out) {
    bool chips = d->cand_count > 1u;
    *y0_out = d->sheet_y + (chips ? 148 : 104);
    *lines_out = chips ? BODY_LINES_WITH_CHIPS : BODY_LINES_NO_CHIPS;
    *pitch_out = chips ? BODY_PITCH_WITH_CHIPS : BODY_PITCH_NO_CHIPS;
}

/// 渲染或测量单页释义正文，返回下一页起始字节偏移；以【开头的词典小节标题自动加粗。
/// Render or measure one wrapped definition page, returning next-page offset; lines starting with 【 render bold.
static pn_status_t layout_def_page(pn_font_t *font, pn_frame_t *frame, const char *text, size_t start_off,
    int x, int y0, int max_w, unsigned max_lines, int pitch, size_t *next_off_out) {
    size_t len = strlen(text);
    size_t pos = start_off < len ? start_off : len;
    unsigned line = 0;
    pn_status_t s = PN_OK;
    char buf[256];

    while (pos < len && line < max_lines && s == PN_OK) {
        if (text[pos] == '\n') {
            pos++;
            if (line == 0u) continue;
            line++;
            continue;
        }
        size_t line_start = pos;
        size_t line_end = pos;
        size_t last_space = 0;
        bool has_space = false;

        while (pos < len && text[pos] != '\n') {
            size_t clen = utf8_char_len(text + pos);
            size_t cand_len = (pos - line_start) + clen;
            if (cand_len >= sizeof(buf)) break;
            memcpy(buf, text + line_start, cand_len);
            buf[cand_len] = '\0';
            int w = 0;
            s = pn_w_text_width(font, buf, &w);
            if (s != PN_OK) break;
            if (w > max_w && line_end > line_start) {
                if (has_space && last_space > line_start && (unsigned char)text[pos] < 0x80u) {
                    line_end = last_space;
                    pos = last_space;
                    while (pos < len && text[pos] == ' ') pos++;
                }
                break;
            }
            if (text[pos] == ' ') {
                last_space = pos;
                has_space = true;
            }
            pos += clen;
            line_end = pos;
        }
        if (s != PN_OK) break;
        if (line_end == line_start && pos < len) {
            size_t clen = utf8_char_len(text + pos);
            pos += clen;
            line_end = pos;
        }
        if (pos < len && text[pos] == '\n') pos++;
        while (line_end > line_start && text[line_end - 1] == ' ') line_end--;

        if (frame && line_end > line_start) {
            size_t draw_len = line_end - line_start;
            if (draw_len >= sizeof(buf)) draw_len = sizeof(buf) - 1u;
            memcpy(buf, text + line_start, draw_len);
            buf[draw_len] = '\0';
            bool is_dict_hdr = (draw_len >= 3u && (unsigned char)buf[0] == 0xE3u &&
                                (unsigned char)buf[1] == 0x80u && (unsigned char)buf[2] == 0x90u);
            s = pn_w_text_ex(font, frame, buf, x, y0 + (int)line * pitch, max_w, PN_ALIGN_LEFT, PN_UI_INK, is_dict_hdr);
        }
        line++;
    }
    if (next_off_out) *next_off_out = pos;
    return s;
}

static pn_status_t rebuild_pages(dict_ui_impl_t *d, pn_font_t *font) {
    d->page_starts[0] = 0;
    d->page_count = 1;
    if (!d->has_hit || !d->hit.definition[0]) {
        d->page_index = 0;
        d->pages_dirty = false;
        return PN_OK;
    }

    int y0 = 0, pitch = BODY_PITCH_NO_CHIPS;
    unsigned max_lines = BODY_LINES_NO_CHIPS;
    body_metrics(d, &y0, &max_lines, &pitch);

    int original = font->pixels;
    pn_status_t s = pn_font_size(font, BODY_FONT_PX);
    size_t len = strlen(d->hit.definition);
    size_t off = 0;
    while (s == PN_OK && off < len && d->page_count < PN_DICT_UI_PAGES_MAX) {
        size_t next_off = off;
        s = layout_def_page(font, NULL, d->hit.definition, off, PN_DICT_CARD_X + CARD_PAD_X, y0, BODY_W, max_lines, pitch, &next_off);
        if (s != PN_OK || next_off <= off) break;
        off = next_off;
        while (off < len && d->hit.definition[off] == '\n') off++;
        if (off < len) d->page_starts[d->page_count++] = off;
    }
    pn_status_t restored = pn_font_size(font, original);
    if (d->page_index >= d->page_count) d->page_index = d->page_count - 1u;
    d->pages_dirty = false;
    return s == PN_OK ? restored : s;
}

/// 按当前勾选的 enabled_mask 执行查询并重置分页状态。
/// Run lookup across the checked enabled_mask and reset pagination state.
static void run_lookup(dict_ui_impl_t *d, const char *word) {
    d->has_hit = false;
    memset(&d->hit, 0, sizeof(d->hit));
    d->page_starts[0] = 0;
    d->page_count = 1;
    d->page_index = 0;
    d->pages_dirty = true;
    d->notice[0] = '\0';
    if (!word || !word[0]) return;
    if (word != d->query) snprintf(d->query, sizeof(d->query), "%s", word);
    if (!d->catalog.count) return;

    uint32_t mask = d->enabled_mask & all_dicts_mask(d->catalog.count);
    if (!mask) mask = all_dicts_mask(d->catalog.count);
    pn_status_t s = pn_stardict_query_masked(d->pool, &d->catalog, d->query, mask, &d->hit);
    if (s == PN_OK) {
        d->has_hit = true;
    } else if (s == PN_EMPTY) {
        size_t single = mask_single_index(mask, d->catalog.count);
        if (single < d->catalog.count) {
            snprintf(d->notice, sizeof(d->notice), "未在「%.48s」中找到「%.48s」", d->catalog.items[single].name, d->query);
        } else {
            snprintf(d->notice, sizeof(d->notice), "未在已勾选的 %zu 本词典中找到「%.48s」",
                mask_popcount(mask, d->catalog.count), d->query);
        }
    } else {
        snprintf(d->notice, sizeof(d->notice), "读取词典失败（%d），请检查文件完整性", (int)s);
    }
}

static void save_current_pref(dict_ui_impl_t *d) {
    if (d->pref_media && d->pref_dir[0] && d->catalog.count > 0u) {
        (void)pn_dict_ui_pref_save_mask(d->pref_media, d->pref_dir, &d->catalog, d->enabled_mask);
    }
}

/// 计算卡片内候选词胶囊坐标。/ Compute candidate chip geometry inside the floating card.
static unsigned chip_rects(const dict_ui_impl_t *d, int xs[CHIP_MAX_SHOWN], int ws[CHIP_MAX_SHOWN]) {
    unsigned shown = d->cand_count > CHIP_MAX_SHOWN ? CHIP_MAX_SHOWN : (unsigned)d->cand_count;
    if (shown <= 1u) return 0;
    int total_w = PN_DICT_CARD_W - 40;
    int gap = 8;
    int w = (total_w - (int)(shown - 1u) * gap) / (int)shown;
    for (unsigned i = 0; i < shown; i++) {
        xs[i] = PN_DICT_CARD_X + 20 + (int)i * (w + gap);
        ws[i] = w;
    }
    return shown;
}

static pn_status_t paint_overlay(void *ctx, pn_font_t *font, pn_font_t *fallback, pn_frame_t *frame) {
    return pn_dict_ui_render((pn_dict_ui_t *)ctx, font, fallback, frame);
}

static pn_status_t render_result_card(dict_ui_impl_t *d, pn_font_t *font, pn_frame_t *frame) {
    draw_card_shell(d, frame);
    const int cx = PN_DICT_CARD_X, cy = d->sheet_y, cw = PN_DICT_CARD_W;
    const size_t enabled_n = mask_popcount(d->enabled_mask, d->catalog.count);
    const size_t single_idx = mask_single_index(d->enabled_mask, d->catalog.count);

    // 标题栏：左命中词头，右“词典”勾选胶囊 + 搜索图标 + 关闭 ×。
    // Header: matched headword left; "Dict" checklist pill + Search icon + close × right.
    char heading[PN_DICT_WORD_MAX * 2u + 8u];
    if (!d->catalog.count) {
        snprintf(heading, sizeof(heading), "离线词典");
    } else if (d->has_hit) {
        if (d->query[0] && strcmp(d->hit.matched_word, d->query) != 0) {
            snprintf(heading, sizeof(heading), "%s (%s)", d->hit.matched_word, d->query);
        } else {
            snprintf(heading, sizeof(heading), "%s", d->hit.matched_word);
        }
    } else if (d->query[0]) {
        snprintf(heading, sizeof(heading), "%s", d->query);
    } else {
        snprintf(heading, sizeof(heading), "查词");
    }
    int hl_x = d->catalog.count > 1u ? (cx + 266) : (cx + 368);
    int hl_w = 88;
    int head_w = d->has_anchor ? (hl_x - (cx + CARD_PAD_X) - 8) : (d->catalog.count > 1u ? 330 : 440);
    pn_status_t s = sized_text_ex(font, frame, heading, 28, cx + CARD_PAD_X, cy + 45, head_w, PN_ALIGN_LEFT, PN_UI_INK, true);

    if (s == PN_OK && d->has_anchor) {
        pn_w_round_fill(frame, hl_x, cy + 14, hl_w, 40, 10, d->sentence_highlighted ? PN_UI_INK : PN_UI_SURFACE);
        pn_w_round_stroke(frame, hl_x, cy + 14, hl_w, 40, 10, 1.5f, d->sentence_highlighted ? PN_UI_INK : 9);
        s = sized_text_ex(font, frame, d->sentence_highlighted ? "已摘录" : "摘录", 19, hl_x + 4, cy + 40, hl_w - 8,
            PN_ALIGN_CENTER, d->sentence_highlighted ? PN_UI_PAPER : PN_UI_INK, d->sentence_highlighted);
    }
    if (s == PN_OK && d->catalog.count > 1u) {
        char pick_lbl[40];
        if (enabled_n == d->catalog.count) {
            snprintf(pick_lbl, sizeof(pick_lbl), "词典 全部");
        } else {
            snprintf(pick_lbl, sizeof(pick_lbl), "词典 %u/%u", (unsigned)enabled_n, (unsigned)d->catalog.count);
        }
        pn_w_round_fill(frame, cx + 362, cy + 14, 104, 40, 10, PN_UI_SURFACE);
        pn_w_round_stroke(frame, cx + 362, cy + 14, 104, 40, 10, 1.5f, 9);
        s = sized_text_ex(font, frame, pick_lbl, 19, cx + 366, cy + 40, 96, PN_ALIGN_CENTER, PN_UI_INK, false);
    }
    if (s == PN_OK) {
        pn_w_round_fill(frame, cx + 474, cy + 14, 46, 40, 10, PN_UI_SURFACE);
        pn_w_round_stroke(frame, cx + 474, cy + 14, 46, 40, 10, 1.5f, 9);
        pn_w_icon(frame, PN_ICON_SEARCH, cx + 486, cy + 23, 22, PN_UI_INK);
        draw_close_x(frame, cx + 550, cy + 34, 10, 2);
    }
    pn_frame_rect(frame, cx + 20, cy + 68, cw - 40, 1, 13);

    // 候选词胶囊（仅多候选时显示）。/ Candidate chips (shown only when >1 candidate).
    int cxs[CHIP_MAX_SHOWN], cws[CHIP_MAX_SHOWN];
    unsigned shown = chip_rects(d, cxs, cws);
    if (shown > 0u) {
        for (unsigned i = 0; i < shown && s == PN_OK; i++) {
            bool sel = ((int)i == d->cand_selected);
            pn_w_round_fill(frame, cxs[i], cy + 75, cws[i], 36, 9, sel ? PN_UI_SURFACE : PN_UI_PAPER);
            pn_w_round_stroke(frame, cxs[i], cy + 75, cws[i], 36, 9, sel ? 2.0f : 1.5f, sel ? PN_UI_INK : 9);
            s = sized_text_ex(font, frame, d->cands[i], 20, cxs[i] + 4, cy + 100, cws[i] - 8, PN_ALIGN_CENTER, PN_UI_INK, sel);
        }
        pn_frame_rect(frame, cx + 20, cy + 117, cw - 40, 1, 14);
    }

    // 释义正文区。/ Definition body area.
    int y0 = 0, pitch = BODY_PITCH_NO_CHIPS;
    unsigned max_lines = BODY_LINES_NO_CHIPS;
    body_metrics(d, &y0, &max_lines, &pitch);

    if (s == PN_OK && !d->catalog.count) {
        int orig = font->pixels;
        s = pn_font_size(font, 23);
        if (s == PN_OK) {
            s = pn_w_text_lines_ex(font, frame,
                "尚未安装离线词典。\n可通过「传书 → 词典」上传 StarDict 词典，或复制到 /piconano/dicts/ 目录。",
                cx + CARD_PAD_X, y0 + 28, BODY_W, 5, 38, PN_UI_MUTED, false);
        }
        pn_status_t r = pn_font_size(font, orig);
        if (s == PN_OK) s = r;
    } else if (s == PN_OK && d->has_hit) {
        if (d->pages_dirty) s = rebuild_pages(d, font);
        if (s == PN_OK) {
            int orig = font->pixels;
            s = pn_font_size(font, BODY_FONT_PX);
            size_t start_off = d->page_index < d->page_count ? d->page_starts[d->page_index] : 0;
            if (s == PN_OK) {
                s = layout_def_page(font, frame, d->hit.definition, start_off, cx + CARD_PAD_X, y0, BODY_W, max_lines, pitch, NULL);
            }
            pn_status_t r = pn_font_size(font, orig);
            if (s == PN_OK) s = r;
        }
        // 右侧细进度条（仅多页时显示滑块比例）。/ Right subtle scrollbar.
        if (d->page_count > 1u) {
            int track_y = shown > 0u ? (cy + 126) : (cy + 80);
            int track_h = (cy + 466) - track_y;
            pn_frame_rect(frame, cx + cw - 12, track_y, 3, track_h, 14);
            int thumb_h = track_h / (int)d->page_count;
            if (thumb_h < 28) thumb_h = 28;
            int thumb_y = track_y + (int)((size_t)(track_h - thumb_h) * d->page_index / (d->page_count - 1u));
            pn_frame_rect(frame, cx + cw - 12, thumb_y, 3, thumb_h, 4);
        }
    } else if (s == PN_OK) {
        int orig = font->pixels;
        s = pn_font_size(font, 23);
        if (s == PN_OK) {
            s = pn_w_text_lines_ex(font, frame,
                d->notice[0] ? d->notice : "未找到释义。可切换候选词、点右上「词典」勾选或点搜索图标。",
                cx + CARD_PAD_X, y0 + 28, BODY_W, 5, 38, PN_UI_MUTED, false);
        }
        pn_status_t r = pn_font_size(font, orig);
        if (s == PN_OK) s = r;
    }

    // 极简底栏（参考图样式）：<   1 / N · 词典摘要   >。/ Minimalist footer (<  1 / N · dict summary  >).
    pn_frame_rect(frame, cx + 20, cy + 476, cw - 40, 1, 11);
    if (s == PN_OK) {
        bool can_prev = d->has_hit && d->page_index > 0u;
        bool can_next = d->has_hit && d->page_index + 1u < d->page_count;
        if (d->has_hit && d->page_count > 1u) {
            pn_w_icon(frame, PN_ICON_BACK, cx + 30, cy + 497, 22, can_prev ? 2 : 12);
            pn_w_icon(frame, PN_ICON_CHEVRON, cx + cw - 52, cy + 497, 22, can_next ? 2 : 12);
        }
        char foot[160];
        if (!d->catalog.count) {
            snprintf(foot, sizeof(foot), "点卡外关闭");
        } else if (d->has_hit) {
            if (single_idx < d->catalog.count) {
                if (d->page_count > 1u) {
                    snprintf(foot, sizeof(foot), "%u / %u · %s",
                        (unsigned)(d->page_index + 1u), (unsigned)d->page_count, d->catalog.items[single_idx].name);
                } else {
                    snprintf(foot, sizeof(foot), "%s", d->catalog.items[single_idx].name);
                }
            } else if (d->page_count > 1u) {
                snprintf(foot, sizeof(foot), "%u / %u · 命中 %zu/%zu 本",
                    (unsigned)(d->page_index + 1u), (unsigned)d->page_count, d->hit.matched_count, enabled_n);
            } else {
                snprintf(foot, sizeof(foot), "命中 %zu/%zu 本词典", d->hit.matched_count, enabled_n);
            }
        } else if (single_idx < d->catalog.count) {
            snprintf(foot, sizeof(foot), "当前词典：%s", d->catalog.items[single_idx].name);
        } else {
            snprintf(foot, sizeof(foot), "已勾选 %zu/%zu 本 · 点「词典」调整", enabled_n, d->catalog.count);
        }
        s = sized_text_ex(font, frame, foot, 21, cx + 76, cy + 516, cw - 152, PN_ALIGN_CENTER, PN_UI_MUTED, false);
    }
    return s;
}

static pn_status_t render_picker_card(dict_ui_impl_t *d, pn_font_t *font, pn_frame_t *frame) {
    draw_card_shell(d, frame);
    const int cx = PN_DICT_CARD_X, cy = d->sheet_y, cw = PN_DICT_CARD_W;
    const size_t enabled_n = mask_popcount(d->enabled_mask, d->catalog.count);
    const size_t total_pages = d->catalog.count ? ((d->catalog.count + PICKER_PER_PAGE - 1u) / PICKER_PER_PAGE) : 1u;
    if (d->picker_page >= total_pages) d->picker_page = total_pages - 1u;

    // 标题栏：左“选择启用词典 (已选 X/Y)”，右关闭 ×。
    // Header: "选择启用词典" with checked count left, close × right.
    char title[80];
    snprintf(title, sizeof(title), "选择启用词典（已选 %u/%u）", (unsigned)enabled_n, (unsigned)d->catalog.count);
    pn_status_t s = sized_text_ex(font, frame, title, 25, cx + CARD_PAD_X, cy + 44, cw - 88, PN_ALIGN_LEFT, PN_UI_INK, true);
    draw_close_x(frame, cx + 550, cy + 34, 10, 2);
    pn_frame_rect(frame, cx + 20, cy + 68, cw - 40, 1, 13);

    // 词典勾选列表（每页 6 本）。/ Dictionary checklist rows (6 per page).
    size_t start = d->picker_page * PICKER_PER_PAGE;
    for (unsigned row = 0; row < PICKER_PER_PAGE && start + row < d->catalog.count && s == PN_OK; row++) {
        size_t di = start + row;
        bool checked = (d->enabled_mask & (1u << di)) != 0u;
        int ry = cy + 78 + (int)row * 64;
        pn_w_round_fill(frame, cx + 20, ry, cw - 40, 56, 10, checked ? PN_UI_SURFACE : PN_UI_PAPER);
        pn_w_round_stroke(frame, cx + 20, ry, cw - 40, 56, 10, checked ? 1.5f : 1.0f, checked ? 4 : 12);
        draw_checkbox(frame, cx + 36, ry + 16, checked);
        s = sized_text_ex(font, frame, d->catalog.items[di].name, 22, cx + 74, ry + 36, 356, PN_ALIGN_LEFT, PN_UI_INK, checked);
        if (s == PN_OK) {
            char wc[32];
            uint32_t words = d->catalog.items[di].word_count;
            if (words >= 10000u) snprintf(wc, sizeof(wc), "%u.%u万词", (unsigned)(words / 10000u), (unsigned)((words % 10000u) / 1000u));
            else snprintf(wc, sizeof(wc), "%u词", (unsigned)words);
            s = sized_text_ex(font, frame, wc, 18, cx + 436, ry + 35, 116, PN_ALIGN_RIGHT, PN_UI_MUTED, false);
        }
    }

    // 底栏：左“全选”，中提示或翻页，右“完成”。/ Footer: "Select All" left, hint/pager middle, "Done" right.
    pn_frame_rect(frame, cx + 20, cy + 476, cw - 40, 1, 11);
    if (s == PN_OK) {
        bool all_on = enabled_n == d->catalog.count;
        s = sized_button(font, frame, "全选", 21, cx + 20, cy + 486, 108, 44, all_on ? PN_W_DISABLED : 0u);
    }
    if (s == PN_OK && total_pages > 1u) {
        bool has_p = d->picker_page > 0u, has_n = d->picker_page + 1u < total_pages;
        pn_w_icon(frame, PN_ICON_BACK, cx + 150, cy + 497, 22, has_p ? 2 : 12);
        char pg[24];
        snprintf(pg, sizeof(pg), "%u / %u", (unsigned)(d->picker_page + 1u), (unsigned)total_pages);
        s = sized_text_ex(font, frame, pg, 20, cx + 184, cy + 516, 180, PN_ALIGN_CENTER, PN_UI_MUTED, false);
        pn_w_icon(frame, PN_ICON_CHEVRON, cx + 376, cy + 497, 22, has_n ? 2 : 12);
    } else if (s == PN_OK) {
        const char *hint = d->picker_hint[0] ? d->picker_hint : "勾选即保存，支持多选";
        s = sized_text_ex(font, frame, hint, 19, cx + 136, cy + 515, 292, PN_ALIGN_CENTER, PN_UI_MUTED, false);
    }
    if (s == PN_OK) {
        s = sized_button(font, frame, "完成", 21, cx + cw - 148, cy + 486, 128, 44, PN_W_SELECTED);
    }
    return s;
}

pn_status_t pn_dict_ui_render(pn_dict_ui_t *ui, pn_font_t *font, pn_font_t *fallback, pn_frame_t *frame) {
    if (!ui || !ui->impl || ui->mode == PN_DUI_CLOSED || !font || !font->impl || !frame || !frame->pixels
        || frame->width != PN_UI_WIDTH || frame->height != PN_UI_HEIGHT) {
        return PN_INVALID;
    }
    dict_ui_impl_t *d = ui->impl;

    pn_w_font_scope_t scope = {0};
    pn_status_t s = pn_w_font_scope_begin(&scope, d->pool, fallback);
    if (s != PN_OK) return s;

    if (ui->mode == PN_DUI_RESULT) {
        s = render_result_card(d, font, frame);
    } else if (ui->mode == PN_DUI_PICKER) {
        s = render_picker_card(d, font, frame);
    } else if (ui->mode == PN_DUI_SEARCH) {
        pn_frame_clear(frame, PN_UI_PAPER);
        s = pn_w_header(font, frame, d->from_search ? "< 返回阅读" : "< 返回结果", "手动查词", NULL);
        if (s == PN_OK) {
            size_t en = mask_popcount(d->enabled_mask, d->catalog.count);
            size_t single = mask_single_index(d->enabled_mask, d->catalog.count);
            char sub[160];
            if (!d->catalog.count) {
                snprintf(sub, sizeof(sub), "尚未安装离线词典（请放入 /piconano/dicts/）");
            } else if (single < d->catalog.count) {
                snprintf(sub, sizeof(sub), "已启用：%s（结果卡可多选）", d->catalog.items[single].name);
            } else if (en == d->catalog.count) {
                snprintf(sub, sizeof(sub), "已启用全部 %zu 本离线词典 · 聚合查询", d->catalog.count);
            } else {
                snprintf(sub, sizeof(sub), "已启用 %zu / %zu 本离线词典 · 聚合查询", en, d->catalog.count);
            }
            s = sized_text_ex(font, frame, sub, 22, PN_UI_MARGIN, 174, 620, PN_ALIGN_LEFT, PN_UI_MUTED, false);
        }
        if (s == PN_OK) {
            pn_w_round_fill(frame, PN_UI_MARGIN, SEARCH_INPUT_Y, 620, SEARCH_INPUT_H, 16, PN_UI_PAPER);
            pn_w_round_stroke(frame, PN_UI_MARGIN, SEARCH_INPUT_Y, 620, SEARCH_INPUT_H, 16, 2.0f, PN_UI_INK);
            if (d->draft[0]) {
                char shown_text[PN_DICT_WORD_MAX + 4];
                snprintf(shown_text, sizeof(shown_text), "%s_", d->draft);
                s = sized_text_ex(font, frame, shown_text, 32, PN_UI_MARGIN + 20, SEARCH_INPUT_Y + 54, 580, PN_ALIGN_LEFT, PN_UI_INK, false);
            } else {
                s = sized_text_ex(font, frame, "输入英文单词或中文词语...", 28, PN_UI_MARGIN + 20, SEARCH_INPUT_Y + 53, 580, PN_ALIGN_LEFT, 7, false);
            }
        }
        if (s == PN_OK) {
            s = sized_button(font, frame, "清空", 28, PN_UI_MARGIN, SEARCH_BTN_Y, 188, SEARCH_BTN_H, d->draft[0] ? 0u : PN_W_DISABLED);
        }
        if (s == PN_OK) {
            bool can_go = d->draft[0] != '\0' || d->kb.pinyin[0] != '\0';
            s = sized_button(font, frame, "立即查询", 30, 236, SEARCH_BTN_Y, 416, SEARCH_BTN_H, can_go ? PN_W_SELECTED : PN_W_DISABLED);
        }
        if (s == PN_OK) {
            const char *hint = d->notice[0] ? d->notice : "阅读正文时也可长按任意词语直接弹出释义";
            s = sized_text_ex(font, frame, hint, 22, PN_UI_MARGIN, 444, 620, PN_ALIGN_LEFT, PN_UI_MUTED, false);
        }
        if (s == PN_OK) {
            s = pn_keyboard_render(&d->kb, font, frame, SEARCH_KB_Y);
        }
    }

    pn_w_font_scope_end(&scope);
    return s;
}

pn_status_t pn_dict_ui_present(pn_dict_ui_t *ui, pn_reader_present_fn present, void *ctx) {
    if (!ui || !ui->impl || ui->mode == PN_DUI_CLOSED) return PN_INVALID;
    dict_ui_impl_t *d = ui->impl;
    if (!present || (!d->reader && !d->epub)) return PN_OK;
    if (d->epub) return pn_epub_app_overlay(d->epub, paint_overlay, ui, present, ctx, PN_REFRESH_GL16);
    return pn_reader_app_overlay(d->reader, paint_overlay, ui, present, ctx, PN_REFRESH_GL16);
}

pn_status_t pn_dict_ui_open_ex(pn_dict_ui_t *ui, pn_pool_t *pool, pn_reader_app_t *reader, pn_epub_app_t *epub,
    const char *dicts_dir, const char cands[][PN_DICT_WORD_MAX], size_t cand_count, bool open_search,
    const pn_dict_ui_options_t *options, pn_reader_present_fn present, void *ctx) {
    if (!ui || !pool) return PN_INVALID;
    pn_dict_ui_close(ui);

    dict_ui_impl_t *d = pn_alloc_tagged(pool, PN_MEM_UI, sizeof(*d));
    if (!d) return PN_NO_MEMORY;
    memset(d, 0, sizeof(*d));
    d->pool = pool;
    d->reader = reader;
    d->epub = epub;
    d->cand_selected = -1;
    d->page_count = 1;
    d->pages_dirty = true;
    pn_keyboard_set_ime(&d->kb, true, false);

    int anchor_y = 0;
    if (options) {
        anchor_y = options->anchor_y;
        if (options->anchor_x > 0 && options->anchor_y > 0 && ((reader && reader->impl) || (epub && epub->impl))) {
            bool is_hl = false;
            pn_status_t hs = (epub && epub->impl)
                ? pn_epub_app_highlight_at(epub, options->anchor_x, options->anchor_y, &is_hl)
                : pn_reader_app_highlight_at(reader, options->anchor_x, options->anchor_y, &is_hl);
            if (hs == PN_OK) {
                d->anchor_x = options->anchor_x;
                d->anchor_y = options->anchor_y;
                d->has_anchor = true;
                d->sentence_highlighted = is_hl;
            }
        }
        if (options->page_frame && options->page_frame->pixels &&
            options->page_frame->width == PN_UI_WIDTH && options->page_frame->height == PN_UI_HEIGHT &&
            options->page_frame->stride >= 342u) {
            d->page_frame = *options->page_frame;
            d->has_page_frame = true;
        }
        if (options->pref_media && options->pref_dir && options->pref_dir[0] &&
            strlen(options->pref_dir) < sizeof(d->pref_dir)) {
            d->pref_media = options->pref_media;
            strcpy(d->pref_dir, options->pref_dir);
        }
    }
    // 词典浮层统一固定在下方弹出，不随触点上下跳动。
    // Always dock the dictionary floating card at the bottom instead of flipping top/bottom.
    (void)anchor_y;
    d->top_docked = false;
    d->sheet_y = PN_DICT_BOTTOM_Y;

    if (dicts_dir && *dicts_dir) {
        (void)pn_stardict_scan(dicts_dir, &d->catalog);
    }
    d->enabled_mask = all_dicts_mask(d->catalog.count);

    // 恢复上次持久化的词典勾选集合；若全部已删则安全回退全选。
    // Restore persisted dictionary selection mask; fall back safely to all if none remain.
    if (d->pref_media && d->pref_dir[0] && d->catalog.count > 0u) {
        uint32_t loaded = d->enabled_mask;
        if (pn_dict_ui_pref_load_mask(d->pref_media, d->pref_dir, &d->catalog, &loaded) == PN_OK && loaded) {
            d->enabled_mask = loaded;
        }
    }

    if (cands && cand_count > 0) {
        size_t take = cand_count > PN_DICT_UI_CAND_MAX ? PN_DICT_UI_CAND_MAX : cand_count;
        for (size_t i = 0; i < take; i++) {
            if (!cands[i][0]) continue;
            snprintf(d->cands[d->cand_count], PN_DICT_WORD_MAX, "%s", cands[i]);
            d->cand_count++;
        }
    }

    ui->impl = d;
    if (open_search || d->cand_count == 0) {
        d->from_search = (d->cand_count == 0);
        if (d->cand_count > 0) snprintf(d->draft, sizeof(d->draft), "%s", d->cands[0]);
        ui->mode = PN_DUI_SEARCH;
    } else {
        d->from_search = false;
        ui->mode = PN_DUI_RESULT;
        size_t chosen = 0;
        for (size_t i = 0; i < d->cand_count; i++) {
            run_lookup(d, d->cands[i]);
            if (d->has_hit) {
                chosen = i;
                break;
            }
        }
        if (!d->has_hit) {
            chosen = 0;
            run_lookup(d, d->cands[0]);
        }
        d->cand_selected = (int)chosen;
        snprintf(d->draft, sizeof(d->draft), "%s", d->cands[chosen]);
    }

    pn_status_t s = pn_dict_ui_present(ui, present, ctx);
    if (s != PN_OK) {
        pn_dict_ui_close(ui);
        return s;
    }
    return PN_OK;
}

pn_status_t pn_dict_ui_open(pn_dict_ui_t *ui, pn_pool_t *pool, pn_reader_app_t *reader, pn_epub_app_t *epub,
    const char *dicts_dir, const char cands[][PN_DICT_WORD_MAX], size_t cand_count, bool open_search,
    pn_reader_present_fn present, void *ctx) {
    return pn_dict_ui_open_ex(ui, pool, reader, epub, dicts_dir, cands, cand_count, open_search, NULL, present, ctx);
}

int pn_dict_ui_hit(const pn_dict_ui_t *ui, int x, int y) {
    if (!ui || !ui->impl || ui->mode == PN_DUI_CLOSED || x < 0 || x >= PN_UI_WIDTH || y < 0 || y >= PN_UI_HEIGHT) {
        return -1;
    }
    const dict_ui_impl_t *d = ui->impl;
    const int cx = PN_DICT_CARD_X, cy = d->sheet_y, cw = PN_DICT_CARD_W, ch = PN_DICT_SHEET_H;

    if (ui->mode == PN_DUI_RESULT) {
        // 点击卡片外任何阅读正文区直接关闭弹窗。/ Tapping anywhere outside the card dismisses it.
        if (x < cx || x >= cx + cw || y < cy || y >= cy + ch) return PN_DUI_DISMISS;
        // 标题栏：关闭 ×、搜索图标、词典多选按钮、按句摘录按钮。/ Header: close ×, search icon, dict checklist pill, highlight pill.
        if (y < cy + 68) {
            if (x >= cx + 524 && x < cx + cw) return PN_DUI_BACK;
            if (x >= cx + 470 && x < cx + 524 && y >= cy + 10 && y < cy + 58) return PN_DUI_OPEN_SEARCH;
            if (d->catalog.count > 1u && x >= cx + 358 && x < cx + 470 && y >= cy + 10 && y < cy + 58) return PN_DUI_OPEN_PICKER;
            if (d->has_anchor && y >= cy + 10 && y < cy + 58) {
                int hl_x = d->catalog.count > 1u ? (cx + 266) : (cx + 368);
                if (x >= hl_x - 4 && x < hl_x + 92) return PN_DUI_TOGGLE_HIGHLIGHT;
            }
            return -1;
        }
        // 候选词胶囊。/ Candidate chips.
        int cxs[CHIP_MAX_SHOWN], cws[CHIP_MAX_SHOWN];
        unsigned shown = chip_rects(d, cxs, cws);
        if (shown > 0u && y >= cy + 72 && y < cy + 116) {
            for (unsigned i = 0; i < shown; i++) {
                if (x >= cxs[i] && x < cxs[i] + cws[i]) return PN_DUI_CAND_BASE + (int)i;
            }
            return -1;
        }
        // 极简底栏翻页箭头。/ Minimalist footer < and > arrows.
        if (y >= cy + 476) {
            if (d->has_hit && d->page_count > 1u) {
                if (d->page_index > 0u && x >= cx + 8 && x < cx + 148) return PN_DUI_PREV_PAGE;
                if (d->page_index + 1u < d->page_count && x >= cx + cw - 148 && x < cx + cw - 8) return PN_DUI_NEXT_PAGE;
            }
            return -1;
        }
        // 正文区左右轻点便捷翻页。/ Left/right body tap shortcuts for multi-page definitions.
        int body_top = shown > 0u ? (cy + 118) : (cy + 70);
        if (d->has_hit && d->page_count > 1u && y >= body_top && y < cy + 476) {
            if (x < cx + cw / 3 && d->page_index > 0u) return PN_DUI_BODY_PREV;
            if (x >= cx + cw / 3 && d->page_index + 1u < d->page_count) return PN_DUI_BODY_NEXT;
        }
        return -1;
    }

    if (ui->mode == PN_DUI_PICKER) {
        // 点击卡片外区域返回查词结果并应用勾选；返回 PN_DUI_DISMISS 避免焦点扫描覆盖正文背景。
        // Tapping outside the card returns to results and applies the selection; return PN_DUI_DISMISS so focus scanning skips the reading background.
        if (x < cx || x >= cx + cw || y < cy || y >= cy + ch) return PN_DUI_DISMISS;
        if (y < cy + 68) {
            if (x >= cx + 524 && x < cx + cw) return PN_DUI_PICKER_DONE;
            return -1;
        }
        size_t total_pages = d->catalog.count ? ((d->catalog.count + PICKER_PER_PAGE - 1u) / PICKER_PER_PAGE) : 1u;
        if (y >= cy + 476) {
            if (x >= cx + 20 && x < cx + 128 && y >= cy + 482 && y < cy + 534) return PN_DUI_PICKER_ALL;
            if (x >= cx + cw - 148 && x < cx + cw - 20 && y >= cy + 482 && y < cy + 534) return PN_DUI_PICKER_DONE;
            if (total_pages > 1u) {
                if (d->picker_page > 0u && x >= cx + 136 && x < cx + 220) return PN_DUI_PICKER_PREV;
                if (d->picker_page + 1u < total_pages && x >= cx + 340 && x < cx + 424) return PN_DUI_PICKER_NEXT;
            }
            return -1;
        }
        size_t start = d->picker_page * PICKER_PER_PAGE;
        for (unsigned row = 0; row < PICKER_PER_PAGE && start + row < d->catalog.count; row++) {
            int ry = cy + 78 + (int)row * 64;
            if (x >= cx + 20 && x < cx + cw - 20 && y >= ry && y < ry + 56) {
                return PN_DUI_PICKER_TOGGLE_BASE + (int)(start + row);
            }
        }
        return -1;
    }

    if (ui->mode == PN_DUI_SEARCH) {
        int header = pn_w_header_hit(x, y, false);
        if (header == 1) return PN_DUI_BACK;
        if (y >= SEARCH_BTN_Y && y < SEARCH_BTN_Y + SEARCH_BTN_H) {
            if (x >= PN_UI_MARGIN && x < PN_UI_MARGIN + 188) return PN_DUI_SEARCH_CLEAR;
            if (x >= 236 && x < 236 + 416) return PN_DUI_SEARCH_GO;
        }
        int code = pn_keyboard_hit(&d->kb, SEARCH_KB_Y, x, y);
        if (code != PN_KB_NONE) return PN_DUI_KB_ENCODE(code);
    }
    return -1;
}

pn_status_t pn_dict_ui_event(pn_dict_ui_t *ui, int command, uint64_t now_ms, pn_reader_present_fn present, void *ctx) {
    if (!ui || !ui->impl || ui->mode == PN_DUI_CLOSED) return PN_INVALID;
    dict_ui_impl_t *d = ui->impl;

    if (command == PN_DUI_BACK || (command == PN_DUI_DISMISS && ui->mode == PN_DUI_RESULT)) {
        if (command == PN_DUI_BACK && ui->mode == PN_DUI_PICKER) {
            d->picker_hint[0] = '\0';
            ui->mode = PN_DUI_RESULT;
            run_lookup(d, d->query);
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command == PN_DUI_BACK && ui->mode == PN_DUI_SEARCH && !d->from_search && (d->query[0] || d->cand_count > 0)) {
            ui->mode = PN_DUI_RESULT;
            return pn_dict_ui_present(ui, present, ctx);
        }
        pn_epub_app_t *epub = d->epub;
        pn_reader_app_t *reader = d->reader;
        pn_frame_t restore = d->page_frame;
        bool has_restore = d->has_page_frame && restore.pixels != NULL && !d->highlight_changed;
        pn_dict_ui_close(ui);
        if (present && has_restore) return present(ctx, &restore, PN_REFRESH_GL16);
        if (present && epub && epub->impl) return pn_epub_app_step(epub, PN_APP_OPEN, now_ms, present, ctx);
        if (present && reader && reader->impl) return pn_reader_app_step(reader, PN_APP_OPEN, now_ms, present, ctx);
        return PN_OK;
    }

    if (ui->mode == PN_DUI_PICKER) {
        if (command == PN_DUI_PICKER_DONE || command == PN_DUI_DISMISS) {
            d->picker_hint[0] = '\0';
            ui->mode = PN_DUI_RESULT;
            run_lookup(d, d->query);
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command == PN_DUI_PICKER_ALL) {
            uint32_t full = all_dicts_mask(d->catalog.count);
            if ((d->enabled_mask & full) == full) return PN_EMPTY;
            d->enabled_mask = full;
            snprintf(d->picker_hint, sizeof(d->picker_hint), "已全选并保存");
            save_current_pref(d);
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command == PN_DUI_PICKER_PREV) {
            if (d->picker_page == 0u) return PN_EMPTY;
            d->picker_page--;
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command == PN_DUI_PICKER_NEXT) {
            size_t total_pages = d->catalog.count ? ((d->catalog.count + PICKER_PER_PAGE - 1u) / PICKER_PER_PAGE) : 1u;
            if (d->picker_page + 1u >= total_pages) return PN_EMPTY;
            d->picker_page++;
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command >= PN_DUI_PICKER_TOGGLE_BASE && command < PN_DUI_PICKER_TOGGLE_BASE + (int)PN_DICT_MAX) {
            size_t idx = (size_t)(command - PN_DUI_PICKER_TOGGLE_BASE);
            if (idx >= d->catalog.count) return PN_EMPTY;
            uint32_t bit = 1u << idx;
            uint32_t next_mask = d->enabled_mask ^ bit;
            if ((next_mask & all_dicts_mask(d->catalog.count)) == 0u) {
                // 至少保留 1 本启用词典。/ Always keep at least one dictionary enabled.
                snprintf(d->picker_hint, sizeof(d->picker_hint), "至少保留 1 本词典");
                return pn_dict_ui_present(ui, present, ctx);
            }
            d->enabled_mask = next_mask;
            snprintf(d->picker_hint, sizeof(d->picker_hint), "已保存（%u/%u 本）",
                (unsigned)mask_popcount(d->enabled_mask, d->catalog.count), (unsigned)d->catalog.count);
            save_current_pref(d);
            return pn_dict_ui_present(ui, present, ctx);
        }
        return PN_EMPTY;
    }

    if (ui->mode == PN_DUI_RESULT) {
        if (command == PN_DUI_TOGGLE_HIGHLIGHT && d->has_anchor) {
            bool added = false;
            pn_status_t hs = (d->epub && d->epub->impl)
                ? pn_epub_app_highlight_toggle(d->epub, d->anchor_x, d->anchor_y, now_ms, NULL, NULL, &added, NULL, 0)
                : (d->reader && d->reader->impl)
                    ? pn_reader_app_highlight_toggle(d->reader, d->anchor_x, d->anchor_y, now_ms, NULL, NULL, &added, NULL, 0)
                    : PN_UNSUPPORTED;
            if (hs == PN_OK) {
                d->sentence_highlighted = added;
                d->highlight_changed = true;
                return pn_dict_ui_present(ui, present, ctx);
            }
            return hs;
        }
        if (command == PN_DUI_OPEN_PICKER && d->catalog.count > 1u) {
            d->picker_page = 0;
            d->picker_hint[0] = '\0';
            ui->mode = PN_DUI_PICKER;
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command == PN_DUI_OPEN_SEARCH) {
            if (d->query[0]) snprintf(d->draft, sizeof(d->draft), "%s", d->query);
            d->kb.pinyin[0] = '\0';
            d->kb.cand_page = 0;
            d->notice[0] = '\0';
            ui->mode = PN_DUI_SEARCH;
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command == PN_DUI_NEXT_DICT && d->catalog.count > 1u) {
            size_t single = mask_single_index(d->enabled_mask, d->catalog.count);
            if (single == SIZE_MAX) d->enabled_mask = 1u;
            else if (single + 1u < d->catalog.count) d->enabled_mask = 1u << (single + 1u);
            else d->enabled_mask = all_dicts_mask(d->catalog.count);
            save_current_pref(d);
            run_lookup(d, d->query);
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command == PN_DUI_PREV_PAGE || command == PN_DUI_BODY_PREV) {
            if (!d->has_hit || d->page_index == 0) return PN_EMPTY;
            d->page_index--;
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command == PN_DUI_NEXT_PAGE || command == PN_DUI_BODY_NEXT) {
            if (!d->has_hit || d->page_index + 1u >= d->page_count) return PN_EMPTY;
            d->page_index++;
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command >= PN_DUI_CAND_BASE && command < PN_DUI_CAND_BASE + (int)PN_DICT_UI_CAND_MAX) {
            size_t idx = (size_t)(command - PN_DUI_CAND_BASE);
            if (idx >= d->cand_count) return PN_EMPTY;
            d->cand_selected = (int)idx;
            snprintf(d->draft, sizeof(d->draft), "%s", d->cands[idx]);
            run_lookup(d, d->cands[idx]);
            return pn_dict_ui_present(ui, present, ctx);
        }
        return PN_EMPTY;
    }

    if (ui->mode == PN_DUI_SEARCH) {
        if (command == PN_DUI_SEARCH_CLEAR) {
            if (!d->draft[0] && !d->kb.pinyin[0]) return PN_EMPTY;
            d->draft[0] = '\0';
            d->kb.pinyin[0] = '\0';
            d->kb.cand_page = 0;
            d->notice[0] = '\0';
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command == PN_DUI_SEARCH_GO) {
            (void)pn_keyboard_flush(&d->kb, d->draft, sizeof(d->draft), PN_DICT_WORD_MAX - 1u);
            if (!d->draft[0]) return PN_EMPTY;
            d->cand_count = 0;
            d->cand_selected = -1;
            d->from_search = false;
            d->top_docked = false;
            d->sheet_y = PN_DICT_BOTTOM_Y;
            run_lookup(d, d->draft);
            ui->mode = PN_DUI_RESULT;
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command >= PN_DUI_KB_BASE) {
            int code = PN_DUI_KB_DECODE(command);
            if (!pn_keyboard_apply(&d->kb, code, d->draft, sizeof(d->draft), PN_DICT_WORD_MAX - 1u)) {
                return PN_EMPTY;
            }
            d->notice[0] = '\0';
            return pn_dict_ui_present(ui, present, ctx);
        }
    }
    return PN_EMPTY;
}

size_t pn_dict_ui_dict_count(const pn_dict_ui_t *ui) {
    return (ui && ui->impl) ? ((const dict_ui_impl_t *)ui->impl)->catalog.count : 0;
}

uint32_t pn_dict_ui_enabled_mask(const pn_dict_ui_t *ui) {
    if (!ui || !ui->impl) return 0u;
    const dict_ui_impl_t *d = ui->impl;
    return d->enabled_mask & all_dicts_mask(d->catalog.count);
}

size_t pn_dict_ui_enabled_count(const pn_dict_ui_t *ui) {
    if (!ui || !ui->impl) return 0u;
    const dict_ui_impl_t *d = ui->impl;
    return mask_popcount(d->enabled_mask, d->catalog.count);
}

size_t pn_dict_ui_selected_dict(const pn_dict_ui_t *ui) {
    if (!ui || !ui->impl) return SIZE_MAX;
    const dict_ui_impl_t *d = ui->impl;
    return mask_single_index(d->enabled_mask, d->catalog.count);
}

size_t pn_dict_ui_matched_dicts(const pn_dict_ui_t *ui) {
    if (!ui || !ui->impl) return 0;
    const dict_ui_impl_t *d = ui->impl;
    return d->has_hit ? d->hit.matched_count : 0;
}

const char *pn_dict_ui_matched_word(const pn_dict_ui_t *ui) {
    if (!ui || !ui->impl) return "";
    const dict_ui_impl_t *d = ui->impl;
    return d->has_hit ? d->hit.matched_word : "";
}

const char *pn_dict_ui_definition(const pn_dict_ui_t *ui) {
    if (!ui || !ui->impl) return "";
    const dict_ui_impl_t *d = ui->impl;
    return d->has_hit ? d->hit.definition : "";
}

size_t pn_dict_ui_page_index(const pn_dict_ui_t *ui) {
    return (ui && ui->impl) ? ((const dict_ui_impl_t *)ui->impl)->page_index : 0;
}

size_t pn_dict_ui_page_count(const pn_dict_ui_t *ui) {
    return (ui && ui->impl) ? ((const dict_ui_impl_t *)ui->impl)->page_count : 0;
}

int pn_dict_ui_sheet_y(const pn_dict_ui_t *ui) {
    return (ui && ui->impl) ? ((const dict_ui_impl_t *)ui->impl)->sheet_y : 0;
}

void pn_dict_ui_close(pn_dict_ui_t *ui) {
    if (!ui) return;
    if (ui->impl) {
        pn_free(ui->impl);
        ui->impl = NULL;
    }
    ui->mode = PN_DUI_CLOSED;
}
