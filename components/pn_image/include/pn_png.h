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
/// 分别写4bpp原灰度与透明度（0透明、15不透明），不与背景合成；两帧尺寸必须相同。
/// Write separate 4bpp raw gray and alpha (0 transparent, 15 opaque) planes without compositing; frame dimensions must match.
pn_status_t pn_png_draw_planes(pn_pool_t *pool,const pn_image_input_t *input,pn_frame_t *gray,pn_frame_t *alpha,pn_image_rect_t rect,pn_png_info_t *info);

typedef struct {void *impl;} pn_png_job_t; ///< 零初始化的扫描线任务，输入/目标须保活 / Zero-initialized scanline job; retain input and target lifetimes
/// 创建可恢复解码；alpha可为NULL，像素直到OK都暂定；不提交显示。/ Create resumable decoding with optional alpha; pixels remain provisional until OK; no presentation.
pn_status_t pn_png_job_begin(pn_png_job_t *,pn_pool_t *,const pn_image_input_t *,pn_frame_t *,pn_frame_t *alpha,pn_image_rect_t);
/// 推进一个头/扫描线/最多4096目标像素/尾校验阶段；BUSY继续，info仅成功更新。
/// Advance one header/scanline/up-to-4096-target-pixels/tail-validation stage; continue on BUSY, updating info only on success.
pn_status_t pn_png_job_step(pn_png_job_t *,pn_png_info_t *);
/// 释放所有解码分配，不关闭借用源；幂等。/ Free all decoding allocations without closing the borrowed source, idempotently.
void pn_png_job_close(pn_png_job_t *);
