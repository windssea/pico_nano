/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：全局翻页偏好（左右手、滑动、边缘点按、三键）的内部A/B记录。
 * English: internal A/B record of global page-turn preferences (hand, swipe, edge taps, keys).
 * 冻结：未知版本或标志位拒绝；无记录即默认。/ Frozen: unknown versions or flag bits are rejected; no record means defaults.
 */
#pragma once
#include "pn_journal.h"
#define PN_INPUT_LEFT_HAND 0x01u ///< 左手模式 / Left-hand mode
#define PN_INPUT_NO_SWIPE 0x02u ///< 关闭滑动 / Swipe off
#define PN_INPUT_NO_EDGE_TAP 0x04u ///< 关闭边缘点按 / Edge taps off
#define PN_INPUT_NO_KEYS 0x08u ///< 关闭三键翻页 / Key turns off
#define PN_INPUT_REFRESH_MASK 0x30u ///< 刷新策略（位4–5：0均衡、1清晰、2省电）/ Refresh policy (bits 4–5: 0 balanced, 1 crisp, 2 saver)
#define PN_INPUT_FLAGS_ALL 0x3Fu
/// 绑定root下input.a/b，不建目录。/ Bind input.a/b under root without creating directories.
pn_status_t pn_input_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io);
/// 读取标志；无记录EMPTY；错误不改输出。/ Load flags; EMPTY without a record; errors keep the output.
pn_status_t pn_input_prefs_load(const pn_journal_io_t *io,uint8_t *flags);
/// 保存标志，失败可能已提交。/ Save flags; failures may still have committed.
pn_status_t pn_input_prefs_save(const pn_journal_io_t *io,uint8_t flags);
