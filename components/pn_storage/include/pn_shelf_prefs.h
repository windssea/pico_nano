/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 书架外观的独立内部A/B记录。/ Separate internal A/B record of shelf appearance.
 */
#pragma once
#include "pn_journal.h"
/// 静态灰阶书架样式。/ Static grayscale shelf styles.
typedef enum {PN_SHELF_DEFAULT=0,PN_SHELF_MINIMAL,PN_SHELF_WOOD,PN_SHELF_PAPER,PN_SHELF_STYLE_COUNT} pn_shelf_style_t;
typedef struct {
    uint8_t columns; ///< 每行3或4本 / Three or four books per row
    uint8_t style; ///< pn_shelf_style_t样式 / A pn_shelf_style_t style
} pn_shelf_prefs_t;
/// 默认3列与默认风格。/ Default three columns and default style.
pn_shelf_prefs_t pn_shelf_prefs_default(void);
/// 绑定root下shelf.a/b，不建目录。/ Bind shelf.a/b under root without making directories.
pn_status_t pn_shelf_prefs_files(pn_journal_files_t *,pn_media_t *,const pn_media_lease_t *,const char *root,pn_journal_io_t *);
/// 无记录EMPTY，错误保留输出。/ EMPTY without a record; errors preserve output.
pn_status_t pn_shelf_prefs_load(const pn_journal_io_t *,pn_shelf_prefs_t *);
/// 保存合法外观；失败可能已提交。/ Save valid appearance; failures may have committed.
pn_status_t pn_shelf_prefs_save(const pn_journal_io_t *,const pn_shelf_prefs_t *);
