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
    uint16_t gl_before_clear; ///< 历史字段仅兼容读写，不控制刷新 / Legacy field retained for record compatibility, never controls refresh
    uint16_t tracking_percent; ///< 字间距0..50字号百分比 / Tracking zero through 50 percent of body size
    uint16_t reading_line; ///< 阅读线：0关闭、1虚线、2点线 / Reading guide: off, dashed or dotted
    int16_t reading_line_offset; ///< 阅读线上下位置-8..8px，2px一步 / Guide vertical offset -8..8 px in 2 px steps
    uint16_t reading_line_shade; ///< 阅读线深浅：0浅、1中、2深、3黑；旧记录为0 / Guide darkness: light, medium, dark or black; legacy records load as light
} pn_style_t;
/// 产品默认舒适模式字号。/ Product default comfortable-mode body size.
enum { PN_STYLE_DEFAULT_PIXELS = 44 };
/// 舒适模式间距：145%行距、40%段距、缩进2字、32px边距；调用方可指定字号。
/// Comfortable spacing: 145% line height, 40% paragraph gap, two-em indent, 32px margins; callers may specify body size.
pn_style_t pn_style_default(int pixels);
/// 校验所有范围。/ Validate every range.
pn_status_t pn_style_validate(const pn_style_t *style);
/// 按内容身份保存64byte schema4配置（58..59为阅读线深浅）；旧版阅读线默认关闭、深浅为浅，不影响位置/书签。
/// Save a 64-byte schema4 configuration by content identity (bytes 58..59 hold guide darkness); legacy guides default off and light without affecting positions/bookmarks.
pn_status_t pn_style_save(const pn_journal_io_t *io,const pn_book_id_t *book,const pn_style_t *style);
/// 错误不改输出；EMPTY由调用方选择默认。
/// Preserve output on errors; caller chooses defaults for EMPTY only.
pn_status_t pn_style_load(const pn_journal_io_t *io,const pn_book_id_t *book,pn_style_t *style);
