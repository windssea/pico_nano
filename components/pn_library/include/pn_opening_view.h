/* SPDX-License-Identifier: Apache-2.0
 * 中文：一次开书的展示快照；不参与书籍身份或租约验证。
 * English: presentation snapshot for one opening attempt, never book identity or lease validation.
 */
#pragma once
#include "pn_cover.h"

typedef struct {
    pn_book_preview_t *preview; ///< 可选且自有的原生缩略图/元数据，计入原UI额度 / Optional owned native thumbnail/metadata, charged to the existing UI budget
    pn_book_id_t path; ///< 完整路径字符串的SHA，仅区分展示目标 / Full path-string SHA, distinguishing presentation targets only
    pn_book_format_t format; ///< 展示目标格式 / Presentation target format
    pn_status_t lookup_status; ///< 首次封面查询结果，不是原书校验结果 / First cover lookup result, never source authentication
    bool resolved; ///< 缺文件或缺内存的回退也已固定 / Missing-file and low-memory fallbacks are frozen too
} pn_opening_view_t;

/// 首次复制已有的已校验预览；同一目标的重试不重查TF。失败只固定展示回退，取消不提交；不授权正文访问。
/// Copy an already-checked preview once; retries for the same target never requery TF. Failures freeze visual fallback, cancellation never commits, and no body access is authorized.
pn_status_t pn_opening_view_resolve(pn_opening_view_t *,pn_pool_t *,pn_media_t *,const char *cache_dir,const pn_catalog_item_t *,const pn_shelf_covers_t *);
/// 仅复制路径匹配的既有内存展示，不读TF/取得租约；可显示旧封面，但绝不授予正文来源资格。/ Copy only matching existing RAM artwork without TF reads or leases; old cover art never grants body-source authority.
pn_status_t pn_opening_view_resolve_resident(pn_opening_view_t *,pn_pool_t *,const pn_catalog_item_t *,const pn_shelf_covers_t *);
/// 成功首屏、取消、离开或新请求时释放；不改原书、进度、租约或缓存文件。/ Release on confirmed first page, cancellation, departure or a new request; never modify source, progress, leases or cache files.
void pn_opening_view_close(pn_opening_view_t *);
