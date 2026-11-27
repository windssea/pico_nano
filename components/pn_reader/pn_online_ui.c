/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设备端在线素材库同源清单解析、缩略图解码与墨水屏界面绘制/命中。
 * English: shared manifest parser, thumbnail decoder, and e-ink UI render/hit logic for on-device online assets.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_online_ui.h"
#include "cJSON.h"
#include "pn_image.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

// 固定设备端展示顺序：锁屏壁纸、阅读遮罩、阅读背景、首页横幅、图书封面、字体、书籍。
// Fixed on-device display order: wallpaper, mask, background, banner, cover, font, book.
typedef struct {
    const char *id;
    const char *title;
    const char *desc;
    pn_upload_kind_t kind;
    bool is_image;
    bool is_mask;
} cat_spec_t;

static const cat_spec_t CAT_SPECS[PN_ONLINE_MAX_CATS] = {
    {"wallpaper",  "锁屏壁纸", "全屏锁屏待机图（PNG / JPG）",     PN_UPLOAD_WALLPAPER,  true,  false},
    {"mask",       "阅读遮罩", "透明锁屏遮罩，叠加阅读正文",       PN_UPLOAD_MASK,       true,  true},
    {"background", "阅读背景", "阅读页底纹与纸感背景图",           PN_UPLOAD_BACKGROUND, true,  false},
    {"banner",     "首页横幅", "主页顶部横幅装饰图",               PN_UPLOAD_BANNER,     true,  false},
    {"cover",      "图书封面", "与书同名的电子书封面图",           PN_UPLOAD_COVER,      true,  false},
    {"font",       "阅读字体", "TTF / OTF 开源中文与西文字体",     PN_UPLOAD_FONT,       false, false},
    {"book",       "公开书籍", "EPUB / TXT 公版与开源电子书",      PN_UPLOAD_BOOK,       false, false},
};

#define CAT_CARD_Y 188
#define CAT_ROW_H PN_W_CARD_ROW_H
#define GRID_Y 192
#define GRID_PITCH 318
#define GRID_CARD_W 196
#define GRID_CARD_H 300
#define LIST_Y 196
#define PAGER_Y 1084
#define PREV_X ((PN_UI_WIDTH - PN_ONLINE_PREV_W) / 2)
#define PREV_Y 136
#define DL_BTN_Y 1052
#define DL_BTN_H 84
#define RETRY_BTN_Y 760

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_sha256_hex(const char *hex, uint8_t out[32]) {
    if (!hex || strlen(hex) != 64u) return false;
    for (unsigned i = 0; i < 32u; i++) {
        int hi = hex_val(hex[i * 2u]), lo = hex_val(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool safe_relative_share_path(const char *path) {
    if (!path || strncmp(path, "assets/share/", 13u) != 0) return false;
    size_t len = strlen(path);
    if (len <= 13u || len >= 192u) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c < 0x20u || c == '\\') return false;
        if (path[i] == '.' && path[i + 1u] == '.') return false;
    }
    return true;
}

static bool safe_basename(const char *name) {
    if (!name || !*name || strlen(name) >= 128u) return false;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return false;
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\' || (unsigned char)*p < 0x20u) return false;
    }
    return true;
}

