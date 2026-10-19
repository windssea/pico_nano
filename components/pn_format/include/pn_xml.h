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
typedef struct {
    void *impl; ///< 单owner解析状态，零初始化且活动时不可移动 / Single-owner parser state; zero-initialize and never move while active
} pn_xml_job_t;
/// 复制输入/回调端口，借用其ctx；ctx须活到close，回调结果在完整EOF前仍暂定。
/// Copy input/hook ports and borrow their contexts until close; callback results remain provisional until verified EOF.
pn_status_t pn_xml_job_begin(pn_xml_job_t *,pn_pool_t *,const pn_xml_input_t *,const pn_xml_hooks_t *,void *,uint64_t,const uint8_t[16]);
/// 每次至多读取并解析4KiB；BUSY且impl非空时待续，终态自动释放；输入前取消可原地暂停。
/// Read/parse at most 4 KiB per step; continue on BUSY with a live impl, terminal results release state, and pre-read cancellation pauses in place.
pn_status_t pn_xml_job_step(pn_xml_job_t *);
/// 同step，仅标记可重试的原始词法错误，不将CRC/输入/回调错误当语法。/ Like step, marking retryable lexical errors only, never CRC/input/hook errors.
pn_status_t pn_xml_job_step_diagnostic(pn_xml_job_t *,bool *syntax);
/// 丢弃未验证的解析状态，不再读取源；调用方同时丢弃暂定模型。/ Discard parser state without further source reads; callers also discard provisional models.
void pn_xml_job_close(pn_xml_job_t *);
/// 串行消费最大32MiB/深度64/属性64，分配全走pool；回调结果为暂定，最终失败须丢弃模型。
/// Serialized consumption up to 32 MiB/depth 64/64 attributes, allocating only from pool; discard provisional callback models on final failure.
pn_status_t pn_xml_parse(pn_pool_t *pool,const pn_xml_input_t *input,const pn_xml_hooks_t *hooks,void *ctx,uint64_t byte_limit,const uint8_t salt[16]);
/// 与parse相同，仅将原始XML词法/实体/匹配错误标为syntax；输入/回调/CRC/内存错误不标记。
/// Same as parse, marking only XML lexical/entity/tag-match errors as syntax, never input/hook/CRC/allocation failures.
pn_status_t pn_xml_parse_diagnostic(pn_pool_t *,const pn_xml_input_t *,const pn_xml_hooks_t *,void *,uint64_t,const uint8_t[16],bool *syntax);
