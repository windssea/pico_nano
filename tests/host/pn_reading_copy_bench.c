/* SPDX-License-Identifier: Apache-2.0
 * 中文：同源缓存拷贝计时及逐字节/池峰对照；不模拟TF或面板。
 * English: shared cache-copy timing, exact bytes and pool peaks; no TF or panel simulation. */
#define _POSIX_C_SOURCE 200809L
#include "pn_page_bitmap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static pn_status_t paint(void *ctx,pn_frame_t *f,size_t first,size_t count,unsigned *missing){
    (void)ctx;(void)first;(void)count;for(size_t i=0;i<415872;i++)f->pixels[i]=(uint8_t)(i*37u+i/342u);*missing=7;return PN_OK;
}
static uint64_t ns(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;}
int main(void){
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    pn_page_bitmap_t cache={0};pn_page_bitmap_init(&cache,&pool);assert(pn_page_bitmap_begin(&cache,1)==PN_OK && pn_page_bitmap_step(&cache,paint,NULL,1)==PN_OK);
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,415872);assert(pixels);pn_frame_t f;assert(pn_frame_bind(&f,pixels,415872,684,1216));unsigned missing=0;
    for(unsigned i=0;i<100;i++)assert(pn_page_bitmap_copy(&cache,&f,&missing)==PN_OK);
    uint64_t start=ns();for(unsigned i=0;i<10000;i++)assert(pn_page_bitmap_copy(&cache,&f,&missing)==PN_OK);uint64_t elapsed=ns()-start;
    assert(missing==7 && !memcmp(f.pixels,cache.frame.pixels,415872));size_t peak=pool.peak;
    pn_page_bitmap_close(&cache);pn_free(pixels);assert(!pool.used && !pool.live);
    printf("copy_ns=%.1f peak=%zu exact=1 used=0 live=0\n",(double)elapsed/10000.,peak);return 0;
}
