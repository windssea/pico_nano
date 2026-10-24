/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：XHTML正文语义事件与稳定位置，不负责分页、图片解码或持久化。
 * English: semantic XHTML body events and stable positions, without pagination, image decoding or persistence.
 * 冻结：事件在完整XML/CRC完成前暂定；语义位置不是XML字节或永久页码。
 * Frozen: events are provisional before complete XML/CRC; semantic locations are neither XML bytes nor permanent page numbers.
 */
#pragma once
#include "pn_epub.h"
#include "pn_semantic.h"
typedef enum {PN_XHTML_TEXT=0,PN_XHTML_BLOCK_OPEN,PN_XHTML_BLOCK_CLOSE,PN_XHTML_BREAK,PN_XHTML_ANCHOR,PN_XHTML_IMAGE} pn_xhtml_event_kind_t;
typedef enum {PN_XHTML_NO_BLOCK=0,PN_XHTML_CONTAINER,PN_XHTML_PARAGRAPH,PN_XHTML_HEADING,PN_XHTML_LIST_ITEM,PN_XHTML_PREFORMATTED} pn_xhtml_block_t;
typedef enum {
    PN_XHTML_ALIGN_LEFT=0, ///< 左对齐 / Left alignment
    PN_XHTML_ALIGN_CENTER, ///< 居中 / Center alignment
    PN_XHTML_ALIGN_RIGHT ///< 右对齐 / Right alignment
} pn_xhtml_align_t;
typedef struct {
    uint32_t value; ///< 千分之一像素或千分之一百分数；0为自动 / Thousandths of pixels or percent; zero means auto
    bool percent; ///< 百分比相对页内可用尺寸 / Percentage relative to available page dimensions
} pn_xhtml_length_t;
typedef struct {
    pn_xhtml_length_t width,height,max_width,max_height; ///< 非继承图片几何约束 / Noninherited image geometry constraints
    pn_xhtml_align_t align; ///< 继承行对齐 / Inherited row alignment
    bool bold,italic,pre,hidden; ///< 基础继承样式；hidden不发正文 / Basic inherited styles; hidden emits no body text
    unsigned heading; ///< 0普通，1至6标题 / Zero for normal text, one through six for headings
} pn_xhtml_style_t;
typedef struct {
    pn_xhtml_event_kind_t kind; ///< 事件类型 / Event kind
    pn_xhtml_position_t position; ///< 与字号无关的资源内语义位置 / Resource-local semantic position independent of font size
    pn_xhtml_style_t style; ///< 当前基础样式 / Current basic style
    pn_xhtml_block_t block; ///< 区分容器/段落/标题等结构，避免重复段距 / Distinguish containers/paragraphs/headings to avoid duplicate paragraph gaps
    uint32_t codepoint; ///< TEXT的Unicode标量 / Unicode scalar for TEXT
    const char *value; ///< ANCHOR的id或IMAGE归一资源路径，回调期有效 / Anchor ID or normalized image resource path, callback lifetime only
    const char *alt; ///< IMAGE替代文字，回调期有效 / Image alternative text, callback lifetime only
} pn_xhtml_event_t;
typedef struct {
    uint64_t elements; ///< 全文元素数量，包括非显示部分 / Document element count including nonvisual parts
    uint64_t text_codepoints; ///< 实际发出的归一正文标量数 / Emitted normalized body scalar count
    uint64_t blocks; ///< 可见块开始数量 / Visible block-open count
    uint64_t images; ///< 图片引用数量，未解码 / Image reference count without decoding
    uint64_t anchors; ///< 正文id事件数量，含隐藏元素 / Body ID event count including hidden elements
} pn_xhtml_stats_t;
typedef pn_status_t (*pn_xhtml_sink_t)(void *,const pn_xhtml_event_t *);
/// 消费已验证EOF的XML流；支持严格XHTML与元素行内样式（无样式表加载），全部解析分配走pool；失败不改stats，sink须丢弃暂定内容。
/// Consume XML with verified EOF, supporting strict XHTML and inline attributes (no stylesheet loading) with pooled allocations; errors preserve stats and require discarding provisional sink content.
pn_status_t pn_xhtml_parse_input(pn_pool_t *pool,const pn_xml_input_t *input,const char *base,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats);
/// 从出版物解析并按预算复用已验证语义；原始词法失败可有界重试，head样式/完整来源CRC/最终XML通过才缓存，失败不改stats。
/// Parse publication resources with bounded lexical retry and budgeted verified semantics; cache only after head styles/full source CRC/final XML pass, preserving stats on failure.
pn_status_t pn_xhtml_parse(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats);
typedef bool (*pn_xhtml_done_t)(void *); ///< 消费窗口已足够，不代表源已校验 / Consumer window is sufficient, not proof of source validation
/// 消费者回到“已消费order个事件”的状态；context为最后跳过的TEXT/IMAGE/非容器BLOCK_OPEN（只含kind/block），NULL表示章首。
/// Reset the consumer as if order events were consumed; context is the last skipped TEXT/IMAGE/noncontainer BLOCK_OPEN (kind/block only), NULL at chapter start.
typedef void (*pn_xhtml_restart_t)(void *ctx,uint64_t order,const pn_xhtml_event_t *context);
/// 已验证缓存可在done后结束复放；冷源仍完整CRC/XML校验与建模，stats仍为全章，失败保持输出。
/// 超出语义上限的章在提供restart时保留focus附近的已验证窗口，复放前以restart接续序号；窗口不够时restart(0)后冷解析重建。
/// Stop verified replay after done; cold sources still complete CRC/XML validation and modeling, with full-chapter stats preserved on failure.
/// With restart, over-cap chapters keep a verified window near focus and restart resumes the order before replay; a short window restarts at zero and rebuilds from a cold parse.
pn_status_t pn_xhtml_parse_window(pn_pool_t *,pn_epub_t *,const char *,const uint8_t salt[16],const pn_xhtml_position_t *focus,pn_xhtml_sink_t,pn_xhtml_done_t,pn_xhtml_restart_t,void *,pn_xhtml_stats_t *);
typedef struct {void *impl; ///< 私有跨轮候选，活动时不可移动 / Private candidate across ticks; immovable while active
} pn_xhtml_job_t;
/// 借用出版物/消费者直到close；原模型保持pin，候选在完整XML/CSS/CRC前不可交付。/ Borrow publication/consumer until close; pin the old model and withhold candidates until complete XML/CSS/CRC verification.
pn_status_t pn_xhtml_job_begin(pn_xhtml_job_t *,pn_pool_t *,pn_epub_t *,const char *,const uint8_t[16],const pn_xhtml_position_t *,pn_xhtml_sink_t,pn_xhtml_done_t,pn_xhtml_restart_t,void *);
/// 模型验证每步最多4KiB解包输入，验证后最多复放4事件；流式退路未满页时24字节，满页后4KiB。BUSY待续，终态须close。/ Model verification takes at most 4 KiB unpacked input per step, then replays at most four events; the streaming fallback uses 24 bytes until the page fills, then 4 KiB. BUSY continues and terminal states require close.
pn_status_t pn_xhtml_job_step(pn_xhtml_job_t *,pn_xhtml_stats_t *);
/// 索引专用：已验证模型按精确焦点跳过前缀，每步各至多32条定位记录和32个事件；逐条检查取消，不改变冷源完整验证，同一job不混用两种step。/ Compiler only: seek the exact focus in verified models, at most 32 seek records and 32 events per step, checking cancellation per item and preserving full cold-source validation; never mix step modes on one job.
pn_status_t pn_xhtml_job_index_step(pn_xhtml_job_t *,pn_xhtml_stats_t *);
/// 释放候选/暂定页及只读包句柄；已完成的验证缓存保持，不能在回调中重入，文件关闭仍可能阻塞。/ Release candidates/provisional pages and read-only packet handles, preserving verified caches; no callback reentry, and file close may block.
void pn_xhtml_job_close(pn_xhtml_job_t *);
/// 释放本出版物语义缓存，借用期间不释放；close使用同一入口。/ Release publication semantics unless borrowed; close uses the same entry point.
void pn_xhtml_cache_close(pn_epub_t *);
/// 显示确认后提升同路径/盐的完整已验证备用模型；只交换槽位，保留两份记录，忙时稍后重试，无IO。/ After display confirmation promote a fully verified alternate matching path/salt; swap slots retaining both models, retry when busy, without IO.
pn_status_t pn_xhtml_cache_select(pn_epub_t *,const char *,const uint8_t[16]);
#define PN_XHTML_SNAPSHOT_MAX (384u*1024u) ///< 含头的可重建序列化上限 / Rebuildable serialized size including header
#define PN_XHTML_SNAPSHOT_HEADER_BYTES 152u ///< 固定小端包头长度 / Fixed little-endian packet header size
/// 只筛选包头的版本/书/章节/长度/头CRC，不证明载荷或模型有效；命中仍须完整解码校验。
/// Filter only header version/book/chapter/size/header CRC; a match still requires full payload/model validation.
bool pn_xhtml_snapshot_header_matches(const uint8_t *,size_t,size_t,const pn_book_id_t *,const char *);
typedef struct pn_xhtml_store {
    void *ctx; ///< 串行控制层上下文 / Serialized controller context
    pn_book_id_t book; ///< 同一打开源完整校验所得摘要 / Full digest verified from the same open source
    pn_status_t (*load)(void *,pn_pool_t *,uint8_t **,size_t *); ///< 返回池计费的完整包，核心用pn_free释放 / Return a complete pool-charged packet, freed by core with pn_free
    pn_status_t (*save)(void *,const uint8_t *,size_t); ///< 同步借用完整包，不呈现或修改原书 / Synchronously borrow a complete packet without presenting or changing the book
    pn_status_t (*take)(void *,pn_pool_t *,uint8_t *,size_t); ///< 可选异步接管：仅OK转移池内包所有权，其他结果仍由核心释放 / Optional deferred ownership: only OK transfers the pooled packet, otherwise core frees it
    pn_status_t (*load_for)(void *,pn_pool_t *,const char *,uint8_t **,size_t *); ///< 可选章节筛选读取，优先于load；相同完整包所有权，不可仅返回包头 / Optional chapter-filtered load preferred over load; same full-packet ownership, never just a header
    pn_status_t (*open_for)(void *,pn_pool_t *,const char *,void **,size_t *); ///< 可选分步读：筛选头后交付句柄及总长，失败不改输出 / Optional staged read: return handle/size only after header filtering
    pn_status_t (*read_step)(void *,uint8_t *,size_t,size_t *); ///< 至多4096字节，EOF必须返回0；错误不改got / At most 4096 bytes; EOF returns zero; errors preserve got
    void (*close_read)(void *); ///< 关闭并释放句柄，取消/失败同样调用 / Close and free the handle, including cancellation/failure
} pn_xhtml_store_t;
/// 空闲控制层显式同步已验证且对应path的模型；失败只影响缓存，不保存位置，不隐式关闭源。
/// Idle controller explicitly syncs a verified model matching path; failures affect cache only, never positions or source lifetime.
pn_status_t pn_xhtml_cache_flush(pn_epub_t *,const char *path);
typedef enum {PN_XHTML_SNAPSHOT_COUNT,PN_XHTML_SNAPSHOT_ALLOC,PN_XHTML_SNAPSHOT_ZERO,PN_XHTML_SNAPSHOT_HEADER,PN_XHTML_SNAPSHOT_RECORDS,PN_XHTML_SNAPSHOT_CRC,PN_XHTML_SNAPSHOT_HEADER_CRC,PN_XHTML_SNAPSHOT_OFFER} pn_xhtml_snapshot_phase_t;
typedef struct {
    pn_xhtml_snapshot_phase_t phase; ///< 本轮实际阶段 / Actual phase of this step
    size_t bytes,records; ///< 本轮处理的字节及记录数，不代表毫秒预算 / Bytes and records processed in this step, not a millisecond budget
} pn_xhtml_snapshot_work_t;
/// 每轮最多16条记录或4096字节编码/清零/CRC；BUSY为准备中，OK为存储端口已接受，不代表异步落盘完成。work可NULL。
/// Encode/zero/CRC at most 16 records or 4096 bytes per step; BUSY means preparing, OK means accepted by storage, not durable asynchronous completion. work may be NULL.
pn_status_t pn_xhtml_cache_flush_step(pn_epub_t *,const char *,pn_xhtml_snapshot_work_t *work);
/// 查询未完成编码，无IO；新输入应先abort再做前台工作。/ Query unfinished encoding without I/O; abort before foreground work on new input.
bool pn_xhtml_cache_pending(const pn_epub_t *);
/// 同样预算保存非显示的另一份完整已验证模型；不提升槽位、不改变位置。/ Persist the other fully verified model with the same work bounds, without slot promotion or position changes.
pn_status_t pn_xhtml_cache_flush_neighbor_step(pn_epub_t *,const char *visible,pn_xhtml_snapshot_work_t *);
/// 释放编码暂存和本任务的模型pin，不改已验证语义，保留可重试标记；不能在存储回调内重入。/ Release staging and this job's model pin, preserving verified semantics and retry eligibility; never reenter from a storage callback.
void pn_xhtml_cache_abort(pn_epub_t *);
/// 控制层放弃已接管包后允许当前已验证模型再次空闲同步；无IO、不使未验证模型有效。/ After abandoning a taken packet, allow idle retry of the currently verified model without I/O or validating a provisional model.
void pn_xhtml_cache_retry(pn_epub_t *);
/// 完整扫描找到唯一id，缺失EMPTY、重复CORRUPT，失败不改position。
/// Scan completely for a unique ID, EMPTY if absent, CORRUPT on duplicates, preserving position on failure.
pn_status_t pn_xhtml_anchor(pn_pool_t *pool,pn_epub_t *epub,const char *path,const char *id,const uint8_t salt[16],pn_xhtml_position_t *position);

/// 完整校验后返回位置首次事件的临时顺序号；不是持久偏移；错误不改输出。
/// Return transient first-event order after complete validation, never a durable offset; errors preserve output.
pn_status_t pn_xhtml_order(pn_pool_t *,pn_epub_t *,const char *,const pn_xhtml_position_t *,const uint8_t salt[16],uint64_t *);
/// 完整校验后选择目标（NULL为章末）前约8192可见事件的检查点；element为0表示章首，不排序locator。/ After full validation choose a checkpoint about 8192 visible events before target (NULL means chapter end); element zero means chapter start, never sort locators.
pn_status_t pn_xhtml_window_start(pn_pool_t *,pn_epub_t *,const char *,const pn_xhtml_position_t *,const uint8_t salt[16],uint64_t *,uint64_t *,pn_xhtml_position_t *);
