/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界A/B记录与文件适配；调用方提供稳定路径和有效租约。
 * English: bounded A/B records and file adapter; caller provides stable paths and valid leases.
 * 冻结：不格式化、不创建目录；owner串行使用，路径不可指向同一文件。
 * Frozen: no formatting or directory creation; serialized owner calls, distinct backing files.
 */
#pragma once
#include "pn_storage.h"
#define PN_JOURNAL_PAYLOAD_MAX 256
#define PN_JOURNAL_RECORD_MAX (20 + PN_JOURNAL_PAYLOAD_MAX)
#define PN_JOURNAL_PATH_MAX 384

typedef struct {
    uint64_t sequence; ///< 单调序号，零无效 / Monotonic sequence, zero invalid
    size_t size; ///< 有效载荷长度 / Payload length
    uint8_t payload[PN_JOURNAL_PAYLOAD_MAX]; ///< 不透明载荷 / Opaque payload
} pn_record_t;
typedef struct {
    void *ctx; ///< 适配上下文 / Adapter context
    pn_status_t (*read)(void *, unsigned, uint8_t *, size_t, size_t *); ///< EMPTY仅代表不存在 / EMPTY means absent only
    pn_status_t (*write_sync)(void *, unsigned, const uint8_t *, size_t); ///< 写入并同步 / Write and synchronize
} pn_journal_io_t;
typedef struct {
    pn_media_t *media; ///< 存储owner / Storage owner
    pn_media_lease_t lease; ///< 调用期间必须有效 / Must remain valid during calls
    char paths[2][PN_JOURNAL_PATH_MAX]; ///< 调用方专有路径 / Caller-owned paths
} pn_journal_files_t;
/// 读取最新完整记录；错误不改输出；未知版本和I/O错误阻止猜测。
/// Read newest complete record; errors preserve output; unknown versions and I/O errors prevent guessing.
pn_status_t pn_journal_load(const pn_journal_io_t *io, pn_record_t *record);
/// 写入另一槽、同步并读回；失败仍可能已提交，重启必须重新load。
/// Write alternate slot, synchronize and read back; failure may still commit, so reload after restart.
pn_status_t pn_journal_save(const pn_journal_io_t *io, const uint8_t *payload, size_t size);
/// 绑定两条不同路径和租约；load需要READ/WRITE，save需要WRITE。
/// Bind two distinct paths and a lease; load requires READ/WRITE, save requires WRITE.
pn_status_t pn_journal_files_init(pn_journal_files_t *files, pn_media_t *media,
    const pn_media_lease_t *lease, const char *a, const char *b, pn_journal_io_t *io);
