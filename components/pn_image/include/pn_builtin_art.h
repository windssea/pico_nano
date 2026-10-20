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
