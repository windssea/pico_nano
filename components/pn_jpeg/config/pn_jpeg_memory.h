/* 线程内作用域，所有JPEG分配受pool约束。/ Thread-local scope constraining every JPEG allocation to pool. */
#pragma once
#include "pn_alloc.h"
/// 替换当前pool并返回前值，退出时恢复。/ Replace the current pool and return its previous value to restore on exit.
pn_pool_t *pn_jpeg_memory_scope(pn_pool_t *pool);
