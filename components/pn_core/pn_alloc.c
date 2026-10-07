/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：实现可移植的受限基础设施；不接触硬件。
 * English: portable bounded infrastructure without hardware access.
 *
 * 冻结：由调用者串行访问，分配头也计入预算。
 * Frozen: access is serialized by callers; allocation headers count against budgets.
 */
#include "pn_alloc.h"
#include <stdlib.h>

typedef union {
    max_align_t alignment;
    struct { pn_pool_t *pool; size_t bytes; } record;
} pn_alloc_header_t;

static void *libc_alloc(void *ctx, size_t bytes) { (void)ctx; return malloc(bytes); }
static void libc_free(void *ctx, void *ptr) { (void)ctx; free(ptr); }

int pn_pool_init(pn_pool_t *pool, size_t limit, pn_malloc_fn alloc, pn_free_fn release, void *ctx) {
    if (!pool || ((alloc == NULL) != (release == NULL))) return -1;
    *pool = (pn_pool_t){ .limit=limit, .malloc_fn=alloc ? alloc : libc_alloc,
        .free_fn=release ? release : libc_free, .ctx=ctx };
    return 0;
}

void *pn_alloc(pn_pool_t *pool, size_t bytes) {
    if (!pool || !pool->malloc_fn || !bytes || bytes > SIZE_MAX-sizeof(pn_alloc_header_t)) return NULL;
    if (pool->attempts == SIZE_MAX) return NULL;
    pool->attempts++;
    if (pool->fail_at && pool->attempts == pool->fail_at) return NULL;
    size_t total=sizeof(pn_alloc_header_t)+bytes;
    if (pool->used > pool->limit || total > pool->limit-pool->used) return NULL;
    pn_alloc_header_t *header=pool->malloc_fn(pool->ctx,total);
    if (!header) return NULL;
    header->record.pool=pool;
    header->record.bytes=total;
    pool->used+=total;
    if (pool->used > pool->peak) pool->peak=pool->used;
    pool->live++;
    return header+1;
}

void pn_free(void *ptr) {
    if (!ptr) return;
    pn_alloc_header_t *header=(pn_alloc_header_t *)ptr-1;
    pn_pool_t *pool=header->record.pool;
    pool->used-=header->record.bytes;
    pool->live--;
    pool->free_fn(pool->ctx,header);
}
