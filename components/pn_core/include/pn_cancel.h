/* SPDX-License-Identifier: Apache-2.0
 * 中文：worker局部的协作取消范围，写事务可临时屏蔽。/ English: worker-local cooperative cancellation scopes, shieldable during write transactions. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "pn_types.h"
typedef bool (*pn_cancelled_fn)(void *ctx); ///< 只读、无阻塞的取消检查 / Read-only nonblocking cancellation check
typedef struct pn_cancel_scope {
    pn_cancelled_fn check; ///< 当前检查器，NULL屏蔽 / Current checker, NULL shields cancellation
    void *ctx; ///< 检查上下文 / Check context
    struct pn_cancel_scope *previous; ///< 外层范围 / Outer scope
} pn_cancel_scope_t;
/// worker入栈；scope在leave前保持有效。/ Push a worker scope, keeping it alive until leave.
void pn_cancel_enter(pn_cancel_scope_t *scope,pn_cancelled_fn check,void *ctx);
/// 恢复外层范围，不跨线程使用。/ Restore the outer scope; never move a scope across threads.
void pn_cancel_leave(pn_cancel_scope_t *scope);
/// 被取消时CANCELLED，否则OK；不读取硬件或介质。/ CANCELLED when cancelled, otherwise OK; no hardware or media access.
pn_status_t pn_cancel_checkpoint(void);
