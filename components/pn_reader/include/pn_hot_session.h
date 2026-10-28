/* SPDX-License-Identifier: Apache-2.0
 * 中文：单槽已保存会话或未显示准备，所有权转移不移动源对象。
 * English: one saved session or never-displayed preparation, transferring ownership without moving sources. */
#pragma once
#include "pn_epub_app.h"
#define PN_HOT_SESSION_MS 120000u ///< 两分钟无命中后释放 / Release after two minutes without a hit
#define PN_HOT_HEADROOM (2u*1024u*1024u) ///< 给管理页保留的池余量 / Pool headroom reserved for management
#define PN_HOT_PREPARE_HEADROOM (4u*1024u*1024u) ///< 开始可选冷准备前的实际空闲量 / Actual free space required before optional cold preparation
typedef struct {
    pn_reader_app_t txt;pn_epub_app_t epub; ///< 唯一保留对象，原wrapper清空 / Sole retained object; original wrapper is cleared
    char path[PN_CATALOG_PATH_MAX]; ///< 选择路径，不能作为内容身份 / Selection path, never a content identity
    pn_book_id_t book; ///< 打开进行中为期望身份，就绪后是原句柄完整身份 / Expected identity while opening, complete handle identity once ready
    pn_media_t *media;uint64_t epoch,policy,at; ///< 挂载/配置代次与保留时刻 / Mount/config generations and retention time
    size_t hits,parks; ///< 只读诊断计数 / Diagnostic counters
    bool prepared,tried; ///< 未显示准备与本活动周期已尝试标志 / Never-displayed preparation and attempted activity flags
    uint64_t activity,attempt_epoch,attempt_policy; ///< 一次输入安静周期/媒体/策略代次只尝试一次 / Attempt once per quiet-input activity/media/policy generation
    size_t preparations,takes; ///< 准备成功与移交次数，不是显示提交 / Successful preparations and handoffs, never presentation commits
    pn_pool_t *prepare_pool; ///< 分阶段准备原池，须保活到drop/take / Original staged-preparation pool, alive until drop/take
    bool layout_pending; ///< 书源/字体已完整校验，首屏可在下一轮准备或由点击接续 / Sources/fonts fully verified; first layout may run next tick or on click
    pn_epub_open_job_t opening; ///< 私有部分EPUB准备，不能呈现或当已验证源 / Private partial EPUB preparation, never presentable or a verified source
    bool deferred_failed; ///< 本次保留期内空闲写页索引已失败，不再重试，关闭兜底 / Idle page-index write failed during this retention; no retry, close still writes
    bool expire_failed; ///< 到期必要保存失败后等待显式重试，不反复阻塞空闲轮 / After a failed mandatory expiry save, await explicit retry without blocking every idle tick
} pn_hot_session_t;
/// 首页空闲只读准备已识别的继续阅读书；不显示/写记录，旧缓存pin保护，不足退EMPTY；同activity/epoch仅一次。/ Read-only idle preparation for an identified continue book; no display/record writes, pin existing caches, EMPTY on admission refusal, once per activity/epoch.
pn_status_t pn_hot_session_prepare(pn_hot_session_t *,pn_pool_t *,pn_media_t *,const pn_catalog_item_t *,const char *font,const char *state,int pixels,uint64_t policy,uint64_t now,uint64_t activity);
/// 只完成原书/字体构造与强身份检查，不排首屏；成功即可take，正常OPEN补剩余工作。SHA/构造仍同步。/ Construct and strongly verify book/fonts without first layout; success permits take and normal OPEN finishes remaining work. Hashing/construction remain synchronous.
pn_status_t pn_hot_session_prepare_sources(pn_hot_session_t *,pn_pool_t *,pn_media_t *,const pn_catalog_item_t *,const char *font,const char *state,int pixels,uint64_t policy,uint64_t now,uint64_t activity);
/// 产品分步入口：EPUB保留私有任务供后续source_step；TXT沿原同步源准备，不提前交接未验证内容。/ Product entry: EPUB retains a private task for source_step; TXT keeps synchronous source preparation, never handing off unverified content.
pn_status_t pn_hot_session_prepare_step(pn_hot_session_t *,pn_pool_t *,pn_media_t *,const pn_catalog_item_t *,const char *,const char *,int,uint64_t,uint64_t,uint64_t);
/// 纯查询部分源任务，不能当作可绘制/已验证状态。/ Query a partial source task, never a drawable/verified state.
bool pn_hot_session_source_pending(const pn_hot_session_t *);
/// 纯查询本次选择能否接续部分任务，供控制层决定是否先显示加载反馈。/ Purely match a selection to a partial job so control can show loading feedback before continuing it.
bool pn_hot_session_source_selected(const pn_hot_session_t *,pn_media_t *,const char *,pn_book_format_t,const pn_book_id_t *,uint64_t,uint64_t);
/// 匹配同书后只移交部分EPUB任务给前台逐轮推进，不同步drain、不交出app或写记录。/ Match and transfer a partial EPUB job for foreground polling, without synchronous draining, an app handoff or record writes.
pn_status_t pn_hot_session_take_opening(pn_hot_session_t *,pn_media_t *,const char *,const pn_book_id_t *,uint64_t,uint64_t,pn_epub_open_job_t *);
/// 推进原任务一轮；摘要阶段取消保留已完成块，后续错误按原释放；不显示或保存。/ Advance the original task once; hash cancellation retains completed chunks, later errors use normal release; never present/save.
pn_status_t pn_hot_session_source_step(pn_hot_session_t *,uint64_t);
/// 纯摘要进度查询；任务完成后EMPTY，不替代内容身份。/ Pure hash-progress query; EMPTY after completion, never a content identity.
pn_status_t pn_hot_session_source_progress(const pn_hot_session_t *,uint64_t *,uint64_t *);
/// 纯查询已校验源是否待排版；不做IO。/ Query verified sources awaiting layout, without I/O.
bool pn_hot_session_layout_pending(const pn_hot_session_t *);
/// 下一安静轮准备首屏，保护旧菜单cache；取消保留已验证对象与可用候选，其他错误释放，仍不显示/写记录。/ Prepare first layout next quiet tick with old menu caches protected; cancellation retains verified objects/usable candidates, other errors release them, without presenting/record writes.
pn_status_t pn_hot_session_layout(pn_hot_session_t *,uint64_t now);
/// 仅查询未显示正文是否有可选绘制，不做IO。/ Query optional unshown-body painting without I/O.
bool pn_hot_session_paint_pending(const pn_hot_session_t *);
/// 安静期每步至多8节点，原帧/原池并保留管理余量；不呈现或写记录。/ At most eight nodes per quiet step in the original frame/pool with management headroom; never present or write records.
pn_status_t pn_hot_session_paint(pn_hot_session_t *,uint64_t now,size_t budget);
/// 强校验后移交未显示对象；调用方随后走正常OPEN和真实回执，不提前确认页面。/ Hand off a verified never-displayed object; caller then uses normal OPEN and actual receipts, never confirming early.
pn_status_t pn_hot_session_take_prepared(pn_hot_session_t *,pn_media_t *,const char *,pn_book_format_t,const pn_book_id_t *,uint64_t,uint64_t,pn_reader_app_t *,pn_epub_app_t *);
/// 保存挂起后移交到空缓存，低预算EMPTY保留原对象供正常关闭；不得复制活动资源。/ Save/suspend then transfer to an empty cache; EMPTY on low budgets leaves the original for ordinary close; never copy live resources.
pn_status_t pn_hot_session_park(pn_hot_session_t *,pn_pool_t *,pn_media_t *,const char *,uint64_t,uint64_t,pn_reader_app_t *,pn_epub_app_t *);
/// 同源/配置/强身份才热恢复；成功才移交，显示失败仍保留缓存供重试。/ Resume only matching source/config/strong identity; transfer on success and retain the cache after display failure.
pn_status_t pn_hot_session_resume(pn_hot_session_t *,pn_media_t *,const char *,pn_book_format_t,const pn_book_id_t *,uint64_t,uint64_t,pn_reader_app_t *,pn_epub_app_t *,pn_reader_present_fn,void *);
/// 释放已保存对象或丢弃未显示准备，后者不保存记录；写/USB/换卡/系统字体关闭前必须调用，幂等。/ Release saved objects or discard never-displayed preparation without saving its records; required before writes/USB/card changes/system-font closure, idempotently.
pn_status_t pn_hot_session_drop(pn_hot_session_t *);
/// 切书不新建/推进可重建检查点，仍提交必要进度/近期日志；失败保留会话，已提交缓存保持。/ Switching never starts/advances rebuildable checkpoints; commit mandatory progress/recents, retaining the session on failure and keeping committed caches.
pn_status_t pn_hot_session_drop_for_switch(pn_hot_session_t *);
/// 由空闲调度到期释放，只保存必要日志；失败保留对象等待显式drop重试，无任务EMPTY。/ Idle-scheduled expiry saves mandatory journals only; failure retains the object until explicit drop retries, EMPTY without work.
pn_status_t pn_hot_session_expire(pn_hot_session_t *,uint64_t);
/// 保留对象有延后日志/当前页/已验证启动结构待写且本次未失败 / Retained journals/current pages/verified startup structures await persistence without a latched failure.
bool pn_hot_session_deferred_pending(const pn_hot_session_t *);
/// 空闲先提交必要日志，再分步收口当前页和已验证结构；切书释放不推进结构。/ At idle commit mandatory journals before current-page/verified-structure checkpoints; switch release never advances structure writes.
pn_status_t pn_hot_session_deferred_flush(pn_hot_session_t *);
