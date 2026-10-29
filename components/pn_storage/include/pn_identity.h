/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：按原文件内容计算SHA-256身份，仅用于内容识别。
 * English: original-file SHA-256 identity for content matching only.
 * 冻结：不写文件；调用方排除文件并发变更，不用于安全认证。
 * Frozen: no file writes; caller excludes concurrent mutation; not for security authentication.
 */
#pragma once
#include "pn_storage.h"
#include "pn_alloc.h"
typedef struct {
    uint8_t sha256[32]; ///< 完整原文件摘要 / Full original-file digest
} pn_book_id_t;
typedef struct {
    uint32_t h[8]; ///< SHA状态 / SHA state
    uint64_t bytes; ///< 已消费字节 / Consumed bytes
    uint8_t block[64]; ///< 原算法分块 / Original algorithm block
    size_t used; ///< 块内用量 / Block usage
} pn_identity_accumulator_t;
/// 借调用方有界状态按原软件算法累计完整流，不分配；finish后须重新init。/ Accumulate a whole stream with the original software algorithm in bounded caller state, without allocation; reinitialize after finish.
void pn_identity_accumulator_init(pn_identity_accumulator_t *);
/// 消费真实成功读写的字节；调用方保证有效指针和总长度不溢出。/ Consume bytes actually read/written; caller guarantees valid pointers and a nonoverflowing total length.
void pn_identity_accumulator_update(pn_identity_accumulator_t *,const void *,size_t);
/// 输出完整SHA并清理工作状态。/ Produce the full SHA and clear working state.
void pn_identity_accumulator_finish(pn_identity_accumulator_t *,pn_book_id_t *);

typedef struct {void *impl;} pn_identity_digest_t; ///< 可复用的池内摘要上下文，不保留输入指针 / Reusable pool-owned digest context without retained input pointers
typedef struct {
    size_t bytes; ///< 端口状态大小 / Port-state size
    pn_status_t (*begin)(void *); ///< 初始化 / Initialize
    pn_status_t (*update)(void *,const void *,size_t); ///< 同步消费全部字节 / Consume all bytes synchronously
    pn_status_t (*finish)(void *,pn_book_id_t *); ///< 完整摘要 / Complete digest
    void (*abort)(void *); ///< 释放端口状态 / Release port state
} pn_identity_digest_port_t;
/// 可选完整SHA端口，启动时设置，活动摘要期间不更改。/ Optional full-SHA port, installed at startup and unchanged during active digests.
void pn_identity_digest_port(const pn_identity_digest_port_t *);
/// 首次分配、完成后复用同一池状态；无端口EMPTY，调用方可回退原软件累加器。/ Allocate once, reuse completed state in the same pool; EMPTY without a port permits the original software accumulator fallback.
pn_status_t pn_identity_digest_begin(pn_identity_digest_t *,pn_pool_t *);
/// 无IO且不借指针；调用方维持来源有效，每次限于自己的有界已读块。/ No IO or retained pointer; callers validate the source and supply bounded blocks already read.
pn_status_t pn_identity_digest_update(pn_identity_digest_t *,const void *,size_t);
/// 完成或失败均结束本次摘要，失败不改out；上下文留待复用/close。/ End on success or error, preserving out on error; retain context for reuse/close.
pn_status_t pn_identity_digest_finish(pn_identity_digest_t *,pn_book_id_t *);
/// 幂等释放池状态；不访问原书。/ Idempotently release pool state without source access.
void pn_identity_digest_close(pn_identity_digest_t *);
/// 流式读取，不按路径或大小猜身份；失败不改输出。调用方禁止文件并发变更。
/// Stream content without guessing identity from path or size; errors preserve output. Caller prevents concurrent file mutation.
pn_status_t pn_identity_file(pn_media_t *media, const pn_media_lease_t *lease,
    const char *path, uint64_t byte_limit, pn_book_id_t *id);
/// 与file同样完整校验，临时缓冲计入pool，硬件/软件同源；错误保持输出。 / Full file verification with a pool-charged temporary buffer and shared hardware/software hashing; preserve output on error.
pn_status_t pn_identity_file_pooled(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,uint64_t byte_limit,pn_book_id_t *id);

