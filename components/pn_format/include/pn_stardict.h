/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：离线 StarDict 词典引擎：解析 .ifo，磁盘二分检索 .idx（不整表载入内存），支持 .dict 与 DictZip（.dict.dz）按块随机解压，并支持多词典与中英文词形候选回退。
 * English: Offline StarDict dictionary engine: parses .ifo, performs on-disk binary search over .idx (never loading full indices into RAM), supports plain .dict and DictZip (.dict.dz) random-access chunk decompression, multi-dictionary lookup, and English/Chinese candidate fallback.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pn_alloc.h"
#include "pn_types.h"

#define PN_DICT_MAX 16u ///< 词典目录上限 / Maximum dictionaries in catalog
#define PN_DICT_NAME_MAX 96u ///< 词典名称上限 / Maximum dictionary title bytes
#define PN_DICT_PATH_MAX 384u ///< 词典文件路径上限 / Maximum dictionary file path bytes
#define PN_DICT_WORD_MAX 128u ///< 词条上限 / Maximum headword bytes
#define PN_DICT_DEF_MAX 4096u ///< 单条释义输出上限 / Maximum formatted UTF-8 definition bytes

/// 单本 StarDict 词典元数据。/ Metadata for one StarDict dictionary.
typedef struct {
    char name[PN_DICT_NAME_MAX]; ///< 词典名称（来自 .ifo bookname 或文件名）/ Book name from .ifo or filename stem
    char ifo_path[PN_DICT_PATH_MAX]; ///< .ifo 完整路径 / Full .ifo path
    char idx_path[PN_DICT_PATH_MAX]; ///< .idx 完整路径 / Full .idx path
    char dict_path[PN_DICT_PATH_MAX]; ///< .dict 或 .dict.dz 完整路径 / Full .dict or .dict.dz path
    char same_type_seq[16]; ///< sametypesequence 字段，可为空 / Optional sametypesequence field
    uint32_t word_count; ///< 词条总数 / Total word count
    uint64_t idx_file_size; ///< .idx 字节长度 / .idx byte size
    uint8_t offset_bits; ///< 索引偏移位宽：32 或 64 / Index offset bit width: 32 or 64
    bool compressed; ///< 是否 DictZip 压缩（.dict.dz）/ True when backed by .dict.dz
} pn_stardict_info_t;

/// 可用词典列表。/ Catalog of discovered StarDict dictionaries.
typedef struct {
    pn_stardict_info_t items[PN_DICT_MAX]; ///< 词典列表（按名称排序）/ Sorted dictionary entries
    size_t count; ///< 实际词典数 / Number of valid dictionaries
} pn_stardict_catalog_t;

/// 单次查词命中结果。/ Result of a successful dictionary lookup.
typedef struct {
    size_t dict_index; ///< 命中词典在目录中的下标 / Index into catalog->items
    char dict_name[PN_DICT_NAME_MAX]; ///< 命中词典名 / Name of matching dictionary
    char matched_word[PN_DICT_WORD_MAX]; ///< 实际命中词头（可能经词形还原或中文前缀截断）/ Actual matched headword
    char definition[PN_DICT_DEF_MAX]; ///< 清洗后的可读 UTF-8 释义 / Cleaned readable UTF-8 definition text
    size_t def_len; ///< 释义有效字节数 / Length of definition in bytes
} pn_stardict_hit_t;

/// 解析单个 .ifo 文件并校验配套 .idx 与 .dict/.dict.dz 存在。
/// Parse one .ifo file and verify its companion .idx and .dict/.dict.dz files exist.
pn_status_t pn_stardict_parse_ifo(const char *ifo_path, pn_stardict_info_t *out);

/// 扫描词典目录（含一级子目录）收集有效 StarDict 词典，按名称排序；目录不存在时返回 PN_OK 且 count=0。
/// Scan dicts_dir (and 1-level subdirectories) for valid StarDict dictionaries, sorted by name; missing dir yields PN_OK with count=0.
pn_status_t pn_stardict_scan(const char *dicts_dir, pn_stardict_catalog_t *out);

/// 在单本词典中精确检索（大小写不敏感，优先大小写完全一致），读取并清洗释义。
/// Look up one word in a single dictionary (case-insensitive, preferring exact case), decompressing and cleaning the definition.
pn_status_t pn_stardict_lookup_one(pn_pool_t *pool, const pn_stardict_info_t *dict, const char *word,
    char *matched_out, size_t matched_cap, char *def_out, size_t def_cap, size_t *def_len_out);

/// 对单个查询词执行智能检索（原文 → 英文词形还原 / 中文前缀回退），按序在单本词典或全部词典中查找。
/// Smart lookup (exact -> English stem fallback / Chinese prefix fallback); pass dict_filter < catalog->count to query one dict, or SIZE_MAX for all dicts.
pn_status_t pn_stardict_query(pn_pool_t *pool, const pn_stardict_catalog_t *catalog, const char *raw_query,
    size_t dict_filter, pn_stardict_hit_t *hit_out);

/// 从正文某行码点序列与点击下标提取查词候选列表（英文自动扩选整词，中文按 4/3/2/前移2/1 字生成候选）。
/// Extract lookup candidates from a line of Unicode codepoints and tapped index (expands full English words or generates Chinese multi-char candidates).
size_t pn_stardict_extract_candidates(const uint32_t *line_cps, size_t line_len, size_t tap_index,
    char out[][PN_DICT_WORD_MAX], size_t max_out);

/// StarDict 规范排序比较：先 ASCII 忽略大小写，再按字节序决胜。
/// Standard StarDict sort comparison: ASCII case-insensitive first, then bytewise strcmp tie-breaker.
int pn_stardict_strcmp(const char *a, const char *b);