static void copy_str(char *dst, size_t cap, const char *src) {
    if (!dst || !cap) return;
    if (!src) { dst[0] = 0; return; }
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1u;
        while (n > 0u && (((unsigned char)src[n]) & 0xc0u) == 0x80u) n--;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void strip_ext(const char *name, char *out, size_t cap) {
    copy_str(out, cap, name);
    char *dot = strrchr(out, '.');
    if (dot && dot > out) *dot = 0;
}

static void format_size(uint32_t bytes, char *out, size_t cap) {
    if (bytes >= 1024u * 1024u) {
        snprintf(out, cap, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        snprintf(out, cap, "%u KB", (unsigned)((bytes + 512u) / 1024u));
    } else {
        snprintf(out, cap, "%u B", (unsigned)bytes);
    }
}

pn_status_t pn_online_parse_manifest(const char *json, size_t len, pn_online_catalog_t *out) {
    if (!json || !len || !out) return PN_INVALID;
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return PN_CORRUPT;
    }
    const cJSON *cats = cJSON_GetObjectItemCaseSensitive(root, "categories");
    if (!cJSON_IsArray(cats)) {
        cJSON_Delete(root);
        return PN_CORRUPT;
    }
    out->cat_count = PN_ONLINE_MAX_CATS;
    for (unsigned ci = 0; ci < PN_ONLINE_MAX_CATS; ci++) {
        const cat_spec_t *spec = &CAT_SPECS[ci];
        pn_online_category_t *dst_cat = &out->categories[ci];
        copy_str(dst_cat->id, sizeof dst_cat->id, spec->id);
        copy_str(dst_cat->title, sizeof dst_cat->title, spec->title);
        copy_str(dst_cat->description, sizeof dst_cat->description, spec->desc);
        dst_cat->kind = spec->kind;
        dst_cat->is_image = spec->is_image;
        dst_cat->is_mask = spec->is_mask;
        dst_cat->first_item = out->item_count;
        dst_cat->count = 0;

        const cJSON *src_cat = NULL;
        const cJSON *iter = NULL;
        cJSON_ArrayForEach(iter, cats) {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(iter, "id");
            if (cJSON_IsString(id) && !strcmp(id->valuestring, spec->id)) {
                src_cat = iter;
                break;
            }
        }
        if (!src_cat) continue;
        const cJSON *items = cJSON_GetObjectItemCaseSensitive(src_cat, "items");
        if (!cJSON_IsArray(items)) continue;

        const cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, items) {
            if (out->item_count >= PN_ONLINE_MAX_ITEMS) break;
            const cJSON *j_name = cJSON_GetObjectItemCaseSensitive(entry, "name");
            const cJSON *j_stem = cJSON_GetObjectItemCaseSensitive(entry, "stem");
            const cJSON *j_path = cJSON_GetObjectItemCaseSensitive(entry, "path");
            const cJSON *j_size = cJSON_GetObjectItemCaseSensitive(entry, "size");
            const cJSON *j_sha = cJSON_GetObjectItemCaseSensitive(entry, "sha256");
            const cJSON *j_w = cJSON_GetObjectItemCaseSensitive(entry, "width");
            const cJSON *j_h = cJSON_GetObjectItemCaseSensitive(entry, "height");
            if (!cJSON_IsString(j_name) || !cJSON_IsString(j_path) || !cJSON_IsNumber(j_size)) continue;
            if (!safe_basename(j_name->valuestring) || !safe_relative_share_path(j_path->valuestring)) continue;
            double sz = j_size->valuedouble;
            if (sz <= 0.0 || sz > 64.0 * 1024.0 * 1024.0) continue;

            // 用共享上传校验核对文件名与类别扩展名合法性。/ Validate filename and extension via the shared upload contract.
            pn_upload_request_t probe = {0};
            probe.id[0] = 1u;
            probe.kind = spec->kind;
            probe.size = (uint64_t)sz;
            copy_str(probe.name, sizeof probe.name, j_name->valuestring);
            if (pn_upload_request_validate(&probe) != PN_OK) continue;

            pn_online_item_t *it = &out->items[out->item_count];
            memset(it, 0, sizeof *it);
            it->kind = spec->kind;
            copy_str(it->name, sizeof it->name, j_name->valuestring);
            if (cJSON_IsString(j_stem) && *j_stem->valuestring) {
                copy_str(it->stem, sizeof it->stem, j_stem->valuestring);
            } else {
                strip_ext(it->name, it->stem, sizeof it->stem);
            }
            copy_str(it->path, sizeof it->path, j_path->valuestring);
            it->size = (uint32_t)sz;
            if (cJSON_IsNumber(j_w) && j_w->valuedouble > 0 && j_w->valuedouble <= 65535) it->width = (uint16_t)j_w->valuedouble;
            if (cJSON_IsNumber(j_h) && j_h->valuedouble > 0 && j_h->valuedouble <= 65535) it->height = (uint16_t)j_h->valuedouble;
            if (cJSON_IsString(j_sha)) {
                it->has_sha256 = parse_sha256_hex(j_sha->valuestring, it->sha256);
            }
            out->item_count++;
            dst_cat->count++;
        }
    }
    cJSON_Delete(root);
    return PN_OK;
}

