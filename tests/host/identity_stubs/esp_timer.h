/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
/// 测试单调时钟，读源可推进时间。/ Test monotonic clock, advanced by source reads.
int64_t esp_timer_get_time(void);
