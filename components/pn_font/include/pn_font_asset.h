/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：稳定地址的候选字体资源，持有源租约、文件与引擎。
 * English: stable-address candidate font resources owning source leases, files and engines.
 * 冻结：不移动活动源，不写字体或选择记录。
 * Frozen: never move live sources or write font/selection records.
 */
#pragma once
#include "pn_font.h"
#include "pn_font_preferences.h"
#include "pn_text_file.h"
/// 打开并核对选定字体，摘要留在实际消费源；失败关闭文件且不改source，lease由调用方持有。/ Open and verify a selected face, retaining its digest in the consumed source; close on failure and preserve source; caller owns lease.
pn_status_t pn_font_reference_open_file(pn_pool_t *,const pn_font_reference_t *,pn_media_t *,const pn_media_lease_t *,pn_text_file_t *,pn_text_source_t *);
typedef struct {void *impl;} pn_font_asset_t; ///< 初始NULL，活动对象不可复制 / Initially NULL; never copy live objects
/// guard保持有效，expected_size=0表示未知；借用media/pool并自持READ租约。
/// Keep guard valid, zero expected_size means unknown; borrow media/pool and own a READ lease.
pn_status_t pn_font_asset_open(pn_font_asset_t *,pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const char *,uint64_t,int);
/// 释放引擎/文件/自持租约，不释放guard，幂等。/ Release engine/file/owned lease without releasing guard, idempotently.
pn_status_t pn_font_asset_close(pn_font_asset_t *);
/// 借用字体可调字号和画字，不能自行关闭，寿命到asset关闭。
/// Borrow for size/drawing, never close directly; lifetime ends when the asset closes.
pn_font_t *pn_font_asset_font(pn_font_asset_t *);
/// 复制已校验引用/信息，至少一个输出非NULL；媒体失效时输出不变。
/// Copy verified reference/info, requiring an output; preserve outputs on media loss.
pn_status_t pn_font_asset_details(const pn_font_asset_t *,pn_font_reference_t *,pn_font_info_t *);
/// 至多128个Unicode样例，检查字形映射及实际光栅，错误不改missing。
/// At most 128 Unicode samples checking mapping and actual rasterization; preserve missing on errors.
pn_status_t pn_font_asset_sample(pn_font_asset_t *,const uint32_t *,size_t,unsigned *missing);

/// 释放临时引擎，保留源/租约/引用；ensure可重开。/ Release temporary engine while retaining source/lease/reference; ensure reopens it.
void pn_font_asset_suspend(pn_font_asset_t *);
/// 确保像素尺寸并返回借用字体，错误保持输出。/ Ensure pixel size and return borrowed font, preserving output on error.
pn_status_t pn_font_asset_ensure(pn_font_asset_t *,int,pn_font_t **);
/// 复制借用源，寿命到asset关闭，不释放它。/ Copy borrowed source valid until asset closes, never release it.
pn_status_t pn_font_asset_source(const pn_font_asset_t *,pn_text_source_t *);
/// 校验保留的已验证文件，不重开引擎或改变字号。/ Verify the retained validated file without reopening the engine or changing size.
pn_status_t pn_font_asset_verify_retained(const pn_font_asset_t *);

/// 只读捕获启动字体引用，PC解析绝对路径，错误保持输出。/ Read-only boot-font reference capture; PC resolves absolute path, preserving output on error.
pn_status_t pn_font_reference_capture(pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const char *,pn_font_reference_t *);
