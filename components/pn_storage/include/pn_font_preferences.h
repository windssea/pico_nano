/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：全局与逐书字体选择、内容身份和继承记录。
 * English: global and per-book font selection, content identity and inheritance records.
 * 冻结：不猜测损坏配置，不写字体文件或阅读进度。
 * Frozen: no guesses through damaged settings, font-file writes or progress changes.
 */
#pragma once
#include "pn_blob.h"
#include "pn_identity.h"
#define PN_FONT_REFERENCE_PATH_MAX 1024
#define PN_FONT_REFERENCE_MAX_BYTES (32u*1024u*1024u)
typedef enum {
    PN_FONT_RESIDENT=0, ///< 常驻子集 / Resident subset
    PN_FONT_FILE=1 ///< 完整文件引用 / Complete-file reference
} pn_font_reference_kind_t;
typedef struct {
    pn_font_reference_kind_t kind; ///< 常驻或完整文件 / Resident or complete file
    char path[PN_FONT_REFERENCE_PATH_MAX]; ///< 规范绝对UTF-8路径 / Canonical absolute UTF-8 path
    uint64_t size; ///< 预期原文件大小 / Expected original-file size
    pn_book_id_t identity; ///< 完整文件SHA，不用于认证 / Complete-file SHA, not authentication
} pn_font_reference_t;
typedef struct {
    bool inherit; ///< 逐书继承全局，其他字段必须为空 / Per-book global inheritance; other fields must be empty
    pn_font_reference_t primary; ///< 正文字体 / Primary font
    pn_font_reference_t fallback; ///< 备用；常驻代表没有额外备用文件 / Fallback; resident means no additional file
} pn_font_preferences_t;
/// 验证范围与字段；全局不允许继承。/ Validate ranges and fields; global records cannot inherit.
pn_status_t pn_font_preferences_validate(const pn_font_preferences_t *,bool global);
/// book为NULL表示全局，非NULL表示逐书；同步双槽写入。/ NULL book means global, otherwise per-book; synchronized dual-slot write.
pn_status_t pn_font_preferences_save(const pn_journal_io_t *,pn_pool_t *,const pn_book_id_t *,const pn_font_preferences_t *);
/// 错误保持输出；未知字段/版本拒绝。/ Preserve output on errors; reject unknown fields/versions.
pn_status_t pn_font_preferences_load(const pn_journal_io_t *,pn_pool_t *,const pn_book_id_t *,pn_font_preferences_t *);
/// 逐书优先，缺失/明确继承才读全局；双缺失EMPTY，错误不回退。
/// Prefer per-book; read global only if absent/explicitly inherited; both absent EMPTY, no error fallback.
pn_status_t pn_font_preferences_resolve(const pn_journal_io_t *global,const pn_journal_io_t *per_book,pn_pool_t *,const pn_book_id_t *,pn_font_preferences_t *,bool *from_book);
/// 绑定既有root的全局或逐书路径，不创建目录。/ Bind global/per-book paths under existing root without creating directories.
pn_status_t pn_font_preferences_files(pn_journal_files_t *,pn_media_t *,const pn_media_lease_t *,const char *,const pn_book_id_t *,pn_journal_io_t *);
/// 校验预期大小和完整SHA；不代替字体引擎校验。/ Verify expected size and complete SHA; does not replace font-engine validation.
pn_status_t pn_font_reference_verify(const pn_font_reference_t *,pn_media_t *,const pn_media_lease_t *);
