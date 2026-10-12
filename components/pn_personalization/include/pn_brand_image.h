/* SPDX-License-Identifier: Apache-2.0
 * 中文：共用的内置品牌图；构建时预处理，运行时只绘制。
 * English: shared built-in brand art, preprocessed at build time and drawn only at runtime.
 */
#pragma once
#include "pn_types.h"
#include "pn_frame.h"
/// 在684×1216白底上等比例居中绘制品牌图，无分配和文件读取。/ Draw the proportional centered brand art on a 684×1216 white canvas, without allocations or file reads.
pn_status_t pn_brand_image_render(pn_frame_t *frame);
