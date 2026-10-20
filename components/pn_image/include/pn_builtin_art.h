/* SPDX-License-Identifier: Apache-2.0
 * 中文：固件内置图像；不访问TF或显示硬件。 / English: firmware artwork without SD or display hardware access.
 */
#pragma once
#include "pn_image.h"
/// 展开原生16灰阶开机图，不分配；pool兼容旧调用，可为NULL。 / Expand native sixteen-gray boot art without allocations; pool is retained for compatibility and may be NULL.
pn_status_t pn_boot_art_draw(pn_pool_t *pool,pn_frame_t *frame);
/// 解压原生16灰阶书签底图；不分配、不读文件。 / Expand the native sixteen-gray bookmark background without allocations or file reads.
pn_status_t pn_bookmark_art_draw(pn_frame_t *frame);
/// 按名称稳定选择只读封面视图；不得写pixels，无分配、解码或文件读取。 / Select a stable read-only cover view by name; never write pixels; no allocation, decoding or file reads.
pn_status_t pn_builtin_cover_frame(const char *name,pn_frame_t *frame);
enum { PN_READING_BACKGROUND_COUNT = 8 }; ///< 固定内置背景数，0另表示无背景 / Fixed built-in count; zero separately means none
/// 背景名称：0为无，越界返回NULL；只读。/ Read-only background name; zero is none and invalid indexes return NULL.
const char *pn_reading_background_name(unsigned index);
/// 原生16灰阶压缩背景缩放至指定矩形，不分配、不读TF；0清为白底。/ Scale compressed native sixteen-gray art into a rectangle without allocation or SD IO; zero clears to white.
pn_status_t pn_reading_background_rect(unsigned index,pn_frame_t *frame,int x,int y,int width,int height);
/// 原生阅读画布背景，0与原白底完全一致。/ Native reader background; zero exactly preserves the original white paper.
pn_status_t pn_reading_background_draw(unsigned index,pn_frame_t *frame);
/// 恢复原生背景的行带，带外像素不变；用于时钟更新，无分配。/ Restore a native background band, preserving pixels outside it; allocation-free clock updates.
pn_status_t pn_reading_background_region(unsigned index,pn_frame_t *frame,int y,int height);
