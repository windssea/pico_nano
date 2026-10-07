/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB事件到有界页节点，保留语义位置和图片引用。
 * English: EPUB events to bounded page nodes retaining semantic positions and image references.
 * 冻结：完整正文CRC后才valid；不呈现、不保存、不使用TXT偏移。
 * Frozen: valid only after complete body CRC; no presentation, persistence or TXT offsets.
 */
#pragma once
#include "pn_xhtml.h"
#include "pn_layout.h"
#include "pn_resource.h"
#define PN_EPUB_PAGE_IMAGES_MAX 16
typedef struct {
    int32_t advance_64; ///< 26.6字形宽度 / Glyph width in 26.6 units
    int ascent,descent,line_height; ///< 垂直度量；line_height为0则使用配置 / Vertical metrics; zero line_height uses configuration
    int pixels; ///< 当前字形像素字号，绘制须复用 / Glyph pixel size to reuse for drawing
} pn_epub_metrics_t;
typedef struct {
    void *ctx; ///< 字体/图片适配owner / Font/image adapter owner
    pn_status_t (*glyph)(void *,uint32_t,const pn_xhtml_style_t *,pn_epub_metrics_t *); ///< 样式相关实际字体度量 / Actual style-dependent font metrics
    pn_status_t (*image)(void *,const char *,int,int,int *,int *); ///< 可选图片尺寸，输入可用宽高；无适配时图片明确UNSUPPORTED / Optional image sizing with viewport bounds; absent adapter returns UNSUPPORTED
} pn_epub_measure_t;
typedef struct {
    char path[PN_ZIP_PATH_MAX],alt[PN_EPUB_META_MAX]; ///< 图片资源及替代文字 / Image resource and alternative text
    int width,height; ///< 实际页内尺寸 / Actual in-page size
} pn_epub_page_image_t;
typedef struct {
    uint64_t source_order; ///< 本次完整解析的临时事件顺序，不持久化 / Transient event order for this complete parse, never persisted
    pn_xhtml_position_t position; ///< 对应源语义位置 / Corresponding source semantic position
    pn_xhtml_style_t style; ///< 源基础样式 / Basic source style
    pn_epub_metrics_t metrics; ///< 用于本次排版的实际度量 / Actual metrics used for this layout
    int32_t x_64; ///< 页内26.6横坐标 / In-page x coordinate in 26.6 units
    int baseline; ///< 页内基线 / In-page baseline
    uint32_t codepoint; ///< 字形标量；图片为0 / Glyph scalar, zero for image nodes
    unsigned image_index; ///< 图片槽；字形为UINT_MAX / Image slot, UINT_MAX for glyphs
} pn_epub_page_node_t;
typedef struct {
    pn_epub_page_node_t *nodes; ///< 调用方缓冲，最多4096项 / Caller buffer, at most 4096 nodes
    size_t capacity,count; ///< 容量与已验证节点数 / Capacity and verified node count
    pn_epub_page_image_t *images; ///< 调用方可选图片缓冲 / Optional caller-owned image buffer
    size_t image_capacity,image_count; ///< 最多16槽 / At most 16 slots
    uint64_t begin_order,next_order; ///< 临时源顺序，用于有界上一页重建 / Transient source order for bounded previous-page reconstruction
    pn_xhtml_position_t begin,next; ///< 首节点及下一页位置，不是永久页号 / First node and next-page positions, not permanent page numbers
    bool valid,has_next; ///< 完整CRC成功及还有下一页 / Complete CRC success and another page exists
} pn_epub_page_t;
/// 输入流分页，start NULL为资源首；精确TEXT匹配或ELEMENT事件开启，错误清valid/count。
/// Paginate input, NULL start means resource beginning; exact TEXT match or ELEMENT event activation; errors clear valid/count.
pn_status_t pn_epub_page_input(pn_pool_t *pool,const pn_xml_input_t *input,const char *base,const uint8_t salt[16],const pn_xhtml_position_t *start,const pn_layout_t *layout,const pn_epub_measure_t *measure,pn_epub_page_t *page);
/// 出版物章节同源分页；填满页后继续验证全资源，不提交显示或持久状态。
/// Shared publication chapter pagination; continue full-resource validation after filling the page, without display or persistent commits.
pn_status_t pn_epub_page_prepare(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],const pn_xhtml_position_t *start,const pn_layout_t *layout,const pn_epub_measure_t *measure,pn_epub_page_t *page);

/// 在stop语义位置前停止生成节点，仍验证完整资源；用于恢复/重排后的上一页，不数值排序locator。
/// Stop nodes before semantic stop while validating the whole resource; reconstruct previous pages after resume/reflow without numeric locator ordering.
pn_status_t pn_epub_page_prepare_until(pn_pool_t *,pn_epub_t *,const char *,const uint8_t salt[16],const pn_xhtml_position_t *start,const pn_xhtml_position_t *stop,const pn_layout_t *,const pn_epub_measure_t *,pn_epub_page_t *);
