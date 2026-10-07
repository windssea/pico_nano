/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB出版物结构与章节资源身份，不负责正文排版和文件寿命。
 * English: EPUB publication structure and chapter resource identities, without layout or file ownership.
 * 冻结：完整container/OPF校验前不发布；不把EPUB转换成永久TXT偏移。
 * Frozen: publish only after complete container/OPF verification; never convert EPUB into permanent TXT offsets.
 */
#pragma once
#include "pn_xml.h"
#include "pn_zip.h"
#define PN_EPUB_ID_MAX 256
#define PN_EPUB_META_MAX 512
#define PN_EPUB_ITEMS_MAX PN_ZIP_ENTRIES_MAX
typedef struct {void *impl;} pn_epub_t; ///< 初始NULL，不可复制活动对象 / Initially NULL; never copy a live object
typedef enum {PN_EPUB_OTHER=0,PN_EPUB_XHTML,PN_EPUB_NCX,PN_EPUB_IMAGE,PN_EPUB_CSS,PN_EPUB_FONT} pn_epub_media_t;
typedef struct {
    char id[PN_EPUB_ID_MAX]; ///< OPF资源ID / OPF resource ID
    char path[PN_ZIP_PATH_MAX]; ///< 容器内归一资源路径 / Normalized container resource path
    uint32_t zip_index; ///< 本次ZIP代次索引，不作永久locator / Current ZIP-generation index, not a persistent locator
    pn_epub_media_t media; ///< 资源类别 / Resource kind
    bool linear; ///< 章节主阅读流标志 / Primary reading-flow flag
} pn_epub_item_t;
typedef struct {
    char title[PN_EPUB_META_MAX],creator[PN_EPUB_META_MAX],identifier[PN_EPUB_META_MAX],language[64]; ///< 元数据UTF8 / UTF-8 metadata
    char package_path[PN_ZIP_PATH_MAX],nav_path[PN_ZIP_PATH_MAX],ncx_path[PN_ZIP_PATH_MAX],cover_path[PN_ZIP_PATH_MAX]; ///< OPF/目录/封面引用 / OPF/navigation/cover references
    size_t manifest_count,spine_count; ///< 资源及章节数量 / Resource and chapter counts
    unsigned version; ///< EPUB 2或3 / EPUB two or three
    bool fixed_layout; ///< 固定版式，阅读层须独立处理 / Fixed layout requiring separate reader handling
    bool cover_declared; ///< 标准声明；false时路径仅filename候选 / Standard declaration; otherwise path is only a filename candidate
} pn_epub_info_t;
/// 借用有效ZIP；salt须来自可信随机源；错误epub仍为空，必须先close再关ZIP和源。
/// Borrow a valid ZIP with trusted random salt; errors leave epub empty; close before ZIP and source.
pn_status_t pn_epub_open(pn_epub_t *epub,pn_pool_t *pool,pn_zip_t *zip,const uint8_t salt[16]);
/// 幂等释放出版物模型，不关闭ZIP。/ Idempotently release the publication model without closing ZIP.
void pn_epub_close(pn_epub_t *epub);
/// 验证媒体后复制元数据，失败输出不变。/ Validate media then copy metadata, preserving output on errors.
pn_status_t pn_epub_info(pn_epub_t *epub,pn_epub_info_t *out);
/// 按OPF章节顺序复制资源身份，保留linear=no，越界EMPTY。/ Copy resource identity in OPF spine order, retaining linear=no; EMPTY beyond the end.
pn_status_t pn_epub_spine(pn_epub_t *epub,size_t index,pn_epub_item_t *out);
/// 精确资源路径定位首个对应章节，非章节资源返回EMPTY，错误输出不变。
/// Locate the first corresponding spine entry by exact resource path; EMPTY for non-spine resources, preserving output on errors.
pn_status_t pn_epub_spine_find(pn_epub_t *epub,const char *path,size_t *index);
/// 在出版物所属ZIP打开精确路径资源流；流必须先于出版物关闭，不改源寿命。
/// Open an exact-path resource stream in the publication's own ZIP; close stream before publication without changing source lifetime.
pn_status_t pn_epub_resource_open(pn_epub_t *epub,const char *path,pn_zip_stream_t *stream);
