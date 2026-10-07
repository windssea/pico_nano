/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：中文字体信息和原生字形样例的纯绘制面板。
 * English: paint-only Chinese font information and native glyph sample panel.
 * 冻结：不选字体，不保存配置，样例不代表完整覆盖。
 * Frozen: never select fonts/save settings or equate samples with full coverage.
 */
#pragma once
#include "pn_font.h"
/// 固定中文/拉丁/数字样例，UTF-8只读。/ Fixed read-only UTF-8 Chinese/Latin/numeric sample.
const char *pn_font_preview_text(void);
/// 684x1216纯绘制，metadata与缺字统计由owner预先校验。
/// Paint-only 684x1216 panel with metadata/missing counts validated by the owner first.
pn_status_t pn_font_preview_draw(pn_font_t *ui,pn_font_t *body,const pn_font_info_t *,uint64_t bytes,unsigned checked,unsigned missing,pn_frame_t *);
