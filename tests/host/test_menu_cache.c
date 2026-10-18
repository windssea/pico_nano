/* SPDX-License-Identifier: Apache-2.0
 * 中文：整页菜单帧两槽缓存：命中、LRU淘汰、失效、pin期间不回收与关闭归零。
 * English: two-slot menu frame cache: hits, LRU eviction, invalidation, pinned reclamation and zero on close. */
#include "pn_menu_cache.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void fill(uint8_t *pixels,unsigned seed){for(size_t i=0;i<684u*1216u/2u;i++)pixels[i]=(uint8_t)(seed+i*7u);}
static void expect(const uint8_t *pixels,unsigned seed){for(size_t i=0;i<684u*1216u/2u;i++)assert(pixels[i]==(uint8_t)(seed+i*7u));}
int main(void){
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    pn_menu_cache_t cache;assert(pn_menu_cache_init(&cache,&pool)==PN_OK);
    bool hit=true,same=true;uint8_t *a=pn_menu_cache_begin(&cache,0x1111u,10u,&hit,&same);assert(a && !hit);fill(a,1);pn_menu_cache_commit(&cache,0x1111u,10u);
    uint8_t *again=pn_menu_cache_begin(&cache,0x1111u,10u,&hit,&same);assert(again==a && hit && same);expect(again,1);pn_menu_cache_commit(&cache,0x1111u,10u);
    // 动态带每槽独立：同一整页、不同时间/电量不命中动态带，只补状态带。/ The dynamic band is per slot: same page with a different clock/battery mismatches it.
    hit=true;assert(pn_menu_cache_begin(&cache,0x1111u,11u,&hit,&same)==a && hit && !same);pn_menu_cache_commit(&cache,0x1111u,11u);
    uint8_t *b=pn_menu_cache_begin(&cache,0x2222u,20u,&hit,&same);assert(b && !hit && b!=a);fill(b,2);pn_menu_cache_commit(&cache,0x2222u,20u);
    // 第二槽不继承第一槽的动态带。/ The second slot never inherits the first slot's dynamic band.
    hit=true;assert(pn_menu_cache_begin(&cache,0x2222u,10u,&hit,&same)==b && hit && !same);pn_menu_cache_commit(&cache,0x2222u,20u);
    // 两槽都命中；第三个key淘汰最旧。/ Both slots hit; a third key evicts the oldest.
    hit=false;assert(pn_menu_cache_begin(&cache,0x1111u,11u,&hit,&same)==a && hit && same);pn_menu_cache_commit(&cache,0x1111u,11u);
    hit=false;assert(pn_menu_cache_begin(&cache,0x2222u,20u,&hit,&same)==b && hit && same);pn_menu_cache_commit(&cache,0x2222u,20u);
    uint8_t *c=pn_menu_cache_begin(&cache,0x3333u,30u,&hit,&same);assert(c && !hit);fill(c,3);pn_menu_cache_commit(&cache,0x3333u,30u);
    hit=false;assert(pn_menu_cache_begin(&cache,0x3333u,30u,&hit,&same)==c && hit && same);pn_menu_cache_commit(&cache,0x3333u,30u);
    // 未提交的begin保持pin，压力回收不得释放；abort后失效。/ An uncommitted begin stays pinned against reclamation; abort invalidates.
    uint8_t *pending=pn_menu_cache_begin(&cache,0x4444u,40u,&hit,&same);assert(pending && !hit);size_t before=pn_menu_cache_bytes(&cache);
    assert(pn_cache_reclaim(&pool,SIZE_MAX)==0 && pn_menu_cache_bytes(&cache)==before);
    pn_menu_cache_abort(&cache);hit=false;assert(pn_menu_cache_begin(&cache,0x4444u,40u,&hit,&same)==pending && !hit);pn_menu_cache_commit(&cache,0x4444u,40u);
    pn_menu_cache_invalidate(&cache);hit=true;assert(pn_menu_cache_begin(&cache,0x4444u,40u,&hit,&same) && !hit);pn_menu_cache_abort(&cache);
    // 无pin时可被整块回收，close后池归零。/ Without pins the whole cache is reclaimable; close returns the pool to zero.
    assert(pn_cache_reclaim(&pool,SIZE_MAX)>0 && pn_menu_cache_bytes(&cache)==0);
    pn_menu_cache_close(&cache);assert(!pool.used && !pool.live);
    puts("menu cache: hit, lru, invalidate, pin and reclaim passed");return 0;
}
