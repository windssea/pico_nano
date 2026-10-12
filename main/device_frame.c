/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：板级90度旋转的成对半字节转换；不改波形或显示驱动。
 * English: paired-nibble conversion for board rotation without changing waveforms or display drivers.
 */
#include "device_frame.h"
pn_status_t pn_device_frame_copy(const pn_frame_t *source,uint8_t *destination,size_t capacity){
    if(!source || !source->pixels || source->width!=684 || source->height!=1216 || source->stride<342 || !destination || capacity<684u*1216u/2u || source->pixels==destination)return PN_INVALID;
    for(int x=0;x<684;x+=2){
        uint8_t *even=destination+(size_t)(683-x)*608,*odd=even-608;
        const uint8_t *at=source->pixels+x/2;
        for(int y=0;y<1216;y+=2){uint8_t a=at[(size_t)y*source->stride],b=at[(size_t)(y+1)*source->stride];
            even[y/2]=(uint8_t)((a&15)|((b&15)<<4));odd[y/2]=(uint8_t)((a>>4)|(b&240));}
    }
    return PN_OK;
}