void pn_online_mark_installed(pn_online_catalog_t *catalog, const char *sd_root) {
    if (!catalog || !sd_root || !*sd_root) return;
    char full[384];
    for (uint16_t i = 0; i < catalog->item_count; i++) {
        pn_online_item_t *it = &catalog->items[i];
        const char *dir = pn_upload_directory(it->kind);
        if (!dir) { it->installed = false; continue; }
        int n = snprintf(full, sizeof full, "%s/%s/%s", sd_root, dir, it->name);
        struct stat st;
        it->installed = (n > 0 && (size_t)n < sizeof full && stat(full, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0);
    }
}

pn_status_t pn_online_url_encode_path(const char *path, char *out, size_t cap) {
    if (!path || !out || !cap) return PN_INVALID;
    static const char HEX[] = "0123456789ABCDEF";
    size_t w = 0;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        unsigned char c = *p;
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                 || c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) {
            if (w + 1u >= cap) return PN_LIMIT;
            out[w++] = (char)c;
        } else {
            if (w + 3u >= cap) return PN_LIMIT;
            out[w++] = '%';
            out[w++] = HEX[c >> 4];
            out[w++] = HEX[c & 15u];
        }
    }
    out[w] = 0;
    return PN_OK;
}

typedef struct {
    const uint8_t *bytes;
    size_t len;
    size_t pos;
} mem_reader_t;

static pn_status_t mem_read(void *ctx, uint8_t *out, size_t cap, size_t *got) {
    mem_reader_t *r = (mem_reader_t *)ctx;
    if (r->pos >= r->len) { *got = 0; return PN_EMPTY; }
    size_t n = r->len - r->pos;
    if (n > cap) n = cap;
    memcpy(out, r->bytes + r->pos, n);
    r->pos += n;
    *got = n;
    return PN_OK;
}

pn_status_t pn_online_decode_preview(pn_pool_t *pool, const uint8_t *bytes, size_t len, bool is_mask, pn_frame_t *dst) {
    if (!pool || !bytes || !len || !dst || !dst->pixels || dst->width <= 0 || dst->height <= 0) return PN_INVALID;
    mem_reader_t probe_r = {bytes, len, 0};
    pn_image_input_t probe_in = {&probe_r, mem_read};
    pn_image_info_t info = {0};
    pn_status_t s = pn_image_probe(pool, &probe_in, &info);
    if (s != PN_OK || !info.width || !info.height) return s == PN_OK ? PN_CORRUPT : s;

    pn_frame_clear(dst, PN_UI_PAPER);
    if (is_mask) {
        // 遮罩底衬淡灰模拟正文线条，便于在墨水屏上分辨透明区与不透明图案。
        // Pre-draw light simulated text rules behind transparent mask regions for e-ink clarity.
        int step = dst->height >= 400 ? 34 : 18;
        int pad = dst->width / 10;
        for (int y = step * 2; y < dst->height - step; y += step) {
            int line_w = ((y / step) % 3 == 0) ? (dst->width - pad * 3) : (dst->width - pad * 2);
            for (int dy = 0; dy < 2 && y + dy < dst->height; dy++) {
                for (int x = pad; x < pad + line_w && x < dst->width; x++) {
                    pn_frame_pixel(dst, x, y + dy, 13);
                }
            }
        }
    }

    // 保持原始宽高比居中放入 dst 区域。/ Aspect-fit centered inside dst.
    int dw = dst->width, dh = dst->height;
    if ((uint64_t)info.width * (uint64_t)dst->height > (uint64_t)info.height * (uint64_t)dst->width) {
        dh = (int)(((uint64_t)info.height * (uint64_t)dst->width + info.width / 2u) / info.width);
        if (dh < 1) dh = 1;
    } else {
        dw = (int)(((uint64_t)info.width * (uint64_t)dst->height + info.height / 2u) / info.height);
        if (dw < 1) dw = 1;
    }
    pn_image_rect_t rect = {
        .x = (dst->width - dw) / 2,
        .y = (dst->height - dh) / 2,
        .width = dw,
        .height = dh,
    };
    mem_reader_t draw_r = {bytes, len, 0};
    pn_image_input_t draw_in = {&draw_r, mem_read};
    return pn_image_draw(pool, &draw_in, dst, rect, &info);
}

const char *pn_online_apply_hint(pn_upload_kind_t kind) {
    switch (kind) {
        case PN_UPLOAD_WALLPAPER:  return "可在「设置 → 锁屏壁纸」中选择应用";
        case PN_UPLOAD_MASK:       return "可在「设置 → 锁屏壁纸 → 遮罩」中选择应用";
        case PN_UPLOAD_BACKGROUND: return "可在阅读内「排版 → 阅读背景」中选择应用";
        case PN_UPLOAD_BANNER:     return "可在「设置 → 书架与主页 → 首页横幅」中选择应用";
        case PN_UPLOAD_COVER:      return "与书同名的封面会在书架自动生效";
        case PN_UPLOAD_FONT:       return "可在「设置 → 字体管理」或书内字体中选用";
        case PN_UPLOAD_BOOK:       return "已加入书库，返回书架即可阅读";
        default:                   return "已保存至设备存储卡";
    }
}

