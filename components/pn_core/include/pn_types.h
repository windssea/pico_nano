/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：共享状态契约，供存储与显示服务使用。
 * English: shared state contracts for storage and display services.
 *
 * 冻结：仅由服务owner串行调用；不执行文件或硬件操作。
 * Frozen: serialized calls by the service owner only; no file or hardware operations.
 */
#pragma once
#include <stdint.h>

typedef enum {
    PN_OK=0, ///< 成功 / Success
    PN_INVALID, ///< 参数或句柄无效 / Invalid argument or handle
    PN_BUSY, ///< 资源占用 / Resource busy
    PN_EMPTY, ///< 无排队任务 / No queued job
    PN_LIMIT, ///< 资源或序号耗尽 / Resource or sequence exhausted
    PN_NO_MEMORY, ///< 分配失败 / Allocation failed
    PN_STALE_MEDIA, ///< 介质已换代 / Media generation expired
    PN_STALE_JOB, ///< 任务已过期 / Job expired
    PN_CANCELLED, ///< 已取消 / Cancelled
    PN_UNSUPPORTED, ///< 不支持 / Unsupported
    PN_CORRUPT, ///< 内容损坏 / Corrupt content
    PN_IO ///< 输入输出失败 / I/O failure
} pn_status_t;

typedef struct {
    uint64_t session; ///< 单调递增会话号，零无效 / Monotonic session ID, zero invalid
    uint32_t generation; ///< 会话内递增代次，零无效 / Increasing session generation, zero invalid
} pn_job_token_t;
