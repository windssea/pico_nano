/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：正文、可选备用和常驻字体的有界回退链。
 * English: bounded primary, optional fallback and resident-font chain.
 * 冻结：只有EMPTY触发回退，其他错误必须保留。
 * Frozen: fallback only on EMPTY; preserve every other error.
 */
#pragma once
#include "pn_font.h"
typedef struct {
    pn_font_t *primary; ///< 借用正文字体，owner保持对象有效 / Borrowed primary; owner keeps object valid
    pn_pool_t *pool; ///< 借用内存owner / Borrowed memory owner
    pn_text_source_t sources[2]; ///< 可选备用与常驻源 / Optional fallback and resident sources
    pn_font_t fonts[2]; ///< 懒加载引擎 / Lazy engines
    unsigned count; ///< 一或两个备用槽 / One or two fallback slots
} pn_font_chain_t;
typedef struct {
    pn_font_t *font; ///< 本次选中引擎 / Selected engine
    int32_t advance_64; ///< 同引擎字宽 / Same-engine advance
    int ascent,descent; ///< 同引擎垂直度量 / Same-engine vertical metrics
} pn_font_choice_t;
/// 初始化空对象，借用源直到clear；不拥有主字体。/ Initialize empty object, borrowing sources until clear; does not own primary.
pn_status_t pn_font_chain_init(pn_font_chain_t *,pn_pool_t *,pn_font_t *,const pn_text_source_t *);
/// 关闭备用引擎，保留源配置以便大图后重开。/ Close fallback engines while retaining source configuration for reopening after images.
void pn_font_chain_suspend(pn_font_chain_t *);
/// 释放备用并清配置，不关闭正文字体或源。/ Release fallbacks and clear configuration without closing primary or sources.
void pn_font_chain_clear(pn_font_chain_t *);
/// 与主字号同步，选择与度量同一字形；错误不改输出。/ Match primary size and select/measure the same glyph, preserving output on error.
pn_status_t pn_font_chain_choose(pn_font_chain_t *,uint32_t,pn_font_choice_t *);
/// 分页器字宽回调，全链缺字才返回EMPTY。/ Layout advance callback; EMPTY only when the entire chain lacks the glyph.
pn_status_t pn_font_chain_advance(void *,uint32_t,int32_t *);
/// 各有效字体最大垂直尺寸，供TXT行框使用。/ Maximum vertical metrics across valid fonts for TXT line boxes.
pn_status_t pn_font_chain_vertical(pn_font_chain_t *,int *,int *);
/// 同源字形绘制；不把错误伪装成缺字。/ Draw the selected glyph without disguising errors as missing glyphs.
pn_status_t pn_font_chain_draw(pn_font_chain_t *,pn_frame_t *,uint32_t,int32_t,int,pn_font_render_t);
