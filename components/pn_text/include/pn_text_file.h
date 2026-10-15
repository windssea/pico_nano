/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TXT文件源port，将stdio和介质租约连接到共享解码器。
 * English: TXT file-source port connecting stdio and media leases to the shared decoder.
 * 冻结：不挂载/格式化，不释放调用方租约；打开对象不可移动。
 * Frozen: no mounting/formatting or releasing caller leases; open objects never move.
 */
#pragma once
#include "pn_text.h"
#include <stdio.h>
#define PN_TEXT_FILE_MAX_BYTES INT32_MAX
#define PN_TEXT_FILE_PATH_MAX 512 ///< 保留源的路径上限 / Retained-source path limit

typedef struct {
    FILE *handle; ///< 首次使用前置零；port独占stdio读/定位，外部不得操作 / Zero before use; the port exclusively owns stdio reads/seeks
    char path[PN_TEXT_FILE_PATH_MAX]; ///< 原打开路径，只用于保留源校验，不作为身份键 / Original path for retained-source checks, never an identity key
    pn_media_t *media; ///< 存储owner / Storage owner
    pn_media_lease_t lease; ///< 调用方保持至close之后 / Caller keeps alive until after close
    uint64_t size; ///< 不变源大小 / Immutable source size
    uint8_t *block; ///< 4KiB读缓存，首次小读时分配，close释放；分配失败则直接读 / 4 KiB read cache, allocated on the first small read and freed by close; reads go straight through if allocation fails
    uint64_t block_start; ///< 缓存块在文件中的起点 / File offset of the cached block
    size_t block_length; ///< 缓存块有效字节数 / Valid bytes in the cached block
    void *read_cache; ///< 池内可回收多块缓存，只有绑定后启用 / Reclaimable pooled multi-block cache, enabled only after binding
    pn_cache_owner_t cache_owner; ///< 文件寿命内注册；池须活到close之后 / Registered for the file lifetime; pool must outlive close
    uint64_t read_position; ///< 最近成功stdio读取后的实际游标，不是缓存命中位置 / Actual stdio cursor after the last successful read, independent of cache hits
    bool read_position_known; ///< 读/定位失败后失效，下次强制定位 / Invalid after read/seek failures; the next read must seek
    pn_book_id_t identity; ///< 仅当前不可变句柄/租约的已验证摘要 / Verified digest for this immutable handle and lease only
    bool identity_known; ///< close/reopen失效，不按路径或mtime复用 / Invalidated by close/reopen; never reused by path or mtime
    uint64_t io_reads,io_bytes,io_seeks,cache_hits,identity_hits; ///< 实际读取/定位与源缓存计数 / Actual reads/seeks and source-cache counters
} pn_text_file_t;
/// 为本port的不变source绑定可选32块LRU读缓存；不分配、不读卡，未知source返回UNSUPPORTED，不能跨池重绑。
/// Bind an optional 32-block LRU read cache to this port's immutable source; no allocation/IO, UNSUPPORTED for other sources, no rebinding across pools.
pn_status_t pn_text_file_cache_bind(const pn_text_source_t *,pn_pool_t *);
/// 仅识别本port原始source；未知UNSUPPORTED，成功摘要绑定当前句柄/租约，失败不改输出。/ Recognize only this port's original source; unknown sources return UNSUPPORTED; bind successful digests to the live handle/lease and preserve output on error.
pn_status_t pn_text_file_identity(const pn_text_source_t *,pn_book_id_t *);
/// 用池内有界扫描缓冲核验并缓存当前打开源摘要；不跨句柄复用。/ Verify and cache the live source digest with a bounded pool buffer; never reuse across handles.
pn_status_t pn_text_file_identity_pooled(pn_pool_t *,const pn_text_source_t *,pn_book_id_t *);
/// 摘要读块同时严格探测编码/检查点，常见UTF编码只扫一次；GBK保持完整重试。/ Strictly probe encoding/checkpoints while reading digest blocks, scanning common UTF encodings once; GBK retains a complete retry.
pn_status_t pn_text_file_inspect(pn_pool_t *,const pn_text_source_t *,pn_book_id_t *,pn_text_encoding_t *,void (*)(void *,uint64_t,bool),void *);
/// 校验原已验证句柄仍有效；TF依靠独占所有权/READ租约，主机对句柄和现路径全SHA，不接受size/mtime命中。/ Verify the original validated handle: TF relies on exclusive ownership/READ leases; hosts hash both handle/current path completely, never accepting size/mtime hits.
pn_status_t pn_text_file_verify_retained(pn_pool_t *,const pn_text_source_t *);
/// 与源接口相同，但直接校验稳定文件对象。/ Same retained-source check through a stable file object.
pn_status_t pn_text_file_verify_handle(pn_pool_t *,pn_text_file_t *);
/// 打开普通文件，不改变source失败输出；最大INT32_MAX bytes以兼容目标fseek。
/// Open a regular file, preserving source output on failure; cap at INT32_MAX bytes for target fseek compatibility.
pn_status_t pn_text_file_open(pn_text_file_t *file,pn_media_t *media,const pn_media_lease_t *lease,
    const char *path,pn_text_source_t *source);
/// 即使旧介质也关闭句柄；所有reader先停止，调用方随后释放租约。
/// Close handles even on expired media; stop all readers first, then caller releases the lease.
pn_status_t pn_text_file_close(pn_text_file_t *file);
