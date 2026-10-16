/* SPDX-License-Identifier: Apache-2.0
 * 中文：签名清单、非活动槽写入与显式启动提交的同源契约。
 * English: shared signed-manifest, inactive-slot writing and explicit boot-commit contract. */
#pragma once
#include "pn_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PN_OTA_PAYLOAD_BYTES 512u
#define PN_OTA_MANIFEST_BYTES 576u
#define PN_OTA_IMAGE_PREFIX 288u
#define PN_OTA_SLOT_BYTES 0x600000u
#define PN_OTA_MAX_IMAGE (PN_OTA_SLOT_BYTES * 9u / 10u)
#define PN_OTA_GITEE_ROOT "https://gitee.com/windssea/pico_nano/releases/download/"
#define PN_OTA_LATEST_URL "https://gitee.com/api/v5/repos/windssea/pico_nano/releases/latest"

typedef enum {
    PN_OTA_IDLE, PN_OTA_ACCEPTED, PN_OTA_WRITING, PN_OTA_VERIFIED,
    PN_OTA_COMMITTED, PN_OTA_CANCELLED, PN_OTA_FAILED
} pn_update_phase_t; ///< 只有COMMITTED允许重启进入新槽 / Only COMMITTED permits rebooting into the new slot

typedef struct {
    uint32_t image_bytes; ///< 应用完整长度 / Complete application length
    uint32_t bootloader_bytes; ///< 已资格引导程序的精确长度 / Exact qualified bootloader length
    uint8_t image_sha256[32]; ///< 由签名保护的应用摘要 / Signed application digest
    uint8_t bootloader_sha256[32]; ///< 由签名保护的已资格引导程序摘要 / Signed qualified-bootloader digest
    char version[32]; ///< 不含诊断后缀的三段版本 / Three-part version without a diagnostic suffix
    char source_commit[41]; ///< 精确固件源码提交 / Exact firmware source commit
    char url[256]; ///< 固定Gitee发行库下的HTTPS直链 / HTTPS attachment URL in the fixed Gitee repository
    char notes[64]; ///< 经签名的短说明 / Signed short release note
} pn_update_manifest_t;

typedef struct {
    unsigned running_slot; ///< 必须为0或1 / Must be zero or one
    bool layout_compatible; ///< 实际双槽、otadata和数据布局已严格核对 / Actual slots, otadata and data layout were checked exactly
    bool running_confirmed; ///< 当前应用不是待确认或无效状态 / Current application is neither pending verification nor invalid
} pn_update_target_t;

typedef struct {
    pn_status_t (*verify_signature)(void *,const uint8_t *,size_t,const uint8_t *); ///< 对512B载荷验证ECDSA-P256/SHA-256，签名为r||s / Verify ECDSA-P256/SHA-256 over 512 bytes with an r||s signature
    pn_status_t (*check_bootloader)(void *,uint32_t,const uint8_t *); ///< 只读核对已资格引导字节，失败不得擦应用槽 / Read-only verification of qualified boot bytes before any app erase
    pn_status_t (*begin)(void *,unsigned,uint32_t); ///< 仅打开指定非活动槽 / Open only the specified inactive slot
    pn_status_t (*write)(void *,const uint8_t *,size_t); ///< 顺序写入，失败不可重用该事务 / Sequential write; a failed transaction cannot be reused
    pn_status_t (*seal)(void *); ///< 由SDK验证完整镜像并结束句柄，不切启动 / SDK validates the complete image and ends the handle without switching boot
    pn_status_t (*verify_flash)(void *,uint32_t,const uint8_t *); ///< 读回整个非活动应用并核对SHA-256 / Read back the entire inactive image and verify SHA-256
    pn_status_t (*activate)(void *,unsigned); ///< 最终重核目标并原子提交otadata / Recheck the target and atomically commit otadata
    void (*abort)(void *); ///< 幂等释放写入句柄，不擦旧有效槽或切启动 / Release writes idempotently without erasing the old slot or changing boot
} pn_update_port_t;

typedef struct {
    pn_update_manifest_t manifest; ///< 已签名验证的元数据 / Signature-verified metadata
    pn_update_port_t port; ///< 端口副本，在活动期不可替换 / Copied port, immutable while active
    void *ctx; ///< 调用方保活的唯一端口上下文 / Caller-owned live port context
    pn_update_phase_t phase; ///< 可公开观察的阶段 / Publicly observable phase
    pn_status_t error; ///< 首个错误 / First error
    uint32_t received; ///< 已接收并交给写入事务的字节 / Bytes accepted into the write transaction
    size_t prefix_used; ///< 尚未交硬件的有界头部字节 / Bounded header bytes not yet handed to hardware
    uint8_t prefix[PN_OTA_IMAGE_PREFIX]; ///< 在擦写前验证真实镜像身份 / Verify actual image identity before erasing
    unsigned target_slot; ///< 始终与初始运行槽不同 / Always different from the initial running slot
    bool opened; ///< 端口写句柄已开始 / Port write handle has started
} pn_update_t;

/// 检查版本及严格固定Gitee直链；错误保持输出。 / Validate a version and strict fixed Gitee URL, preserving output on error.
pn_status_t pn_update_version(const char *,uint32_t *);
bool pn_update_gitee_url(const char *);
/// 验签、元数据/兼容/旧引导核对；不写Flash，错误保持out。 / Verify signature, metadata, compatibility and bootloader without flash writes; errors preserve out.
pn_status_t pn_update_prepare(pn_update_t *,const uint8_t *,size_t,const char *,const pn_update_target_t *,const pn_update_port_t *,void *);
/// 任意短块顺序供数，真实应用头通过前不擦槽。 / Feed arbitrary sequential short blocks without erasing before actual image-header validation.
pn_status_t pn_update_feed(pn_update_t *,const uint8_t *,size_t);
/// 长度、SDK完整性与Flash全内容摘要通过后进入VERIFIED，不切槽。 / Enter VERIFIED only after length, SDK integrity and full flash-digest checks, without switching slots.
pn_status_t pn_update_finish(pn_update_t *);
/// 只能从VERIFIED提交；调用方先完成网络/保存/电源屏障。 / Commit only from VERIFIED after the caller completes network, persistence and power barriers.
pn_status_t pn_update_commit(pn_update_t *);
/// COMMITTED后不可取消；其他阶段释放端口，不改变启动选择。 / Cancellation is disallowed after COMMITTED; otherwise release the port without changing boot.
pn_status_t pn_update_cancel(pn_update_t *);
