/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界文本分页节点；字体度量由调用方提供。
 * English: bounded text page nodes using caller-provided font metrics.
 * 冻结：不栅格化、不分配、不保存永久页码；失败不消费reader。
 * Frozen: no rasterization, allocation or permanent page numbers; failures do not consume the reader.
 */
#pragma once
#include "pn_text.h"
#define PN_PAGE_GLYPHS_MAX 4096

typedef struct {
    void *ctx; ///< 字体上下文 / Font context
    pn_status_t (*advance)(void *,uint32_t,int32_t *); ///< 26.6像素度量，含空白 / 26.6 pixel advance including whitespace
    pn_status_t (*heading_advance)(void *,uint32_t,int32_t *); ///< 可选：TXT章节标题26.6像素度量，NULL同正文 / Optional TXT chapter heading 26.6 advance; NULL uses body advance
    int heading_line_height; ///< 可选：TXT章节标题行高，0同正文 / Optional TXT chapter heading line height, 0 uses body
    int heading_ascent; ///< 可选：TXT章节标题上伸，0同正文 / Optional TXT chapter heading ascent, 0 uses body
} pn_font_metrics_t;
typedef struct {
    int width; ///< 正文区域宽度，不含边距 / Content width excluding margins
    int height; ///< 正文区域高度 / Content height
    int line_height; ///< 基线间距 / Baseline spacing
    int ascent; ///< 首行基线距离顶部 / First baseline distance from top
    int indent; ///< 段首缩进像素 / Paragraph indentation pixels
    int paragraph_gap; ///< 换段额外间距 / Extra paragraph gap
    int32_t letter_spacing_64; ///< 字间额外26.6间距，-2048..4096 / Extra inter-glyph spacing in 26.6 units, -2048 through 4096
    bool compact_paragraphs; ///< TXT忽略重复空行的额外高度，源字符仍保留 / TXT collapses repeated blank-line height while retaining source characters
    uint8_t chapter_align; ///< 章节标题对齐：0默认、1左、2中、3右 / Chapter heading alignment: 0 default, 1 left, 2 center, 3 right
    uint8_t chapter_gap; ///< 章节标题段后间距：0默认、1紧凑、2宽松、3宽敞 / Chapter heading trailing gap: 0 default, 1 compact, 2 loose, 3 wide
    bool chapter_style_txt; ///< 是否启用TXT独立章节行识别与标题排版 / Enable TXT standalone chapter-line detection and heading layout
} pn_layout_t;
typedef struct {
    pn_text_char_t source; ///< 字符与原文件范围 / Character and original range
    int32_t x_64; ///< 行内26.6位置 / Horizontal 26.6 position
    int baseline; ///< 区域内基线 / Baseline within content area
    int32_t advance_64; ///< 字形度量 / Glyph advance
    uint8_t heading; ///< 0正文、1TXT章节标题行 / 0 body glyph, 1 TXT chapter heading glyph
} pn_page_glyph_t;
/// 判断一行独立文本（不含换行）是否为常见中英文章节标题。/ Check whether a standalone line of codepoints (without newline) matches a common CJK/English chapter heading.
bool pn_text_is_chapter_line(const uint32_t *cps,size_t count);
/// 安全叠加字间距（负字间距保留正向步进下限，防止窄字重叠或死循环）。/ Safely apply letter spacing (negative tracking preserves a positive advance floor against overlap or loops).
static inline int32_t pn_layout_tracked_advance(int32_t advance_64,int32_t letter_spacing_64){
    if(advance_64<=0)return advance_64;
    if(letter_spacing_64>=0)return advance_64+letter_spacing_64;
    int32_t floor_step=advance_64/2;
    if(floor_step<64)floor_step=advance_64<64?advance_64:64;
    int32_t step=advance_64+letter_spacing_64;
    return step<floor_step?floor_step:step;
}
typedef struct {
    pn_page_glyph_t *glyphs; ///< 调用方缓冲，最多4096项 / Caller buffer, up to 4096 entries
    size_t capacity; ///< 缓冲容量 / Buffer capacity
    size_t count; ///< 有效字符数量，含换行 / Valid character count including newlines
    uint64_t begin; ///< 页首源位置 / Page start source position
    uint64_t end; ///< 下页源位置 / Next page source position
    bool next_paragraph_start; ///< 下页是否从段首开始 / Whether next page starts a paragraph
    bool valid; ///< 仅成功返回后可用 / Usable only after successful return
} pn_text_page_t;
/// 返回共享页节点并推进reader；EMPTY为EOF；错误清valid/count，glyph缓冲可有部分内容。
/// Return shared page nodes and advance reader; EMPTY means EOF; errors clear valid/count but may leave partial buffer contents.
pn_status_t pn_text_paginate(pn_text_reader_t *reader,const pn_layout_t *layout,
    const pn_font_metrics_t *metrics,bool paragraph_start,pn_text_page_t *page);
