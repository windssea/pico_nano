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
#include "pn_types.h"
#include <stdbool.h>
#include <stddef.h>
#define PN_MEDIA_MAX_LEASES 16

typedef enum {
    PN_MEDIA_READ=1, ///< 可共享读取 / Shared reading
    PN_MEDIA_WRITE, ///< 独占写入 / Exclusive writing
    PN_MEDIA_USB ///< 独占主机访问 / Exclusive host access
} pn_media_access_t;

typedef struct pn_media pn_media_t;
typedef struct {
    pn_media_t *owner; ///< 原服务，不可跨对象使用 / Original service, never cross-use objects
    uint64_t ticket; ///< 不重复租约号 / Nonrepeating lease ID
    uint64_t epoch; ///< 取得时的介质代次 / Media generation at acquisition
    pn_media_access_t access; ///< 租约访问方式 / Lease access mode
} pn_media_lease_t;

struct pn_media {
    uint64_t epoch; ///< 当前介质代次 / Current media generation
    uint64_t media_id; ///< 介质实例标识，不是书籍摘要 / Media instance ID, not a book hash
    uint64_t next_ticket; ///< 下一个不重复租约号 / Next nonrepeating lease ID
    bool available; ///< 挂载服务已确认可用 / Mount service confirmed availability
    bool usb_pending; ///< 交接栅栏，阻止新读写 / Handoff fence blocking new readers/writers
    bool exhausted; ///< 代次耗尽后永久关闭 / Permanently closed after generation exhaustion
    pn_media_lease_t slots[PN_MEDIA_MAX_LEASES]; ///< 活动租约，旧代仍须释放 / Active leases, including old generations to release
};

/// 仅在首次使用或所有外部句柄消失后初始化；对象不可移动。
/// Initialize only before first use or after all external handles disappear; never move the object.
void pn_media_init(pn_media_t *media);
/// 实际非破坏挂载成功后标记可用；旧租约未释放时不允许换卡。
/// Mark available after actual non-destructive mount succeeds; old leases must drain before replacement.
pn_status_t pn_media_attach(pn_media_t *media, uint64_t media_id);
/// 立即使所有旧租约失效，但保留它们直到消费者停止并释放。
/// Expire all old leases immediately, retaining them until consumers stop and release them.
pn_status_t pn_media_detach(pn_media_t *media);
/// 取得当前代租约；READ共享，WRITE与USB独占；USB须先建交接栅栏，不等待或执行I/O。
/// Acquire a current lease; READ shares, WRITE and USB exclude; USB requires a handoff fence, with no waiting or I/O.
pn_status_t pn_media_acquire(pn_media_t *media, pn_media_access_t access, pn_media_lease_t *lease);
/// 检查访问/提交资格；不替代I/O期间的拔卡错误检测。
/// Check access/commit eligibility; does not replace media-loss detection during I/O.
pn_status_t pn_media_validate(const pn_media_t *media, const pn_media_lease_t *lease);
/// 旧代也可释放；重复释放或拷贝句柄不得减少其他租约。
/// Release even old generations; repeat releases or copied handles never reduce other leases.
pn_status_t pn_media_release(pn_media_t *media, pn_media_lease_t *lease);
/// 建立USB交接栅栏，已有消费者可结束；返回成功不代表已经交出介质。
/// Establish a USB handoff fence while existing consumers drain; success is not a completed handoff.
pn_status_t pn_media_request_usb(pn_media_t *media);
/// 取消未完成交接；USB活动时返回BUSY，必须先结束主机访问。
/// Cancel a pending handoff; active USB returns BUSY until host access ends.
pn_status_t pn_media_cancel_usb(pn_media_t *media);
/// 活动句柄数量包含已失效但未释放的租约。
/// Count includes expired leases that have not yet been released.
size_t pn_media_active(const pn_media_t *media);