static pn_icon_t cat_icon(pn_upload_kind_t kind) {
    switch (kind) {
        case PN_UPLOAD_WALLPAPER:  return PN_ICON_LOCK;
        case PN_UPLOAD_MASK:       return PN_ICON_IMAGE;
        case PN_UPLOAD_BACKGROUND: return PN_ICON_TYPESET;
        case PN_UPLOAD_BANNER:     return PN_ICON_HOME;
        case PN_UPLOAD_COVER:      return PN_ICON_BOOKMARK;
        case PN_UPLOAD_FONT:       return PN_ICON_FONT;
        case PN_UPLOAD_BOOK:       return PN_ICON_SHELF;
        default:                   return PN_ICON_IMPORT;
    }
}

static unsigned cat_pages(const pn_online_category_t *cat) {
    if (!cat || !cat->count) return 1u;
    return (unsigned)((cat->count + PN_ONLINE_PAGE_SIZE - 1u) / PN_ONLINE_PAGE_SIZE);
}

static pn_status_t render_connecting(pn_font_t *font, pn_frame_t *frame, const pn_online_view_t *view) {
    pn_status_t s = pn_w_header(font, frame, "< 返回", "在线下载", NULL);
    const char *title = (view->status_title && *view->status_title) ? view->status_title : "正在连接网络";
    const char *sub = (view->status_line && *view->status_line) ? view->status_line : "正在获取共享素材目录，请稍候…";
    if (s == PN_OK) s = pn_w_loading(font, frame, title, sub, -1, -1);
    if (s == PN_OK && view->can_retry) {
        if (pn_font_size(font, 30) == PN_OK) {
            s = pn_w_button(font, frame, "重新连接", PN_UI_MARGIN + 120, RETRY_BTN_Y, 380, 84, PN_W_SELECTED);
        }
    }
    return s;
}

static pn_status_t render_categories(pn_font_t *font, pn_frame_t *frame, const pn_online_view_t *view) {
    const pn_online_catalog_t *cat = view->catalog;
    pn_status_t s = pn_w_header(font, frame, "< 返回", "在线下载", "刷新");
    char sub[128];
    if (view->status_line && *view->status_line) {
        copy_str(sub, sizeof sub, view->status_line);
    } else if (view->network_name && *view->network_name) {
        snprintf(sub, sizeof sub, "已连「%s」· %s", view->network_name,
                 (cat && *cat->source_label) ? cat->source_label : "共享素材库");
    } else {
        snprintf(sub, sizeof sub, "%s", (cat && *cat->source_label) ? cat->source_label : "选择素材类别浏览缩略图并下载");
    }
    if (s == PN_OK && pn_font_size(font, 24) == PN_OK) {
        s = pn_w_text_ex(font, frame, sub, PN_UI_MARGIN + 4, 162, 612, PN_ALIGN_LEFT, PN_UI_MUTED, false);
    }
    unsigned count = (cat && cat->cat_count <= PN_ONLINE_MAX_CATS) ? cat->cat_count : 0u;
    if (count > 0u) {
        pn_w_card(frame, CAT_CARD_Y, (int)count);
        for (unsigned i = 0; i < count && s == PN_OK; i++) {
            const pn_online_category_t *c = &cat->categories[i];
            char value[24];
            if (c->count > 0u) snprintf(value, sizeof value, "%u 项", (unsigned)c->count);
            else snprintf(value, sizeof value, "暂无");
            s = pn_w_card_row(font, frame, c->title, c->description, value, cat_icon(c->kind),
                              PN_ROW_CHEVRON, CAT_CARD_Y + (int)i * CAT_ROW_H, i + 1u == count);
        }
    }
    if (s == PN_OK && pn_font_size(font, 22) == PN_OK) {
        s = pn_w_text_lines_ex(font, frame,
            "下载的素材自动经过 SHA-256 完整性校验并按分类存入存储卡固定目录；返回传书页将自动关闭 Wi-Fi 省电。",
            PN_UI_MARGIN + 8, CAT_CARD_Y + (int)count * CAT_ROW_H + 46, 604, 3, 32, PN_UI_MUTED, false);
    }
    return s;
}

