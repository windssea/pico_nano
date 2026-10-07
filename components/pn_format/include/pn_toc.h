/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB目录条目与章节/锚点身份，不执行阅读跳转。
 * English: EPUB table-of-contents entries and chapter/anchor identities, without executing reader jumps.
 * 冻结：完整目录CRC/XML校验后才发布；锚点存在性由正文层验证。
 * Frozen: publish only after complete navigation CRC/XML checks; body code validates anchor existence.
 */
#pragma once
#include "pn_epub.h"
#include "pn_resource.h"
typedef struct {void *impl;} pn_toc_t; ///< 初始NULL，不复制活动对象 / Initially NULL; never copy a live object
typedef struct {
    char label[PN_EPUB_META_MAX]; ///< 归一空白后的UTF8标题 / UTF-8 label with normalized whitespace
    char path[PN_ZIP_PATH_MAX]; ///< 跳转资源路径，分组为空 / Target resource path, empty for groups
    char fragment[PN_RESOURCE_FRAGMENT_MAX]; ///< 一次解码后的锚点，未核对正文 / Once-decoded anchor, not yet checked against body
    size_t spine_index; ///< 章节序号，分组为SIZE_MAX / Spine ordinal, SIZE_MAX for groups
    unsigned level; ///< 从0起的目录层级 / Zero-based navigation level
    bool target; ///< 有可解析的章节引用 / Has a resolved chapter reference
} pn_toc_entry_t;
/// 优先nav，否则NCX；借用出版物及其源，必须先close目录；无目录返回EMPTY。
/// Prefer nav over NCX; borrow publication and its source and close TOC first; EMPTY when absent.
pn_status_t pn_toc_open(pn_toc_t *toc,pn_pool_t *pool,pn_epub_t *epub,const uint8_t salt[16]);
/// 验证媒体后返回数量，错误输出不变。/ Validate media before returning count, preserving output on errors.
pn_status_t pn_toc_count(pn_toc_t *toc,size_t *count);
/// 按文件先序遍历顺序复制条目，越界EMPTY，错误输出不变。/ Copy entries in document preorder, EMPTY beyond the end, preserving output on errors.
pn_status_t pn_toc_get(pn_toc_t *toc,size_t index,pn_toc_entry_t *entry);
/// 幂等释放，不关闭借用的对象。/ Idempotently free without closing borrowed objects.
void pn_toc_close(pn_toc_t *toc);
