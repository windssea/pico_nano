/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：XHTML正文语义事件与稳定位置，不负责分页、图片解码或持久化。
 * English: semantic XHTML body events and stable positions, without pagination, image decoding or persistence.
 * 冻结：事件在完整XML/CRC完成前暂定；语义位置不是XML字节或永久页码。
 * Frozen: events are provisional before complete XML/CRC; semantic locations are neither XML bytes nor permanent page numbers.
 */
#pragma once
#include "pn_epub.h"
#include "pn_semantic.h"
typedef enum {PN_XHTML_TEXT=0,PN_XHTML_BLOCK_OPEN,PN_XHTML_BLOCK_CLOSE,PN_XHTML_BREAK,PN_XHTML_ANCHOR,PN_XHTML_IMAGE} pn_xhtml_event_kind_t;
typedef enum {PN_XHTML_NO_BLOCK=0,PN_XHTML_CONTAINER,PN_XHTML_PARAGRAPH,PN_XHTML_HEADING,PN_XHTML_LIST_ITEM,PN_XHTML_PREFORMATTED} pn_xhtml_block_t;
typedef enum {
    PN_XHTML_ALIGN_LEFT=0, ///< 左对齐 / Left alignment
    PN_XHTML_ALIGN_CENTER, ///< 居中 / Center alignment
    PN_XHTML_ALIGN_RIGHT ///< 右对齐 / Right alignment
} pn_xhtml_align_t;
typedef struct {
    uint32_t value; ///< 千分之一像素或千分之一百分数；0为自动 / Thousandths of pixels or percent; zero means auto
    bool percent; ///< 百分比相对页内可用尺寸 / Percentage relative to available page dimensions
} pn_xhtml_length_t;
typedef struct {
    pn_xhtml_length_t width,height,max_width,max_height; ///< 非继承图片几何约束 / Noninherited image geometry constraints
    pn_xhtml_align_t align; ///< 继承行对齐 / Inherited row alignment
    bool bold,italic,pre,hidden; ///< 基础继承样式；hidden不发正文 / Basic inherited styles; hidden emits no body text
    unsigned heading; ///< 0普通，1至6标题 / Zero for normal text, one through six for headings
} pn_xhtml_style_t;
typedef struct {
    pn_xhtml_event_kind_t kind; ///< 事件类型 / Event kind
    pn_xhtml_position_t position; ///< 与字号无关的资源内语义位置 / Resource-local semantic position independent of font size
    pn_xhtml_style_t style; ///< 当前基础样式 / Current basic style
    pn_xhtml_block_t block; ///< 区分容器/段落/标题等结构，避免重复段距 / Distinguish containers/paragraphs/headings to avoid duplicate paragraph gaps
    uint32_t codepoint; ///< TEXT的Unicode标量 / Unicode scalar for TEXT
    const char *value; ///< ANCHOR的id或IMAGE归一资源路径，回调期有效 / Anchor ID or normalized image resource path, callback lifetime only
    const char *alt; ///< IMAGE替代文字，回调期有效 / Image alternative text, callback lifetime only
} pn_xhtml_event_t;
typedef struct {
    uint64_t elements; ///< 全文元素数量，包括非显示部分 / Document element count including nonvisual parts
    uint64_t text_codepoints; ///< 实际发出的归一正文标量数 / Emitted normalized body scalar count
    uint64_t blocks; ///< 可见块开始数量 / Visible block-open count
    uint64_t images; ///< 图片引用数量，未解码 / Image reference count without decoding
    uint64_t anchors; ///< 正文id事件数量，含隐藏元素 / Body ID event count including hidden elements
} pn_xhtml_stats_t;
typedef pn_status_t (*pn_xhtml_sink_t)(void *,const pn_xhtml_event_t *);
/// 消费已验证EOF的XML流；支持严格XHTML与元素行内样式（无样式表加载），全部解析分配走pool；失败不改stats，sink须丢弃暂定内容。
/// Consume XML with verified EOF, supporting strict XHTML and inline attributes (no stylesheet loading) with pooled allocations; errors preserve stats and require discarding provisional sink content.
pn_status_t pn_xhtml_parse_input(pn_pool_t *pool,const pn_xml_input_t *input,const char *base,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats);
/// 从出版物解析并按预算复用本会话已验证语义；head样式/完整CRC/XML通过才缓存，复放仍检查介质与取消，失败不改stats。
/// Parse publication resources and reuse budgeted verified session semantics; cache only after head styles/full CRC/XML pass, rechecking media/cancellation on replay and preserving stats on failure.
pn_status_t pn_xhtml_parse(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats);
/// 释放本出版物语义缓存，借用期间不释放；close使用同一入口。/ Release publication semantics unless borrowed; close uses the same entry point.
void pn_xhtml_cache_close(pn_epub_t *);
/// 完整扫描找到唯一id，缺失EMPTY、重复CORRUPT，失败不改position。
/// Scan completely for a unique ID, EMPTY if absent, CORRUPT on duplicates, preserving position on failure.
pn_status_t pn_xhtml_anchor(pn_pool_t *pool,pn_epub_t *epub,const char *path,const char *id,const uint8_t salt[16],pn_xhtml_position_t *position);

/// 完整校验后返回位置首次事件的临时顺序号；不是持久偏移；错误不改输出。
/// Return transient first-event order after complete validation, never a durable offset; errors preserve output.
pn_status_t pn_xhtml_order(pn_pool_t *,pn_epub_t *,const char *,const pn_xhtml_position_t *,const uint8_t salt[16],uint64_t *);
/// 完整校验后选择目标（NULL为章末）前约8192可见事件的检查点；element为0表示章首，不排序locator。/ After full validation choose a checkpoint about 8192 visible events before target (NULL means chapter end); element zero means chapter start, never sort locators.
pn_status_t pn_xhtml_window_start(pn_pool_t *,pn_epub_t *,const char *,const pn_xhtml_position_t *,const uint8_t salt[16],uint64_t *,uint64_t *,pn_xhtml_position_t *);