/// 借用只读块计算完整摘要，错误不改输出。/ Hash a borrowed read-only block; preserve output on errors.
pn_status_t pn_identity_bytes(const uint8_t *,size_t,pn_book_id_t *);
/// 有界流式读取回调；每次必须返回1..capacity字节。/ Bounded streaming reader; each call must return 1..capacity bytes.
typedef pn_status_t (*pn_identity_read_fn)(void *,uint64_t,uint8_t *,size_t,size_t *);
/// 可选硬件摘要端口；仅同步借用buffer/capacity，启动时安装，保留完整SHA与失败不改输出契约。
/// Optional hardware digest port borrowing buffer/capacity only during the call; install at startup, preserving full SHA and unchanged output on failure.
typedef pn_status_t (*pn_identity_stream_port_fn)(void *,uint64_t,pn_identity_read_fn,uint8_t *,size_t,pn_book_id_t *);
/// NULL恢复同源软件实现；不可在摘要运行期间修改。/ NULL restores shared software hashing; never change while hashing.
void pn_identity_stream_port(pn_identity_stream_port_fn);
/// 自管临时缓冲的同步池摘要端口，仅用于大于4KiB的pooled入口；不得保留read/ctx，失败不改调用方输出。/ Synchronous pooled port owning its scratch, used only above 4 KiB; never retain read/ctx, and preserve caller output on failure.
typedef pn_status_t (*pn_identity_pooled_port_fn)(void *,uint64_t,pn_identity_read_fn,pn_book_id_t *);
/// 启动时与stream端口配套安装；切换stream端口会清除此能力，NULL回原借用缓冲路径。/ Install alongside the stream port at startup; changing the stream port clears this capability, and NULL restores borrowed scratch.
void pn_identity_pooled_port(pn_identity_pooled_port_fn);
/// 对精确长度流计算摘要；调用方排除变更并在read中验证介质。/ Hash an exact-length stream; caller excludes mutation and checks media in read.
pn_status_t pn_identity_stream(void *,uint64_t,pn_identity_read_fn,pn_book_id_t *);
/// 自管端口可直接借其工作区；否则临时32KiB计入pool，准入失败退4KiB，保留完整校验/取消/错误输出契约。
/// An owning port supplies its scratch directly; otherwise charge 32 KiB to pool with a 4 KiB fallback, preserving full verification/cancellation/error-output contracts.
pn_status_t pn_identity_stream_pooled(pn_pool_t *,void *,uint64_t,pn_identity_read_fn,pn_book_id_t *);

typedef struct {void *impl;} pn_identity_job_t; ///< 活动任务不可复制，源/ctx须保活到close / Never copy a live job; source/ctx outlive close
typedef struct {
    size_t bytes; ///< 零初始化端口状态字节 / Zero-initialized port-state bytes
    pn_status_t (*begin)(void *); ///< 初始化摘要，不读源 / Initialize hashing without source reads
    pn_status_t (*step)(void *,void *,uint64_t,size_t,pn_identity_read_fn,size_t *); ///< 至多一次源read且不超过给定上限；CANCELLED不能改变摘要 / At most one read within the supplied cap; CANCELLED cannot mutate the hash
    pn_status_t (*finish)(void *,pn_book_id_t *); ///< 只在全部源字节成功后调用 / Called only after all source bytes succeed
    void (*abort)(void *); ///< 零状态/失败/完成后均可释放，不能访问源 / Release zero/failed/finished states without source access
} pn_identity_job_port_t;
/// 启动时安装常驻不变描述；NULL用同源软件，切换stream端口会清除；活动摘要期间不可更换。/ Install a persistent immutable descriptor at startup; NULL uses shared software, changing stream clears it; never change with active hashes.
void pn_identity_job_port(const pn_identity_job_port_t *);
/// 只初始化，不读取；完整长度须可SHA编码，源不可并发修改，调用方维护介质验证。/ Initialize without reads; length must fit SHA encoding, caller prevents mutation and validates media.
pn_status_t pn_identity_job_begin(pn_identity_job_t *,pn_pool_t *,void *,uint64_t,pn_identity_read_fn);
/// 每步最多一次32KiB供数；BUSY继续，CANCELLED保留已完成块，其余错误终止；仅最终OK改输出。/ At most one 32 KiB read per step; BUSY continues, CANCELLED retains completed chunks, other errors stop; only final OK changes output.
pn_status_t pn_identity_job_step(pn_identity_job_t *,pn_book_id_t *);
/// 纯查询已进入摘要的字节，不代表文件内容/显示已确认。/ Query bytes incorporated into the hash, never content/presentation confirmation.
pn_status_t pn_identity_job_progress(const pn_identity_job_t *,uint64_t *,uint64_t *);
/// 放弃/完成均须close归还上下文，不关闭借用源，幂等。/ Close on abort/success to release context, without closing the borrowed source; idempotent.
void pn_identity_job_close(pn_identity_job_t *);
