/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：首页/书库整页帧的两槽LRU缓存，只复用与当前输入完全一致的已确认画面；压力下可整块丢弃重绘。
 * English: two-slot LRU of whole home/library frames, reused only when every input matches the confirmed picture; droppable and repaintable under pressure.
 * 冻结：调用者串行；不绘制、不读取文件；缓存帧不得在未失效时被局部修改。
 * Frozen: serialized callers; never draws or reads files; a cached frame must not be partially modified without invalidation.
 */
#pragma once
#include "pn_alloc.h"
#include "pn_types.h"
#include <stdbool.h>
#include <stdint.h>

#define PN_MENU_CACHE_SLOTS 2
typedef struct {
    bool valid; ///< 内容与key仍有效 / Content still matches key
    uint64_t key; ///< 调用方计算的全部绘制输入摘要 / Digest of every drawing input
    uint64_t age; ///< LRU时间戳 / LRU stamp
    uint8_t *pixels; ///< 684×1216/4bpp整页 / Full 684×1216 4bpp page
} pn_menu_cache_entry_t;
typedef struct {
    pn_pool_t *pool; ///< 注册池，先close再释放 / Registered pool; close before releasing
    pn_cache_owner_t owner; ///< 可回收缓存owner / Reclaimable cache owner
    pn_menu_cache_entry_t entries[PN_MENU_CACHE_SLOTS]; ///< 固定两槽 / Fixed two slots
    unsigned last; ///< 最近一次begin选中的槽 / Slot selected by the latest begin
    uint64_t clock; ///< 单调LRU计数 / Monotonic LRU counter
} pn_menu_cache_t;

/// 初始化并挂到池；同一池只挂一次。/ Initialize and attach to the pool once.
pn_status_t pn_menu_cache_init(pn_menu_cache_t *cache,pn_pool_t *pool);
/// 注销并释放全部帧。/ Detach and free every frame.
void pn_menu_cache_close(pn_menu_cache_t *cache);
/// 查找key；命中返回只读/可整页复用的像素，未命中返回可绘制槽（可能为NULL，调用方走原路径）。
/// Find key; a hit returns reusable pixels, a miss returns a drawable slot (NULL means the caller keeps its original path).
uint8_t *pn_menu_cache_begin(pn_menu_cache_t *cache,uint64_t key,bool *hit);
/// begin成功后确认该槽；失败调用abort，绝不能提交半帧。/ Confirm the slot after begin; an abort is mandatory on failure, never commit a partial frame.
void pn_menu_cache_commit(pn_menu_cache_t *cache,uint64_t key);
/// begin后放弃本槽并解除pin；已绘制的半帧不可再用。/ Abandon the slot and unpin after begin; a partial frame is never reusable.
void pn_menu_cache_abort(pn_menu_cache_t *cache);
/// 使全部槽失效但保留已分配像素；任何页面/字体/偏好/封面变化都应调用。/ Invalidate all slots but keep pixels; call on any page, font, preference or cover change.
void pn_menu_cache_invalidate(pn_menu_cache_t *cache);
/// 释放像素但保留注册；进入阅读/传输等需要内存的路径调用，未pin时生效。/ Free pixels but keep registration; used before reading/transfer, only while unpinned.
void pn_menu_cache_drop(pn_menu_cache_t *cache);
/// 当前分配字节，供账本/诊断。/ Allocated bytes for accounting/diagnostics.
size_t pn_menu_cache_bytes(const pn_menu_cache_t *cache);
