/* SPDX-License-Identifier: Apache-2.0
 * 中文：二分查找固件字节片段，原始字体身份、度量和校验保持。
 * English: binary-search firmware byte spans while preserving original font identity, metrics and validation. */
#include "pn_font_blob.h"
#include <stdbool.h>
#include <string.h>
pn_status_t pn_font_blob_read(const pn_font_blob_t *b,uint64_t offset,uint8_t *out,size_t cap,size_t *n){
    if(!b || !out || !n || offset>b->size)return PN_INVALID;
    size_t at=(size_t)offset,wanted=b->size-at;if(wanted>cap)wanted=cap;
    if(!wanted){*n=0;return PN_OK;}
    if(!b->spans || !b->count || !b->local || !b->shared || b->spans[b->count-1].end!=b->size)return PN_CORRUPT;
    size_t lo=0,hi=b->count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(b->spans[mid].end<=at)lo=mid+1;else hi=mid;}
    size_t left=wanted;
    while(left){
        if(lo>=b->count)return PN_CORRUPT;
        const pn_font_span_t *span=&b->spans[lo];size_t begin=lo?b->spans[lo-1].end:0,end=span->end;
        if(begin>at || end<=at || end>b->size)return PN_CORRUPT;
        bool shared=(span->source&0x80000000u)!=0;size_t source=span->source&0x7fffffffu,bank_size=shared?b->shared_size:b->local_size;
        if(source>bank_size || end-begin>bank_size-source)return PN_CORRUPT;
        size_t take=end-at;if(take>left)take=left;
        memcpy(out,(shared?b->shared:b->local)+source+at-begin,take);out+=take;at+=take;left-=take;lo++;
    }
    *n=wanted;return PN_OK;
}
