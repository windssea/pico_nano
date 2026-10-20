/* SPDX-License-Identifier: Apache-2.0
 * 中文：共用的内置品牌图；构建时预处理，运行时只绘制。
 * English: shared built-in brand art, preprocessed at build time and drawn only at runtime.
 */
#pragma once
#include "pn_types.h"
#include "pn_frame.h"
/// 在684×1216画布展开熊猫默认壁纸，无分配和文件读取。/ Expand the panda default wallpaper into a 684×1216 canvas without allocations or file reads.
pn_status_t pn_brand_image_render(pn_frame_t *frame);
/// 在品牌图右下角以内置5×7点阵绘制“v版本”，只支持数字与点；不分配、不读文件，非法字符不改帧。
/// Draw "v<version>" in the bottom-right corner with built-in 5×7 glyphs (digits and dots only), without allocations or file reads; invalid characters leave the frame unchanged.
pn_status_t pn_brand_version_render(pn_frame_t *frame,const char *version);
