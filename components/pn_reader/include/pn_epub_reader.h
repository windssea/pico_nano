/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：原生EPUB语义导航与显示回执，借用出版物、度量和页缓冲。
 * English: native EPUB semantic navigation and display receipts borrowing publication, metrics and page buffers.
 * 冻结：未显示成功不提交位置；不使用TXT偏移或永久页号。
 * Frozen: no position commit before successful presentation; no TXT offsets or permanent page numbers.
 */
#pragma once
#include "pn_epub_page.h"
#include "pn_reader.h"
#include "pn_epub_progress.h"
typedef struct {void *impl;} pn_epub_reader_t; ///< 初始NULL，不复制活动会话 / Initially NULL; never copy a live session

typedef struct {
    pn_epub_location_t begin,next; ///< 准备页首与下一页请求 / Prepared page start and next-page request
    bool has_next; ///< 有后续页或候选线性资源；NEXT确定实际末尾 / Further page or candidate linear resource; NEXT determines actual end
} pn_epub_reader_anchor_t;
typedef struct {
    pn_epub_reader_t *owner; ///< 原会话对象 / Original session object
    pn_job_token_t token; ///< 会话及排版代次 / Session and layout generation
    uint64_t ticket; ///< 不重复准备号 / Nonrepeating preparation ID
    pn_read_intent_t intent; ///< 原导航意图 / Original navigation intent
    pn_epub_reader_anchor_t anchor; ///< 语义请求与后续位置 / Semantic request and following location
} pn_epub_reader_receipt_t;
/// 仅匹配显示成功后回调；payload仅回调期有效，owner串行不可重入；写失败外层须保留保存屏障。
/// Callback after matching display success; payload has callback lifetime and owner calls are serialized/nonreentrant; save failure requires the outer save barrier.
typedef pn_status_t (*pn_epub_confirm_fn)(void *,const pn_epub_progress_t *,bool,uint64_t);
/// 初始化；外层须验证book对应借用epub，字体/源/缓冲寿命覆盖会话，固定版式拒绝。
/// Initialize; outer owner verifies book matches borrowed epub and retains fonts/sources/buffers; fixed layout is rejected.
pn_status_t pn_epub_reader_init(pn_epub_reader_t *,pn_pool_t *,pn_epub_t *,const pn_book_id_t *,const uint8_t salt[16],const pn_layout_t *,const pn_epub_measure_t *,pn_epub_page_t *,pn_job_token_t,pn_epub_confirm_fn,void *);
/// 幂等释放导航状态，不关闭借用源；外层先处理保存屏障。/ Release navigation state idempotently without closing sources; outer owner handles save barriers first.
void pn_epub_reader_close(pn_epub_reader_t *);
/// FIRST/JUMP可首次请求；JUMP需location，其他可NULL；pending时BUSY，失败receipt不变。
/// FIRST/JUMP can start a session; JUMP requires location, others permit NULL; BUSY while pending, preserving receipt on errors.
pn_status_t pn_epub_reader_prepare(pn_epub_reader_t *,pn_read_intent_t,const pn_epub_location_t *,pn_epub_reader_receipt_t *);
/// 仅完整图片/正文/显示成功后success=true；匹配回执提交，不接受重复或过期确认。
/// Set success only after complete images/body/display; commit matching receipts, rejecting repeated or stale completion.
pn_status_t pn_epub_reader_complete(pn_epub_reader_t *,const pn_epub_reader_receipt_t *,bool,uint64_t);
/// 借用有效准备页，下一次prepare/reflow/close失效；不证明图片已解码。
/// Borrow a valid prepared page until next prepare/reflow/close; it does not prove images were decoded.
pn_status_t pn_epub_reader_page(pn_epub_reader_t *,const pn_epub_page_t **);
/// 更新布局、推进代次、丢pending/history并保留visible；随后CURRENT。/ Update layout/generation, discard pending/history and retain visible; then CURRENT.
pn_status_t pn_epub_reader_reflow(pn_epub_reader_t *,const pn_layout_t *);
/// 仅已确认位置；无显示EMPTY、错误不改输出。/ Confirmed position only; EMPTY before presentation, preserving output on errors.
pn_status_t pn_epub_reader_progress(pn_epub_reader_t *,pn_epub_progress_t *);
