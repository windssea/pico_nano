/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：全局「我的摘录」跨书查询与管理界面实现，见 pn_excerpts_ui.h。
 * English: Global cross-book "My Excerpts" query and management UI implementation, see pn_excerpts_ui.h.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_excerpts_ui.h"
#include "pn_home_prefs.h"
#include "pn_keyboard.h"
#include "pn_widgets.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define LIST_SUMMARY_Y 136
#define LIST_ROW_Y0 194
#define LIST_ROW_H 152
#define LIST_ROW_PITCH 166
#define LIST_PAGER_Y 1084
#define SEARCH_INPUT_Y 196
#define SEARCH_INPUT_H 86
#define SEARCH_BTN_Y 304
#define SEARCH_BTN_H 76
#define SEARCH_KB_Y (PN_UI_HEIGHT - PN_KB_HEIGHT - 24)
#define DETAIL_BTN_Y0 568
#define DETAIL_BTN_PITCH 106
#define DELETE_BTN_Y 620

typedef struct {
    pn_highlight_t hl;
    char book_title[96];
    char book_path[PN_RECENT_PATH_MAX];
    unsigned recent_rank; ///< 匹配 recent 的排位（越小越靠前），未匹配为 999 / Recent rank (smaller is newer), 999 if unknown
} excerpt_entry_t;

typedef struct {
    pn_pool_t *pool;
    pn_media_t *state_media;
    char state_dir[PN_JOURNAL_PATH_MAX];
    excerpt_entry_t *entries;
    size_t count;
    uint16_t filtered[PN_EXCERPTS_UI_MAX];
    size_t filtered_count;
    unsigned page;
    int selected; ///< 指向 filtered[selected] / Index into filtered[]
    char query[PN_EXCERPTS_QUERY_MAX];
    char draft[PN_EXCERPTS_QUERY_MAX];
    char notice[96];
    pn_keyboard_t kb;
} ex_impl_t;

static pn_status_t sized_text_ex(pn_font_t *font, pn_frame_t *frame, const char *text, int size,
    int x, int baseline, int width, pn_align_t align, uint8_t ink, bool bold) {
    int orig = font->pixels;
    pn_status_t s = pn_font_size(font, size);
    if (s == PN_OK) s = pn_w_text_ex(font, frame, text, x, baseline, width, align, ink, bold);
    pn_status_t r = pn_font_size(font, orig);
    return s == PN_OK ? r : s;
}

static pn_status_t sized_button(pn_font_t *font, pn_frame_t *frame, const char *label, int size,
    int x, int y, int w, int h, unsigned style) {
    int orig = font->pixels;
    pn_status_t s = pn_font_size(font, size);
    if (s == PN_OK) s = pn_w_button(font, frame, label, x, y, w, h, style);
    pn_status_t r = pn_font_size(font, orig);
    return s == PN_OK ? r : s;
}

