/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：锁屏后的浅睡，只由电源键（经PMU与IO扩展中断）唤醒。
 * English: light sleep after locking, woken only by the power key via the PMU and IO-expander interrupt.
 * 冻结：调用前已保存位置、关闭正文、锁屏已呈现且显示轨关闭；不写PMU配置、不切EN、不访问TF。
 * Frozen: callers have saved positions, closed the book, presented the lock page and powered the panel off; no PMU configuration writes, EN changes or TF access.
 */
#pragma once
#include <stdbool.h>
/// 等电源键松开后浅睡，直到PMU确认按键唤醒；唤醒源配置失败返回false，调用方继续轮询解锁。
/// Light-sleep after the power key is released until the PMU confirms a key wake; returns false if the wake source cannot be armed, so callers keep polling to unlock.
bool pn_device_sleep_until_key(void);
