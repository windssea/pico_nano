/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "pn_types.h"
/// 读取FCA9555 P0原始输入；失败不修改输出，不改变挂载缓存。/ Read raw FCA9555 P0 inputs; leave output unchanged on failure and never mutate mount caches.
pn_status_t pn_device_card_inputs(uint8_t *inputs);
/// 只读卡检测：OK有卡，STALE_MEDIA确认无卡，IO读取失败；不改变挂载缓存。
/// Read-only card detect: OK present, STALE_MEDIA confirmed absent, IO read failure; never mutate mount caches.
pn_status_t pn_device_card_presence(void);
