/* SPDX-License-Identifier: Apache-2.0
 * 中文：外部阅读底图由控制层一次准备，绘制只访问内存。 / English: the controller prepares external paper once; painting accesses memory only. */
#pragma once
#include "pn_wallpaper.h"
#include "pn_style.h"
typedef struct {
    pn_frame_t frame; ///< 当前唯一常驻4bpp底图 / The single resident 4bpp underlay
    pn_book_id_t identity; ///< 已完整验证的源身份 / Fully verified source identity
    char name[PN_BACKGROUND_NAME_MAX]; ///< 已准备的文件名 / Prepared basename
} pn_paper_t;
/// 可信owner配置；PC可用PN_BACKGROUND_DIR/PN_BACKGROUND_CACHE隔离样本。/ Trusted owner configuration; PC can isolate samples with PN_BACKGROUND_DIR/PN_BACKGROUND_CACHE.
const char *pn_paper_directory(void);
const char *pn_paper_cache_directory(void);
/// 空对象准备候选，失败不改变旧对象；builtin无需分配。/ Prepare a candidate in an empty object; failure preserves the old object, built-ins allocate nothing.
pn_status_t pn_paper_open(pn_paper_t *,pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const pn_style_t *);
/// 当前源引用是否已经准备，不读卡。/ Whether this reference is already prepared, without card IO.
bool pn_paper_matches(const pn_paper_t *,const pn_style_t *);
/// 仅恢复已保存但不可用的选择为白底，不改变持久引用。/ Restore an unavailable saved selection on white paper without changing its persistent reference.
void pn_paper_fallback(pn_paper_t *,const pn_style_t *);
/// 纯绘制整帧或逻辑行带；缺失外部资源用白底。/ Pure full-frame or logical-band painting; unavailable external resources use white paper.
pn_status_t pn_paper_draw(const pn_paper_t *,unsigned,pn_frame_t *);
pn_status_t pn_paper_region(const pn_paper_t *,unsigned,pn_frame_t *,int,int);
/// 释放控制层所有资源，可重复。/ Release controller-owned resources, idempotently.
void pn_paper_close(pn_paper_t *);
