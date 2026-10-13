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

typedef struct {
    FILE *handle; ///< 首次使用前置零；port独占stdio读/定位，外部不得操作 / Zero before use; the port exclusively owns stdio reads/seeks
    pn_media_t *media; ///< 存储owner / Storage owner
    pn_media_lease_t lease; ///< 调用方保持至close之后 / Caller keeps alive until after close
    uint64_t size; ///< 不变源大小 / Immutable source size
    uint8_t *block; ///< 4KiB读缓存，首次小读时分配，close释放；分配失败则直接读 / 4 KiB read cache, allocated on the first small read and freed by close; reads go straight through if allocation fails
    uint64_t block_start; ///< 缓存块在文件中的起点 / File offset of the cached block
    size_t block_length; ///< 缓存块有效字节数 / Valid bytes in the cached block
    uint64_t read_position; ///< 最近成功stdio读取后的实际游标，不是缓存命中位置 / Actual stdio cursor after the last successful read, independent of cache hits
    bool read_position_known; ///< 读/定位失败后失效，下次强制定位 / Invalid after read/seek failures; the next read must seek
    pn_book_id_t identity; ///< 仅当前不可变句柄/租约的已验证摘要 / Verified digest for this immutable handle and lease only
    bool identity_known; ///< close/reopen失效，不按路径或mtime复用 / Invalidated by close/reopen; never reused by path or mtime
    uint64_t io_reads,io_bytes,io_seeks,cache_hits,identity_hits; ///< 实际读取/定位与源缓存计数 / Actual reads/seeks and source-cache counters
} pn_text_file_t;
/// 仅识别本port原始source；未知UNSUPPORTED，成功摘要绑定当前句柄/租约，失败不改输出。/ Recognize only this port's original source; unknown sources return UNSUPPORTED; bind successful digests to the live handle/lease and preserve output on error.
pn_status_t pn_text_file_identity(const pn_text_source_t *,pn_book_id_t *);
/// 打开普通文件，不改变source失败输出；最大INT32_MAX bytes以兼容目标fseek。
/// Open a regular file, preserving source output on failure; cap at INT32_MAX bytes for target fseek compatibility.
pn_status_t pn_text_file_open(pn_text_file_t *file,pn_media_t *media,const pn_media_lease_t *lease,
    const char *path,pn_text_source_t *source);
/// 即使旧介质也关闭句柄；所有reader先停止，调用方随后释放租约。
/// Close handles even on expired media; stop all readers first, then caller releases the lease.
pn_status_t pn_text_file_close(pn_text_file_t *file);
