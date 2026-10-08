/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：异步装配上传、WiFi与HTTP，主界面仅访问状态快照。
 * English: asynchronously assemble uploads, WiFi and HTTP; the main UI accesses snapshots only.
 * 冻结：调用前归还所有TF读租约，released前主界面不得访问media。
 * Frozen: return all TF read leases before opening; main UI must not access media before released.
 */
#pragma once
#include "device_wifi.h"
#include "pn_transfer_worker.h"
#include "pn_network_store.h"
typedef enum {PN_DTRANSFER_STARTING,PN_DTRANSFER_READY,PN_DTRANSFER_STOPPING,PN_DTRANSFER_STOPPED,PN_DTRANSFER_FAILED} pn_device_transfer_phase_t; ///< 生命周期 / Lifecycle
typedef struct {
 pn_device_transfer_phase_t phase; ///< 当前阶段 / Current phase
 bool released; ///< 所有后台资源已停止，media已归还 / All background resources stopped, media returned
 pn_status_t error; ///< 最初失败或清理错误 / Initial failure or cleanup error
 pn_device_wifi_state_t network; ///< 无STA秘密的网络快照 / Network snapshot without STA secrets
 pn_transfer_worker_state_t work; ///< 任务快照 / Worker snapshot
 char pin[7]; ///< 显示用配对码 / Display pairing code
} pn_device_transfer_state_t;
typedef struct {void *impl;} pn_device_transfer_t; ///< 活动对象不可复制 / Never copy a live object
/// 立即移交media，独立任务装配；root为可信已挂载根，ESP固定/sdcard。
/// Immediately hand off media and assemble on a dedicated task; trusted mounted root, fixed /sdcard on ESP.
pn_status_t pn_device_transfer_open(pn_device_transfer_t *,pn_media_t *,const char *,const pn_device_wifi_config_t *);
/// 请求停止或重试清理，不等待文件校验/HTTP退出。/ Request stop or cleanup retry without waiting for validation/HTTP exit.
pn_status_t pn_device_transfer_request_stop(pn_device_transfer_t *);
/// 锁保护快照，不从UI访问后台对象。/ Locked snapshot without UI access to background objects.
pn_status_t pn_device_transfer_state(pn_device_transfer_t *,pn_device_transfer_state_t *);
/// released前BUSY；released后join并释放，保留已保存的阅读位置与上传记录。
/// BUSY before released; join and release afterwards, preserving saved reading positions and upload records.
pn_status_t pn_device_transfer_close(pn_device_transfer_t *);
/// 取出网页提交的配网请求（主任务调用后写内部存储）；无请求EMPTY。/ Take a provisioning request from the web page (main task then writes internal storage); EMPTY when none.
pn_status_t pn_device_transfer_take_network(pn_device_transfer_t *,pn_network_credentials_t *,bool *forget);
/// 主任务回报已保存名称与结果，供网页查询；初始化时以PN_EMPTY播种。/ The main task reports the saved name and result for the web page; seed with PN_EMPTY at start.
pn_status_t pn_device_transfer_network_result(pn_device_transfer_t *,pn_status_t,const char *saved_ssid);
