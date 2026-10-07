/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界XML流与UTF8命名空间回调，消费完整输入后才成功。
 * English: bounded XML streams with UTF-8 namespace callbacks; success requires complete input.
 * 冻结：不获取外部实体，禁止内部DTD；必须由调用方注入可信随机盐。
 * Frozen: no external entity fetches or internal DTD; callers must inject trusted random salt.
 */
#pragma once
#include "pn_alloc.h"
#include "pn_types.h"
#define PN_XML_DEPTH_MAX 64
typedef struct {
    void *ctx; ///< 输入owner / Input owner
    pn_status_t (*read)(void *,uint8_t *,size_t,size_t *); ///< PN_EMPTY仅在完整验证EOF / PN_EMPTY only at fully verified EOF
} pn_xml_input_t;
typedef struct {
    pn_status_t (*start)(void *,const char *,const char *const *); ///< 展开名URI|local及成对属性，仅回调期有效 / Expanded URI|local names and paired attributes, callback lifetime only
    pn_status_t (*end)(void *,const char *); ///< 同样展开的结束名 / Equally expanded end name
    pn_status_t (*text)(void *,const char *,size_t); ///< UTF8片段，非NUL终止 / UTF-8 fragment, not NUL-terminated
} pn_xml_hooks_t;
/// 串行消费最大32MiB/深度64/属性64，分配全走pool；回调结果为暂定，最终失败须丢弃模型。
/// Serialized consumption up to 32 MiB/depth 64/64 attributes, allocating only from pool; discard provisional callback models on final failure.
pn_status_t pn_xml_parse(pn_pool_t *pool,const pn_xml_input_t *input,const pn_xml_hooks_t *hooks,void *ctx,uint64_t byte_limit,const uint8_t salt[16]);
