/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：逐书排版配置载荷，字体文件选择另由字体管理负责。
 * English: per-book typesetting payload; font-file selection belongs to font management.
 * 冻结：不存页码、不改位置；无效/未知配置不猜默认。
 * Frozen: no page numbers or position changes; never guess defaults for invalid or unknown configuration.
 */
#pragma once
#include "pn_progress.h"
typedef struct {
    uint16_t pixels; ///< 正文28..72px / Body 28..72 px
    uint16_t line_percent; ///< 行距100..220百分比 / Line spacing 100..220 percent
    uint16_t gap_percent; ///< 段距0..100字号百分比 / Paragraph gap 0..100 percent of size
    uint16_t indent_em; ///< 首行0..2个em / First-line zero through two em
    uint16_t margin; ///< 左右16..80偶数像素 / Even horizontal margins 16..80 px
    uint16_t gl_before_clear; ///< 0..30次GL后GC / GC after zero through 30 GL presentations
    uint16_t tracking_percent; ///< 字间距0..50字号百分比 / Tracking zero through 50 percent of body size
} pn_style_t;
/// 默认145%行距、25%段距、无缩进、32px边距和12次GL。
/// Default 145% line spacing, 25% gap, no indent, 32px margins and 12 GL presentations.
pn_style_t pn_style_default(int pixels);
/// 校验所有范围。/ Validate every range.
pn_status_t pn_style_validate(const pn_style_t *style);
/// 按内容身份保存64byte schema2配置（读取旧版字距补零），不影响位置/书签。
/// Save a 64-byte schema2 configuration by content identity (legacy reads default tracking to zero) without affecting positions/bookmarks.
pn_status_t pn_style_save(const pn_journal_io_t *io,const pn_book_id_t *book,const pn_style_t *style);
/// 错误不改输出；EMPTY由调用方选择默认。
/// Preserve output on errors; caller chooses defaults for EMPTY only.
pn_status_t pn_style_load(const pn_journal_io_t *io,const pn_book_id_t *book,pn_style_t *style);
