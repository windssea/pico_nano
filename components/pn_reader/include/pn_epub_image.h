/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：出版物图片资源到通用PNG/JPEG解码器，借用出版物和pool。
 * English: publication image resources to shared PNG/JPEG decoding, borrowing publication and pool.
 * 冻结：绘制成功必须包含ZIP资源CRC；不写封面缓存或阅读位置。
 * Frozen: draw success includes ZIP resource CRC; no cover-cache or reading-position writes.
 */
#pragma once
#include "pn_epub.h"
#include "pn_image.h"
/// 仅验证图片头，不能作完整资源验收。/ Verify image headers only, not full-resource acceptance.
pn_status_t pn_epub_image_probe(pn_pool_t *pool,pn_epub_t *epub,const char *path,pn_image_info_t *info);
/// 图片及ZIP验证EOF后才成功；错误info不变，暂定帧不可呈现。
/// Succeed only after image and ZIP verified EOF; errors preserve info and prohibit provisional-frame presentation.
pn_status_t pn_epub_image_draw(pn_pool_t *pool,pn_epub_t *epub,const char *path,pn_frame_t *frame,pn_image_rect_t rect,pn_image_info_t *info);