static pn_status_t render_grid(pn_font_t *font, pn_frame_t *frame, const pn_online_view_t *view) {
    const pn_online_catalog_t *catalog = view->catalog;
    if (!catalog || view->cat_index >= catalog->cat_count) return render_categories(font, frame, view);
    const pn_online_category_t *cat = &catalog->categories[view->cat_index];
    unsigned total_pages = cat_pages(cat);
    unsigned page = view->page_index < total_pages ? view->page_index : total_pages - 1u;
    char page_tag[24];
    snprintf(page_tag, sizeof page_tag, "%u/%u", page + 1u, total_pages);
    pn_status_t s = pn_w_header(font, frame, "< 分类", cat->title, cat->count ? page_tag : NULL);

    if (cat->count == 0u) {
        if (s == PN_OK && pn_font_size(font, 28) == PN_OK) {
            char empty[96];
            snprintf(empty, sizeof empty, "「%s」暂未收录在线素材", cat->title);
            s = pn_w_text_ex(font, frame, empty, PN_UI_MARGIN, 460, 620, PN_ALIGN_CENTER, PN_UI_INK, true);
        }
        if (s == PN_OK && pn_font_size(font, 24) == PN_OK) {
            s = pn_w_text_lines_centered_ex(font, frame,
                "后续往仓库 assets/share 对应目录添加素材并推送后，这里会自动显示并支持在线预览下载。",
                PN_UI_MARGIN + 32, 520, 556, 3, 36, PN_UI_MUTED, false, NULL);
        }
        if (s == PN_OK && pn_font_size(font, 28) == PN_OK) {
            s = pn_w_button(font, frame, "返回素材分类", PN_UI_MARGIN + 140, 680, 340, 80, PN_W_SELECTED);
        }
        return s;
    }

    unsigned start = page * PN_ONLINE_PAGE_SIZE;
    unsigned on_page = cat->count > start ? cat->count - start : 0u;
    if (on_page > PN_ONLINE_PAGE_SIZE) on_page = PN_ONLINE_PAGE_SIZE;

    char sub[128];
    if (view->status_line && *view->status_line) {
        copy_str(sub, sizeof sub, view->status_line);
    } else if (cat->is_image && view->thumbs_loaded < on_page) {
        snprintf(sub, sizeof sub, "共 %u 项 · 正在加载缩略图 %u/%u · 点击可预览下载",
                 (unsigned)cat->count, (unsigned)view->thumbs_loaded, on_page);
    } else {
        snprintf(sub, sizeof sub, "共 %u 项 · 点击任意素材预览并下载到设备", (unsigned)cat->count);
    }
    if (s == PN_OK && pn_font_size(font, 23) == PN_OK) {
        s = pn_w_text_ex(font, frame, sub, PN_UI_MARGIN + 4, 162, 612, PN_ALIGN_LEFT, PN_UI_MUTED, false);
    }

    if (cat->is_image) {
        for (unsigned i = 0; i < on_page && s == PN_OK; i++) {
            const pn_online_item_t *it = &catalog->items[cat->first_item + start + i];
            int x = PN_UI_MARGIN + (int)(i % 3u) * 212;
            int y = GRID_Y + (int)(i / 3u) * GRID_PITCH;
            pn_w_round_stroke(frame, x, y, GRID_CARD_W, GRID_CARD_H, 12, it->installed ? 2.5f : 1.0f,
                              it->installed ? PN_UI_INK : PN_UI_RULE);
            if (view->thumbs[i]) {
                pn_frame_t thumb = {(uint8_t *)view->thumbs[i], PN_ONLINE_THUMB_W, PN_ONLINE_THUMB_H, PN_ONLINE_THUMB_W / 2};
                const int dw = 132, dh = 224, dx = x + (GRID_CARD_W - dw) / 2, dy = y + 10;
                for (int row = 0; row < dh; row++) {
                    for (int col = 0; col < dw; col++) {
                        pn_frame_pixel(frame, dx + col, dy + row,
                                       pn_frame_get(&thumb, col * PN_ONLINE_THUMB_W / dw, row * PN_ONLINE_THUMB_H / dh));
                    }
                }
                pn_w_outline(frame, dx - 1, dy - 1, dw + 2, dh + 2, 1, PN_UI_RULE);
            } else {
                pn_w_icon(frame, view->thumb_failed[i] ? PN_ICON_IMAGE : PN_ICON_LOADING,
                          x + 72, y + 96, 52, PN_UI_MUTED);
            }
            if (pn_font_size(font, 20) == PN_OK) {
                s = pn_w_text_ex(font, frame, it->stem, x + 8, y + 260, GRID_CARD_W - 16, PN_ALIGN_CENTER, PN_UI_INK, it->installed);
            }
            if (s == PN_OK && pn_font_size(font, 18) == PN_OK) {
                char meta[32];
                if (it->installed) snprintf(meta, sizeof meta, "已下载");
                else format_size(it->size, meta, sizeof meta);
                s = pn_w_text_ex(font, frame, meta, x + 8, y + 286, GRID_CARD_W - 16, PN_ALIGN_CENTER,
                                 it->installed ? PN_UI_INK : PN_UI_MUTED, it->installed);
            }
        }
    } else {
        pn_w_card(frame, LIST_Y, (int)on_page);
        for (unsigned i = 0; i < on_page && s == PN_OK; i++) {
            const pn_online_item_t *it = &catalog->items[cat->first_item + start + i];
            char sz[32];
            format_size(it->size, sz, sizeof sz);
            s = pn_w_card_row(font, frame, it->stem, sz, it->installed ? "已下载" : "下载",
                              cat_icon(cat->kind), PN_ROW_CHEVRON, LIST_Y + (int)i * CAT_ROW_H, i + 1u == on_page);
        }
    }

    if (s == PN_OK && pn_font_size(font, 28) == PN_OK) {
        s = pn_w_button(font, frame, "上一页", PN_UI_MARGIN, PAGER_Y, 196, 80, page == 0u ? PN_W_DISABLED : 0u);
        if (s == PN_OK) {
            char mid[32];
            snprintf(mid, sizeof mid, "第 %u / %u 页", page + 1u, total_pages);
            if (pn_font_size(font, 24) == PN_OK) {
                s = pn_w_text_ex(font, frame, mid, 236, PAGER_Y + 48, 212, PN_ALIGN_CENTER, PN_UI_MUTED, false);
            }
        }
        if (s == PN_OK && pn_font_size(font, 28) == PN_OK) {
            s = pn_w_button(font, frame, "下一页", 456, PAGER_Y, 196, 80, page + 1u >= total_pages ? PN_W_DISABLED : 0u);
        }
    }
    return s;
}

