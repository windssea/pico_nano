/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源离线词典界面实现，见 pn_dict_ui.h。
 * English: Shared offline dictionary UI implementation, see pn_dict_ui.h.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_dict_ui.h"
#include "pn_keyboard.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <string.h>

#define CHIP_Y 142
#define CHIP_H 52
#define CHIP_MAX_SHOWN 4u
#define PAGER_Y 1092
#define PAGER_H 80
#define SEARCH_INPUT_Y 204
#define SEARCH_INPUT_H 84
#define SEARCH_BTN_Y 310
#define SEARCH_BTN_H 80
#define SEARCH_KB_Y 680
#define DEF_FONT_PX 26
#define DEF_LINE_PITCH 38
#define DEF_BOTTOM_Y 1068

typedef struct {
    pn_pool_t *pool;
    pn_reader_app_t *reader;
    pn_epub_app_t *epub;
    pn_stardict_catalog_t catalog;
    size_t active_dict; ///< SIZE_MAX 表示全部词典级联，否则限定单本词典 / SIZE_MAX cascades all dicts, else single dict index
    char cands[PN_DICT_UI_CAND_MAX][PN_DICT_WORD_MAX];
    size_t cand_count;
    int cand_selected;
    char query[PN_DICT_WORD_MAX];
    char draft[PN_DICT_WORD_MAX];
    pn_keyboard_t kb;
    pn_stardict_hit_t hit;
    bool has_hit;
    bool from_search;
    bool pages_dirty;
    size_t page_starts[PN_DICT_UI_PAGES_MAX];
    size_t page_count;
    size_t page_index;
    char notice[128];
} dict_ui_impl_t;

/// 计算 UTF-8 字符字节数。/ Byte length of one UTF-8 character.
static size_t utf8_char_len(const char *s) {
    unsigned char c = (unsigned char)*s;
    if (!c) return 0;
    if (c < 0x80u) return 1;
    if ((c & 0xE0u) == 0xC0u) return s[1] ? 2 : 1;
    if ((c & 0xF0u) == 0xE0u) return (s[1] && s[2]) ? 3 : 1;
    if ((c & 0xF8u) == 0xF0u) return (s[1] && s[2] && s[3]) ? 4 : 1;
    return 1;
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

/// 渲染或测量单页释义正文，返回下一页起始字节偏移。
/// Render or measure one page of wrapped definition text, returning the byte offset where the next page starts.
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
                // 英文单词尽量按空格断行，避免截断单词。/ Prefer breaking ASCII words at whitespace when possible.
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
            s = pn_w_text_ex(font, frame, buf, x, y0 + (int)line * pitch, max_w, PN_ALIGN_LEFT, PN_UI_INK, false);
        }
        line++;
    }
    if (next_off_out) *next_off_out = pos;
    return s;
}

/// 计算正文可用起始 y（受候选栏与副标题影响）。/ Compute definition body start Y based on candidate chips and sub-caption.
static int result_dict_bar_y(const dict_ui_impl_t *d) {
    return d->cand_count > 1u ? (CHIP_Y + CHIP_H + 14) : 144;
}

static void result_def_bounds(const dict_ui_impl_t *d, int *y0_out, unsigned *lines_out) {
    int bar_y = result_dict_bar_y(d);
    int head_y = bar_y + 56 + 44;
    bool show_orig = d->has_hit && d->query[0] && strcmp(d->hit.matched_word, d->query) != 0;
    int y0 = head_y + (show_orig ? 64 : 36) + 28;
    int avail = DEF_BOTTOM_Y - (y0 - DEF_FONT_PX);
    unsigned lines = avail > DEF_LINE_PITCH ? (unsigned)(avail / DEF_LINE_PITCH) : 1u;
    if (lines > 20u) lines = 20u;
    *y0_out = y0;
    *lines_out = lines;
}

