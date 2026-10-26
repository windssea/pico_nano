/* SPDX-License-Identifier: Apache-2.0
 * 中文：完整来源绑定的启动结构缓存，不保存页码或图片。/ English: full-source-bound startup structures, without pages or images. */
#pragma once
#include "pn_epub.h"
#include "pn_identity.h"
typedef struct {void *impl;} pn_bootstrap_t; ///< 可选空闲写入，初始NULL / Optional idle writer, initially NULL
/// 只检查内部可信根是否可用，用于安排完整源复核；不授予读取缓存资格。/ Check for an internal root to schedule full source verification; never cache-reading authority.
bool pn_bootstrap_hint(const char *,const pn_book_id_t *);
/// 仅在完整原书SHA已验证后调用；完整包SHA/边界/版本通过才成功，任何失败输出均清空。/ Call only after full original SHA verification; require whole-packet SHA, bounds and version, clearing outputs on every failure.
pn_status_t pn_bootstrap_load(pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const pn_text_source_t *,const pn_book_id_t *,const char *,const char *,pn_zip_t *,pn_epub_t *);
/// 借用完整验证模型与稳定源到完成/放弃；复用原配额，准入失败可跳过。/ Borrow fully verified models and stable source until finish/abort; reuse original quotas and skip denied admission.
pn_status_t pn_bootstrap_begin(pn_bootstrap_t *,pn_pool_t *,pn_media_t *,const pn_media_lease_t *,pn_media_t *,const pn_media_lease_t *,const pn_text_source_t *,const pn_book_id_t *,const char *,const char *,pn_zip_t *,pn_epub_t *);
/// 每步至多一个结构记录，完整读回比较/SHA及两个事务分阶段。/ At most one structure record per step, with full comparison/SHA readback and two staged commits.
pn_status_t pn_bootstrap_step(pn_bootstrap_t *);
bool pn_bootstrap_pending(const pn_bootstrap_t *); ///< 是否借用模型 / Whether models are borrowed
/// 取消时仅清理当前有效代自有暂存，释放全部池与租约。/ Clean only owned staging in the current valid epoch, releasing all pool memory and leases.
void pn_bootstrap_abort(pn_bootstrap_t *);
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#if CONFIG_PN_TEST_CONSOLE
/// 仅诊断owner设置读端故障：0正常、1缺失、2内容损坏；不修改任何TF文件。/ Diagnostic owner only: inject read-side normal/missing/corrupt modes without modifying TF files.
void pn_bootstrap_diag_fault(unsigned);
#endif
#endif
