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
/// 对精确长度流计算摘要；调用方排除变更并在read中验证介质。/ Hash an exact-length stream; caller excludes mutation and checks media in read.
pn_status_t pn_identity_stream(void *,uint64_t,pn_identity_read_fn,pn_book_id_t *);
/// 临时32KiB缓冲计入pool，用后释放；准入失败退回4KiB，完整校验/取消和错误输出契约不变。
/// Charge a temporary 32 KiB buffer to pool and release it afterwards; fall back to 4 KiB while preserving complete hashing, cancellation and error output.
pn_status_t pn_identity_stream_pooled(pn_pool_t *,void *,uint64_t,pn_identity_read_fn,pn_book_id_t *);
