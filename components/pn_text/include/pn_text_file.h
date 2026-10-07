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
    FILE *handle; ///< 首次使用前置零 / Zero before first use
    pn_media_t *media; ///< 存储owner / Storage owner
    pn_media_lease_t lease; ///< 调用方保持至close之后 / Caller keeps alive until after close
    uint64_t size; ///< 不变源大小 / Immutable source size
} pn_text_file_t;
/// 打开普通文件，不改变source失败输出；最大INT32_MAX bytes以兼容目标fseek。
/// Open a regular file, preserving source output on failure; cap at INT32_MAX bytes for target fseek compatibility.
pn_status_t pn_text_file_open(pn_text_file_t *file,pn_media_t *media,const pn_media_lease_t *lease,
    const char *path,pn_text_source_t *source);
/// 即使旧介质也关闭句柄；所有reader先停止，调用方随后释放租约。
/// Close handles even on expired media; stop all readers first, then caller releases the lease.
pn_status_t pn_text_file_close(pn_text_file_t *file);
