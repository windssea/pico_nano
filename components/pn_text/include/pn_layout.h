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
} pn_font_metrics_t;
typedef struct {
    int width; ///< 正文区域宽度，不含边距 / Content width excluding margins
    int height; ///< 正文区域高度 / Content height
    int line_height; ///< 基线间距 / Baseline spacing
    int ascent; ///< 首行基线距离顶部 / First baseline distance from top
    int indent; ///< 段首缩进像素 / Paragraph indentation pixels
    int paragraph_gap; ///< 换段额外间距 / Extra paragraph gap
    int32_t letter_spacing_64; ///< 字间额外26.6间距，0..4096 / Extra inter-glyph spacing in 26.6 units, zero through 4096
} pn_layout_t;
typedef struct {
    pn_text_char_t source; ///< 字符与原文件范围 / Character and original range
    int32_t x_64; ///< 行内26.6位置 / Horizontal 26.6 position
    int baseline; ///< 区域内基线 / Baseline within content area
    int32_t advance_64; ///< 字形度量 / Glyph advance
} pn_page_glyph_t;
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
