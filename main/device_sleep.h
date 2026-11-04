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
#include <stdint.h>
/// 一次睡眠等待的RAM诊断；成功调用耗时含SDK进出开销，不是实测电流。/ RAM diagnostics for one sleep wait; successful-call time includes SDK entry/exit overhead, not measured current.
typedef struct {
    uint64_t elapsed_us; ///< 等待总时间 / Total wait time
    uint64_t sleep_call_us; ///< 成功浅睡调用累计时间（含进出开销）/ Time in successful light-sleep calls, including transitions
    unsigned attempts,successes; ///< 真实SDK调用与成功数；轮询诊断均为0 / Actual SDK calls and successes; both zero in polling diagnostics
    unsigned timer,gpio,other; ///< 成功调用的唤醒原因 / Wake causes of successful calls
    unsigned pending,held,timer_only; ///< 挂起输入、持续低与仅定时器睡眠轮次 / Pending inputs, held-low samples and timer-only sleep rounds
    unsigned irq_cooldowns; ///< 连续快速GPIO唤醒触发的冷却次数 / Cooldowns triggered by consecutive fast GPIO wakes
    int error; ///< 最近SDK失败码，0表示没有SDK错误 / Latest SDK error, zero if none
    const char *outcome; ///< 静态结果说明，不能充当介质证明 / Static outcome text, never media proof
} pn_device_sleep_stats_t;
/// 同一product owner读取最近一次完整等待的统计；不写盘、不访问硬件。/ The product owner reads the last completed wait; no disk or hardware access.
pn_device_sleep_stats_t pn_device_sleep_stats(void);
/// 等电源键松开后浅睡，IOE_INT唤醒与500ms复核均读取PMU；配置/睡眠失败或按键未空闲时返回false，调用方继续轮询解锁。
/// Light-sleep after key release, checking the PMU on IOE_INT wake and every 500 ms; return false on setup/sleep failure or a held key so callers keep polling to unlock.
bool pn_device_sleep_until_key(void);
/// 输入暂停后、外围停车前观察原始输入；只有读成功且PGOOD低才准许停车，不消费PMU按键。/ Observe raw inputs after input pause and before parking; allow parking only after a valid PGOOD-low read, without consuming PMU keys.
bool pn_device_sleep_handoff_begin(void);
/// 外围恢复后、正文恢复前复核卡位/PGOOD/未知IRQ，覆盖触摸复位等待窗口。/ Recheck card/PGOOD/unknown IRQ after peripheral restore and before body resume, covering the touch-reset wait.
void pn_device_sleep_handoff_end(void);
/// 浅睡期间没有卡变化、读取失败或未归因中断；仅PMU输入变化与定时复核不使证明失效，仍须校验挂载与租约。/ No card change, read failure or unattributed IRQ during sleep; PMU-only input transitions and timers preserve continuity, with mount/lease validation still required.
bool pn_device_sleep_media_unchanged(void);
/// 首次失去介质连续性证明的原因；仅诊断，不判定真实拔卡。/ First loss of media-continuity proof; diagnostic only, not proof of card removal.
const char *pn_device_sleep_media_reason(void);
