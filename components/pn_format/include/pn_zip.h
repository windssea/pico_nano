/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界只读ZIP目录与资源流，源由调用方保活。
 * English: bounded read-only ZIP directory and resource streams, borrowing caller-owned sources.
 * 冻结：不写/解包到文件；未知ZIP特性拒绝，CRC未通过不称资源完整。
 * Frozen: no file writes/extraction; reject unknown ZIP features and never claim completeness before CRC verification.
 */
#pragma once
#include "pn_text.h"
#include "pn_alloc.h"
#define PN_ZIP_PATH_MAX 1024
#define PN_ZIP_ENTRIES_MAX 32768
typedef struct {void *impl;} pn_zip_t; ///< 初始NULL，不可移动 / Initially NULL, never move
typedef struct {void *impl;} pn_zip_stream_t; ///< 初始NULL / Initially NULL
typedef struct {
    char path[PN_ZIP_PATH_MAX]; ///< ZIP内精确UTF8路径 / Exact UTF-8 ZIP path
    uint32_t packed,unpacked,local_offset; ///< 压缩/原长/本地头偏移 / Packed/unpacked sizes and local-header offset
    uint16_t method,flags; ///< 存储0/deflate8与标志 / Stored zero/deflate eight and flags
    bool directory; ///< 目录，不可正文流打开 / Directory, not readable as content
} pn_zip_info_t;
/// 验证全部中央/本地头及重叠，不解压整书；source含validate取消/介质错误。
/// Validate all central/local headers and overlap without inflating the book; source validate supplies cancellation/media errors.
pn_status_t pn_zip_open(pn_zip_t *zip,pn_pool_t *pool,const pn_text_source_t *source);
typedef struct {void *impl;} pn_zip_scan_t; ///< 分步打开句柄，初始NULL / Staged-open handle, initially NULL
/// 分步打开：先校验EOCD/中央目录；之后把整文件顺序读到的字节交给feed，本地头/描述符在流过时校验，免去逐条随机读。
/// Staged open: validate EOCD/central directory first; then feed bytes read sequentially across the file so local headers/descriptors are checked as they stream past, avoiding per-entry random reads.
pn_status_t pn_zip_scan_begin(pn_zip_scan_t *scan,pn_pool_t *pool,const pn_text_source_t *source);
/// 按偏移递增喂入源字节（可跳过）；不读源、不报错，错误留到finish。/ Feed source bytes in increasing offsets (gaps allowed); never reads the source or reports errors until finish.
void pn_zip_scan_feed(pn_zip_scan_t *scan,uint64_t offset,const uint8_t *bytes,size_t size);
/// 未截取的条目随机读校验，再查重叠/重名；与pn_zip_open相同的完整校验，成功才设置zip。scan随后为空。
/// Validate uncaptured entries with random reads, then overlap/duplicates; the same full validation as pn_zip_open, setting zip only on success. scan is empty afterwards.
pn_status_t pn_zip_scan_finish(pn_zip_scan_t *scan,pn_zip_t *zip);
/// 释放未完成的分步打开；幂等。/ Release an unfinished staged open idempotently.
void pn_zip_scan_abort(pn_zip_scan_t *scan);
/// 有流仍活动返回BUSY；不关闭借用的source。/ BUSY with active streams; never close borrowed source.
pn_status_t pn_zip_close(pn_zip_t *zip);
/// 验证目录数量或0。/ Validated entry count or zero.
size_t pn_zip_count(const pn_zip_t *zip);
/// 只验证源的介质代次，不为句柄校验读取条目名称。/ Validate source media generation without reading entry names for a handle check.
pn_status_t pn_zip_validate(pn_zip_t *zip);
/// 精确查找，EMPTY为不存在；错误不改index。/ Exact lookup, EMPTY if absent; preserve index on errors.
pn_status_t pn_zip_find(pn_zip_t *zip,const char *path,uint32_t *index);
/// 返回当前代条目，错误不改输出。/ Get current-generation entry, preserving output on errors.
pn_status_t pn_zip_info(pn_zip_t *zip,uint32_t index,pn_zip_info_t *info);
/// owner串行使用，同一源读取可交错；每流约40KiB，不整条资源分配。
/// Serialized owner calls may interleave source reads; about 40 KiB per stream, without whole-resource allocation.
pn_status_t pn_zip_stream_open(pn_zip_t *zip,uint32_t index,pn_zip_stream_t *stream);
/// 每次最多8192byte；片段未校验完整，只有EOF校验成功才verified。
/// At most 8192 bytes per call; fragments are provisional until verified at successful EOF.
pn_status_t pn_zip_stream_read(pn_zip_stream_t *stream,uint8_t *out,size_t capacity,size_t *size);
/// CRC/长度/压缩结束均已验证。/ CRC, length and compressed end have all been verified.
bool pn_zip_stream_verified(const pn_zip_stream_t *stream);
/// 释放流与借用引用；幂等。/ Release stream and borrowed reference idempotently.
void pn_zip_stream_close(pn_zip_stream_t *stream);
