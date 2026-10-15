/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：按内容选择PNG/JPEG，共用输入与灰阶绘制契约。
 * English: select PNG/JPEG by content with shared input and grayscale drawing contracts.
 * 冻结：不按扩展名猜格式；错误info不变，不提交暂定像素。
 * Frozen: never guess format from extensions; errors preserve info and never present provisional pixels.
 */
#pragma once
#include "pn_jpeg.h"
typedef enum {
    PN_IMAGE_PNG=1, ///< PNG图片 / PNG image
    PN_IMAGE_JPEG=2 ///< JPEG图片 / JPEG image
} pn_image_kind_t;
typedef struct {
    pn_image_kind_t kind; ///< 按签名确认的类型 / Type confirmed by signature
    uint32_t width,height; ///< 原始尺寸 / Native dimensions
    bool progressive,interlaced; ///< JPEG渐进式或PNG隔行 / Progressive JPEG or interlaced PNG
} pn_image_info_t;
/// 消耗头；不会校验完整图片或EOF，错误保持info。/ Consume headers without full-image/EOF validation, preserving info on errors.
pn_status_t pn_image_probe(pn_pool_t *pool,const pn_image_input_t *input,pn_image_info_t *info);
/// 完整解码及验证源EOF；错误frame可能部分变化，不得呈现。/ Decode completely and verify source EOF; errors may partly change frame and cannot be presented.
pn_status_t pn_image_draw(pn_pool_t *pool,const pn_image_input_t *input,pn_frame_t *frame,pn_image_rect_t rect,pn_image_info_t *info);

typedef struct {void *impl;} pn_image_job_t; ///< 按签名分发的可恢复任务，零初始化 / Signature-dispatched resumable job, zero-initialized
/// 创建任务；复制输入回调但借用ctx/帧/池，直到close不得移动或释放。/ Create a job, copying callbacks but borrowing ctx/frame/pool until close; never move or release them early.
pn_status_t pn_image_job_begin(pn_image_job_t *,pn_pool_t *,const pn_image_input_t *,pn_frame_t *,pn_image_rect_t);
/// 最多推进八个解码小步骤；BUSY继续，所有像素在完整EOF/CRC前暂定，info仅OK更新。
/// Advance at most eight decoder substeps; continue on BUSY, keep pixels provisional until full EOF/CRC, and update info only on OK.
/// 调用前取消可暂停；库内错误/取消终止任务；头和库调用不保证固定毫秒上限。
/// Cancellation before the call can pause; errors/cancellation inside the library terminate the job; headers/library calls have no fixed millisecond bound.
pn_status_t pn_image_job_step(pn_image_job_t *,pn_image_info_t *);
/// 取消并释放，不读取剩余源、不提交像素；幂等。/ Cancel and release without draining the source or presenting pixels, idempotently.
void pn_image_job_close(pn_image_job_t *);
