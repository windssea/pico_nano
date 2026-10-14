/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "pn_frame.h"
#include "pn_types.h"
/// 转换684×1216低半字节在先的灰度帧为板级1216×684反向竖屏布局，不分配内存。
/// Convert a low-nibble-first 684×1216 grayscale frame to the board's inverted-portrait 1216×684 layout without allocations.
pn_status_t pn_device_frame_copy(const pn_frame_t *source,uint8_t *destination,size_t capacity);
/// 逻辑坐标中的矩形；width为0表示无变化。/ Rectangle in logical coordinates; zero width means no change.
typedef struct {int x,y,width,height;} pn_device_box_t;
/// 比较板级布局的目标帧与已显示帧（如epdiy前/后缓冲），给出逻辑坐标外接框并外扩margin像素；不分配。
/// Compare board-layout target and shown buffers (such as epdiy front/back), returning the logical bounding box grown by margin pixels; no allocations.
pn_status_t pn_device_frame_dirty(const uint8_t *target,const uint8_t *shown,size_t capacity,int margin,pn_device_box_t *box);
/// 把板级布局帧还原为684×1216逻辑帧（pn_device_frame_copy的逆变换），不分配。/ Restore a board-layout buffer into a 684×1216 logical frame (the inverse of pn_device_frame_copy) without allocations.
pn_status_t pn_device_frame_restore(const uint8_t *source,size_t capacity,pn_frame_t *destination);
