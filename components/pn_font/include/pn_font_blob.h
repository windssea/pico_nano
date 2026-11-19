/* SPDX-License-Identifier: Apache-2.0
 * 中文：固件只读字体的零分配分段视图；不是新的字体或持久格式。
 * English: allocation-free span views of firmware fonts, not a new font or persistent format. */
#pragma once
#include "pn_types.h"
#include <stddef.h>
typedef struct {
    uint32_t end; ///< 原始字体内的排他终点 / Exclusive end in the original font
    uint32_t source; ///< 高位选共享库，其余位是库内起点 / High bit selects the shared bank; remaining bits give its offset
} pn_font_span_t;
typedef struct {
    const uint8_t *local,*shared; ///< 不可变Flash字节，shared为NULL时走shared_read / Immutable flash bytes; uses shared_read when shared is NULL
    size_t local_size,shared_size; ///< 两个库的边界 / Bounds of both banks
    const pn_font_span_t *spans; ///< 按原始终点递增的片段 / Spans ordered by original exclusive ends
    size_t count,size; ///< 片段数和完整原字体长度 / Span count and complete original font length
    pn_status_t (*shared_read)(uint64_t offset,uint8_t *out,size_t cap,size_t *n); ///< 可选压缩共享库读取回调 / Optional reader callback for a compressed shared bank
} pn_font_blob_t;
/// 零分配读取完整原始字节流，含跨段/EOF；不访问TF或解压。/ Read exact original bytes across spans/EOF without allocation, TF access or decompression.
pn_status_t pn_font_blob_read(const pn_font_blob_t *,uint64_t,uint8_t *,size_t,size_t *);
