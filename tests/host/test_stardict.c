/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：离线 StarDict 词典引擎与 UI 回归测试：.ifo 解析、大 .idx 磁盘二分、32/64位偏移、.dict 与 DictZip .dict.dz 跨块随机解压、HTML/Pango 清洗、英文词形回退、中文候选提取、多词典级联、TXT 坐标取词与 pn_dict_ui 全流程无泄漏。
 * English: Offline StarDict engine & UI regression: .ifo parsing, large .idx on-disk binary search, 32/64-bit offsets, plain .dict & multi-chunk DictZip .dict.dz random access, markup cleanup, English stem fallback, Chinese candidate extraction, multi-dict cascade, TXT word-at-point, and leak-free pn_dict_ui flow.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_dict_ui.h"
#include "pn_keyboard.h"
#include "pn_reader_app.h"
#include "pn_stardict.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put_be32(FILE *fp, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    assert(fwrite(b, 1, 4, fp) == 4);
}
static void put_be64(FILE *fp, uint64_t v) {
    put_be32(fp, (uint32_t)(v >> 32));
    put_be32(fp, (uint32_t)v);
}
static void put_le16(FILE *fp, uint16_t v) {
    uint8_t b[2] = {(uint8_t)(v & 0xFFu), (uint8_t)(v >> 8)};
    assert(fwrite(b, 1, 2, fp) == 2);
}

typedef struct {
    const char *word;
    const char *def;
} entry_t;

static int cmp_entry(const void *pa, const void *pb) {
    const entry_t *a = pa, *b = pb;
    return pn_stardict_strcmp(a->word, b->word);
}

/// 生成标准纯文本 StarDict 词典（32位偏移，含 600+ 填充词强制触发多轮磁盘二分）。
/// Build a plain 32-bit StarDict dictionary with 600+ entries forcing multi-step on-disk binary search.
static void build_plain_dict(const char *dir, const char *base, const char *bookname) {
    static char pad_words[1200][24];
    static entry_t list[1220];
    size_t n = 0;

    for (unsigned i = 0; i < 1200; i++) {
        snprintf(pad_words[i], sizeof(pad_words[i]), "item%04u", i);
        list[n++] = (entry_t){pad_words[i], "filler entry"};
    }
    list[n++] = (entry_t){"Apple", "<b>Apple</b> (proper): tech company &amp; brand"};
    list[n++] = (entry_t){"apple", "<i>n.</i> a crisp round fruit<br>苹果 &lt;名词&gt;"};
    list[n++] = (entry_t){"book", "n. a written or printed work"};
    list[n++] = (entry_t){"run", "v. move at a speed faster than a walk; 奔跑"};
    list[n++] = (entry_t){"story", "n. an account of imaginary or real people and events"};
    list[n++] = (entry_t){"world", "n. the earth together with all its countries and peoples"};
    // 超长释义，用于测试 UI 多页翻页 / Long definition for multi-page UI pagination testing.
    static char long_def[3200];
    size_t lp = 0;
    for (int i = 1; i <= 38; i++) {
        lp += (size_t)snprintf(long_def + lp, sizeof(long_def) - lp,
            "%d. Detailed definition line number %d with 中文释义说明 and examples.\n", i, i);
    }
    list[n++] = (entry_t){"verbose", long_def};
    qsort(list, n, sizeof(list[0]), cmp_entry);

    char ifo_p[512], idx_p[512], dict_p[512];
    snprintf(ifo_p, sizeof(ifo_p), "%s/%s.ifo", dir, base);
    snprintf(idx_p, sizeof(idx_p), "%s/%s.idx", dir, base);
    snprintf(dict_p, sizeof(dict_p), "%s/%s.dict", dir, base);

    FILE *fidx = fopen(idx_p, "wb"), *fdict = fopen(dict_p, "wb");
    assert(fidx && fdict);
    uint32_t dict_off = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t dlen = (uint32_t)strlen(list[i].def);
        assert(fwrite(list[i].def, 1, dlen, fdict) == dlen);
        size_t wlen = strlen(list[i].word) + 1u;
        assert(fwrite(list[i].word, 1, wlen, fidx) == wlen);
        put_be32(fidx, dict_off);
        put_be32(fidx, dlen);
        dict_off += dlen;
    }
    long idx_sz = ftell(fidx);
    assert(idx_sz > 16384); // 确保大于 2*IDX_WIN_BYTES 真正走二分循环 / Larger than 2*IDX_WIN_BYTES to force binary search loop.
    assert(fclose(fidx) == 0 && fclose(fdict) == 0);

    FILE *fifo = fopen(ifo_p, "wb");
    assert(fifo);
    fprintf(fifo,
        "StarDict's dict ifo file\n"
        "version=2.4.2\n"
        "wordcount=%zu\n"
        "idxfilesize=%ld\n"
        "bookname=%s\n"
        "sametypesequence=h\n",
        n, idx_sz, bookname);
    assert(fclose(fifo) == 0);
}