/// 按当前字体度量计算全部释义分页起点。/ Compute exact byte offsets for all definition pages using active font metrics.
static pn_status_t rebuild_pages(dict_ui_impl_t *d, pn_font_t *font) {
    d->page_starts[0] = 0;
    d->page_count = 1;
    if (!d->has_hit || !d->hit.definition[0]) {
        d->page_index = 0;
        d->pages_dirty = false;
        return PN_OK;
    }
    int y0 = 0;
    unsigned max_lines = 1;
    result_def_bounds(d, &y0, &max_lines);

    int original = font->pixels;
    pn_status_t s = pn_font_size(font, DEF_FONT_PX);
    size_t len = strlen(d->hit.definition);
    size_t off = 0;
    while (s == PN_OK && off < len && d->page_count < PN_DICT_UI_PAGES_MAX) {
        size_t next_off = off;
        s = layout_def_page(font, NULL, d->hit.definition, off, PN_UI_MARGIN, y0, 620, max_lines, DEF_LINE_PITCH, &next_off);
        if (s != PN_OK || next_off <= off) break;
        off = next_off;
        if (off < len) d->page_starts[d->page_count++] = off;
    }
    pn_status_t restored = pn_font_size(font, original);
    if (d->page_index >= d->page_count) d->page_index = d->page_count - 1u;
    d->pages_dirty = false;
    return s == PN_OK ? restored : s;
}

/// 执行单次查询并重置分页状态。/ Execute one query and reset pagination state.
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
    pn_status_t s = pn_stardict_query(d->pool, &d->catalog, d->query, d->active_dict, &d->hit);
    if (s == PN_OK) {
        d->has_hit = true;
    } else if (s == PN_EMPTY) {
        snprintf(d->notice, sizeof(d->notice), "未在%s中找到「%.48s」",
            d->active_dict < d->catalog.count ? "当前词典" : "已安装词典", word);
    } else {
        snprintf(d->notice, sizeof(d->notice), "读取词典失败（%d），请检查文件完整性", (int)s);
    }
}

/// 计算候选词胶囊坐标，确保绘制与命中完全一致。/ Compute candidate chip geometry shared by render and hit test.
static unsigned chip_rects(const dict_ui_impl_t *d, int xs[CHIP_MAX_SHOWN], int ws[CHIP_MAX_SHOWN]) {
    unsigned shown = d->cand_count > CHIP_MAX_SHOWN ? CHIP_MAX_SHOWN : (unsigned)d->cand_count;
    if (shown <= 1u) return 0;
    int gap = 10;
    int w = (620 - (int)(shown - 1u) * gap) / (int)shown;
    for (unsigned i = 0; i < shown; i++) {
        xs[i] = PN_UI_MARGIN + (int)i * (w + gap);
        ws[i] = w;
    }
    return shown;
}

static pn_status_t paint_overlay(void *ctx, pn_font_t *font, pn_font_t *fallback, pn_frame_t *frame) {
    return pn_dict_ui_render((pn_dict_ui_t *)ctx, font, fallback, frame);
}

