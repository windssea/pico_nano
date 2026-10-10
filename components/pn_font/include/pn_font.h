/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：受限TTF度量与4bpp字体绘制，包含常驻UI子集源。
 * English: bounded TTF metrics and 4bpp font drawing with a resident UI subset source.
 * 冻结：字体源保持有效；owner串行使用；不写设置或硬件。
 * Frozen: font source remains valid; serialized owner calls; no settings or hardware writes.
 */
#pragma once
#include "pn_alloc.h"
#include "pn_frame.h"
#include "pn_layout.h"
typedef enum {PN_FONT_GRAY=0,PN_FONT_BINARY=1} pn_font_render_t;
typedef struct {
    void *impl; ///< 初始化为NULL，内部引擎 / Initialize to NULL, private engine
    int pixels; ///< 当前像素高度 / Current pixel height
    uint8_t ink; ///< 字形混合到的灰阶，0为黑（默认）/ Gray the glyphs blend towards, 0 is black (default)
} pn_font_t;
/// 仅接受有glyf的单TTF源，最大32MiB；全部引擎分配走pool。
/// Accept single glyf TTF sources only, at most 32 MiB; all engine allocations use pool.
pn_status_t pn_font_open(pn_font_t *font,pn_pool_t *pool,const pn_text_source_t *source,int pixels);
/// 释放引擎，不关闭调用方源，幂等。
/// Release engine without closing caller source, idempotently.
void pn_font_close(pn_font_t *font);
/// 设置8至128像素，不保存全局或逐书设置。
/// Set 8 through 128 pixels without saving global or per-book settings.
pn_status_t pn_font_size(pn_font_t *font,int pixels);
/// 字符度量26.6；缺字返回EMPTY以便上层fallback。
/// Character 26.6 advance; missing glyph returns EMPTY for higher-level fallback.
pn_status_t pn_font_advance(void *font,uint32_t codepoint,int32_t *advance);
/// 当前ascender/descender，错误不更改输出。
/// Current ascender/descender; errors preserve output.
pn_status_t pn_font_vertical(pn_font_t *font,int *ascent,int *descent);
/// x使用26.6，baseline为像素；裁切写入，灰阶覆盖或DU二值阈值。
/// x uses 26.6 and baseline uses pixels; clipped coverage drawing or binary DU threshold.
pn_status_t pn_font_draw(pn_font_t *font,pn_frame_t *frame,uint32_t codepoint,int32_t x_64,int baseline,pn_font_render_t mode);
/// 常驻子集源，不分配、不访问卡；没有完整中文fallback承诺。
/// Resident subset source without allocation or card access; not a full Chinese fallback.
pn_text_source_t pn_font_builtin_source(void);

#define PN_FONT_NAME_MAX 128
typedef struct {
    char family[PN_FONT_NAME_MAX]; ///< 原始UTF-8字体族名，优先中文 / Original UTF-8 family, preferring Chinese
    char style[PN_FONT_NAME_MAX]; ///< 原始样式名 / Original style name
    uint32_t glyphs; ///< 字形总数，不等于中文字覆盖数 / Total glyphs, not Chinese coverage
    uint16_t weight; ///< OS/2字重1..1000，0未知 / OS/2 weight 1..1000, zero unknown
    bool variable,names_limited; ///< 可变表存在与名称扫描受限 / Variable table present and limited name scan
} pn_font_info_t;
/// 有界只读元数据，错误保持输出，不宣称全字库覆盖。/ Bounded read-only metadata, preserving outputs on error; no full-coverage claim.
pn_status_t pn_font_info(pn_font_t *,pn_font_info_t *);

/// 逐字形加载所有轮廓，校验字体引擎/源错误；不证明全字库覆盖或全部字号光栅结果。
/// Load every glyph outline and validate engine/source errors; not full character coverage or all-size raster verification.
pn_status_t pn_font_validate(pn_font_t *);
