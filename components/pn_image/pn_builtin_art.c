/* SPDX-License-Identifier: Apache-2.0
 * 中文：原生压缩背景直接展开；封面借用Flash缩略图。 / English: expand native compressed backgrounds and borrow flash-backed cover views.
 */
#include "pn_builtin_art.h"
#include <string.h>
extern const uint8_t pn_boot_art_packed[],pn_bookmark_art_packed[],pn_default_cover_pixels[];
extern const size_t pn_boot_art_packed_size,pn_bookmark_art_packed_size,pn_default_cover_pixels_size;
/// 受限PackBits直接展开到目标画布，逐行还原异或，无堆分配。 / Bounded PackBits expands into the destination with row XOR restoration and no heap allocation.
static pn_status_t draw(const uint8_t *data,size_t size,pn_frame_t *frame){
    if(!frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride!=342)return PN_INVALID;
    if(size<2 || data[0]>1)return PN_CORRUPT;
    size_t at=1,out=0,bytes=342u*1216u;
    while(at<size && out<bytes){
        unsigned c=data[at++];if(c==128)return PN_CORRUPT;
        size_t n=c<128?c+1u:257u-c;
        if(n>bytes-out || (c<128?n:1u)>size-at)return PN_CORRUPT;
        for(size_t i=0;i<n;i++){uint8_t value=data[at+(c<128?i:0)];if(data[0] && out>=342)value^=frame->pixels[out-342];frame->pixels[out++]=value;}
        at+=c<128?n:1u;
    }
    return out==bytes && at==size?PN_OK:PN_CORRUPT;
}
pn_status_t pn_boot_art_draw(pn_pool_t *pool,pn_frame_t *frame){
    (void)pool;return draw(pn_boot_art_packed,pn_boot_art_packed_size,frame);
}
pn_status_t pn_bookmark_art_draw(pn_frame_t *frame){
    return draw(pn_bookmark_art_packed,pn_bookmark_art_packed_size,frame);
}
pn_status_t pn_builtin_cover_frame(const char *name,pn_frame_t *frame){
    if(!name || !frame)return PN_INVALID;
    if(pn_default_cover_pixels_size!=8u*3600u)return PN_CORRUPT;
    uint32_t hash=2166136261u;
    for(const unsigned char *p=(const unsigned char *)name;*p;p++){hash^=*p;hash*=16777619u;}
    *frame=(pn_frame_t){(uint8_t *)pn_default_cover_pixels+(hash%8u)*3600u,72,100,36};return PN_OK;
}
