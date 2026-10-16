/* SPDX-License-Identifier: Apache-2.0
 * 中文：保存前的单次WiFi连接测试：只借用无线、拿到IP即成功并立即断开；不读TF、不保存凭据。
 * English: one-shot WiFi connection test before saving: borrow the radio, succeed on an IP address and disconnect at once; no TF access and no credential storage. */
#pragma once
#include "device_wifi.h"
typedef struct {
 bool released; ///< 无线已完整退出，结果可用 / Radio fully stopped and the result is final
 pn_status_t result; ///< OK已取得IP；EMPTY找不到网络；INVALID认证失败；IO超时/其他；CANCELLED取消 / OK got an IP; EMPTY no network; INVALID authentication; IO timeout/other; CANCELLED
 unsigned reason; ///< 最近SDK断开原因，0为无 / Latest SDK disconnect reason, zero when none
} pn_device_wifi_probe_state_t;
typedef struct {void *impl; ///< 初始NULL，released后close释放 / Initially NULL; close frees it after release
} pn_device_wifi_probe_t;
/// 复制凭据，首次poll才联网；不等待。/ Copy credentials and connect on the first poll without waiting.
pn_status_t pn_device_wifi_probe_open(pn_device_wifi_probe_t *,const pn_device_wifi_config_t *);
/// 无等待推进；总时限30秒。/ Advance without waiting, with a 30-second overall limit.
pn_status_t pn_device_wifi_probe_poll(pn_device_wifi_probe_t *,uint64_t now_ms);
/// 请求取消，继续poll至released。/ Request cancellation and keep polling until released.
pn_status_t pn_device_wifi_probe_stop(pn_device_wifi_probe_t *);
/// 不含凭据的快照。/ Credential-free snapshot.
pn_status_t pn_device_wifi_probe_state(const pn_device_wifi_probe_t *,pn_device_wifi_probe_state_t *);
/// released前BUSY。/ BUSY before release.
pn_status_t pn_device_wifi_probe_close(pn_device_wifi_probe_t *);
/// SDK断开原因归类为EMPTY/INVALID/IO。/ Classify an SDK disconnect reason as EMPTY, INVALID or IO.
pn_status_t pn_device_wifi_probe_classify(unsigned reason);