/// 生成 64 位偏移 + 多块 RFC 1952 DictZip（.dict.dz）中文词典（放在一级子目录中测试递归扫描）。
/// Build a 64-bit-offset multi-chunk RFC 1952 DictZip (.dict.dz) Chinese dictionary in a subdirectory.
static void build_dz_chinese_dict(const char *dir, const char *base, const char *bookname) {
    entry_t list[] = {
        {"安", "单字：平静、稳定。"},
        {"安静", "adj. 没有声音，没有吵闹和喧哗；安稳平静。"},
        {"白夜", "高纬度地区夏季太阳落山后天空仍然微明的现象。"},
        {"世界", "地球上所有地方与人类社会的总称。"}
    };
    size_t n = sizeof(list) / sizeof(list[0]);
    qsort(list, n, sizeof(list[0]), cmp_entry);

    // 拼接原始 dict 字节流，故意选用 32 字节小块使词条跨越多个 DictZip 压缩块。
    // Concatenate raw dict payload and use tiny 32-byte chunks so entries span multiple DictZip chunks.
    uint8_t raw[1024];
    size_t raw_len = 0;
    uint64_t offs[8];
    uint32_t szs[8];
    for (size_t i = 0; i < n; i++) {
        offs[i] = raw_len;
        szs[i] = (uint32_t)strlen(list[i].def);
        assert(raw_len + szs[i] <= sizeof(raw));
        memcpy(raw + raw_len, list[i].def, szs[i]);
        raw_len += szs[i];
    }

    char ifo_p[512], idx_p[512], dz_p[512];
    snprintf(ifo_p, sizeof(ifo_p), "%s/%s.ifo", dir, base);
    snprintf(idx_p, sizeof(idx_p), "%s/%s.idx", dir, base);
    snprintf(dz_p, sizeof(dz_p), "%s/%s.dict.dz", dir, base);

    // 写 64 位偏移 .idx / Write 64-bit offset .idx.
    FILE *fidx = fopen(idx_p, "wb");
    assert(fidx);
    for (size_t i = 0; i < n; i++) {
        size_t wlen = strlen(list[i].word) + 1u;
        assert(fwrite(list[i].word, 1, wlen, fidx) == wlen);
        put_be64(fidx, offs[i]);
        put_be32(fidx, szs[i]);
    }
    long idx_sz = ftell(fidx);
    assert(fclose(fidx) == 0);

    // 写 RFC 1952 DictZip (.dict.dz)，每块采用 raw-deflate stored block。
    // Write RFC 1952 DictZip (.dict.dz) with raw-deflate stored blocks per chunk.
    const uint16_t chlen = 32u;
    uint16_t chcnt = (uint16_t)((raw_len + chlen - 1u) / chlen);
    assert(chcnt >= 3u);
    FILE *fdz = fopen(dz_p, "wb");
    assert(fdz);
    // 10-byte GZIP header with FEXTRA=0x04
    uint8_t hdr[10] = {0x1Fu, 0x8Bu, 0x08u, 0x04u, 0, 0, 0, 0, 0, 0xFFu};
    assert(fwrite(hdr, 1, 10, fdz) == 10);
    uint16_t sublen = (uint16_t)(6u + chcnt * 2u);
    uint16_t xlen = (uint16_t)(4u + sublen);
    put_le16(fdz, xlen);
    assert(fwrite("RA", 1, 2, fdz) == 2);
    put_le16(fdz, sublen);
    put_le16(fdz, 1u); // VER = 1
    put_le16(fdz, chlen);
    put_le16(fdz, chcnt);
    for (uint16_t c = 0; c < chcnt; c++) {
        size_t start = (size_t)c * chlen;
        size_t take = (raw_len - start > chlen) ? chlen : (raw_len - start);
        put_le16(fdz, (uint16_t)(5u + take));
    }
    for (uint16_t c = 0; c < chcnt; c++) {
        size_t start = (size_t)c * chlen;
        size_t take = (raw_len - start > chlen) ? chlen : (raw_len - start);
        uint8_t bfinal = (c + 1u == chcnt) ? 0x01u : 0x00u;
        assert(fwrite(&bfinal, 1, 1, fdz) == 1);
        put_le16(fdz, (uint16_t)take);
        put_le16(fdz, (uint16_t)(~take & 0xFFFFu));
        assert(fwrite(raw + start, 1, take, fdz) == take);
    }
    // 8-byte GZIP trailer (dummy CRC/ISIZE, DictZip random-access reads chunk data directly)
    uint8_t trailer[8] = {0};
    assert(fwrite(trailer, 1, 8, fdz) == 8);
    assert(fclose(fdz) == 0);

    FILE *fifo = fopen(ifo_p, "wb");
    assert(fifo);
    fprintf(fifo,
        "StarDict's dict ifo file\n"
        "version=3.0.0\n"
        "wordcount=%zu\n"
        "idxfilesize=%ld\n"
        "idxoffsetbits=64\n"
        "bookname=%s\n"
        "sametypesequence=m\n",
        n, idx_sz, bookname);
    assert(fclose(fifo) == 0);
}

