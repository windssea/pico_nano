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
    uint16_t margin; ///< 左边距（旧记录同时作为右边距），0..160偶数像素 / Left margin (also right margin in legacy records), even 0..160 px
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
    uint16_t hide_images; ///< 0默认显示书内图片，1仅保留占位，不解码图片 / Zero shows book images by default; one keeps placeholders without image decoding
    uint16_t hide_bars; ///< 0默认显示上下栏，1隐藏状态栏与页脚 / Zero shows top/bottom bars by default; one hides status bar and footer
    uint16_t margin_top; ///< 正文上边距0..200偶数像素 / Even body top margin 0..200 px
    uint16_t margin_bottom; ///< 正文下边距0..200偶数像素 / Even body bottom margin 0..200 px
    int16_t margin_right_delta; ///< 正文右边距相对左边距偏移（偶数，0表示与左边距相同）/ Even right-margin delta relative to left margin (zero means symmetric)
    uint16_t bg_margin_top; ///< 阅读背景上边距0..200偶数像素 / Even reading-background top margin 0..200 px
    uint16_t bg_margin_bottom; ///< 阅读背景下边距0..200偶数像素 / Even reading-background bottom margin 0..200 px
    uint16_t bg_margin_left; ///< 阅读背景左边距0..160偶数像素 / Even reading-background left margin 0..160 px
    uint16_t bg_margin_right; ///< 阅读背景右边距0..160偶数像素 / Even reading-background right margin 0..160 px
} pn_style_t;
/// 有效四周边距与正文区域几何辅助。/ Helpers for effective four-side margins and body viewport geometry.
static inline int pn_style_margin_left(const pn_style_t *s){return s?(int)s->margin:32;}
static inline int pn_style_margin_right(const pn_style_t *s){return s?(int)s->margin+(int)s->margin_right_delta:32;}
static inline int pn_style_margin_top(const pn_style_t *s){return s?(int)s->margin_top:0;}
static inline int pn_style_margin_bottom(const pn_style_t *s){return s?(int)s->margin_bottom:0;}
static inline void pn_style_set_margin_left(pn_style_t *s,int left){if(!s)return;int right=pn_style_margin_right(s);s->margin=(uint16_t)left;s->margin_right_delta=(int16_t)(right-left);}
static inline void pn_style_set_margin_right(pn_style_t *s,int right){if(!s)return;s->margin_right_delta=(int16_t)(right-(int)s->margin);}
static inline void pn_style_set_margins_lr(pn_style_t *s,int lr){if(!s)return;s->margin=(uint16_t)lr;s->margin_right_delta=0;}
static inline int pn_style_body_top(const pn_style_t *s){return (s && s->hide_bars?0:32)+pn_style_margin_top(s);}
static inline int pn_style_body_height(const pn_style_t *s){return (s && s->hide_bars?1216:1144)-pn_style_margin_bottom(s)-pn_style_body_top(s);}
static inline int pn_style_body_width(const pn_style_t *s){return 684-pn_style_margin_left(s)-pn_style_margin_right(s);}
static inline bool pn_style_has_bg_margins(const pn_style_t *s){return s && (s->bg_margin_top || s->bg_margin_bottom || s->bg_margin_left || s->bg_margin_right);}
static inline bool pn_style_has_extended_layout(const pn_style_t *s){return s && (s->hide_bars || s->margin_top || s->margin_bottom || s->margin_right_delta || s->margin<16 || s->margin>80 || pn_style_has_bg_margins(s));}
/// 比较排版字段、去上下栏、四周边距及图片策略；仅改背景或背景边距不丢语义邻页。
/// Compare typesetting, bar visibility, four-side body margins and image policy; changing paper or paper margins preserves semantic neighbors.
static inline bool pn_style_same_typesetting(const pn_style_t *a,const pn_style_t *b){return a && b && a->hide_images==b->hide_images && a->hide_bars==b->hide_bars && a->margin_top==b->margin_top && a->margin_bottom==b->margin_bottom && a->margin_right_delta==b->margin_right_delta && !memcmp(a,b,offsetof(pn_style_t,background));}
/// 产品默认舒适模式字号。/ Product default comfortable-mode body size.
enum { PN_STYLE_DEFAULT_PIXELS = 44 };
/// 舒适模式间距：145%行距、40%段距、缩进2字、32px边距；调用方可指定字号。
/// Comfortable spacing: 145% line height, 40% paragraph gap, two-em indent, 32px margins; callers may specify body size.
pn_style_t pn_style_default(int pixels);
/// 校验所有范围。/ Validate every range.
pn_status_t pn_style_validate(const pn_style_t *style);
/// 按内容身份保存64byte配置：标准字重写schema4（58..59为阅读线深浅），非标准字重写schema5（60..61为字重），非标准清晰度写schema6（62..63为清晰度），背景非0写schema7（6..7为背景编号）；旧固件不能解析schema7，回退前需选“无”并应用以写回旧schema。
/// Save a 64-byte configuration by content identity: regular weight writes schema4 (bytes 58..59 guide darkness), other weights schema5 (bytes 60..61 weight), non-standard sharpness schema6 (bytes 62..63), nonzero backgrounds use schema7 (bytes 6..7); older firmware cannot parse schema7; select None and apply before downgrading to restore a legacy schema.
/// 隐藏图片使用schema9（内置背景64byte）或10（外部背景完整载荷）；重新显示时写回原schema4..8，其他字段保持。
/// Hidden images use schema 9 (64-byte built-in paper) or 10 (full external-paper payload); showing images writes the original schema 4..8 again, retaining other fields.
pn_status_t pn_style_save(const pn_journal_io_t *io,const pn_book_id_t *book,const pn_style_t *style);
/// 错误不改输出；EMPTY由调用方选择默认。
/// Preserve output on errors; caller chooses defaults for EMPTY only.
/// 旧schema7保留ID4/7；其他合法旧ID仅背景回退0，原日志不重写，其他字段不变。 / Legacy schema7 keeps IDs 4/7; other valid old IDs resolve to paper 0 without rewriting the journal or changing other fields.
pn_status_t pn_style_load(const pn_journal_io_t *io,const pn_book_id_t *book,pn_style_t *style);
/// 绑定root下global_style.a/b作为全局默认排版日志（不建目录）。/ Bind global_style.a/b under root as the global default typesetting journal (no mkdir).
pn_status_t pn_style_global_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io);
/// 全局默认排版的固定虚拟内容身份。/ Fixed sentinel book identity for global default typesetting.
pn_book_id_t pn_style_global_id(void);
/// 从状态目录读取全局默认排版；无记录返回PN_EMPTY，错误保留输出。/ Load global default style from state_dir; EMPTY if unset, preserving output on error.
pn_status_t pn_style_global_load(pn_media_t *state_media,const char *state_dir,pn_style_t *style);
/// 保存全局默认排版到状态目录。/ Save global default style into state_dir.
pn_status_t pn_style_global_save(pn_media_t *state_media,const char *state_dir,const pn_style_t *style);
/// 删除单书排版覆盖文件（style.a/b），使该书恢复继承全局默认排版。/ Remove per-book style override files (style.a/b) so the book inherits global defaults again.
pn_status_t pn_style_clear_files(const pn_journal_files_t *files);
