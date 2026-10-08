/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：传输WiFi会话的AP/STA生命周期与有限重试，凭据仅在RAM。
 * English: transfer-session AP/STA lifecycle with bounded retries and RAM-only credentials.
 * 冻结：不写NVS/不自动换模式；调用前独占RF/ADC，关闭前先停止HTTP。
 * Frozen: no NVS writes or automatic mode switching; require exclusive RF/ADC before opening and stop HTTP before closing.
 */
#pragma once
#include "pn_types.h"
#include <stdbool.h>
typedef enum {PN_WIFI_AP=1,PN_WIFI_STA} pn_device_wifi_mode_t; ///< 显式网络模式 / Explicit network mode
typedef enum {PN_WIFI_STARTING,PN_WIFI_CONNECTING,PN_WIFI_READY,PN_WIFI_FAILED,PN_WIFI_STOPPING} pn_device_wifi_phase_t; ///< 会话阶段 / Session phase
typedef struct {
 pn_device_wifi_mode_t mode; ///< 显式选择的模式 / Explicitly selected mode
 char ssid[33]; ///< STA名称，最多32字节 / STA name, at most 32 bytes
 char password[64]; ///< STA口令，可空代表显式开放网络 / STA password, empty for an explicitly selected open network
} pn_device_wifi_config_t;
typedef struct {
 pn_device_wifi_mode_t mode; ///< 当前模式 / Current mode
 pn_device_wifi_phase_t phase; ///< 当前阶段 / Current phase
 char ssid[33]; ///< 显示名称 / Display name
 char ap_password[13]; ///< 本次AP口令，STA始终为空 / Current AP password, always empty for STA
 char address[16]; ///< 已确认IPv4，无地址时为空 / Confirmed IPv4, empty when unavailable
 uint64_t generation; ///< 网络身份变化代次 / Network identity change generation
 unsigned attempts,clients; ///< STA连接次数/AP客户端数 / STA connection attempts or AP client count
 int error; ///< SDK错误供控制层翻译 / SDK error for controller translation
} pn_device_wifi_state_t;
typedef struct {void *impl;} pn_device_wifi_t; ///< 活动会话不复制 / Never copy a live session
/// 单owner打开，不取得TF租约；短暂SAR熵窗口必须在RF/ADC未使用时完成。
/// Single-owner opening without TF leases; finish the brief SAR entropy window before RF/ADC use.
pn_status_t pn_device_wifi_open(pn_device_wifi_t *,const pn_device_wifi_config_t *);
/// owner驱动连接/地址确认，回调只更新锁保护状态。/ Owner drives connection/address confirmation; callbacks update locked state only.
pn_status_t pn_device_wifi_poll(pn_device_wifi_t *);
/// 锁保护只读快照，不暴露STA口令。/ Locked read-only snapshot without exposing STA passwords.
pn_status_t pn_device_wifi_state(pn_device_wifi_t *,pn_device_wifi_state_t *);
/// HTTP/worker停止后调用；失败保留对象重试，不销毁未注销回调的上下文。
/// Call after HTTP/worker stop; failures retain the object for retry, never destroy contexts with registered callbacks.
pn_status_t pn_device_wifi_close(pn_device_wifi_t *);
