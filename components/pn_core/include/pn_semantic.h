/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享语义位置契约，不依赖解析器、存储或绘制。
 * English: shared semantic-location contracts independent of parsing, storage and rendering.
 * 冻结：版本1位置不是字节/页号，不对元组数值排序。
 * Frozen: version-one positions are neither bytes nor page numbers; never sort locator tuples numerically.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define PN_EPUB_LOCATION_PATH_MAX 1024
#define PN_XHTML_LOCATOR_VERSION 1
typedef enum {
    PN_XHTML_ELEMENT=0, ///< 元素边界 / Element boundary
    PN_XHTML_TEXT_POSITION=1 ///< 解码文本位置 / Decoded text position
} pn_xhtml_position_kind_t;
typedef struct {
    uint64_t element; ///< 全文元素先序号，从1起 / One-based document element preorder ordinal
    uint64_t offset; ///< 解码文本区间内的Unicode标量偏移 / Unicode scalar offset in a decoded text run
    uint32_t run; ///< 当前元素的直接文本区间号，从0起 / Zero-based direct text-run ordinal within the element
    pn_xhtml_position_kind_t kind; ///< 元素边界或文本位置 / Element boundary or text position
} pn_xhtml_position_t;
typedef struct {
    char path[PN_EPUB_LOCATION_PATH_MAX]; ///< 出版物规范章节路径 / Canonical publication chapter path
    pn_xhtml_position_t position; ///< 资源内语义位置 / Resource-local semantic position
    unsigned version; ///< 必须locator v1 / Must use locator v1
    bool chapter_start; ///< 章节首请求，position须全零 / Chapter-start request requiring zero position
} pn_epub_location_t;
