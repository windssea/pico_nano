/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：轻量离线全拼中文输入法查表接口；静态只读Flash常量，无堆分配。
 * English: Lightweight offline full-pinyin Chinese IME lookup API; const Flash tables, zero heap allocation.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// 判断给定小写拼音串是否为合法音节或音节前缀。
/// Return true when the lowercase pinyin string is a valid syllable or prefix of one.
bool pn_pinyin_ime_has_prefix(const char *pinyin);

/// 按全拼（无精确匹配时按前缀）查询候选汉字；从offset开始写入最多cap个UTF-8单字到out，并可选返回候选总数。
/// Look up candidate hanzi by exact syllable (falling back to prefix matches); writes up to cap UTF-8 chars from offset into out and optionally reports the total count.
size_t pn_pinyin_ime_lookup(const char *pinyin,size_t offset,char out[][5],size_t cap,size_t *total_out);

#ifdef __cplusplus
}
#endif
