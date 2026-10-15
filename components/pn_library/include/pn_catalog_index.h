/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界批次构建的可删除书库索引。/ English: disposable catalog index built with bounded batches. */
#pragma once
#include "pn_catalog.h"
typedef struct {void *impl;} pn_catalog_index_t; ///< NULL初始化，单owner / Initialize NULL, one owner
typedef struct {
    uint64_t hits,records_read,builds; ///< 命中、记录读取及构建次数 / Hits, record reads and builds
} pn_catalog_index_stats_t;
/// 绑定书库及专用缓存目录，不扫描或创建文件。/ Bind the library and cache folder without scans or file creation.
pn_status_t pn_catalog_index_init(pn_catalog_index_t *,pn_pool_t *,pn_media_t *,const char *,const char *);
/// 只读取页；缺失/失效/损坏返回EMPTY供旧扫描回退，保留page。/ Read-only paging; EMPTY on absent/stale/corrupt caches for scan fallback, preserving page.
pn_status_t pn_catalog_index_page(pn_catalog_index_t *,const pn_media_lease_t *,const char *,bool,char,const char *,size_t,pn_catalog_page_t *);
/// 空闲时取得缓存写租约，与只读书源/字体共存，其他写者和USB仍互斥；64记录排序批次及两路外部归并，可取消，不改原书。
/// Idle cache-write lease coexists with read-only books/fonts while excluding other writers and USB; cancellable 64-record runs and two-way merges preserve books.
pn_status_t pn_catalog_index_build(pn_catalog_index_t *);
/// 受控安装/删除后显式失效；外部目录变化也自动失效。/ Explicit invalidation after controlled installs/deletes; external directory changes also invalidate.
void pn_catalog_index_invalidate(pn_catalog_index_t *);
/// 读取软件计数，无面板性能含义。/ Software counters without panel-performance meaning.
void pn_catalog_index_stats(const pn_catalog_index_t *,pn_catalog_index_stats_t *);
/// 释放对象，文件留给下次启动；不隐式构建。/ Release the object, retaining files for restart; no implicit build.
void pn_catalog_index_close(pn_catalog_index_t *);
