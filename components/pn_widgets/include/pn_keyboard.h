/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源屏幕键盘：数字行、大小写字母层、符号层与可选离线全拼中文输入法；只绘制、命中和更新键盘/缓冲状态。
 * English: shared on-screen keyboard with digits, upper/lower-case letters, symbols and optional offline full-pinyin Chinese IME; only draws, hit-tests and updates keyboard/buffer state.
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
#define PN_KB_DELETE (-4) ///< 删除末位字符或拼音字母 / Delete last char or pinyin letter
#define PN_KB_IME_TOGGLE (-5) ///< 中/英输入法切换 / Toggle Chinese Pinyin IME vs English
#define PN_KB_IME_PREV (-6) ///< 候选字上一页 / Previous candidate page
#define PN_KB_IME_NEXT (-7) ///< 候选字下一页 / Next candidate page
#define PN_KB_IME_RAW (-8) ///< 直接提交当前拼音原文 / Commit raw pinyin letters
#define PN_KB_IME_CLEAR (-9) ///< 清空当前拼音缓冲 / Clear active pinyin buffer
#define PN_KB_CAND_COUNT 6 ///< 每页候选汉字数 / Candidate hanzi per page
#define PN_KB_CAND_BASE (-20) ///< 候选字0..5：PN_KB_CAND_BASE - i / Candidates 0..5: PN_KB_CAND_BASE - i
#define PN_KB_PUNCT_COUNT 11 ///< 中文常用标点数 / Quick Chinese punctuation count
#define PN_KB_PUNCT_BASE (-40) ///< 中文标点0..10：PN_KB_PUNCT_BASE - i / Chinese punctuation 0..10: PN_KB_PUNCT_BASE - i
#define PN_KB_PINYIN_MAX 7 ///< 最长单音节拼音（zhuang=6）含结尾0 / Longest single syllable + NUL
/// 字符键命中返回其ASCII码（32空格至126）。/ Character keys return their ASCII code (32 for space through 126).

/// 键盘状态：当前层、大写锁定与可选全拼中文状态。/ Keyboard state: active layer, caps latch and optional full-pinyin Chinese state.
typedef struct {
    bool symbols; ///< true为符号层 / True on the symbol layer
    bool caps; ///< 字母层大写锁定 / Caps latch on the letter layer
    bool ime_enabled; ///< 是否允许切换全拼中文输入法 / Whether offline Chinese Pinyin IME is enabled
    bool chinese; ///< 当前处于全拼中文模式 / Whether currently in Chinese Pinyin mode
    char pinyin[PN_KB_PINYIN_MAX]; ///< 当前正在拼写的全拼缓冲 / Active pinyin composition buffer
    unsigned short cand_page; ///< 候选字页码（每页6字）/ Candidate page index (6 per page)
} pn_keyboard_t;

/// 回到小写字母层（关闭中文输入法）。/ Return to the lower-case letter layer (IME disabled).
void pn_keyboard_reset(pn_keyboard_t *kb);
/// 启用或禁用中文全拼输入法，并指定初始是否进入中文模式。/ Enable or disable Chinese Pinyin IME and choose initial mode.
void pn_keyboard_set_ime(pn_keyboard_t *kb,bool enabled,bool start_chinese);
/// 在[y, y+PN_KB_HEIGHT)绘制全宽键盘；字体字号临时改变后恢复。/ Draw the full-width keyboard in [y, y+PN_KB_HEIGHT), restoring the font size afterwards.
pn_status_t pn_keyboard_render(const pn_keyboard_t *kb,pn_font_t *font,pn_frame_t *frame,int y);
/// 命中：字符键返回ASCII，功能/候选/标点键返回PN_KB_*，其余PN_KB_NONE。/ Hit test: characters return ASCII, function/candidate/punctuation keys PN_KB_*, anything else PN_KB_NONE.
int pn_keyboard_hit(const pn_keyboard_t *kb,int y,int px,int py);
/// 处理SHIFT/LAYER/IME_TOGGLE并返回true；其他代码不改状态返回false。/ Apply SHIFT/LAYER/IME_TOGGLE and return true; other codes leave state unchanged and return false.
bool pn_keyboard_toggle(pn_keyboard_t *kb,int code);
/// 统一应用键盘码到键盘状态与目标UTF-8文本缓冲；状态或文本变化时返回true。
/// Unified handler applying any keyboard code to keyboard state and target UTF-8 text buffer; returns true when state or text changed.
bool pn_keyboard_apply(pn_keyboard_t *kb,int code,char *text,size_t cap,size_t max);
/// 若拼音缓冲非空则自动提交首个候选字（或原文）并清空缓冲；无待提交内容返回false。
/// Flush pending pinyin by committing the first candidate (or raw letters); returns false if empty.
bool pn_keyboard_flush(pn_keyboard_t *kb,char *text,size_t cap,size_t max);

/// 追加一个可打印ASCII；超出max字节或cap返回false。/ Append one printable ASCII character; false beyond max bytes or the capacity.
bool pn_text_field_append(char *text,size_t cap,size_t max,int code);
/// 追加一段UTF-8文本（汉字、中文标点等）；超出max字节或cap返回false。/ Append a UTF-8 string (hanzi, CJK punctuation, etc.); false beyond max bytes or capacity.
bool pn_text_field_append_utf8(char *text,size_t cap,size_t max,const char *utf8);
/// 删除末尾一个完整UTF-8字符；空时返回false。/ Delete the last complete UTF-8 character; false when empty.
bool pn_text_field_delete(char *text);
