/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：固定分块DEFLATE压缩固件字体的按需解压与LRU块缓存读取接口。
 * English: On-demand decompression and LRU block-cache reader for fixed-block DEFLATE firmware fonts.
 */
#pragma once
#include "pn_types.h"
#include <stddef.h>
#include <stdint.h>

#define PN_FONT_ZBANK_BLOCK_MAX 32768u ///< 支持的最大单块解压尺寸 / Maximum uncompressed block size

/// 固定块原始DEFLATE压缩库描述。/ Descriptor of a fixed-block raw-DEFLATE compressed bank.
typedef struct {
    const uint8_t *zbytes;    ///< 连续压缩块数据 / Concatenated compressed block payload
    const uint32_t *zoffsets; ///< 长度为blocks+1的压缩偏移表 / Compressed offset table of length blocks + 1
    size_t blocks;            ///< 总块数 / Total block count
    size_t block_size;        ///< 单块原始字节数（2的幂且<=PN_FONT_ZBANK_BLOCK_MAX）/ Uncompressed block size (power of two, <= max)
    size_t size;              ///< 完整原始字节长度 / Complete original byte length
} pn_font_zbank_t;

/// 按原始偏移读取精确字节，跨块与EOF安全；owner串行调用。/ Read exact original bytes across blocks and EOF; serialized owner calls.
pn_status_t pn_font_zbank_read(const pn_font_zbank_t *bank, uint64_t offset, uint8_t *out, size_t cap, size_t *n);

/// 读取内置常驻中文阅读字体原始字节。/ Read exact original bytes of the resident CJK reading font.
pn_status_t pn_reading_font_read(uint64_t offset, uint8_t *out, size_t cap, size_t *n);
