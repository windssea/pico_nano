/* SPDX-License-Identifier: Apache-2.0
 * 中文：单个已保存会话的有界热保留，所有权转移不移动源对象。
 * English: bounded retention of one saved session, transferring ownership without moving sources. */
#pragma once
#include "pn_epub_app.h"
#define PN_HOT_SESSION_MS 120000u ///< 两分钟无命中后释放 / Release after two minutes without a hit
#define PN_HOT_HEADROOM (2u*1024u*1024u) ///< 给管理页保留的池余量 / Pool headroom reserved for management
typedef struct {
    pn_reader_app_t txt;pn_epub_app_t epub; ///< 唯一保留对象，原wrapper清空 / Sole retained object; original wrapper is cleared
    char path[PN_CATALOG_PATH_MAX]; ///< 选择路径，不能作为内容身份 / Selection path, never a content identity
    pn_book_id_t book; ///< 原验证句柄的完整内容身份 / Complete identity of the original verified handle
    pn_media_t *media;uint64_t epoch,policy,at; ///< 挂载/配置代次与保留时刻 / Mount/config generations and retention time
    size_t hits,parks; ///< 只读诊断计数 / Diagnostic counters
} pn_hot_session_t;
/// 保存挂起后移交到空缓存，低预算EMPTY保留原对象供正常关闭；不得复制活动资源。/ Save/suspend then transfer to an empty cache; EMPTY on low budgets leaves the original for ordinary close; never copy live resources.
pn_status_t pn_hot_session_park(pn_hot_session_t *,pn_pool_t *,pn_media_t *,const char *,uint64_t,uint64_t,pn_reader_app_t *,pn_epub_app_t *);
/// 同源/配置/强身份才热恢复；成功才移交，显示失败仍保留缓存供重试。/ Resume only matching source/config/strong identity; transfer on success and retain the cache after display failure.
pn_status_t pn_hot_session_resume(pn_hot_session_t *,pn_media_t *,const char *,pn_book_format_t,const pn_book_id_t *,uint64_t,uint64_t,pn_reader_app_t *,pn_epub_app_t *,pn_reader_present_fn,void *);
/// 释放已保存对象，写/USB/换卡/系统字体关闭前必须调用；幂等。/ Release saved objects before writes/USB/card changes/system-font closure, idempotently.
pn_status_t pn_hot_session_drop(pn_hot_session_t *);
/// 到期后释放；无任务EMPTY。/ Release when expired; EMPTY without work.
pn_status_t pn_hot_session_expire(pn_hot_session_t *,uint64_t);
