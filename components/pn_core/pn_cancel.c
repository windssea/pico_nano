/* SPDX-License-Identifier: Apache-2.0
 * 中文：线程局部协作取消，不强停写入。/ English: thread-local cooperative cancellation without killing writes. */
#include "pn_cancel.h"
static _Thread_local pn_cancel_scope_t *current;
void pn_cancel_enter(pn_cancel_scope_t *scope,pn_cancelled_fn check,void *ctx){scope->check=check;scope->ctx=ctx;scope->previous=current;current=scope;}
void pn_cancel_leave(pn_cancel_scope_t *scope){if(current==scope)current=scope->previous;}
pn_status_t pn_cancel_checkpoint(void){return current && current->check && current->check(current->ctx)?PN_CANCELLED:PN_OK;}
