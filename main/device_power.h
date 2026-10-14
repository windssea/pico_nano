/* SPDX-License-Identifier: Apache-2.0
 * 中文：动态调频端口；工作owner持有最高频锁，空闲时降到下限。不改PMU、VCOM或面板时序。
 * English: dynamic-frequency port; working owners hold the max-frequency lock and idle drops to the floor. No PMU, VCOM or panel-timing changes. */
#pragma once
#include <stdbool.h>
/// 持锁owner，各自只由一个任务修改。/ Lock owners, each changed by a single task only.
typedef enum {
    PN_POWER_PRODUCT=0, ///< 产品worker处理输入/排版/保存 / Product worker handling input, layout and saving
    PN_POWER_DISPLAY,   ///< 显示任务扫描面板（行供数依赖CPU）/ Display task scanning the panel (line feeding is CPU-bound)
    PN_POWER_SESSION,   ///< 无线传书或对时会话 / Wireless transfer or time-sync session
    PN_POWER_OWNERS
} pn_device_power_owner_t;
/// 配置调频与锁；失败或未启用时所有调用为空操作并保持最高频。/ Configure DFS and locks; on failure or when disabled every call is a no-op and the CPU stays at maximum.
void pn_device_power_init(void);
/// 设置owner是否需要最高频；重复设置同值不重复取放锁。/ Set whether an owner needs maximum frequency; repeating the same value does not re-acquire or release.
void pn_device_power_busy(pn_device_power_owner_t owner,bool busy);
/// owner当前是否持锁（主机测试与日志）。/ Whether an owner currently holds its lock (host tests and logs).
bool pn_device_power_held(pn_device_power_owner_t owner);
/// 调频是否已生效。/ Whether DFS is active.
bool pn_device_power_active(void);