pn_status_t pn_dict_ui_render(pn_dict_ui_t *ui, pn_font_t *font, pn_font_t *fallback, pn_frame_t *frame) {
    if (!ui || !ui->impl || ui->mode == PN_DUI_CLOSED || !font || !font->impl || !frame || !frame->pixels
        || frame->width != PN_UI_WIDTH || frame->height != PN_UI_HEIGHT) {
        return PN_INVALID;
    }
    dict_ui_impl_t *d = ui->impl;
    pn_frame_clear(frame, PN_UI_PAPER);

    pn_w_font_scope_t scope = {0};
    pn_status_t s = pn_w_font_scope_begin(&scope, d->pool, fallback);
    if (s != PN_OK) return s;

    if (ui->mode == PN_DUI_RESULT) {
        s = pn_w_header(font, frame, "< 返回阅读", "词典", "搜索");

        // 候选词切换栏（中文多字候选时显示）/ Candidate selector chips when multiple candidates exist.
        int cxs[CHIP_MAX_SHOWN], cws[CHIP_MAX_SHOWN];
        unsigned shown = chip_rects(d, cxs, cws);
        for (unsigned i = 0; i < shown && s == PN_OK; i++) {
            bool sel = ((int)i == d->cand_selected);
            pn_w_round_fill(frame, cxs[i], CHIP_Y, cws[i], CHIP_H, 14, sel ? PN_UI_SURFACE : PN_UI_PAPER);
            pn_w_round_stroke(frame, cxs[i], CHIP_Y, cws[i], CHIP_H, 14, sel ? 2.5f : 1.5f, sel ? PN_UI_INK : 9);
            s = sized_text_ex(font, frame, d->cands[i], 24, cxs[i] + 8, CHIP_Y + 35, cws[i] - 16, PN_ALIGN_CENTER, PN_UI_INK, sel);
        }

        // 词典状态栏与切换按钮 / Active dictionary bar and switcher pill.
        int bar_y = result_dict_bar_y(d);
        if (s == PN_OK) {
            char label[128];
            if (!d->catalog.count) {
                snprintf(label, sizeof(label), "未检测到离线词典");
            } else if (d->has_hit) {
                snprintf(label, sizeof(label), "词典：%s", d->hit.dict_name);
            } else if (d->active_dict < d->catalog.count) {
                snprintf(label, sizeof(label), "词典：%s", d->catalog.items[d->active_dict].name);
            } else {
                snprintf(label, sizeof(label), "已载入 %zu 本词典（自动匹配）", d->catalog.count);
            }
            int label_w = d->catalog.count > 1u ? 408 : 620;
            s = sized_text_ex(font, frame, label, 22, PN_UI_MARGIN, bar_y + 32, label_w, PN_ALIGN_LEFT, PN_UI_MUTED, false);
        }
        if (s == PN_OK && d->catalog.count > 1u) {
            char sw[64];
            if (d->active_dict < d->catalog.count) {
                snprintf(sw, sizeof(sw), "切换 (%u/%u)", (unsigned)(d->active_dict + 1u), (unsigned)d->catalog.count);
            } else {
                snprintf(sw, sizeof(sw), "全部 (%u本)", (unsigned)d->catalog.count);
            }
            pn_w_round_fill(frame, 452, bar_y + 2, 200, 44, 14, PN_UI_SURFACE);
            pn_w_round_stroke(frame, 452, bar_y + 2, 200, 44, 14, 1.5f, 9);
            s = sized_text_ex(font, frame, sw, 21, 460, bar_y + 31, 184, PN_ALIGN_CENTER, PN_UI_INK, false);
        }
        pn_frame_rect(frame, PN_UI_MARGIN, bar_y + 54, 620, 1, PN_UI_RULE);

        if (s == PN_OK && !d->catalog.count) {
            pn_w_icon(frame, PN_ICON_SEARCH, (PN_UI_WIDTH - 72) / 2, 340, 72, PN_UI_INK);
            s = sized_text_ex(font, frame, "尚未安装离线词典", 34, PN_UI_MARGIN, 468, 620, PN_ALIGN_CENTER, PN_UI_INK, true);
            if (s == PN_OK) {
                int orig = font->pixels;
                s = pn_font_size(font, 24);
                if (s == PN_OK) {
                    s = pn_w_text_lines_ex(font, frame,
                        "支持 StarDict 格式（.ifo、.idx 与 .dict 或 .dict.dz）。",
                        PN_UI_MARGIN, 536, 620, 2, 38, PN_UI_MUTED, false);
                }
                if (s == PN_OK) {
                    s = pn_w_text_lines_ex(font, frame,
                        "可通过「传书 → 词典」上传，或用 USB 复制到：",
                        PN_UI_MARGIN, 616, 620, 2, 38, PN_UI_MUTED, false);
                }
                if (s == PN_OK) {
                    s = pn_w_text_lines_ex(font, frame,
                        "/piconano/dicts/ 目录（支持一级子目录）。",
                        PN_UI_MARGIN, 676, 620, 2, 38, PN_UI_INK, true);
                }
                pn_status_t r = pn_font_size(font, orig);
                if (s == PN_OK) s = r;
            }
        } else if (s == PN_OK && d->has_hit) {
            if (d->pages_dirty) s = rebuild_pages(d, font);
            int head_y = bar_y + 56 + 44;
            if (s == PN_OK) {
                s = sized_text_ex(font, frame, d->hit.matched_word, 36, PN_UI_MARGIN, head_y, 620, PN_ALIGN_LEFT, PN_UI_INK, true);
            }
            bool show_orig = d->query[0] && strcmp(d->hit.matched_word, d->query) != 0;
            if (s == PN_OK && show_orig) {
                char orig_line[180];
                snprintf(orig_line, sizeof(orig_line), "查询原词：%s", d->query);
                s = sized_text_ex(font, frame, orig_line, 21, PN_UI_MARGIN, head_y + 30, 620, PN_ALIGN_LEFT, PN_UI_MUTED, false);
            }
            int y0 = 0;
            unsigned max_lines = 1;
            result_def_bounds(d, &y0, &max_lines);
            pn_frame_rect(frame, PN_UI_MARGIN, y0 - 38, 620, 1, PN_UI_SELECT);
            if (s == PN_OK) {
                int orig = font->pixels;
                s = pn_font_size(font, DEF_FONT_PX);
                size_t start_off = d->page_index < d->page_count ? d->page_starts[d->page_index] : 0;
                if (s == PN_OK) {
                    s = layout_def_page(font, frame, d->hit.definition, start_off, PN_UI_MARGIN, y0, 620, max_lines, DEF_LINE_PITCH, NULL);
                }
                pn_status_t r = pn_font_size(font, orig);
                if (s == PN_OK) s = r;
            }
        } else if (s == PN_OK) {
            int head_y = bar_y + 56 + 60;
            const char *q = d->query[0] ? d->query : "未选中词语";
            s = sized_text_ex(font, frame, q, 34, PN_UI_MARGIN, head_y, 620, PN_ALIGN_LEFT, PN_UI_INK, true);
            if (s == PN_OK) {
                int orig = font->pixels;
                s = pn_font_size(font, 26);
                if (s == PN_OK) {
                    s = pn_w_text_lines_ex(font, frame,
                        d->notice[0] ? d->notice : "未找到释义。可点击上方候选词、切换词典，或点右上「搜索」。",
                        PN_UI_MARGIN, head_y + 64, 620, 4, 42, PN_UI_MUTED, false);
                }
                pn_status_t r = pn_font_size(font, orig);
                if (s == PN_OK) s = r;
            }
        }

        // 底部分页条 / Bottom pager.
        if (s == PN_OK) {
            bool can_prev = d->has_hit && d->page_index > 0;
            bool can_next = d->has_hit && d->page_index + 1u < d->page_count;
            s = sized_button(font, frame, "上一页", 28, PN_UI_MARGIN, PAGER_Y, 196, PAGER_H, can_prev ? 0u : PN_W_DISABLED);
            if (s == PN_OK && d->has_hit) {
                char ptext[64];
                snprintf(ptext, sizeof(ptext), "第 %u / %u 页", (unsigned)(d->page_index + 1u), (unsigned)d->page_count);
                s = sized_text_ex(font, frame, ptext, 24, 228, PAGER_Y + 49, 228, PN_ALIGN_CENTER, PN_UI_MUTED, false);
            }
            if (s == PN_OK) {
                s = sized_button(font, frame, "下一页", 28, 456, PAGER_Y, 196, PAGER_H, can_next ? 0u : PN_W_DISABLED);
            }
        }
    } else if (ui->mode == PN_DUI_SEARCH) {
        s = pn_w_header(font, frame, d->from_search ? "< 返回阅读" : "< 返回结果", "手动查词", NULL);
        if (s == PN_OK) {
            char sub[96];
            if (d->catalog.count) snprintf(sub, sizeof(sub), "已载入 %zu 本离线词典 · 支持中英文查词", d->catalog.count);
            else snprintf(sub, sizeof(sub), "尚未安装离线词典（请放入 /piconano/dicts/）");
            s = sized_text_ex(font, frame, sub, 22, PN_UI_MARGIN, 174, 620, PN_ALIGN_LEFT, PN_UI_MUTED, false);
        }
        // 输入框 / Query input box.
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
            const char *hint = d->notice[0] ? d->notice : "阅读正文时也可长按任意词语直接查词";
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

pn_status_t pn_dict_ui_open(pn_dict_ui_t *ui, pn_pool_t *pool, pn_reader_app_t *reader, pn_epub_app_t *epub,
    const char *dicts_dir, const char cands[][PN_DICT_WORD_MAX], size_t cand_count, bool open_search,
    pn_reader_present_fn present, void *ctx) {
    if (!ui || !pool) return PN_INVALID;
    pn_dict_ui_close(ui);

    dict_ui_impl_t *d = pn_alloc_tagged(pool, PN_MEM_UI, sizeof(*d));
    if (!d) return PN_NO_MEMORY;
    memset(d, 0, sizeof(*d));
    d->pool = pool;
    d->reader = reader;
    d->epub = epub;
    d->active_dict = SIZE_MAX;
    d->cand_selected = -1;
    d->page_count = 1;
    d->pages_dirty = true;
    pn_keyboard_set_ime(&d->kb, true, false);

    if (dicts_dir && *dicts_dir) {
        (void)pn_stardict_scan(dicts_dir, &d->catalog);
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
        // 依次尝试所有候选词，优先自动呈现首个命中释义（例如中文 2 字词优先于 4 字未命中串）。
        // Probe candidates in order so the first dictionary hit opens automatically (e.g., 2-char CJK word over 4-char miss).
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

int pn_dict_ui_hit(const pn_dict_ui_t *ui, int x, int y) {
    if (!ui || !ui->impl || ui->mode == PN_DUI_CLOSED || x < 0 || x >= PN_UI_WIDTH || y < 0 || y >= PN_UI_HEIGHT) {
        return -1;
    }
    const dict_ui_impl_t *d = ui->impl;
    int header = pn_w_header_hit(x, y, ui->mode == PN_DUI_RESULT);
    if (header == 1) return PN_DUI_BACK;
    if (header == 2 && ui->mode == PN_DUI_RESULT) return PN_DUI_OPEN_SEARCH;

    if (ui->mode == PN_DUI_RESULT) {
        int cxs[CHIP_MAX_SHOWN], cws[CHIP_MAX_SHOWN];
        unsigned shown = chip_rects(d, cxs, cws);
        if (shown > 0 && y >= CHIP_Y && y < CHIP_Y + CHIP_H) {
            for (unsigned i = 0; i < shown; i++) {
                if (x >= cxs[i] && x < cxs[i] + cws[i]) return PN_DUI_CAND_BASE + (int)i;
            }
        }
        int bar_y = result_dict_bar_y(d);
        if (d->catalog.count > 1u && x >= 444 && x < 660 && y >= bar_y - 4 && y < bar_y + 52) {
            return PN_DUI_NEXT_DICT;
        }
        if (y >= PAGER_Y && y < PAGER_Y + PAGER_H) {
            if (x >= PN_UI_MARGIN && x < PN_UI_MARGIN + 196) return PN_DUI_PREV_PAGE;
            if (x >= 456 && x < 456 + 196) return PN_DUI_NEXT_PAGE;
        }
        return -1;
    }

    if (ui->mode == PN_DUI_SEARCH) {
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

    if (command == PN_DUI_BACK) {
        if (ui->mode == PN_DUI_SEARCH && !d->from_search && (d->query[0] || d->cand_count > 0)) {
            ui->mode = PN_DUI_RESULT;
            return pn_dict_ui_present(ui, present, ctx);
        }
        pn_epub_app_t *epub = d->epub;
        pn_reader_app_t *reader = d->reader;
        pn_dict_ui_close(ui);
        if (present && epub && epub->impl) return pn_epub_app_step(epub, PN_APP_OPEN, now_ms, present, ctx);
        if (present && reader && reader->impl) return pn_reader_app_step(reader, PN_APP_OPEN, now_ms, present, ctx);
        return PN_OK;
    }

    if (ui->mode == PN_DUI_RESULT) {
        if (command == PN_DUI_OPEN_SEARCH) {
            if (d->query[0]) snprintf(d->draft, sizeof(d->draft), "%s", d->query);
            d->kb.pinyin[0] = '\0';
            d->kb.cand_page = 0;
            d->notice[0] = '\0';
            ui->mode = PN_DUI_SEARCH;
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command == PN_DUI_NEXT_DICT && d->catalog.count > 1u) {
            if (d->active_dict == SIZE_MAX) d->active_dict = 0;
            else if (d->active_dict + 1u < d->catalog.count) d->active_dict++;
            else d->active_dict = SIZE_MAX;
            run_lookup(d, d->query);
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command == PN_DUI_PREV_PAGE) {
            if (!d->has_hit || d->page_index == 0) return PN_EMPTY;
            d->page_index--;
            return pn_dict_ui_present(ui, present, ctx);
        }
        if (command == PN_DUI_NEXT_PAGE) {
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

void pn_dict_ui_close(pn_dict_ui_t *ui) {
    if (!ui) return;
    if (ui->impl) {
        pn_free(ui->impl);
        ui->impl = NULL;
    }
    ui->mode = PN_DUI_CLOSED;
}
