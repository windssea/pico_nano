/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "pn_storage.h"
/// USB交接与恢复阶段。/ USB handoff and recovery phases.
typedef enum {
    PN_USB_STARTING, ///< 准备并移交 / Prepare and hand off
    PN_USB_READY, ///< 主机可访问 / Host access enabled
    PN_USB_STOPPING, ///< 停止回调和驱动 / Stop callbacks and drivers
    PN_USB_RESTORING, ///< 本地非破坏重挂 / Non-destructive local remount
    PN_USB_FAILED, ///< 保留状态供重试 / Retained state for retry
    PN_USB_DONE ///< 完整释放 / Fully released
} pn_device_usb_phase_t;
/// USB端口操作；仅产品owner调用，stop成功保证回调和卡访问已排空。/ USB port operations on product owner; successful stop guarantees drained callbacks/card access.
typedef struct {
    void *ctx; ///< 端口上下文 / Port context
    pn_status_t (*unmount)(void *ctx); ///< 同步卸载本地FAT / Sync and unmount local FAT
    pn_status_t (*start)(void *ctx); ///< 开始独占MSC / Start exclusive MSC
    pn_status_t (*stop)(void *ctx,uint64_t now); ///< 可重试停止屏障 / Retryable stop barrier
    pn_status_t (*remount)(void *ctx); ///< 非破坏重挂，BUSY表示尚在探测 / Non-destructive remount, BUSY while probing
    bool (*connected)(void *ctx); ///< 真实主机枚举状态 / Actual host enumeration state
} pn_device_usb_port_t;
/// 不可移动的USB交接状态；不向应用暴露块设备。/ Immovable USB handoff state; no block-device access for applications.
typedef struct {
    pn_device_usb_port_t port; ///< 板级或主机端口 / Board or host port
    pn_media_t *media; ///< 独占的TF服务 / Exclusive TF service
    pn_media_lease_t lease; ///< USB租约 / USB lease
    pn_device_usb_phase_t phase; ///< 对外状态 / Visible phase
    pn_status_t error; ///< 最后错误 / Last error
    uint64_t media_id; ///< 重挂的介质标识 / Identity for reattachment
    bool open,stop_requested,unmounted,backend,detached,released; ///< 所有权阶段 / Ownership stages
} pn_device_usb_t;
/// 已关闭全部TF消费者后进入；失败不启动端口。/ Enter after closing all TF consumers; failures never start the port.
pn_status_t pn_device_usb_open(pn_device_usb_t *usb,pn_media_t *media,pn_device_usb_port_t port);
/// 推进一个交接阶段；失败保留所有权供停止重试。/ Advance one handoff stage; failures retain ownership for stop retry.
pn_status_t pn_device_usb_poll(pn_device_usb_t *usb,uint64_t now);
/// 请求结束或重试停止，完成前不允许导航/睡眠。/ Request end or retry stop; no navigation/sleep before completion.
void pn_device_usb_request_stop(pn_device_usb_t *usb);
/// 仅完整停止后关闭状态。/ Close state only after the complete barrier.
pn_status_t pn_device_usb_close(pn_device_usb_t *usb);
/// 当前运行环境的端口；主机默认不可用。/ Port for this runtime, unavailable by default on host.
pn_device_usb_port_t pn_device_usb_board_port(void);