static unsigned present_calls;
static pn_refresh_t last_refresh;
static const uint8_t *last_presented_px;
static pn_status_t dummy_present(void *ctx, const pn_frame_t *f, pn_refresh_t p) {
    (void)ctx;
    present_calls++;
    last_refresh = p;
    last_presented_px = f ? f->pixels : NULL;
    return PN_OK;
}

/// 生成小型第 3 本词典，用于验证多词典“全部”聚合与单本固定持久化。
/// Build a small 3rd dictionary to verify multi-dict "All" aggregation and pinned-dict persistence.
static void build_extra_dict(const char *dir, const char *base, const char *bookname) {
    entry_t list[] = {
        {"apple", "n. [Oxford] round fruit with red or green skin; 苹果（牛津释义）"},
        {"book", "n. [Oxford] bound set of pages; 书籍（牛津释义）"},
        {"quiet", "adj. making very little noise"}
    };
    size_t n = sizeof(list) / sizeof(list[0]);
    qsort(list, n, sizeof(list[0]), cmp_entry);
    char ifo_p[512], idx_p[512], dict_p[512];
    snprintf(ifo_p, sizeof(ifo_p), "%s/%s.ifo", dir, base);
    snprintf(idx_p, sizeof(idx_p), "%s/%s.idx", dir, base);
    snprintf(dict_p, sizeof(dict_p), "%s/%s.dict", dir, base);
    FILE *fidx = fopen(idx_p, "wb"), *fdict = fopen(dict_p, "wb");
    assert(fidx && fdict);
    uint32_t off = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t dlen = (uint32_t)strlen(list[i].def);
        assert(fwrite(list[i].def, 1, dlen, fdict) == dlen);
        size_t wlen = strlen(list[i].word) + 1u;
        assert(fwrite(list[i].word, 1, wlen, fidx) == wlen);
        put_be32(fidx, off);
        put_be32(fidx, dlen);
        off += dlen;
    }
    long idx_sz = ftell(fidx);
    assert(fclose(fidx) == 0 && fclose(fdict) == 0);
    FILE *fifo = fopen(ifo_p, "wb");
    assert(fifo);
    fprintf(fifo, "StarDict's dict ifo file\nversion=2.4.2\nwordcount=%zu\nidxfilesize=%ld\nbookname=%s\nsametypesequence=m\n",
        n, idx_sz, bookname);
    assert(fclose(fifo) == 0);
}

