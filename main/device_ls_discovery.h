/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "pn_types.h"
typedef struct {void *impl;} pn_device_ls_discovery_t; ///< 独立UDP发现owner / Separate UDP discovery owner
/// 按LocalSend组播发布HTTP接收地址，JSON中不包含接收授权。
/// Advertise the HTTP receiver over LocalSend multicast without embedding receive grants.
pn_status_t pn_device_ls_discovery_open(pn_device_ls_discovery_t *out,const char *address,uint16_t port,const char *fingerprint);
/// 停止并join发现任务后释放socket，失败保留owner供重试。
/// Stop and join discovery before releasing its socket, retaining the owner on failure for retry.
pn_status_t pn_device_ls_discovery_close(pn_device_ls_discovery_t *out);
