/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源屏幕键盘：数字行、大小写字母层与符号层，覆盖全部可打印ASCII；只绘制、命中和切换层，不保存输入。
 * English: shared on-screen keyboard with a digit row, upper/lower-case letters and a symbol layer covering every printable ASCII character; it only draws, hit-tests and switches layers, never storing input.
 */
#pragma once
#include "pn_font.h"
#include "pn_frame.h"
#include <stdbool.h>
#include <stddef.h>

#define PN_KB_ROWS 5 ///< 键盘行数 / Keyboard rows
#define PN_KB_ROW_H 96 ///< 行距（含8px间隙）/ Row pitch including an 8 px gap
#define PN_KB_HEIGHT (PN_KB_ROWS*PN_KB_ROW_H) ///< 键盘总高 / Total keyboard height
#define PN_KB_NONE (-1) ///< 空白或键间隙 / Blank space or a key gap
#define PN_KB_SHIFT (-2) ///< 切换大小写（锁定式）/ Toggle upper case (latching)
#define PN_KB_LAYER (-3) ///< 字母层/符号层切换 / Switch between letter and symbol layers
#define PN_KB_DELETE (-4) ///< 删除末位字符 / Delete the last character
/// 字符键命中返回其ASCII码（32空格至126）。/ Character keys return their ASCII code (32 for space through 126).

/// 键盘状态：只有当前层与大写锁定。/ Keyboard state: just the active layer and the caps latch.
typedef struct {
    bool symbols; ///< true为符号层 / True on the symbol layer
    bool caps; ///< 字母层大写锁定 / Caps latch on the letter layer
} pn_keyboard_t;

/// 回到小写字母层。/ Return to the lower-case letter layer.
void pn_keyboard_reset(pn_keyboard_t *kb);
/// 在[y, y+PN_KB_HEIGHT)绘制全宽键盘；字体字号临时改变后恢复。/ Draw the full-width keyboard in [y, y+PN_KB_HEIGHT), restoring the font size afterwards.
pn_status_t pn_keyboard_render(const pn_keyboard_t *kb,pn_font_t *font,pn_frame_t *frame,int y);
/// 命中：字符键返回ASCII，功能键返回PN_KB_*，其余PN_KB_NONE。/ Hit test: characters return ASCII, function keys PN_KB_*, anything else PN_KB_NONE.
int pn_keyboard_hit(const pn_keyboard_t *kb,int y,int px,int py);
/// 处理SHIFT/LAYER并返回true；其他代码不改状态返回false。/ Apply SHIFT/LAYER and return true; other codes leave the state unchanged and return false.
bool pn_keyboard_toggle(pn_keyboard_t *kb,int code);

/// 追加一个可打印ASCII；超出max字节或cap返回false。/ Append one printable ASCII character; false beyond max bytes or the capacity.
bool pn_text_field_append(char *text,size_t cap,size_t max,int code);
/// 删除末尾一个完整UTF-8字符；空时返回false。/ Delete the last complete UTF-8 character; false when empty.
bool pn_text_field_delete(char *text);
