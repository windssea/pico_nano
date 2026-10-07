/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：受限资源与绘制的公共契约，不依赖设备或操作系统。
 * English: bounded resource and drawing contracts, independent of hardware and OS.
 *
 * 冻结：调用者串行访问；不修改硬件电源或设置。
 * Frozen: callers serialize access; never modify hardware power or settings.
 */
#include "pn_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)
static void *fail_malloc(void *ctx, size_t bytes) { (void)ctx; (void)bytes; return NULL; }
static void release(void *ctx, void *ptr) { (void)ctx; free(ptr); }
int main(void) {
    pn_pool_t pool;
    CHECK(pn_pool_init(&pool,1024,NULL,NULL,NULL)==0);
    CHECK(pn_alloc(&pool,0)==NULL);
    CHECK(pn_alloc(&pool,SIZE_MAX)==NULL);
    CHECK(pool.used==0 && pool.live==0);
    void *a=pn_alloc(&pool,100);
    CHECK(a!=NULL);
    CHECK((uintptr_t)a % alignof(max_align_t)==0);
    CHECK(pool.used>100 && pool.peak==pool.used && pool.live==1);
    size_t charged=pool.used;
    CHECK(pn_alloc(&pool,1024)==NULL);
    CHECK(pool.used==charged && pool.live==1);
    pool.fail_at=pool.attempts+1;
    CHECK(pn_alloc(&pool,10)==NULL);
    CHECK(pool.used==charged);
    void *b=pn_alloc(&pool,1);
    CHECK(b!=NULL && pool.live==2);
    pn_free(a); pn_free(b); pn_free(NULL);
    CHECK(pool.used==0 && pool.live==0 && pool.peak>charged);
    CHECK(pn_pool_init(&pool,1024,fail_malloc,release,NULL)==0);
    CHECK(pn_alloc(&pool,10)==NULL && pool.used==0 && pool.live==0);
    CHECK(pn_pool_init(&pool,1024,fail_malloc,NULL,NULL)!=0);
    CHECK(pn_pool_init(NULL,1024,NULL,NULL,NULL)!=0);
    puts("allocation cap, alignment, overflow, injection and release passed");
    return 0;
}
