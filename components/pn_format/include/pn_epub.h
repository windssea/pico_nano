/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB出版物结构与章节资源身份，不负责正文排版和文件寿命。
 * English: EPUB publication structure and chapter resource identities, without layout or file ownership.
 * 冻结：完整container/OPF校验前不发布；不把EPUB转换成永久TXT偏移。
 * Frozen: publish only after complete container/OPF verification; never convert EPUB into permanent TXT offsets.
 */
#pragma once
#include "pn_xml.h"
#include "pn_zip.h"
#define PN_EPUB_ID_MAX 256
#define PN_EPUB_META_MAX 512
#define PN_EPUB_ITEMS_MAX PN_ZIP_ENTRIES_MAX
struct pn_xhtml_store;
typedef struct {
    void *impl; ///< 出版物模型，初始NULL，不可复制活动对象 / Publication model, initially NULL; never copy a live object
    void *semantic_cache; ///< 本会话可选语义缓存，初始NULL，由close释放 / Optional session semantic cache, initially NULL, released by close
    void *semantic_cache_alt; ///< 可选预排的下一章模型，不替换当前章 / Optional next-chapter model for speculative prefetch; never replaces the current chapter
    const struct pn_xhtml_store *semantic_store; ///< 控制层可选持久缓存端口，寿命覆盖出版物 / Optional controller persistence port, outliving the publication
} pn_epub_t;
typedef enum {PN_EPUB_OTHER=0,PN_EPUB_XHTML,PN_EPUB_NCX,PN_EPUB_IMAGE,PN_EPUB_CSS,PN_EPUB_FONT} pn_epub_media_t;
typedef struct {
    char id[PN_EPUB_ID_MAX]; ///< OPF资源ID / OPF resource ID
    char path[PN_ZIP_PATH_MAX]; ///< 容器内归一资源路径 / Normalized container resource path
    uint32_t zip_index; ///< 本次ZIP代次索引，不作永久locator / Current ZIP-generation index, not a persistent locator
    uint64_t unpacked; ///< 资源未压缩长度，打开时已由ZIP目录取得 / Uncompressed resource size already read from the ZIP directory during open
    pn_epub_media_t media; ///< 资源类别 / Resource kind
    bool linear; ///< 章节主阅读流标志 / Primary reading-flow flag
} pn_epub_item_t;
typedef struct {
    char title[PN_EPUB_META_MAX],creator[PN_EPUB_META_MAX],identifier[PN_EPUB_META_MAX],language[64]; ///< 元数据UTF8 / UTF-8 metadata
    char package_path[PN_ZIP_PATH_MAX],nav_path[PN_ZIP_PATH_MAX],ncx_path[PN_ZIP_PATH_MAX],cover_path[PN_ZIP_PATH_MAX]; ///< OPF/目录/封面引用 / OPF/navigation/cover references
    size_t manifest_count,spine_count; ///< 资源及章节数量 / Resource and chapter counts
    unsigned version; ///< EPUB 2或3 / EPUB two or three
    bool fixed_layout; ///< 固定版式，阅读层须独立处理 / Fixed layout requiring separate reader handling
    bool cover_declared; ///< 标准声明；false时路径仅filename候选 / Standard declaration; otherwise path is only a filename candidate
} pn_epub_info_t;
/// 借用有效ZIP；salt须来自可信随机源；错误epub仍为空，必须先close再关ZIP和源。
/// Borrow a valid ZIP with trusted random salt; errors leave epub empty; close before ZIP and source.
pn_status_t pn_epub_open(pn_epub_t *epub,pn_pool_t *pool,pn_zip_t *zip,const uint8_t salt[16]);
typedef struct {
    void *impl; ///< 暂定出版物/流式XML状态，零初始化且不可移动 / Provisional publication/streaming XML state; zero-initialize and never move while active
} pn_epub_job_t;
/// 开始可暂停元信息解析；借用ZIP直到任务终结，尚不发布出版物。/ Begin resumable metadata parsing, borrowing ZIP until termination without publishing a book.
pn_status_t pn_epub_job_begin(pn_epub_job_t *,pn_pool_t *,pn_zip_t *,const uint8_t[16]);
/// 每次一个阶段或4KiB XML；BUSY待续，OK才设置epub，输入前取消保留任务供暂停/关闭。
/// One stage or 4 KiB of XML per call; continue on BUSY, set epub only on OK, and pre-input cancellation retains the job for pause/close.
pn_status_t pn_epub_job_step(pn_epub_job_t *,pn_epub_t *);
/// 丢弃暂定模型并关闭流，不关闭借用ZIP。/ Discard the provisional model and close streams, keeping the borrowed ZIP open.
void pn_epub_job_close(pn_epub_job_t *);
/// 幂等释放出版物模型，不关闭ZIP。/ Idempotently release the publication model without closing ZIP.
void pn_epub_close(pn_epub_t *epub);
/// 验证媒体后复制元数据，失败输出不变。/ Validate media then copy metadata, preserving output on errors.
pn_status_t pn_epub_info(pn_epub_t *epub,pn_epub_info_t *out);
/// 按OPF章节顺序复制资源身份，保留linear=no，越界EMPTY。/ Copy resource identity in OPF spine order, retaining linear=no; EMPTY beyond the end.
pn_status_t pn_epub_spine(pn_epub_t *epub,size_t index,pn_epub_item_t *out);
/// 精确资源路径定位首个对应章节，非章节资源返回EMPTY，错误输出不变。
/// Locate the first corresponding spine entry by exact resource path; EMPTY for non-spine resources, preserving output on errors.
/// 命中复用完整验证后的内存路径索引，每次仍验证源；不增加索引数组或跳过原始ZIP/OPF/资源校验。/ Hits reuse the fully verified in-memory path index and still validate the source each time; no extra index array or skipped ZIP/OPF/resource checks.
pn_status_t pn_epub_spine_find(pn_epub_t *epub,const char *path,size_t *index);
/// 在出版物所属ZIP打开精确路径资源流；流必须先于出版物关闭，不改源寿命。
/// Open an exact-path resource stream in the publication's own ZIP; close stream before publication without changing source lifetime.
pn_status_t pn_epub_resource_open(pn_epub_t *epub,const char *path,pn_zip_stream_t *stream);
typedef struct {
    pn_epub_t *epub; ///< 借用出版物，须先关闭输入 / Borrowed publication; close input first
    pn_zip_stream_t stream; ///< 超限或内存不足时的流式回退 / Streaming fallback for limits or allocation failure
    const uint8_t *bytes; ///< 已完成CRC验证的章缓存 / Chapter cache verified through CRC completion
    size_t size,offset; ///< 缓存长度与独立读取位置 / Cache size and independent read offset
} pn_epub_chapter_input_t;
/// 打开最多128KiB单章缓存；input初始全零，活动对象返回BUSY；超限走原流，借用期间不替换。
/// Open a single chapter cache capped at 128 KiB; zero-initialize input, active inputs return BUSY, and oversized/live-borrowed chapters stream.
pn_status_t pn_epub_chapter_open(pn_epub_t *epub,const char *path,pn_epub_chapter_input_t *input);
/// XML输入回调，每次仍验证介质有效，EOF前完整CRC已通过。/ XML input callback validating media on every read; cached CRC passed before publication.
pn_status_t pn_epub_chapter_read(void *ctx,uint8_t *out,size_t capacity,size_t *size);
/// 释放借用/流，幂等，不释放出版物持有的缓存。/ Release borrow/stream idempotently, keeping the publication-owned cache.
void pn_epub_chapter_close(pn_epub_chapter_input_t *input);
/// 逐条导出完整出版物，含元信息、manifest及spine，不保存章节/排版。游标初始0。/ Export complete metadata, manifest and spine one record per step, without chapters/layout; cursor starts at zero.
pn_status_t pn_epub_snapshot_write(pn_epub_t *,pn_format_io_t *,uint32_t *);
/// 仅内部可信包加载器调用，沿原预算重建模型；整包SHA成功前不得使用。/ Internal trusted-packet loaders only; rebuild under original budgets and never use before whole-packet SHA succeeds.
pn_status_t pn_epub_snapshot_read(pn_epub_t *,pn_pool_t *,pn_zip_t *,pn_format_io_t *);
