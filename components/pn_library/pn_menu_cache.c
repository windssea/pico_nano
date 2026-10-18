/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：整页菜单帧的两槽LRU；只保存像素，不参与绘制或持久化。
 * English: two-slot LRU of whole menu frames; stores pixels only and never draws or persists.
 */
#include "pn_menu_cache.h"
#include <string.h>

#define MENU_PAGE_BYTES (684u*1216u/2u)
static size_t bytes_of(void *ctx){
    pn_menu_cache_t *cache=ctx;size_t sum=0;
    for(unsigned i=0;i<PN_MENU_CACHE_SLOTS;i++)sum+=pn_alloc_size(cache->entries[i].pixels);
    return sum;
}
static void drop(void *ctx){
    pn_menu_cache_t *cache=ctx;
    for(unsigned i=0;i<PN_MENU_CACHE_SLOTS;i++){
        pn_free(cache->entries[i].pixels);cache->entries[i]=(pn_menu_cache_entry_t){0};
    }
}
pn_status_t pn_menu_cache_init(pn_menu_cache_t *cache,pn_pool_t *pool){
    if(!cache || !pool || cache->owner.pool)return PN_INVALID;
    *cache=(pn_menu_cache_t){.pool=pool};
    pn_cache_attach(pool,&cache->owner,PN_CACHE_MENU,cache,bytes_of,drop);
    return PN_OK;
}
void pn_menu_cache_close(pn_menu_cache_t *cache){
    if(!cache || !cache->owner.pool)return;
    pn_cache_detach(&cache->owner);drop(cache);
}
uint8_t *pn_menu_cache_begin(pn_menu_cache_t *cache,uint64_t key,uint64_t aux,bool *hit,bool *same_aux){
    if(hit)*hit=false;
    if(same_aux)*same_aux=false;
    if(!cache || !cache->owner.pool || !key)return NULL;
    for(unsigned i=0;i<PN_MENU_CACHE_SLOTS;i++){
        if(cache->entries[i].valid && cache->entries[i].key==key){
            cache->last=i;cache->entries[i].age=++cache->clock;
            pn_cache_pin(&cache->owner);pn_cache_observe(cache->pool,PN_CACHE_MENU,true);
            if(hit)*hit=true;
            if(same_aux)*same_aux=cache->entries[i].aux==aux;
            return cache->entries[i].pixels;
        }
    }
    pn_cache_observe(cache->pool,PN_CACHE_MENU,false);
    unsigned slot=0;bool selected=false;
    for(unsigned i=0;i<PN_MENU_CACHE_SLOTS && !selected;i++)if(!cache->entries[i].valid){slot=i;selected=true;}
    if(!selected){
        for(unsigned i=1;i<PN_MENU_CACHE_SLOTS;i++)if(cache->entries[i].age<cache->entries[slot].age)slot=i;
    }
    pn_menu_cache_entry_t *entry=&cache->entries[slot];
    if(!entry->pixels)entry->pixels=pn_cache_alloc(cache->pool,PN_CACHE_MENU,PN_MEM_FRAME,MENU_PAGE_BYTES);
    if(!entry->pixels)return NULL;
    entry->valid=false;entry->key=0;entry->aux=0;cache->last=slot;
    pn_cache_pin(&cache->owner);
    return entry->pixels;
}
void pn_menu_cache_commit(pn_menu_cache_t *cache,uint64_t key,uint64_t aux){
    if(!cache || !cache->owner.pool || cache->last>=PN_MENU_CACHE_SLOTS)return;
    pn_menu_cache_entry_t *entry=&cache->entries[cache->last];
    if(entry->pixels && key){entry->key=key;entry->aux=aux;entry->valid=true;entry->age=++cache->clock;}
    else entry->valid=false;
    pn_cache_unpin(&cache->owner);
}
void pn_menu_cache_abort(pn_menu_cache_t *cache){
    if(!cache || !cache->owner.pool)return;
    if(cache->last<PN_MENU_CACHE_SLOTS)cache->entries[cache->last].valid=false;
    pn_cache_unpin(&cache->owner);
}
void pn_menu_cache_invalidate(pn_menu_cache_t *cache){
    if(!cache)return;
    for(unsigned i=0;i<PN_MENU_CACHE_SLOTS;i++){cache->entries[i].valid=false;cache->entries[i].key=0;}
}
void pn_menu_cache_drop(pn_menu_cache_t *cache){
    if(!cache || cache->owner.pins)return;
    drop(cache);
}
size_t pn_menu_cache_bytes(const pn_menu_cache_t *cache){return cache?bytes_of((void *)cache):0;}
