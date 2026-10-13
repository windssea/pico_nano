/* SPDX-License-Identifier: Apache-2.0
 * 中文：产品与模拟器共享板级资源上限，来源见HARDWARE.md。/ English: board resource ceilings shared by product and simulator; see HARDWARE.md. */
#pragma once
/// ESP32-S3内部SRAM总量，不等于空闲堆。/ Total internal SRAM, not free heap.
#define PN_HW_SRAM_BYTES (512u*1024u)
/// 板载外部PSRAM和Flash总量。/ Installed external PSRAM and flash capacities.
#define PN_HW_PSRAM_BYTES (8u*1024u*1024u)
#define PN_HW_FLASH_BYTES (16u*1024u*1024u)
/// 应用池上限；固件还按实际可用PSRAM再缩减。/ App pool ceiling; firmware further clamps to actual available PSRAM.
#define PN_HW_POOL_BYTES (6u*1024u*1024u)
#define PN_HW_PSRAM_RESERVE (256u*1024u)
/// 产品worker任务栈，以字节计。/ Product worker stack in bytes.
#define PN_HW_PRODUCT_STACK_BYTES 32768u
/// 当前面板逻辑像素与原生灰阶。/ Current panel logical pixels and native gray levels.
#define PN_HW_WIDTH 684u
#define PN_HW_HEIGHT 1216u
#define PN_HW_GRAY_LEVELS 16u
#define PN_HW_FRAME_BYTES (PN_HW_WIDTH*PN_HW_HEIGHT/2u)
