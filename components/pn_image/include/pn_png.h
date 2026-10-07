/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界PNG扫描线到灰阶帧，输入源与显示由调用方拥有。
 * English: bounded PNG scanlines to grayscale frames, with caller-owned input and display.
 * 冻结：无整图RGBA；像素在全部PNG/资源校验前暂定，不推屏。
 * Frozen: no whole-image RGBA buffer; pixels are provisional before complete PNG/resource checks; no presentation.
 */
#pragma once
#include "pn_alloc.h"
#include "pn_types.h"
#include "pn_frame.h"
typedef struct {
    void *ctx; ///< 流owner / Stream owner
    pn_status_t (*read)(void *,uint8_t *,size_t,size_t *); ///< EOF须完成源完整性校验后返回EMPTY / Return EMPTY at EOF only after source integrity verification
} pn_image_input_t;
typedef struct {
    uint32_t width,height; ///< 原始尺寸，最多8192且总像素最多16Mi / Native dimensions up to 8192 and at most 16 Mi pixels total
    unsigned depth,color_type,interlace; ///< PNG头参数 / PNG header parameters
} pn_png_info_t;
typedef struct {int x,y,width,height;} pn_image_rect_t; ///< 目标框，最多4096宽高，可裁切 / Destination rectangle up to 4096 dimensions, permitting clipping
/// 读取并验证PNG签名/IHDR CRC；仅尺寸探测，不验证整图；失败info不变，消耗输入。
/// Verify PNG signature/IHDR CRC and probe dimensions only, not the whole image; errors preserve info and consume input.
pn_status_t pn_png_probe(const pn_image_input_t *input,pn_png_info_t *info);
/// 任意PNG色型/深度含Adam7，逐扫描线解码、透明合成到原灰阶背景和最近邻缩放。
/// Decode PNG color types/depths including Adam7 scanline by scanline, compositing on existing grayscale and scaling by nearest neighbor.
/// 校验到IEND及源EOF，拒绝尾随数据；错误可能改部分fb，调用方必须丢弃暂定帧。
/// Verify through IEND and source EOF, rejecting trailing data; errors may change partial framebuffer data, requiring discard of the provisional frame.
pn_status_t pn_png_draw(pn_pool_t *pool,const pn_image_input_t *input,pn_frame_t *frame,pn_image_rect_t rect,pn_png_info_t *info);