/// 解析 "<64位小写hex>.highlights" 目录名为 32 字节 SHA-256 书目身份。
/// Parse "<64-hex>.highlights" directory name into a 32-byte SHA-256 book ID.
static bool parse_highlights_dir(const char *name, pn_book_id_t *out) {
    static const char suffix[] = ".highlights";
    size_t len = strlen(name);
    if (len != 64u + sizeof(suffix) - 1u) return false;
    if (memcmp(name + 64u, suffix, sizeof(suffix) - 1u) != 0) return false;
    for (unsigned i = 0; i < 32u; i++) {
        unsigned hi = 0, lo = 0;
        unsigned char c0 = (unsigned char)name[i * 2u];
        unsigned char c1 = (unsigned char)name[i * 2u + 1u];
        if (c0 >= '0' && c0 <= '9') hi = (unsigned)(c0 - '0');
        else if (c0 >= 'a' && c0 <= 'f') hi = (unsigned)(c0 - 'a' + 10);
        else return false;
        if (c1 >= '0' && c1 <= '9') lo = (unsigned)(c1 - '0');
        else if (c1 >= 'a' && c1 <= 'f') lo = (unsigned)(c1 - 'a' + 10);
        else return false;
        out->sha256[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

/// 从完整书籍路径提取可读书名（去目录前缀与 .epub/.txt 后缀）。
/// Extract readable book title from full path (strip directory prefix and .epub/.txt suffix).
static void title_from_path(const char *path, char *out, size_t cap) {
    if (!path || !*path) {
        snprintf(out, cap, "未知书籍");
        return;
    }
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t len = strlen(base);
    const char *dot = strrchr(base, '.');
    if (dot && (!strcasecmp(dot, ".epub") || !strcasecmp(dot, ".txt"))) {
        len = (size_t)(dot - base);
    }
    if (len == 0u) {
        snprintf(out, cap, "未知书籍");
        return;
    }
    if (len >= cap) {
        len = cap - 1u;
        while (len > 0u && (((unsigned char)base[len] & 0xC0u) == 0x80u)) len--;
    }
    memcpy(out, base, len);
    out[len] = '\0';
}

/// ASCII 大小写不敏感子串匹配（兼容 UTF-8 中文精确子串匹配）。
/// Case-insensitive ASCII substring match (works identically for UTF-8 CJK substrings).
static bool contains_ci(const char *hay, const char *needle) {
    if (!needle || !*needle) return true;
    if (!hay || !*hay) return false;
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i]) {
            unsigned char a = (unsigned char)p[i];
            unsigned char b = (unsigned char)needle[i];
            if (a < 128u) a = (unsigned char)tolower(a);
            if (b < 128u) b = (unsigned char)tolower(b);
            if (a != b) break;
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

static void rebuild_filter(ex_impl_t *d) {
    d->filtered_count = 0;
    for (size_t i = 0; i < d->count && d->filtered_count < PN_EXCERPTS_UI_MAX; i++) {
        const excerpt_entry_t *e = &d->entries[i];
        if (!d->query[0] || contains_ci(e->hl.text, d->query) || contains_ci(e->book_title, d->query)) {
            d->filtered[d->filtered_count++] = (uint16_t)i;
        }
    }
    unsigned total_pages = d->filtered_count ? (unsigned)((d->filtered_count + PN_EXCERPTS_UI_ROWS - 1u) / PN_EXCERPTS_UI_ROWS) : 1u;
    if (d->page >= total_pages) d->page = total_pages - 1u;
    if (d->selected >= (int)d->filtered_count) d->selected = d->filtered_count ? (int)d->filtered_count - 1 : -1;
}

static int compare_entries(const excerpt_entry_t *a, const excerpt_entry_t *b) {
    if (a->recent_rank != b->recent_rank) return a->recent_rank < b->recent_rank ? -1 : 1;
    if (a->hl.id != b->hl.id) return a->hl.id > b->hl.id ? -1 : 1;
    return 0;
}

static void sort_entries(excerpt_entry_t *arr, size_t n) {
    for (size_t i = 1; i < n; i++) {
        excerpt_entry_t key = arr[i];
        size_t j = i;
        while (j > 0 && compare_entries(&key, &arr[j - 1]) < 0) {
            arr[j] = arr[j - 1];
            j--;
        }
        arr[j] = key;
    }
}

pn_status_t pn_excerpts_ui_open(pn_excerpts_ui_t *ui, pn_pool_t *pool, pn_media_t *state_media,
    const char *state_dir, const pn_recent_snapshot_t *recent) {
    if (!ui || !pool || !state_media || !state_dir || !*state_dir) return PN_INVALID;
    pn_excerpts_ui_close(ui);

    ex_impl_t *d = pn_alloc_tagged(pool, PN_MEM_UI, sizeof(*d));
    if (!d) return PN_NO_MEMORY;
    memset(d, 0, sizeof(*d));
    d->pool = pool;
    d->state_media = state_media;
    d->selected = -1;
    snprintf(d->state_dir, sizeof(d->state_dir), "%s", state_dir);
    pn_keyboard_set_ime(&d->kb, true, false);

    d->entries = pn_alloc_tagged(pool, PN_MEM_UI, PN_EXCERPTS_UI_MAX * sizeof(*d->entries));
    if (!d->entries) {
        pn_free(d);
        return PN_NO_MEMORY;
    }

    // 临时持短 READ 租约扫描全部 *.highlights 目录并汇总每本书的有效摘录。
    // Hold a short READ lease to scan all *.highlights directories and aggregate live excerpts.
    pn_media_lease_t lease = {0};
    pn_status_t s = pn_media_acquire(state_media, PN_MEDIA_READ, &lease);
    if (s == PN_OK) {
        DIR *dir = opendir(state_dir);
        if (dir) {
            pn_highlight_t *batch = pn_alloc_tagged(pool, PN_MEM_UI, PN_HIGHLIGHTS_MAX * sizeof(*batch));
            if (batch) {
                struct dirent *ent;
                while ((ent = readdir(dir)) != NULL && d->count < PN_EXCERPTS_UI_MAX) {
                    pn_book_id_t book = {0};
                    if (!parse_highlights_dir(ent->d_name, &book)) continue;
                    pn_highlights_t store = {0};
                    if (pn_highlights_init(&store, state_media, &lease, state_dir, &book) != PN_OK) continue;
                    size_t got = 0;
                    if (pn_highlights_load_all(&store, batch, PN_HIGHLIGHTS_MAX, &got) != PN_OK || got == 0u) continue;

                    // 从 recent 快照解析书名与路径。/ Resolve title and path from recent snapshot.
                    char title[96] = "已摘录书籍";
                    char bpath[PN_RECENT_PATH_MAX] = "";
                    unsigned rank = 999u;
                    if (recent) {
                        for (size_t ri = 0; ri < recent->count; ri++) {
                            if (memcmp(recent->items[ri].book.sha256, book.sha256, 32u) == 0) {
                                rank = (unsigned)ri;
                                snprintf(bpath, sizeof(bpath), "%s", recent->items[ri].path);
                                title_from_path(bpath, title, sizeof(title));
                                break;
                            }
                        }
                    }
                    for (size_t hi = 0; hi < got && d->count < PN_EXCERPTS_UI_MAX; hi++) {
                        excerpt_entry_t *dst = &d->entries[d->count++];
                        memset(dst, 0, sizeof(*dst));
                        dst->hl = batch[hi];
                        dst->recent_rank = rank;
                        snprintf(dst->book_title, sizeof(dst->book_title), "%s", title);
                        snprintf(dst->book_path, sizeof(dst->book_path), "%s", bpath);
                    }
                }
                pn_free(batch);
            }
            closedir(dir);
        }
        (void)pn_media_release(state_media, &lease);
    } else {
        snprintf(d->notice, sizeof(d->notice), "内部存储暂不可读");
    }

    sort_entries(d->entries, d->count);
    rebuild_filter(d);
    ui->impl = d;
    ui->mode = PN_EXUI_LIST;
    ui->open_request = false;
    return PN_OK;
}

pn_status_t pn_excerpts_ui_render(const pn_excerpts_ui_t *ui, pn_font_t *font, pn_frame_t *frame) {
    if (!ui || !ui->impl || ui->mode == PN_EXUI_CLOSED || !font || !font->impl || !frame || !frame->pixels
        || frame->width != PN_UI_WIDTH || frame->height != PN_UI_HEIGHT) {
        return PN_INVALID;
    }
    const ex_impl_t *d = ui->impl;
    pn_frame_clear(frame, PN_UI_PAPER);

    if (ui->mode == PN_EXUI_LIST) {
        const char *title = d->query[0] ? "摘录查询" : "我的摘录";
        const char *action = d->query[0] ? "清除" : "搜索";
        pn_status_t s = pn_w_header(font, frame, "< 书库", title, action);
        if (s != PN_OK) return s;

        char summary[112];
        if (d->query[0]) {
            snprintf(summary, sizeof(summary), "关键词「%.28s」· 命中 %zu 条", d->query, d->filtered_count);
        } else {
            snprintf(summary, sizeof(summary), "共 %zu 条高亮摘录 · 点条目查看或跳转", d->count);
        }
        s = sized_text_ex(font, frame, summary, 22, PN_UI_MARGIN, LIST_SUMMARY_Y + 26, 620, PN_ALIGN_LEFT, PN_UI_MUTED, false);
        if (s != PN_OK) return s;

        if (d->filtered_count == 0u) {
            pn_w_icon(frame, d->query[0] ? PN_ICON_SEARCH : PN_ICON_LIST, (PN_UI_WIDTH - 72) / 2, 400, 72, PN_UI_INK);
            s = sized_text_ex(font, frame, d->query[0] ? "没有找到匹配的摘录" : "还没有高亮摘录", 32,
                PN_UI_MARGIN, 524, 620, PN_ALIGN_CENTER, PN_UI_INK, true);
            if (s == PN_OK) {
                const char *tip = d->query[0]
                    ? "试试更短的关键词，或点右上「清除」查看全部摘录。"
                    : "阅读时长按正文，或在词典弹窗顶栏点「摘录」即可保存整句。";
                int orig = font->pixels;
                s = pn_font_size(font, 24);
                if (s == PN_OK) s = pn_w_text_lines_centered_ex(font, frame, tip, 64, 574, 556, 2, 36, PN_UI_MUTED, false, NULL);
                pn_status_t r = pn_font_size(font, orig);
                if (s == PN_OK) s = r;
            }
            if (s == PN_OK && d->query[0]) {
                s = sized_button(font, frame, "修改关键词", 28, 212, 668, 260, 76, 0u);
            }
        } else {
            size_t start = (size_t)d->page * PN_EXCERPTS_UI_ROWS;
            for (unsigned row = 0; row < PN_EXCERPTS_UI_ROWS && start + row < d->filtered_count && s == PN_OK; row++) {
                const excerpt_entry_t *e = &d->entries[d->filtered[start + row]];
                int y = LIST_ROW_Y0 + (int)row * LIST_ROW_PITCH;
                pn_w_round_fill(frame, PN_UI_MARGIN + 2, y + 2, 616, LIST_ROW_H, 16, PN_UI_SELECT);
                pn_w_round_fill(frame, PN_UI_MARGIN, y, 620, LIST_ROW_H, 16, PN_UI_PAPER);
                pn_w_round_stroke(frame, PN_UI_MARGIN, y, 620, LIST_ROW_H, 16, 1.5f, 9);

                // 顶行：书名标签 + 右侧箭头。/ Top row: book title meta + right chevron.
                char hdr[128];
                snprintf(hdr, sizeof(hdr), "《%.44s》 · #%llu", e->book_title, (unsigned long long)e->hl.id);
                s = sized_text_ex(font, frame, hdr, 20, PN_UI_MARGIN + 18, y + 32, 540, PN_ALIGN_LEFT, 5, true);
                pn_w_icon(frame, PN_ICON_CHEVRON, PN_UI_WIDTH - PN_UI_MARGIN - 36, y + 14, 22, 6);
                pn_frame_rect(frame, PN_UI_MARGIN + 18, y + 44, 584, 1, 14);

                // 正文两行摘录预览。/ Two-line wrapped excerpt quote preview.
                if (s == PN_OK) {
                    int orig = font->pixels;
                    s = pn_font_size(font, 24);
                    if (s == PN_OK) {
                        const char *quote = e->hl.text[0] ? e->hl.text : "(无摘要)";
                        s = pn_w_text_lines_ex(font, frame, quote, PN_UI_MARGIN + 18, y + 82, 584, 2, 36, PN_UI_INK, false);
                    }
                    pn_status_t r = pn_font_size(font, orig);
                    if (s == PN_OK) s = r;
                }
            }
        }

        // 底部翻页栏。/ Bottom pagination bar.
        unsigned total_pages = d->filtered_count ? (unsigned)((d->filtered_count + PN_EXCERPTS_UI_ROWS - 1u) / PN_EXCERPTS_UI_ROWS) : 1u;
        bool has_prev = d->page > 0u;
        bool has_next = d->page + 1u < total_pages;
        if (s == PN_OK) s = sized_button(font, frame, "上一页", 28, 32, LIST_PAGER_Y, 196, 80, has_prev ? 0u : PN_W_DISABLED);
        if (s == PN_OK) {
            char pg[32];
            snprintf(pg, sizeof(pg), "%u / %u", d->page + 1u, total_pages);
            s = sized_text_ex(font, frame, pg, 24, 228, LIST_PAGER_Y + 50, 228, PN_ALIGN_CENTER, PN_UI_MUTED, false);
        }
        if (s == PN_OK) s = sized_button(font, frame, "下一页", 28, 456, LIST_PAGER_Y, 196, 80, has_next ? 0u : PN_W_DISABLED);
        return s;
    }

    if (ui->mode == PN_EXUI_SEARCH) {
        pn_status_t s = pn_w_header(font, frame, "< 返回", "搜索摘录", NULL);
        if (s == PN_OK) {
            s = sized_text_ex(font, frame, "按摘录正文或书名关键词筛选", 22, PN_UI_MARGIN, 174, 620, PN_ALIGN_LEFT, PN_UI_MUTED, false);
        }
        if (s == PN_OK) {
            pn_w_round_fill(frame, PN_UI_MARGIN, SEARCH_INPUT_Y, 620, SEARCH_INPUT_H, 16, PN_UI_PAPER);
            pn_w_round_stroke(frame, PN_UI_MARGIN, SEARCH_INPUT_Y, 620, SEARCH_INPUT_H, 16, 2.0f, PN_UI_INK);
            if (d->draft[0]) {
                char shown[PN_EXCERPTS_QUERY_MAX + 4];
                snprintf(shown, sizeof(shown), "%s_", d->draft);
                s = sized_text_ex(font, frame, shown, 30, PN_UI_MARGIN + 20, SEARCH_INPUT_Y + 54, 580, PN_ALIGN_LEFT, PN_UI_INK, false);
            } else {
                s = sized_text_ex(font, frame, "输入关键词（支持中文拼音/英文）...", 26, PN_UI_MARGIN + 20, SEARCH_INPUT_Y + 52, 580, PN_ALIGN_LEFT, 7, false);
            }
        }
        if (s == PN_OK) {
            bool has_text = d->draft[0] != '\0' || d->kb.pinyin[0] != '\0';
            s = sized_button(font, frame, "清空", 28, PN_UI_MARGIN, SEARCH_BTN_Y, 188, SEARCH_BTN_H, has_text ? 0u : PN_W_DISABLED);
            if (s == PN_OK) {
                s = sized_button(font, frame, "立即查询", 28, 236, SEARCH_BTN_Y, 416, SEARCH_BTN_H, PN_W_SELECTED);
            }
        }
        if (s == PN_OK) s = pn_keyboard_render(&d->kb, font, frame, SEARCH_KB_Y);
        return s;
    }

    // DETAIL / DELETE 共用摘录详情头部。/ DETAIL and DELETE share the selected excerpt card header.
    if (d->selected < 0 || (size_t)d->selected >= d->filtered_count) return PN_INVALID;
    const excerpt_entry_t *e = &d->entries[d->filtered[d->selected]];
    pn_status_t s = pn_w_header(font, frame, "< 返回", ui->mode == PN_EXUI_DELETE ? "删除摘录" : "摘录详情", NULL);
    if (s != PN_OK) return s;

    char book_lbl[128];
    snprintf(book_lbl, sizeof(book_lbl), "来自《%.52s》 · 摘录 #%llu", e->book_title, (unsigned long long)e->hl.id);
    s = sized_text_ex(font, frame, book_lbl, 22, PN_UI_MARGIN, 174, 620, PN_ALIGN_LEFT, 5, true);
    if (s != PN_OK) return s;

    // 摘录全文卡片。/ Full excerpt quote card.
    pn_w_round_fill(frame, PN_UI_MARGIN, 198, 620, 326, 16, PN_UI_SURFACE);
    pn_w_round_stroke(frame, PN_UI_MARGIN, 198, 620, 326, 16, 1.5f, 10);
    {
        int orig = font->pixels;
        s = pn_font_size(font, 28);
        if (s == PN_OK) {
            s = pn_w_text_lines_ex(font, frame, e->hl.text[0] ? e->hl.text : "(无摘要)",
                PN_UI_MARGIN + 22, 248, 576, 6, 44, PN_UI_INK, false);
        }
        pn_status_t r = pn_font_size(font, orig);
        if (s == PN_OK) s = r;
    }
    if (s != PN_OK) return s;

    if (ui->mode == PN_EXUI_DETAIL) {
        bool can_jump = e->book_path[0] != '\0';
        s = sized_button(font, frame, can_jump ? "打开书籍并跳转" : "原书路径未知，无法跳转", 30,
            PN_UI_MARGIN, DETAIL_BTN_Y0, 620, 84, can_jump ? PN_W_SELECTED : PN_W_DISABLED);
        if (s == PN_OK) {
            s = sized_button(font, frame, "设为左上角文案", 30, PN_UI_MARGIN, DETAIL_BTN_Y0 + DETAIL_BTN_PITCH, 620, 84, 0u);
        }
        if (s == PN_OK) {
            s = sized_button(font, frame, "删除这条摘录", 30, PN_UI_MARGIN, DETAIL_BTN_Y0 + 2 * DETAIL_BTN_PITCH, 620, 84, 0u);
        }
        if (s == PN_OK && d->notice[0]) {
            s = sized_text_ex(font, frame, d->notice, 24, PN_UI_MARGIN, DETAIL_BTN_Y0 + 3 * DETAIL_BTN_PITCH + 28, 620, PN_ALIGN_LEFT, 5, false);
        }
    } else {
        s = sized_text_ex(font, frame, "确定删除这条高亮摘录？此操作无法撤销。", 28,
            PN_UI_MARGIN, 578, 620, PN_ALIGN_LEFT, PN_UI_INK, false);
        if (s == PN_OK) s = sized_button(font, frame, "取消", 30, 32, DELETE_BTN_Y, 300, 92, 0u);
        if (s == PN_OK) s = sized_button(font, frame, "确认删除", 30, 352, DELETE_BTN_Y, 300, 92, 0u);
    }
    return s;
}

int pn_excerpts_ui_hit(const pn_excerpts_ui_t *ui, int x, int y) {
    if (!ui || !ui->impl || ui->mode == PN_EXUI_CLOSED || x < 0 || x >= PN_UI_WIDTH || y < 0 || y >= PN_UI_HEIGHT) {
        return -1;
    }
    const ex_impl_t *d = ui->impl;
    int header = pn_w_header_hit(x, y, ui->mode == PN_EXUI_LIST);
    if (header == 1) return PN_EXUI_BACK;
    if (header == 2 && ui->mode == PN_EXUI_LIST) {
        return d->query[0] ? PN_EXUI_CLEAR_FILTER : PN_EXUI_OPEN_SEARCH;
    }

    if (ui->mode == PN_EXUI_LIST) {
        if (d->filtered_count == 0u) {
            if (d->query[0] && x >= 212 && x < 472 && y >= 668 && y < 744) return PN_EXUI_OPEN_SEARCH;
            return -1;
        }
        size_t start = (size_t)d->page * PN_EXCERPTS_UI_ROWS;
        for (unsigned row = 0; row < PN_EXCERPTS_UI_ROWS && start + row < d->filtered_count; row++) {
            int ry = LIST_ROW_Y0 + (int)row * LIST_ROW_PITCH;
            if (x >= PN_UI_MARGIN && x < PN_UI_WIDTH - PN_UI_MARGIN && y >= ry && y < ry + LIST_ROW_H) {
                return PN_EXUI_ROW_BASE + (int)row;
            }
        }
        if (y >= LIST_PAGER_Y && y < LIST_PAGER_Y + 80) {
            unsigned total_pages = (unsigned)((d->filtered_count + PN_EXCERPTS_UI_ROWS - 1u) / PN_EXCERPTS_UI_ROWS);
            if (x >= 32 && x < 228 && d->page > 0u) return PN_EXUI_PREV;
            if (x >= 456 && x < 652 && d->page + 1u < total_pages) return PN_EXUI_NEXT;
        }
        return -1;
    }

    if (ui->mode == PN_EXUI_SEARCH) {
        if (y >= SEARCH_BTN_Y && y < SEARCH_BTN_Y + SEARCH_BTN_H) {
            if (x >= PN_UI_MARGIN && x < PN_UI_MARGIN + 188) return PN_EXUI_SEARCH_CLEAR;
            if (x >= 236 && x < 236 + 416) return PN_EXUI_SEARCH_GO;
        }
        int code = pn_keyboard_hit(&d->kb, SEARCH_KB_Y, x, y);
        if (code != PN_KB_NONE) return PN_EXUI_KB_ENCODE(code);
        return -1;
    }

    if (ui->mode == PN_EXUI_DETAIL) {
        if (x >= PN_UI_MARGIN && x < PN_UI_WIDTH - PN_UI_MARGIN) {
            if (y >= DETAIL_BTN_Y0 && y < DETAIL_BTN_Y0 + 84) return PN_EXUI_JUMP;
            if (y >= DETAIL_BTN_Y0 + DETAIL_BTN_PITCH && y < DETAIL_BTN_Y0 + DETAIL_BTN_PITCH + 84) return PN_EXUI_SET_MOTTO;
            if (y >= DETAIL_BTN_Y0 + 2 * DETAIL_BTN_PITCH && y < DETAIL_BTN_Y0 + 2 * DETAIL_BTN_PITCH + 84) return PN_EXUI_REMOVE;
        }
        return -1;
    }

    if (ui->mode == PN_EXUI_DELETE) {
        if (y >= DELETE_BTN_Y && y < DELETE_BTN_Y + 92) {
            if (x >= 32 && x < 332) return PN_EXUI_CANCEL;
            if (x >= 352 && x < 652) return PN_EXUI_CONFIRM;
        }
    }
    return -1;
}

pn_status_t pn_excerpts_ui_event(pn_excerpts_ui_t *ui, int command) {
    if (!ui || !ui->impl || ui->mode == PN_EXUI_CLOSED) return PN_INVALID;
    ex_impl_t *d = ui->impl;
    ui->open_request = false;
    d->notice[0] = '\0';

    if (command == PN_EXUI_BACK) {
        if (ui->mode == PN_EXUI_LIST) {
            pn_excerpts_ui_close(ui);
            return PN_OK;
        }
        if (ui->mode == PN_EXUI_DELETE) {
            ui->mode = PN_EXUI_DETAIL;
            return PN_OK;
        }
        ui->mode = PN_EXUI_LIST;
        return PN_OK;
    }

    if (ui->mode == PN_EXUI_LIST) {
        if (command == PN_EXUI_OPEN_SEARCH) {
            snprintf(d->draft, sizeof(d->draft), "%s", d->query);
            d->kb.pinyin[0] = '\0';
            d->kb.cand_page = 0;
            ui->mode = PN_EXUI_SEARCH;
            return PN_OK;
        }
        if (command == PN_EXUI_CLEAR_FILTER) {
            if (!d->query[0]) return PN_EMPTY;
            d->query[0] = '\0';
            d->page = 0;
            rebuild_filter(d);
            return PN_OK;
        }
        if (command == PN_EXUI_PREV) {
            if (d->page == 0u) return PN_EMPTY;
            d->page--;
            return PN_OK;
        }
        if (command == PN_EXUI_NEXT) {
            unsigned total_pages = d->filtered_count ? (unsigned)((d->filtered_count + PN_EXCERPTS_UI_ROWS - 1u) / PN_EXCERPTS_UI_ROWS) : 1u;
            if (d->page + 1u >= total_pages) return PN_EMPTY;
            d->page++;
            return PN_OK;
        }
        if (command >= PN_EXUI_ROW_BASE && command < PN_EXUI_ROW_BASE + (int)PN_EXCERPTS_UI_ROWS) {
            size_t idx = (size_t)d->page * PN_EXCERPTS_UI_ROWS + (size_t)(command - PN_EXUI_ROW_BASE);
            if (idx >= d->filtered_count) return PN_EMPTY;
            d->selected = (int)idx;
            ui->mode = PN_EXUI_DETAIL;
            return PN_OK;
        }
        return PN_EMPTY;
    }

    if (ui->mode == PN_EXUI_SEARCH) {
        if (command == PN_EXUI_SEARCH_CLEAR) {
            if (!d->draft[0] && !d->kb.pinyin[0]) return PN_EMPTY;
            d->draft[0] = '\0';
            d->kb.pinyin[0] = '\0';
            d->kb.cand_page = 0;
            return PN_OK;
        }
        if (command == PN_EXUI_SEARCH_GO) {
            (void)pn_keyboard_flush(&d->kb, d->draft, sizeof(d->draft), PN_EXCERPTS_QUERY_MAX - 1u);
            snprintf(d->query, sizeof(d->query), "%s", d->draft);
            d->page = 0;
            rebuild_filter(d);
            ui->mode = PN_EXUI_LIST;
            return PN_OK;
        }
        if (command >= PN_EXUI_KB_BASE) {
            int code = PN_EXUI_KB_DECODE(command);
            return pn_keyboard_apply(&d->kb, code, d->draft, sizeof(d->draft), PN_EXCERPTS_QUERY_MAX - 1u) ? PN_OK : PN_EMPTY;
        }
        return PN_EMPTY;
    }

    if (ui->mode == PN_EXUI_DETAIL) {
        if (d->selected < 0 || (size_t)d->selected >= d->filtered_count) return PN_INVALID;
        const excerpt_entry_t *e = &d->entries[d->filtered[d->selected]];
        if (command == PN_EXUI_JUMP) {
            if (!e->book_path[0]) {
                snprintf(d->notice, sizeof(d->notice), "该书不在最近列表，请从书库打开后查看");
                return PN_OK;
            }
            ui->open_request = true;
            snprintf(ui->open_path, sizeof(ui->open_path), "%s", e->book_path);
            ui->open_book = e->hl.book;
            ui->open_highlight_id = e->hl.id;
            ui->open_is_epub = e->hl.is_epub;
            return PN_OK;
        }
        if (command == PN_EXUI_SET_MOTTO) {
            pn_home_prefs_t hp = pn_home_prefs_default();
            (void)pn_home_prefs_load_dir(d->state_media, d->state_dir, &hp);
            size_t tlen = strlen(e->hl.text);
            if (tlen >= sizeof(hp.header)) {
                tlen = sizeof(hp.header) - 1u;
                while (tlen > 0u && (((unsigned char)e->hl.text[tlen] & 0xC0u) == 0x80u)) tlen--;
            }
            memcpy(hp.header, e->hl.text, tlen);
            hp.header[tlen] = '\0';
            pn_status_t sv = pn_home_prefs_save_dir(d->state_media, d->state_dir, &hp);
            if (sv == PN_OK) {
                pn_w_set_brand(hp.header);
                ui->motto_changed = true;
                snprintf(d->notice, sizeof(d->notice), "已设为全局左上角文案");
            } else {
                snprintf(d->notice, sizeof(d->notice), "保存失败，请重试");
            }
            return PN_OK;
        }
        if (command == PN_EXUI_REMOVE) {
            ui->mode = PN_EXUI_DELETE;
            return PN_OK;
        }
        return PN_EMPTY;
    }

    if (ui->mode == PN_EXUI_DELETE) {
        if (command == PN_EXUI_CANCEL) {
            ui->mode = PN_EXUI_DETAIL;
            return PN_OK;
        }
        if (command == PN_EXUI_CONFIRM) {
            if (d->selected < 0 || (size_t)d->selected >= d->filtered_count) return PN_INVALID;
            size_t raw_idx = d->filtered[d->selected];
            const excerpt_entry_t *e = &d->entries[raw_idx];
            pn_media_lease_t lease = {0};
            pn_status_t s = pn_media_acquire(d->state_media, PN_MEDIA_WRITE, &lease);
            if (s == PN_OK) {
                pn_highlights_t store = {0};
                s = pn_highlights_init(&store, d->state_media, &lease, d->state_dir, &e->hl.book);
                if (s == PN_OK) s = pn_highlights_delete(&store, e->hl.id);
                (void)pn_media_release(d->state_media, &lease);
            }
            if (s != PN_OK) {
                snprintf(d->notice, sizeof(d->notice), "删除失败，请重试");
                ui->mode = PN_EXUI_DETAIL;
                return PN_OK;
            }
            // 从内存表移除已删摘录并重算筛选与页码。/ Remove deleted entry and rebuild filter & pagination.
            if (raw_idx + 1u < d->count) {
                memmove(&d->entries[raw_idx], &d->entries[raw_idx + 1u], (d->count - raw_idx - 1u) * sizeof(*d->entries));
            }
            d->count--;
            d->selected = -1;
            rebuild_filter(d);
            ui->mode = PN_EXUI_LIST;
            return PN_OK;
        }
    }
    return PN_EMPTY;
}

size_t pn_excerpts_ui_total_count(const pn_excerpts_ui_t *ui) {
    return (ui && ui->impl) ? ((const ex_impl_t *)ui->impl)->count : 0u;
}

size_t pn_excerpts_ui_filtered_count(const pn_excerpts_ui_t *ui) {
    return (ui && ui->impl) ? ((const ex_impl_t *)ui->impl)->filtered_count : 0u;
}

void pn_excerpts_ui_close(pn_excerpts_ui_t *ui) {
    if (!ui) return;
    if (ui->impl) {
        ex_impl_t *d = ui->impl;
        pn_free(d->entries);
        pn_free(d);
        ui->impl = NULL;
    }
    ui->mode = PN_EXUI_CLOSED;
    ui->open_request = false;
    ui->open_highlight_id = 0;
}
