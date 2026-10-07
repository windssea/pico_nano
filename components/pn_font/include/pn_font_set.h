/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：完整正文/备用字体组，资源地址稳定，所有权显式转移。
 * English: complete primary/fallback sets with stable resources and explicit ownership transfer.
 * 冻结：不保存选择，不移动文件源；仅EMPTY回退。
 * Frozen: no selection persistence or source moves; fallback on EMPTY only.
 */
#pragma once
#include "pn_font_asset.h"
#include "pn_font_chain.h"
typedef struct {void *impl;} pn_font_set_t; ///< 初始NULL，不可复制活动组 / Initially NULL, never copy live sets
/// 校验所有引用身份并装配，prefs必须为明确选择。/ Verify all identities and wire concrete preferences.
pn_status_t pn_font_set_open(pn_font_set_t *,pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const pn_font_preferences_t *,int);
/// 关闭所有资源，幂等。/ Close all resources, idempotently.
void pn_font_set_close(pn_font_set_t *);
/// 关闭临时引擎但保持源和租约，用于大图前。/ Close temporary engines while retaining sources/leases before large images.
void pn_font_set_suspend(pn_font_set_t *);
/// 确保字号和借用指针，错误不改输出。/ Ensure size/borrowed pointers, preserving output on error.
pn_status_t pn_font_set_ensure(pn_font_set_t *,int,pn_font_t **,pn_font_chain_t **);
/// 借用主源，不能关闭或移动它。/ Borrow primary source without closing or moving it.
pn_status_t pn_font_set_source(const pn_font_set_t *,pn_text_source_t *);
/// 显式移交到空对象，源地址不变。/ Explicitly transfer to an empty object without moving sources.
pn_status_t pn_font_set_move(pn_font_set_t *to,pn_font_set_t *from);
