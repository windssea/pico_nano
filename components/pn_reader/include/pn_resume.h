/* SPDX-License-Identifier: Apache-2.0
 * 中文：可删除的原生续读帧，位置仍由原进度日志管理。/ English: disposable native resume frames; original journals still own
 * progress. */
#pragma once
#include "pn_epub_reader.h"
#include "pn_frame.h"
typedef struct {
    void *impl;
} pn_resume_t; ///< 单owner，零初始化，不复制活动对象 / Single owner, zero initialize, never copy live objects
typedef struct {
    unsigned format, missing, pixels; ///< 1 TXT / 2 EPUB，缺字及末字字号 / TXT or EPUB, missing glyphs and final font size
    pn_reader_anchor_t text;          ///< TXT原页边界 / Original TXT page boundaries
    pn_epub_reader_anchor_t epub;     ///< EPUB原页边界 / Original EPUB page boundaries
} pn_resume_page_t;
typedef pn_status_t (*pn_resume_space_fn)(void *, const char *, uint64_t *); ///< 实际卷空闲容量 / Actual volume free capacity
/// 启动时注册两卷容量端口；主机NULL用statvfs，设备缺端口不写。/ Register volume capacity ports; host NULL uses statvfs, device
/// missing ports disable writes.
void pn_resume_space_ports(pn_resume_space_fn, pn_resume_space_fn, void *);
/// 可信owner绑定TF根与内部状态根、完整书/字体布局身份；不会创建文件。/ Bind trusted TF/state roots and complete book/font-layout
/// identity without creating files.
pn_status_t pn_resume_bind(pn_resume_t *, pn_pool_t *, pn_media_t *, const pn_media_lease_t *, pn_media_t *,
                           const pn_media_lease_t *, const char *, const char *, const pn_book_id_t *, const pn_book_id_t *,
                           const pn_style_t *);
/// 只在完整源身份有效后调用；完整SHA/CRC/长度/设置/位置匹配才输出，失败帧视为未定义且不得显示。/ Call only with fully verified
/// sources; require complete SHA/CRC/length/settings/location matches; never present a failed output frame.
pn_status_t pn_resume_load(pn_resume_t *, unsigned, const pn_resume_page_t *, pn_frame_t *, pn_resume_page_t *,
                           pn_status_t (*)(void *), void *);
/// 借用完整帧直到收口/abort；改变或回收帧之前必须abort。/ Borrow a complete frame until completion/abort; abort before changing
/// or reclaiming it.
pn_status_t pn_resume_begin(pn_resume_t *, unsigned, const pn_resume_page_t *, const pn_frame_t *, pn_cache_owner_t *);
/// 每步最多8行编码，摘要/同步/提交分阶段；缓存错误可回退原路径。/ Encode at most eight rows per step, separate
/// hashing/sync/commit phases; cache errors permit the original path.
pn_status_t pn_resume_step(pn_resume_t *);
bool pn_resume_pending(const pn_resume_t *); ///< 是否借用帧 / Whether a frame is borrowed
/// 当前未提交写入槽：0当前页、1下一页、-1无任务；不改状态或访问文件。/ Uncommitted slot: 0 current, 1 next, -1 idle; no state change or file I/O.
int pn_resume_pending_slot(const pn_resume_t *);
void pn_resume_abort(pn_resume_t *);         ///< 归还借用，失卡后不触碰路径 / Return borrows, never touch paths after media loss
void pn_resume_close(pn_resume_t *);         ///< 放弃任务并释放全部池内存 / Abort and release all pooled memory
/// 显示回执使用的规范位置比较，不比较填充字节。/ Canonical location equality for display receipts, excluding padding bytes.
bool pn_resume_location_equal(const pn_epub_location_t *, const pn_epub_location_t *);
