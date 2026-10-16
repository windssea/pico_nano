/* SPDX-License-Identifier: Apache-2.0
 * 中文：诊断镜像的USB串口测试控制台；产品镜像不编译其实现。
 * English: USB serial test console for diagnostic images; product images compile no implementation. */
#pragma once
#include "device_async.h"
#include <stddef.h>
#if PN_DEVICE_TEST_CONSOLE
/// 由显示任务调用：给出当前已显示参考缓冲（板级布局）及是否可信；不得分配或阻塞。
/// Called on the display task: return the shown reference buffer (board layout) and whether it is trustworthy; must not allocate or block.
typedef bool (*pn_test_console_shown_fn)(const uint8_t **buffer,size_t *bytes);
/// 在异步端口启动后调用一次：安装串口驱动、发布输入/显示钩子并启动低优先级控制台任务。失败只记日志，不影响产品。
/// Call once after the async port starts: install the serial driver, publish input/display hooks and start a low-priority console task. Failures only log and never affect the product.
void pn_device_test_console_start(pn_device_async_t *port,pn_test_console_shown_fn shown);
/// 锁屏等待中消费一次控制台电源指令，作为唤醒按键；输入任务暂停时由睡眠适配器调用。
/// Consume one console power command as a wake key during the lock wait; called by the sleep adapter while the input task is paused.
bool pn_device_test_console_take_power(void);
#endif
