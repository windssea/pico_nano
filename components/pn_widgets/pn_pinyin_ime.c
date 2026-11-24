/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：轻量离线全拼中文输入法查表实现，基于GB2312一级汉字（3755字）及常用字频/多音字前置。
 * English: Lightweight offline full-pinyin Chinese IME lookup over GB2312 level-1 hanzi (3755 chars) with frequency/polyphone promotion.
 */
#include "pn_pinyin_ime.h"
#include "pn_pinyin_ime.generated.h"
#include <string.h>

/// 二分查找首个py>=key的条目索引。/ Binary search the first entry index with py >= key.
static size_t lower_bound(const char *key){
    size_t lo=0,hi=PN_IME_SYLLABLE_COUNT;
    while(lo<hi){
        size_t mid=lo+(hi-lo)/2u;
        if(strcmp(pn_ime_entries[mid].py,key)<0)lo=mid+1u;
        else hi=mid;
    }
    return lo;
}

/// 从UTF-8字节池拷贝第index个3字节汉字。/ Copy the index-th 3-byte UTF-8 hanzi from the pool.
static void copy_hanzi(const pn_ime_entry_t *e,size_t index,char dst[5]){
    size_t pos=(size_t)e->offset+index*3u;
    if(pos+3u>PN_IME_POOL_BYTES){dst[0]=0;return;}
    dst[0]=(char)pn_ime_pool[pos];
    dst[1]=(char)pn_ime_pool[pos+1u];
    dst[2]=(char)pn_ime_pool[pos+2u];
    dst[3]=0;
}

bool pn_pinyin_ime_has_prefix(const char *pinyin){
    if(!pinyin || !*pinyin)return false;
    size_t len=strlen(pinyin);
    size_t idx=lower_bound(pinyin);
    return idx<PN_IME_SYLLABLE_COUNT && strncmp(pn_ime_entries[idx].py,pinyin,len)==0;
}

size_t pn_pinyin_ime_lookup(const char *pinyin,size_t offset,char out[][5],size_t cap,size_t *total_out){
    if(total_out)*total_out=0;
    if(!pinyin || !*pinyin)return 0;
    size_t len=strlen(pinyin);
    size_t lo=lower_bound(pinyin);
    if(lo>=PN_IME_SYLLABLE_COUNT || strncmp(pn_ime_entries[lo].py,pinyin,len)!=0)return 0;
    // 优先精确匹配完整音节。/ Prefer an exact full-syllable match first.
    if(strcmp(pn_ime_entries[lo].py,pinyin)==0){
        const pn_ime_entry_t *e=&pn_ime_entries[lo];
        if(total_out)*total_out=e->count;
        if(!out || !cap || offset>=e->count)return 0;
        size_t avail=e->count-offset;
        if(avail>cap)avail=cap;
        for(size_t i=0;i<avail;i++)copy_hanzi(e,offset+i,out[i]);
        return avail;
    }
    // 未拼完时展示所有前缀匹配音节的高频首字。/ While typing a partial prefix, surface top chars from matching syllables.
    size_t hi=lo;
    while(hi<PN_IME_SYLLABLE_COUNT && strncmp(pn_ime_entries[hi].py,pinyin,len)==0)hi++;
    size_t matches=hi-lo;
    size_t per_syl=matches==1u?pn_ime_entries[lo].count:(matches<=3u?6u:2u);
    size_t total=0,written=0;
    for(size_t k=lo;k<hi;k++){
        size_t take=pn_ime_entries[k].count<per_syl?pn_ime_entries[k].count:per_syl;
        if(out && written<cap && total+take>offset){
            size_t start=offset>total?offset-total:0u;
            for(size_t i=start;i<take && written<cap;i++)copy_hanzi(&pn_ime_entries[k],i,out[written++]);
        }
        total+=take;
    }
    if(total_out)*total_out=total;
    return written;
}
