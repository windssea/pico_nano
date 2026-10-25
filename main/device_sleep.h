/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：锁屏后的浅睡，只由电源键（经PMU与IO扩展中断）唤醒。
 * English: light sleep after locking, unlocked only by the PMU-confirmed power key, with IO-expander IRQ and timer checks.
 * 冻结：调用前已保存位置、挂起或关闭正文、锁屏已呈现且显示轨关闭；不写PMU配置、不切EN、不访问TF。
 * Frozen: callers have saved positions, suspended or closed the book, presented the lock page and powered the panel off; no PMU configuration writes, EN changes or TF access.
 */
#pragma once
#include <stdbool.h>
/// 等电源键松开后浅睡，IOE_INT唤醒与500ms复核均读取PMU；配置/睡眠失败或按键未空闲时返回false，调用方继续轮询解锁。
/// Light-sleep after key release, checking the PMU on IOE_INT wake and every 500 ms; return false on setup/sleep failure or a held key so callers keep polling to unlock.
bool pn_device_sleep_until_key(void);
/// 浅睡期间没有非电源GPIO唤醒/失败（定时复核除外）；仍须校验挂载与租约。/ No non-key GPIO wake/failure during light sleep (timer checks excluded); still validate mount and leases.
bool pn_device_sleep_media_unchanged(void);
/// 首次失去介质连续性证明的原因；仅诊断，不判定真实拔卡。/ First loss of media-continuity proof; diagnostic only, not proof of card removal.
const char *pn_device_sleep_media_reason(void);
