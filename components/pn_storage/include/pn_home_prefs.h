/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：主页个性化偏好（左上角自定义名言/文案与正在阅读上方自定义背景图），保存在内部状态目录home_prefs.a/b。
 * English: Home personalization preferences (custom top-left motto and custom top banner image), stored in internal home_prefs.a/b.
 */
#pragma once
#include "pn_journal.h"
#define PN_HOME_HEADER_MAX 96
#define PN_HOME_BANNER_MAX 148
typedef struct {
    char header[PN_HOME_HEADER_MAX]; ///< 主页左上角自定义文案，空串回退“PicoNano” / Custom Home top-left text; empty falls back to "PicoNano"
    char banner[PN_HOME_BANNER_MAX]; ///< 正在阅读上方自定义背景图文件名（位于wallpapers目录），空串使用默认山水 / Custom Home top banner filename in wallpapers dir; empty uses default mountains
} pn_home_prefs_t;
/// 默认全空（显示PicoNano与默认山水）。/ Default empty preferences (PicoNano + default mountains).
pn_home_prefs_t pn_home_prefs_default(void);
/// 绑定root下home_prefs.a/b，不建目录。/ Bind home_prefs.a/b under root without creating directories.
pn_status_t pn_home_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io);
/// 读取偏好；无记录返回PN_EMPTY，错误保留输出。/ Load preferences; EMPTY if absent, preserving output on error.
pn_status_t pn_home_prefs_load(const pn_journal_io_t *io,pn_home_prefs_t *out);
/// 保存合法偏好。/ Save valid preferences.
pn_status_t pn_home_prefs_save(const pn_journal_io_t *io,const pn_home_prefs_t *prefs);
/// 直接从状态目录加载（无记录返回PN_OK并写默认值，错误保留输出）。/ Load directly from state_dir (absent returns PN_OK with defaults; errors preserve output).
pn_status_t pn_home_prefs_load_dir(pn_media_t *state_media,const char *state_dir,pn_home_prefs_t *out);
/// 直接保存到状态目录。/ Save directly into state_dir.
pn_status_t pn_home_prefs_save_dir(pn_media_t *state_media,const char *state_dir,const pn_home_prefs_t *prefs);
/// 缓存预解码的684×146四灰阶主页顶部背景图到状态目录（home_banner.raw），避免每次回主页重新解码PNG/JPG卡顿。
/// Cache pre-decoded 684x146 4bpp Home top banner pixels to state_dir (home_banner.raw) to avoid re-decoding PNG/JPG on every Home visit.
pn_status_t pn_home_banner_cache_save(pn_media_t *state_media,const char *state_dir,const char *banner_name,const uint8_t *pixels,size_t bytes);
/// 读取预解码主页背景缓存；名称或CRC不匹配返回PN_EMPTY。/ Load cached Home banner pixels; returns PN_EMPTY on name/CRC mismatch.
pn_status_t pn_home_banner_cache_load(pn_media_t *state_media,const char *state_dir,const char *banner_name,uint8_t *pixels,size_t bytes);
/// 清除主页背景缓存文件。/ Remove cached Home banner file.
void pn_home_banner_cache_clear(pn_media_t *state_media,const char *state_dir);
