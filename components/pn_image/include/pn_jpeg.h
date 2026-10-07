/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界JPEG头与逐行灰阶绘制，源和显示由调用方拥有。
 * English: bounded JPEG headers and scanline grayscale drawing with caller-owned sources and display.
 * 冻结：全源EOF前像素暂定，错误不可呈现；不创建整图RGB或临时文件。
 * Frozen: provisional pixels before source EOF; errors cannot be presented; no whole-image RGB or temporary files.
 */
#pragma once
#include "pn_png.h"
typedef struct {
    uint32_t width,height; ///< 原始尺寸，最多8192及16Mi像素 / Native dimensions up to 8192 and 16 Mi pixels
    bool progressive; ///< 渐进式Huffman / Progressive Huffman coding
} pn_jpeg_info_t;
/// 借助受限解码器探测头；不验证整图，失败info不变。/ Probe headers with the bounded decoder, not the whole image; errors preserve info.
pn_status_t pn_jpeg_probe(pn_pool_t *pool,const pn_image_input_t *input,pn_jpeg_info_t *info);
/// 8位灰度/RGB/YCbCr，支持基线与渐进式；最近邻缩放到4bpp；渐进式系数同样计入pool。
/// Eight-bit grayscale/RGB/YCbCr baseline and progressive decoding to nearest-scaled 4bpp; progressive coefficients also count against pool.
/// 需要EOI和验证EOF；错误可能改部分帧，调用方须丢弃。/ Require EOI and verified EOF; errors may change part of the frame, requiring caller discard.
pn_status_t pn_jpeg_draw(pn_pool_t *pool,const pn_image_input_t *input,pn_frame_t *frame,pn_image_rect_t rect,pn_jpeg_info_t *info);