static pn_status_t render_detail(pn_font_t *font, pn_frame_t *frame, const pn_online_view_t *view) {
    const pn_online_catalog_t *catalog = view->catalog;
    if (!catalog || view->cat_index >= catalog->cat_count) return render_categories(font, frame, view);
    const pn_online_category_t *cat = &catalog->categories[view->cat_index];
    if (view->item_index >= cat->count) return render_grid(font, frame, view);
    const pn_online_item_t *it = &catalog->items[cat->first_item + view->item_index];

    pn_status_t s = pn_w_header(font, frame, "< 返回", cat->title, it->installed ? "已下载" : NULL);

    if (cat->is_image) {
        if (view->detail_preview) {
            pn_frame_t prev = {(uint8_t *)view->detail_preview, PN_ONLINE_PREV_W, PN_ONLINE_PREV_H, PN_ONLINE_PREV_W / 2};
            for (int y = 0; y < PN_ONLINE_PREV_H; y++) {
                for (int x = 0; x < PN_ONLINE_PREV_W; x++) {
                    pn_frame_pixel(frame, PREV_X + x, PREV_Y + y, pn_frame_get(&prev, x, y));
                }
            }
        } else {
            for (int y = 0; y < PN_ONLINE_PREV_H; y++) {
                for (int x = 0; x < PN_ONLINE_PREV_W; x++) {
                    pn_frame_pixel(frame, PREV_X + x, PREV_Y + y, PN_UI_SURFACE);
                }
            }
            pn_w_icon(frame, view->detail_preview_failed ? PN_ICON_IMAGE : PN_ICON_LOADING,
                      PREV_X + PN_ONLINE_PREV_W / 2 - 32, PREV_Y + PN_ONLINE_PREV_H / 2 - 52, 64, PN_UI_MUTED);
            if (s == PN_OK && pn_font_size(font, 24) == PN_OK) {
                s = pn_w_text_ex(font, frame, view->detail_preview_failed ? "大图预览不可用，仍可直接下载" : "正在加载高清预览…",
                                 PREV_X + 16, PREV_Y + PN_ONLINE_PREV_H / 2 + 44, PN_ONLINE_PREV_W - 32,
                                 PN_ALIGN_CENTER, PN_UI_MUTED, false);
            }
        }
        pn_w_outline(frame, PREV_X - 2, PREV_Y - 2, PN_ONLINE_PREV_W + 4, PN_ONLINE_PREV_H + 4, 2, PN_UI_INK);
    } else {
        pn_w_round_fill(frame, PN_UI_MARGIN, 240, 620, 320, PN_UI_CARD_RADIUS, PN_UI_SURFACE);
        pn_w_round_stroke(frame, PN_UI_MARGIN, 240, 620, 320, PN_UI_CARD_RADIUS, 1.0f, PN_UI_RULE);
        pn_w_icon(frame, cat_icon(cat->kind), PN_UI_WIDTH / 2 - 40, 310, 80, PN_UI_INK);
        if (s == PN_OK && pn_font_size(font, 28) == PN_OK) {
            s = pn_w_text_ex(font, frame, it->name, PN_UI_MARGIN + 24, 444, 572, PN_ALIGN_CENTER, PN_UI_INK, true);
        }
        if (s == PN_OK && pn_font_size(font, 24) == PN_OK) {
            s = pn_w_text_ex(font, frame, pn_online_apply_hint(cat->kind), PN_UI_MARGIN + 24, 496, 572,
                             PN_ALIGN_CENTER, PN_UI_MUTED, false);
        }
    }

    int info_y = cat->is_image ? 794 : 640;
    if (s == PN_OK && pn_font_size(font, 30) == PN_OK) {
        s = pn_w_text_ex(font, frame, it->stem, PN_UI_MARGIN, info_y, 620, PN_ALIGN_CENTER, PN_UI_INK, true);
    }
    char meta[128], sz[32];
    format_size(it->size, sz, sizeof sz);
    if (it->width && it->height) {
        snprintf(meta, sizeof meta, "%u×%u · %s · 目录 /%s",
                 (unsigned)it->width, (unsigned)it->height, sz, pn_upload_directory(it->kind));
    } else {
        snprintf(meta, sizeof meta, "%s · 目录 /%s", sz, pn_upload_directory(it->kind));
    }
    if (s == PN_OK && pn_font_size(font, 22) == PN_OK) {
        s = pn_w_text_ex(font, frame, meta, PN_UI_MARGIN, info_y + 42, 620, PN_ALIGN_CENTER, PN_UI_MUTED, false);
    }
    if (s == PN_OK && pn_font_size(font, 22) == PN_OK) {
        const char *hint = (view->status_line && *view->status_line)
                         ? view->status_line
                         : (it->installed ? pn_online_apply_hint(it->kind) : "下载后自动做 SHA-256 校验并安全写入存储卡");
        s = pn_w_text_lines_centered_ex(font, frame, hint, PN_UI_MARGIN + 16, info_y + 84, 588, 2, 32,
                                        it->installed ? PN_UI_INK : PN_UI_MUTED, false, NULL);
    }

    // 下载进度条 / Download progress bar
    if (view->downloading) {
        int bar_x = PN_UI_MARGIN + 20, bar_y = DL_BTN_Y - 46, bar_w = 580, bar_h = 16;
        pn_w_round_fill(frame, bar_x, bar_y, bar_w, bar_h, 8, PN_UI_SURFACE);
        pn_w_round_stroke(frame, bar_x, bar_y, bar_w, bar_h, 8, 1.0f, PN_UI_STROKE);
        unsigned pct = view->download_percent > 100u ? 100u : view->download_percent;
        int fill_w = (int)((uint32_t)bar_w * pct / 100u);
        if (fill_w >= 8) pn_w_round_fill(frame, bar_x, bar_y, fill_w, bar_h, 8, PN_UI_INK);
    }

    if (s == PN_OK && pn_font_size(font, 30) == PN_OK) {
        if (view->downloading) {
            char label[40];
            snprintf(label, sizeof label, "下载中 %u%%（点击取消）", (unsigned)view->download_percent);
            s = pn_w_button(font, frame, label, PN_UI_MARGIN, DL_BTN_Y, 620, DL_BTN_H, 0u);
        } else {
            s = pn_w_button(font, frame, it->installed ? "已下载 · 重新下载" : "下载到设备",
                            PN_UI_MARGIN, DL_BTN_Y, 620, DL_BTN_H, it->installed ? 0u : PN_W_SELECTED);
        }
    }
    return s;
}