int main(void) {
    char root[] = "/tmp/pn-dict-XXXXXX";
    assert(mkdtemp(root));
    char dicts_dir[512], sub_dir[512], state_dir[512], book_path[512];
    snprintf(dicts_dir, sizeof(dicts_dir), "%s/dicts", root);
    snprintf(sub_dir, sizeof(sub_dir), "%s/dicts/zh_cn", root);
    snprintf(state_dir, sizeof(state_dir), "%s/state", root);
    snprintf(book_path, sizeof(book_path), "%s/sample.txt", root);
    assert(mkdir(dicts_dir, 0755) == 0);
    assert(mkdir(sub_dir, 0755) == 0);
    assert(mkdir(state_dir, 0755) == 0);

    build_plain_dict(dicts_dir, "en_basic", "English Basic");
    build_extra_dict(dicts_dir, "en_oxford", "Oxford Mini");
    build_dz_chinese_dict(sub_dir, "zh_mini", "中文词典");

    pn_pool_t pool;
    assert(pn_pool_init(&pool, 4u * 1024u * 1024u, NULL, NULL, NULL) == 0);

    // 1. 扫描根目录 + 一级子目录 / Scan root + 1-level subdirectory.
    pn_stardict_catalog_t *cat = pn_alloc_tagged(&pool, PN_MEM_DOCUMENT, sizeof(*cat));
    assert(cat);
    assert(pn_stardict_scan(dicts_dir, cat) == PN_OK && cat->count == 3u);
    assert(strcmp(cat->items[0].name, "English Basic") == 0 && !cat->items[0].compressed && cat->items[0].offset_bits == 32u);
    assert(strcmp(cat->items[1].name, "Oxford Mini") == 0 && !cat->items[1].compressed && cat->items[1].offset_bits == 32u);
    assert(strcmp(cat->items[2].name, "中文词典") == 0 && cat->items[2].compressed && cat->items[2].offset_bits == 64u);

    // 1b. 多词典聚合与位掩码子集查询：全选、2/3子集、单本（无【词典名】前缀）与未命中。
    // Multi-dict aggregation & bitmask subset query: all, 2-of-3 subset, single-dict (no 【DictName】 header), and miss.
    pn_stardict_multi_hit_t *mhit = pn_alloc_tagged(&pool, PN_MEM_DOCUMENT, sizeof(*mhit));
    assert(mhit);
    assert(pn_stardict_query_all(&pool, cat, "apple", mhit) == PN_OK);
    assert(mhit->matched_count == 2u && strcmp(mhit->matched_word, "apple") == 0);
    assert(strstr(mhit->definition, "【English Basic】") && strstr(mhit->definition, "a crisp round fruit"));
    assert(strstr(mhit->definition, "【Oxford Mini】") && strstr(mhit->definition, "牛津释义"));
    assert(!strstr(mhit->definition, "中文词典"));
    // 掩码只启用 Oxford Mini + 中文词典 (0x6 = bit1|bit2)：查 apple 只出 Oxford Mini，查 世界 出 中文词典。
    assert(pn_stardict_query_masked(&pool, cat, "apple", 0x6u, mhit) == PN_OK);
    assert(mhit->matched_count == 1u && strstr(mhit->definition, "【Oxford Mini】") && !strstr(mhit->definition, "English Basic"));
    assert(pn_stardict_query_masked(&pool, cat, "世界", 0x6u, mhit) == PN_OK);
    assert(mhit->matched_count == 1u && strstr(mhit->definition, "【中文词典】"));
    // 仅启用单本词典 (0x2 = Oxford Mini)：直接展示纯释义，不冗余加【Oxford Mini】标题。
    assert(pn_stardict_query_masked(&pool, cat, "apple", 0x2u, mhit) == PN_OK);
    assert(mhit->matched_count == 1u && strstr(mhit->definition, "牛津释义") && !strstr(mhit->definition, "【Oxford Mini】"));
    assert(pn_stardict_query_all(&pool, cat, "世界", mhit) == PN_OK && mhit->matched_count == 1u && strstr(mhit->definition, "【中文词典】"));
    assert(pn_stardict_query_all(&pool, cat, "nonexistent_zzz", mhit) == PN_EMPTY);
    pn_free(mhit);

    // 2. 精确大小写优先 + HTML/实体清洗 / Exact case preference + HTML/entity cleanup.
    pn_stardict_hit_t *hit = pn_alloc_tagged(&pool, PN_MEM_DOCUMENT, sizeof(*hit));
    assert(hit);
    assert(pn_stardict_query(&pool, cat, "apple", SIZE_MAX, hit) == PN_OK);
    assert(strcmp(hit->matched_word, "apple") == 0 && strstr(hit->definition, "a crisp round fruit") && strstr(hit->definition, "<名词>") && !strstr(hit->definition, "<i>"));
    assert(pn_stardict_query(&pool, cat, "Apple", SIZE_MAX, hit) == PN_OK);
    assert(strcmp(hit->matched_word, "Apple") == 0 && strstr(hit->definition, "tech company & brand"));

    // 3. 大表边界词检索（首/中/尾）/ Binary search across start, middle, and tail of 1200+ entries.
    assert(pn_stardict_query(&pool, cat, "item0000", SIZE_MAX, hit) == PN_OK && strcmp(hit->matched_word, "item0000") == 0);
    assert(pn_stardict_query(&pool, cat, "item0600", SIZE_MAX, hit) == PN_OK && strcmp(hit->matched_word, "item0600") == 0);
    assert(pn_stardict_query(&pool, cat, "item1199", SIZE_MAX, hit) == PN_OK && strcmp(hit->matched_word, "item1199") == 0);
    assert(pn_stardict_query(&pool, cat, "nonexistent_zzz", SIZE_MAX, hit) == PN_EMPTY);

    // 4. 英文词形还原回退 / English inflection fallback.
    assert(pn_stardict_query(&pool, cat, "running", SIZE_MAX, hit) == PN_OK && strcmp(hit->matched_word, "run") == 0);
    assert(pn_stardict_query(&pool, cat, "stories", SIZE_MAX, hit) == PN_OK && strcmp(hit->matched_word, "story") == 0);
    assert(pn_stardict_query(&pool, cat, "books", SIZE_MAX, hit) == PN_OK && strcmp(hit->matched_word, "book") == 0);
    assert(pn_stardict_query(&pool, cat, "world's", SIZE_MAX, hit) == PN_OK && strcmp(hit->matched_word, "world") == 0);

    // 5. 多块 DictZip（.dict.dz）64位偏移中文检索 + 前缀回退 / Multi-chunk DictZip 64-bit Chinese lookup + prefix fallback.
    assert(pn_stardict_query(&pool, cat, "安静", SIZE_MAX, hit) == PN_OK && strcmp(hit->dict_name, "中文词典") == 0 && strstr(hit->definition, "没有声音"));
    assert(pn_stardict_query(&pool, cat, "安静的夜晚", SIZE_MAX, hit) == PN_OK && strcmp(hit->matched_word, "安静") == 0);
    assert(pn_stardict_query(&pool, cat, "世界", 2u, hit) == PN_OK && strstr(hit->definition, "地球上所有地方"));
    assert(pn_stardict_query(&pool, cat, "世界", 0u, hit) == PN_EMPTY); // 限定英文词典时未命中

    // 6. 码点行提取候选词 / Candidate extraction from codepoint lines.
    uint32_t zh_line[] = {0x5B89, 0x9759, 0x7684, 0x591C}; // "安静的夜"
    char cands[6][PN_DICT_WORD_MAX];
    size_t nc = pn_stardict_extract_candidates(zh_line, 4u, 1u, cands, 6u); // 点在「静」上
    assert(nc >= 2u);
    bool saw_anjing = false;
    for (size_t i = 0; i < nc; i++) if (strcmp(cands[i], "安静") == 0) saw_anjing = true;
    assert(saw_anjing);

    // 7. pn_dict_ui 弹窗全流程：多词典聚合、单本固定持久化与删除回退、居底/居顶叠加不改底层帧、命中与退出恢复无泄漏。
    pn_font_t font = {0};
    pn_text_source_t fsrc = pn_font_builtin_source();
    assert(pn_font_open(&font, &pool, &fsrc, 24) == PN_OK);
    uint8_t *px = pn_alloc_tagged(&pool, PN_MEM_FRAME, 684u * 1216u / 2u);
    uint8_t *page_px = pn_alloc_tagged(&pool, PN_MEM_FRAME, 684u * 1216u / 2u);
    uint8_t *page_orig = pn_alloc_tagged(&pool, PN_MEM_FRAME, 684u * 1216u / 2u);
    assert(px && page_px && page_orig);
    memset(page_px, 0xA5, 684u * 1216u / 2u);
    memcpy(page_orig, page_px, 684u * 1216u / 2u);
    pn_frame_t frame, page_frame;
    assert(pn_frame_bind(&frame, px, 684u * 1216u / 2u, 684, 1216));
    assert(pn_frame_bind(&page_frame, page_px, 684u * 1216u / 2u, 684, 1216));

    pn_media_t state_media;
    pn_media_init(&state_media);
    assert(pn_media_attach(&state_media, 99) == PN_OK);

    pn_dict_ui_t ui = {0};
    const char long_cands[2][PN_DICT_WORD_MAX] = {"verbose", "apple"};
    pn_dict_ui_options_t opt = {.anchor_y = 200, .page_frame = &page_frame, .pref_media = &state_media, .pref_dir = state_dir};
    assert(pn_dict_ui_open_ex(&ui, &pool, NULL, NULL, dicts_dir, long_cands, 2u, false, &opt, NULL, NULL) == PN_OK);
    assert(ui.mode == PN_DUI_RESULT && pn_dict_ui_dict_count(&ui) == 3u && pn_dict_ui_enabled_count(&ui) == 3u);
    assert(pn_dict_ui_enabled_mask(&ui) == 0x7u && pn_dict_ui_selected_dict(&ui) == SIZE_MAX);
    assert(pn_dict_ui_sheet_y(&ui) == PN_DICT_BOTTOM_Y && strcmp(pn_dict_ui_matched_word(&ui), "verbose") == 0);
    assert(pn_dict_ui_render(&ui, &font, NULL, &frame) == PN_OK && pn_dict_ui_page_count(&ui) >= 2u);
    // 悬浮居中弹窗外四周保留底层阅读帧像素，底层 page_px 本身保持字节级不变。
    assert(memcmp(px, page_orig, 342u * (unsigned)(PN_DICT_BOTTOM_Y - 4)) == 0);
    // 卡片左右两侧外边距（0..PN_DICT_CARD_X-1 与 PN_DICT_CARD_X+PN_DICT_CARD_W+8..683）保留原阅读正文像素。
    assert(px[342u * (unsigned)(PN_DICT_BOTTOM_Y + 100) + 10u] == 0xA5u);
    assert(px[342u * (unsigned)(PN_DICT_BOTTOM_Y + 100) + 336u] == 0xA5u);
    assert(memcmp(px + 342u * (unsigned)PN_DICT_BOTTOM_Y, page_orig + 342u * (unsigned)PN_DICT_BOTTOM_Y, 342u * (unsigned)PN_DICT_SHEET_H) != 0);
    assert(memcmp(page_px, page_orig, 342u * 1216u) == 0);

    // 悬浮弹窗命中测试：上下左右四侧外边距关闭、×、搜索图标、词典多选入口、底栏翻页与正文左右快捷翻页。
    assert(pn_dict_ui_hit(&ui, 100, 100) == PN_DUI_DISMISS);
    assert(pn_dict_ui_hit(&ui, 10, PN_DICT_BOTTOM_Y + 200) == PN_DUI_DISMISS);
    assert(pn_dict_ui_hit(&ui, 670, PN_DICT_BOTTOM_Y + 200) == PN_DUI_DISMISS);
    assert(pn_dict_ui_hit(&ui, 300, PN_DICT_BOTTOM_Y + PN_DICT_SHEET_H + 10) == PN_DUI_DISMISS);
    assert(pn_dict_ui_hit(&ui, PN_DICT_CARD_X + 550, PN_DICT_BOTTOM_Y + 34) == PN_DUI_BACK);
    assert(pn_dict_ui_hit(&ui, PN_DICT_CARD_X + 495, PN_DICT_BOTTOM_Y + 34) == PN_DUI_OPEN_SEARCH);
    assert(pn_dict_ui_hit(&ui, PN_DICT_CARD_X + 410, PN_DICT_BOTTOM_Y + 34) == PN_DUI_OPEN_PICKER);
    assert(pn_dict_ui_hit(&ui, PN_DICT_CARD_X + 540, PN_DICT_BOTTOM_Y + 506) == PN_DUI_NEXT_PAGE);
    assert(pn_dict_ui_hit(&ui, PN_DICT_CARD_X + 420, PN_DICT_BOTTOM_Y + 260) == PN_DUI_BODY_NEXT);
    assert(pn_dict_ui_event(&ui, PN_DUI_BODY_NEXT, 100, NULL, NULL) == PN_OK && pn_dict_ui_page_index(&ui) == 1u);
    assert(pn_dict_ui_hit(&ui, PN_DICT_CARD_X + 80, PN_DICT_BOTTOM_Y + 260) == PN_DUI_BODY_PREV);
    assert(pn_dict_ui_event(&ui, PN_DUI_PREV_PAGE, 200, NULL, NULL) == PN_OK && pn_dict_ui_page_index(&ui) == 0u);

    // 切换至第 2 个候选词 "apple"：全部模式下同时命中 English Basic 和 Oxford Mini 两本词典！
    assert(pn_dict_ui_event(&ui, PN_DUI_CAND_BASE + 1, 300, NULL, NULL) == PN_OK && strcmp(pn_dict_ui_matched_word(&ui), "apple") == 0);
    assert(pn_dict_ui_matched_dicts(&ui) == 2u);
    assert(strstr(pn_dict_ui_definition(&ui), "【English Basic】") && strstr(pn_dict_ui_definition(&ui), "【Oxford Mini】"));

    // 打开词典多选面板，取消勾选第 0 本（English Basic），保留第 1 + 2 本（Oxford Mini + 中文词典，mask = 0x6）。
    assert(pn_dict_ui_event(&ui, PN_DUI_OPEN_PICKER, 305, NULL, NULL) == PN_OK && ui.mode == PN_DUI_PICKER);
    assert(pn_dict_ui_render(&ui, &font, NULL, &frame) == PN_OK);
    assert(pn_dict_ui_hit(&ui, PN_DICT_CARD_X + 120, PN_DICT_BOTTOM_Y + 95) == PN_DUI_PICKER_TOGGLE_BASE + 0);
    assert(pn_dict_ui_hit(&ui, PN_DICT_CARD_X + 120, PN_DICT_BOTTOM_Y + 159) == PN_DUI_PICKER_TOGGLE_BASE + 1);
    assert(pn_dict_ui_hit(&ui, PN_DICT_CARD_X + 60, PN_DICT_BOTTOM_Y + 506) == PN_DUI_PICKER_ALL);
    assert(pn_dict_ui_hit(&ui, PN_DICT_CARD_X + 500, PN_DICT_BOTTOM_Y + 506) == PN_DUI_PICKER_DONE);
    assert(pn_dict_ui_hit(&ui, 10, 10) == PN_DUI_DISMISS);
    // 取消勾选第 0 本 -> 剩余 2 本（0x6）立即持久化。
    assert(pn_dict_ui_event(&ui, PN_DUI_PICKER_TOGGLE_BASE + 0, 310, NULL, NULL) == PN_OK);
    assert(pn_dict_ui_enabled_mask(&ui) == 0x6u && pn_dict_ui_enabled_count(&ui) == 2u);
    // 点击卡片外（PN_DUI_DISMISS）在多选态应返回结果页并应用 2/3 子集，而非直接关闭词典！
    assert(pn_dict_ui_event(&ui, PN_DUI_DISMISS, 315, NULL, NULL) == PN_OK && ui.mode == PN_DUI_RESULT);
    assert(pn_dict_ui_matched_dicts(&ui) == 1u && strstr(pn_dict_ui_definition(&ui), "【Oxford Mini】") && !strstr(pn_dict_ui_definition(&ui), "English Basic"));

    // 关闭弹窗通过底层 page_frame 做 GL16 局部恢复；重开应保持 2/3 勾选集合（0x6）且支持居顶弹出！
    unsigned before_dismiss = present_calls;
    assert(pn_dict_ui_event(&ui, PN_DUI_DISMISS, 330, dummy_present, NULL) == PN_OK && ui.mode == PN_DUI_CLOSED && !ui.impl);
    assert(present_calls == before_dismiss + 1u && last_refresh == PN_REFRESH_GL16 && last_presented_px == page_px);

    opt.anchor_y = 920;
    assert(pn_dict_ui_open_ex(&ui, &pool, NULL, NULL, dicts_dir, long_cands + 1, 1u, false, &opt, NULL, NULL) == PN_OK);
    // 词典浮层统一固定在下方弹出，不随触点上下变换。/ Dictionary floating card always docks at the bottom.
    assert(pn_dict_ui_enabled_mask(&ui) == 0x6u && pn_dict_ui_enabled_count(&ui) == 2u && pn_dict_ui_sheet_y(&ui) == PN_DICT_BOTTOM_Y);
    assert(strstr(pn_dict_ui_definition(&ui), "牛津释义") && !strstr(pn_dict_ui_definition(&ui), "English Basic"));
    assert(pn_dict_ui_hit(&ui, 300, 300) == PN_DUI_DISMISS);

    // 再进入多选取消勾选第 2 本（仅剩第 1 本 Oxford Mini，0x2），并验证最后 1 本无法取消勾选。
    assert(pn_dict_ui_event(&ui, PN_DUI_OPEN_PICKER, 340, NULL, NULL) == PN_OK && ui.mode == PN_DUI_PICKER);
    assert(pn_dict_ui_event(&ui, PN_DUI_PICKER_TOGGLE_BASE + 2, 345, NULL, NULL) == PN_OK);
    assert(pn_dict_ui_enabled_mask(&ui) == 0x2u && pn_dict_ui_selected_dict(&ui) == 1u);
    assert(pn_dict_ui_event(&ui, PN_DUI_PICKER_TOGGLE_BASE + 1, 350, NULL, NULL) == PN_OK);
    assert(pn_dict_ui_enabled_mask(&ui) == 0x2u && pn_dict_ui_enabled_count(&ui) == 1u); // 最后一本保留
    assert(pn_dict_ui_event(&ui, PN_DUI_PICKER_DONE, 355, NULL, NULL) == PN_OK && ui.mode == PN_DUI_RESULT);
    assert(strstr(pn_dict_ui_definition(&ui), "牛津释义") && !strstr(pn_dict_ui_definition(&ui), "【Oxford Mini】"));

    // 手动查词页 + 键盘输入 "books" + 提交查询（保持单选 Oxford Mini）/ Manual search preserves checked dictionary.
    assert(pn_dict_ui_event(&ui, PN_DUI_OPEN_SEARCH, 400, NULL, NULL) == PN_OK && ui.mode == PN_DUI_SEARCH);
    assert(pn_dict_ui_render(&ui, &font, NULL, &frame) == PN_OK);
    assert(pn_dict_ui_event(&ui, PN_DUI_SEARCH_CLEAR, 410, NULL, NULL) == PN_OK);
    const char *word = "books";
    for (size_t i = 0; word[i]; i++) assert(pn_dict_ui_event(&ui, PN_DUI_KB_ENCODE((int)word[i]), 420 + i, NULL, NULL) == PN_OK);
    assert(pn_dict_ui_event(&ui, PN_DUI_SEARCH_GO, 500, NULL, NULL) == PN_OK && ui.mode == PN_DUI_RESULT);
    assert(strcmp(pn_dict_ui_matched_word(&ui), "book") == 0 && strstr(pn_dict_ui_definition(&ui), "牛津释义"));

    // 点“全选”恢复全部 3 本词典并持久化。/ Select All restores all 3 dictionaries and persists.
    assert(pn_dict_ui_event(&ui, PN_DUI_OPEN_PICKER, 510, NULL, NULL) == PN_OK && ui.mode == PN_DUI_PICKER);
    assert(pn_dict_ui_event(&ui, PN_DUI_PICKER_ALL, 520, NULL, NULL) == PN_OK && pn_dict_ui_enabled_mask(&ui) == 0x7u);
    assert(pn_dict_ui_event(&ui, PN_DUI_PICKER_DONE, 530, NULL, NULL) == PN_OK && ui.mode == PN_DUI_RESULT);
    assert(pn_dict_ui_matched_dicts(&ui) == 2u);
    assert(pn_dict_ui_event(&ui, PN_DUI_BACK, 600, NULL, NULL) == PN_OK && ui.mode == PN_DUI_CLOSED && !ui.impl);

    // 若已勾选的词典全部被删除（含 v1 单词典格式兼容），下次打开安全回退到全选。
    // If all saved dictionaries are deleted (including legacy v1 single-dict format), reopen safely falls back to all.
    assert(pn_dict_ui_pref_save(&state_media, state_dir, "removed_dict.ifo") == PN_OK);
    assert(pn_dict_ui_open_ex(&ui, &pool, NULL, NULL, dicts_dir, long_cands + 1, 1u, false, &opt, NULL, NULL) == PN_OK);
    assert(pn_dict_ui_enabled_mask(&ui) == 0x7u && pn_dict_ui_selected_dict(&ui) == SIZE_MAX && pn_dict_ui_matched_dicts(&ui) == 2u);
    pn_dict_ui_close(&ui);

    pn_free(page_orig);
    pn_free(page_px);
    pn_free(px);
    pn_font_close(&font);
    pn_free(hit);
    pn_free(cat);

    // 8. TXT 阅读器坐标取词测试 / TXT reader screen coordinate word extraction.
    FILE *fb = fopen(book_path, "wb");
    assert(fb);
    const char *sample_txt = "The running fox met a quiet world today.\n";
    assert(fwrite(sample_txt, 1, strlen(sample_txt), fb) == strlen(sample_txt));
    assert(fclose(fb) == 0);
    pn_reader_app_t app = {0};
    assert(pn_reader_app_open(&app, &pool, book_path, NULL, NULL, 44, 1000) == PN_OK);
    unsigned before_txt = present_calls;
    assert(pn_reader_app_step(&app, PN_APP_OPEN, 1000, dummy_present, NULL) == PN_OK && present_calls == before_txt + 1u);
    char tapped[6][PN_DICT_WORD_MAX];
    size_t tcnt = 0;
    // 首行靠左位置命中 "The" 或 "running" / Top-left of body hits "The" or "running".
    assert(pn_reader_app_word_at(&app, 180, 90, tapped, 6u, &tcnt) == PN_OK && tcnt >= 1u);
    assert(strcmp(tapped[0], "The") == 0 || strcmp(tapped[0], "running") == 0);
    // 远低于正文的空白区返回 PN_EMPTY / Blank area far below text returns PN_EMPTY.
    assert(pn_reader_app_word_at(&app, 340, 1000, tapped, 6u, &tcnt) == PN_EMPTY && tcnt == 0u);
    assert(pn_reader_app_close(&app, 2000) == PN_OK);

    assert(!pool.live && !pool.used);

    // 清理临时目录 / Clean temporary files.
    char p[512];
    const char *files[] = {
        "dicts/en_basic.ifo", "dicts/en_basic.idx", "dicts/en_basic.dict",
        "dicts/en_oxford.ifo", "dicts/en_oxford.idx", "dicts/en_oxford.dict",
        "dicts/zh_cn/zh_mini.ifo", "dicts/zh_cn/zh_mini.idx", "dicts/zh_cn/zh_mini.dict.dz",
        "state/dict_pref.a", "state/dict_pref.b",
        "sample.txt"
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        snprintf(p, sizeof(p), "%s/%s", root, files[i]);
        (void)unlink(p);
    }
    assert(rmdir(state_dir) == 0 && rmdir(sub_dir) == 0 && rmdir(dicts_dir) == 0 && rmdir(root) == 0);
    puts("stardict: scan, 32/64-bit idx binary search, plain & multi-chunk DictZip, stem/CJK fallback, UI & TXT word_at passed");
    return 0;
}
