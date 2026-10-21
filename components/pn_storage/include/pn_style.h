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
#include "pn_reading_background.h"
#include <stddef.h>
#include <string.h>
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
    int16_t weight; ///< 字重-1细、0标准、1..3加粗；只改字形笔画不改字宽与分页，旧记录为0 / Weight: -1 light, 0 regular, 1..3 bolder; changes strokes only, never advances or pagination; legacy records load as 0
    uint16_t clarity; ///< 清晰度0标准、1清晰、2锐利、3黑白；只改字形边缘灰阶，不改分页，旧记录为0 / Sharpness: standard, clear, sharp or black-and-white; changes glyph edge grays only, never pagination; legacy records load as 0
    uint16_t background; ///< 持久背景ID：0无、4竹影、7纯色中；旧1..8读取时仅移除款回退无，不改变分页 / Persistent paper ID: None 0, bamboo 4, texture 7; retired legacy IDs 1..8 load as None without changing pagination
    char background_name[PN_BACKGROUND_NAME_MAX]; ///< 外部背景单文件名，内置时全零 / External paper basename, all zero for built-ins
    pn_book_id_t background_identity; ///< 外部背景完整源摘要，内置时全零 / Full external-source digest, all zero for built-ins
} pn_style_t;
/// 只比较背景之前的排版字段，背景替换不丢语义邻页。/ Compare typesetting fields preceding the paper reference; replacing paper preserves semantic neighbors.
static inline bool pn_style_same_typesetting(const pn_style_t *a,const pn_style_t *b){return a && b && !memcmp(a,b,offsetof(pn_style_t,background));}
/// 产品默认舒适模式字号。/ Product default comfortable-mode body size.
enum { PN_STYLE_DEFAULT_PIXELS = 44 };
/// 舒适模式间距：145%行距、40%段距、缩进2字、32px边距；调用方可指定字号。
/// Comfortable spacing: 145% line height, 40% paragraph gap, two-em indent, 32px margins; callers may specify body size.
pn_style_t pn_style_default(int pixels);
/// 校验所有范围。/ Validate every range.
pn_status_t pn_style_validate(const pn_style_t *style);
/// 按内容身份保存64byte配置：标准字重写schema4（58..59为阅读线深浅），非标准字重写schema5（60..61为字重），非标准清晰度写schema6（62..63为清晰度），背景非0写schema7（6..7为背景编号）；旧固件不能解析schema7，回退前需选“无”并应用以写回旧schema。
/// Save a 64-byte configuration by content identity: regular weight writes schema4 (bytes 58..59 guide darkness), other weights schema5 (bytes 60..61 weight), non-standard sharpness schema6 (bytes 62..63), nonzero backgrounds use schema7 (bytes 6..7); older firmware cannot parse schema7; select None and apply before downgrading to restore a legacy schema.
pn_status_t pn_style_save(const pn_journal_io_t *io,const pn_book_id_t *book,const pn_style_t *style);
/// 错误不改输出；EMPTY由调用方选择默认。
/// Preserve output on errors; caller chooses defaults for EMPTY only.
/// 旧schema7保留ID4/7；其他合法旧ID仅背景回退0，原日志不重写，其他字段不变。 / Legacy schema7 keeps IDs 4/7; other valid old IDs resolve to paper 0 without rewriting the journal or changing other fields.
pn_status_t pn_style_load(const pn_journal_io_t *io,const pn_book_id_t *book,pn_style_t *style);