pn_status_t pn_online_ui_render(pn_font_t *font, pn_frame_t *frame, const pn_online_view_t *view) {
    if (!font || !font->impl || !frame || !frame->pixels || !view || frame->width != PN_UI_WIDTH || frame->height != PN_UI_HEIGHT) {
        return PN_INVALID;
    }
    int original = font->pixels;
    pn_frame_clear(frame, PN_UI_PAPER);
    pn_status_t s = PN_OK;
    switch (view->screen) {
        case PN_ONLINE_SCREEN_CONNECTING: s = render_connecting(font, frame, view); break;
        case PN_ONLINE_SCREEN_CATEGORIES: s = render_categories(font, frame, view); break;
        case PN_ONLINE_SCREEN_GRID:       s = render_grid(font, frame, view); break;
        case PN_ONLINE_SCREEN_DETAIL:     s = render_detail(font, frame, view); break;
        default: s = PN_INVALID; break;
    }
    pn_status_t restored = pn_font_size(font, original);
    return s == PN_OK ? restored : s;
}

int pn_online_ui_hit(const pn_online_view_t *view, int x, int y) {
    if (!view || x < 0 || x >= PN_UI_WIDTH || y < 0 || y >= PN_UI_HEIGHT) return -1;
    // 顶部返回按钮区域 / Header back button hit zone
    if (y < 120 && x < 200) return PN_ONLINE_CMD_BACK;

    if (view->screen == PN_ONLINE_SCREEN_CONNECTING) {
        if (view->can_retry && x >= PN_UI_MARGIN + 120 && x < PN_UI_MARGIN + 500 && y >= RETRY_BTN_Y && y < RETRY_BTN_Y + 84) {
            return PN_ONLINE_CMD_RETRY;
        }
        return -1;
    }

    if (view->screen == PN_ONLINE_SCREEN_CATEGORIES) {
        if (y < 120 && x >= PN_UI_WIDTH - 160) return PN_ONLINE_CMD_REFRESH;
        if (!view->catalog || x < PN_UI_MARGIN || x >= PN_UI_WIDTH - PN_UI_MARGIN) return -1;
        unsigned count = view->catalog->cat_count <= PN_ONLINE_MAX_CATS ? view->catalog->cat_count : 0u;
        if (y >= CAT_CARD_Y && y < CAT_CARD_Y + (int)count * CAT_ROW_H) {
            return PN_ONLINE_CMD_CAT_BASE + (y - CAT_CARD_Y) / CAT_ROW_H;
        }
        return -1;
    }

    if (view->screen == PN_ONLINE_SCREEN_GRID) {
        if (!view->catalog || view->cat_index >= view->catalog->cat_count) return -1;
        const pn_online_category_t *cat = &view->catalog->categories[view->cat_index];
        if (cat->count == 0u) {
            if (x >= PN_UI_MARGIN + 140 && x < PN_UI_MARGIN + 480 && y >= 680 && y < 760) return PN_ONLINE_CMD_BACK;
            return -1;
        }
        unsigned total_pages = cat_pages(cat);
        unsigned page = view->page_index < total_pages ? view->page_index : total_pages - 1u;
        unsigned start = page * PN_ONLINE_PAGE_SIZE;
        unsigned on_page = cat->count > start ? cat->count - start : 0u;
        if (on_page > PN_ONLINE_PAGE_SIZE) on_page = PN_ONLINE_PAGE_SIZE;

        if (y >= PAGER_Y && y < PAGER_Y + 80) {
            if (x >= PN_UI_MARGIN && x < PN_UI_MARGIN + 196) return page > 0u ? PN_ONLINE_CMD_PREV_PAGE : -1;
            if (x >= 456 && x < 456 + 196) return page + 1u < total_pages ? PN_ONLINE_CMD_NEXT_PAGE : -1;
            return -1;
        }

        if (cat->is_image) {
            for (unsigned i = 0; i < on_page; i++) {
                int cx = PN_UI_MARGIN + (int)(i % 3u) * 212;
                int cy = GRID_Y + (int)(i / 3u) * GRID_PITCH;
                if (x >= cx && x < cx + GRID_CARD_W && y >= cy && y < cy + GRID_CARD_H) {
                    return PN_ONLINE_CMD_ITEM_BASE + (int)i;
                }
            }
        } else if (x >= PN_UI_MARGIN && x < PN_UI_WIDTH - PN_UI_MARGIN &&
                   y >= LIST_Y && y < LIST_Y + (int)on_page * CAT_ROW_H) {
            return PN_ONLINE_CMD_ITEM_BASE + (y - LIST_Y) / CAT_ROW_H;
        }
        return -1;
    }

    if (view->screen == PN_ONLINE_SCREEN_DETAIL) {
        if (x >= PN_UI_MARGIN && x < PN_UI_WIDTH - PN_UI_MARGIN && y >= DL_BTN_Y && y < DL_BTN_Y + DL_BTN_H) {
            return view->downloading ? PN_ONLINE_CMD_CANCEL_DL : PN_ONLINE_CMD_DOWNLOAD;
        }
        return -1;
    }
    return -1;
}
